#include "HarpoonSkinSync.h"
#include "soh/Notification/Notification.h"
#include "soh/Enhancements/mod_menu.h"
#include "soh/OTRGlobals.h" // for appShortName

#include <ship/Context.h>
#include <ship/resource/File.h>
#include <ship/resource/Resource.h>
#include <ship/resource/ResourceLoader.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/Archive.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <ship/resource/archive/O2rArchive.h>
#include <ship/utils/StrHash64.h>
#ifdef INCLUDE_MPQ_SUPPORT
#include <ship/resource/archive/OtrArchive.h>
#endif
#include <fast/resource/type/Texture.h>
#include <fast/resource/type/DisplayList.h>
#include <fast/resource/type/Vertex.h>
#include <fast/resource/type/Matrix.h>
#include "soh/resource/type/Skeleton.h"
#include <libultraship/libultraship.h>
extern "C" {
#include <libultraship/libultra/gbi.h>
#include <z64animation.h>
}

#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define HSS_LOG(fmt, ...)                                                              \
    do {                                                                               \
        char _buf[512];                                                                \
        snprintf(_buf, sizeof(_buf), "[HarpoonSkinSync] " fmt, ##__VA_ARGS__);          \
        SPDLOG_INFO("{}", _buf);                                                       \
    } while (0)

// =============================================================================
// Override-style .o2r registry
// =============================================================================
//
// Each .o2r file in harpoon/skins/ that we can open and that contains at
// least one DL resource is loaded as a "skin override". Every DL inside the
// archive is pre-parsed via the standard SOH ResourceLoader (DisplayListFactory
// path) into a native Gfx* whose memory lives for the lifetime of the entry.
//
// At draw time, when a Harpoon dummy player needs to render with a remote's
// .o2r overrides applied, BeginRemoteOverrides pushes the matching entries
// onto an "active overrides" stack. PakLoader_GetDLOverride consults
// HarpoonSkinSync_GetDLOverride for every gSPDisplayList path and returns the
// first match found in the active stack — letting different .o2rs touching
// disjoint DL paths (sword vs body limb vs hand) compose naturally.

// =============================================================================
// HarpoonSyncWrapperArchive — global mount with prefixed paths
// =============================================================================
//
// Why this exists: my pre-patcher converts SETTIMG_OTR_FILEPATH bytecode to
// raw-pointer G_SETTIMG, which works for textures that don't need Fast::
// Texture metadata at runtime (Mario, MM Young Link). But community packers
// (like Gerudo Player) emit Fast::Texture resources with Flags & TEX_FLAG_
// LOAD_AS_IMG / LOAD_AS_RAW — those flags are read at draw time from
// raw_tex_metadata.tex_flags and route to ImportTextureImg / ImportTextureRaw
// instead of the standard N64-format decode path. Raw-pointer pre-patching
// drops those flags (rawTexMetadata = {}) so the engine tries to decode an
// HD/PNG-encoded blob as raw N64 bytes → the chaotic-stripe garbage we saw.
//
// The fix: globally mount each override archive under a NON-CONFLICTING path
// prefix (`__hsync__/<modName>/...`). The patcher then leaves the OTR
// FILEPATH opcode intact for flagged textures and just rewrites the path
// string from `__OTR__alt/objects/foo` to `__OTR____hsync__/<modName>/alt/
// objects/foo`. At runtime, gfx_set_timg_otr_filepath_handler_custom calls
// LoadResourceProcess on the prefixed path, which lands in this wrapper,
// which forwards to the inner archive — returning a fully-populated Fast::
// Texture with Flags / Type / scales. The local user is unaffected because
// nothing in their bytecode ever queries `__hsync__/...` paths.
class HarpoonSyncWrapperArchive : public Ship::Archive {
public:
    HarpoonSyncWrapperArchive(std::shared_ptr<Ship::Archive> inner, const std::string& prefix)
        : Ship::Archive(prefix), mInner(std::move(inner)), mPrefix(prefix) {}

    bool Open() override {
        if (!mInner) return false;
        // Inner archive must already be Open()ed by the caller — we only
        // index its files under the prefixed namespace.
        auto innerFiles = mInner->ListFiles();
        if (!innerFiles) return false;
        for (auto& [hash, path] : *innerFiles) {
            // IndexFile populates the parent's mHashes (CRC64(prefix+path) →
            // prefix+path), and AddArchive will mirror those into the global
            // ArchiveManager mFileToArchive map so runtime hash lookups land
            // on this wrapper.
            IndexFile(mPrefix + path);
            mLocalReverseMap[CRC64((mPrefix + path).c_str())] = path;
        }
        SetLoaded(true);
        return true;
    }

    bool Close() override {
        SetLoaded(false);
        return true;
    }

    std::shared_ptr<Ship::File> LoadFile(const std::string& filePath) override {
        if (!mInner) return nullptr;
        if (filePath.compare(0, mPrefix.size(), mPrefix) != 0) return nullptr;
        return mInner->LoadFile(filePath.substr(mPrefix.size()));
    }

    std::shared_ptr<Ship::File> LoadFile(uint64_t hash) override {
        if (!mInner) return nullptr;
        auto it = mLocalReverseMap.find(hash);
        if (it == mLocalReverseMap.end()) return nullptr;
        return mInner->LoadFile(it->second);
    }

    bool WriteFile(const std::string& /*filename*/,
                   const std::vector<uint8_t>& /*data*/) override { return false; }

    const std::string& GetPrefix() const { return mPrefix; }

private:
    std::shared_ptr<Ship::Archive> mInner;
    std::string mPrefix;
    // CRC64(prefix+path) -> path-without-prefix (i.e. inner archive's key)
    std::unordered_map<uint64_t, std::string> mLocalReverseMap;
};

// Persistent storage for the rewritten path strings the patcher embeds into
// SETTIMG_OTR_FILEPATH commands. The engine reads these as `const char*` at
// every draw, so the pointed-to memory must outlive the override registry.
static std::vector<std::unique_ptr<std::string>> sPatchedPathStrings;

namespace {

struct O2rOverride {
    std::string name; // filename without extension — matches mod_menu's enabledModFiles entries
    std::shared_ptr<Ship::Archive> archive;
    std::map<std::string, Gfx*> dlsByPath;
    // Holds shared_ptrs to the typed IResource objects so the underlying Gfx
    // memory they own is kept alive.
    std::vector<std::shared_ptr<Ship::IResource>> resourceHolders;

    // Optional override Link skeletons. When this override is active for a
    // remote dummy, HarpoonDummyPlayer_Draw swaps the dummy's skelAnime to
    // walk this skeleton instead of vanilla — so limb DLs declared at custom
    // paths inside the override (e.g. `bone003_*_layer_Opaque` in MM Young
    // Link) get queried by the engine via gSPDisplayList and resolved
    // against this override's `dlsByPath`. Without this, vanilla skel walks
    // vanilla limb names and the override's bone${N}_* paths are never
    // queried — resulting in vanilla rendering despite the override matching.
    std::shared_ptr<Ship::IResource> adultSkelHolder;
    void** adultLimbTable = nullptr;
    int adultDListCount = 0;
    std::shared_ptr<Ship::IResource> childSkelHolder;
    void** childLimbTable = nullptr;
    int childDListCount = 0;

    // Per-override eye + mouth texture overrides. Set when the .o2r/.otr
    // bundles its own face textures at the canonical Link paths
    // (gLinkAdultEyesOpenTex etc.). When this override is active for a
    // remote dummy, HarpoonSkinSync_GetVanillaEye/MouthTexture prefers
    // these over the vanilla cache so Mario's dummy gets Mario's eyes,
    // MM Young Link's dummy gets MM Young Link's eyes, etc. Without this
    // every dummy ends up wearing vanilla Link eyes.
    void* eyeImageData[2][8] = {};
    void* mouthImageData[2][4] = {};
    std::vector<std::shared_ptr<Ship::IResource>> faceTexHolders;

    // Per-override prefix used by the runtime-resolution fallback. When this
    // is non-empty, the override's archive has been wrapped + mounted into
    // the global ArchiveManager under `__hsync__/<modName>/...` paths, so
    // the patcher can leave certain SETTIMG_OTR_FILEPATH commands intact
    // (just rewriting the path string to the prefixed version) and let the
    // runtime LoadResourceProcess re-fetch the Fast::Texture with full
    // metadata — needed for community-packed mods like Gerudo Player whose
    // textures use TEX_FLAG_LOAD_AS_IMG / LOAD_AS_RAW import paths that
    // require the full Fast::Texture (Flags / Width / scales) which
    // raw-pointer pre-patching loses.
    std::string harpoonSyncPrefix;
};

static std::vector<O2rOverride> sOverrides;
// Indices into sOverrides currently active for the dummy being drawn (cleared
// in EndRemoteOverrides). Walked by GetDLOverride; first hit wins.
static std::vector<size_t> sActiveOverrideIndices;
// Independent flag for "we are currently inside a remote-dummy draw block".
// Set true in BeginRemoteOverrides regardless of whether the remote's
// broadcast mod list matched any of our installed overrides — we still need
// to gate the vanilla-DL fallback so it fires even when zero overrides are
// active, otherwise the dummy walks vanilla limb names which fall through
// to the global stack and pull in whatever skin mods the LOCAL user has
// mounted (texture-level leak).
static bool sInRemoteDraw = false;
// Dedupe state for the UI notifications.
static std::unordered_set<std::string> sNotified;

// Vanilla-fallback DL cache: pre-loaded DIRECTLY from oot.o2r (and oot-mq.o2r if
// present) at startup, BYPASSING the global ArchiveManager so the local user's
// own mods don't contaminate it. Used when a remote dummy queries a path that
// isn't in any of its active override .o2rs — without this, the call would
// fall through the gSPDisplayList wrapper to ResourceMgr_LoadGfxByName which
// would resolve against the global stack (where the LOCAL user's mods are
// mounted) and apply THEIR overrides to the REMOTE's dummy. The fallback
// returns vanilla so the dummy stays isolated.
static std::map<std::string, Gfx*> sVanillaDLs;
static std::vector<std::shared_ptr<Ship::IResource>> sVanillaResourceHolders;
static std::vector<std::shared_ptr<Ship::Archive>> sVanillaArchives;
// Forward decl — full definition lower in the namespace where it's used by
// PatchContext. Per-vanilla-archive hash → path map mirrors sVanillaArchives
// so the override patcher can resolve vanilla shared assets by hash via a
// LOCAL map (the global ArchiveManager doesn't know about an unmounted
// override archive's hashes).
struct ArchiveHandle;
static std::vector<std::unique_ptr<ArchiveHandle>> sVanillaArchiveHandles;

// Vanilla Link skeleton cache. The skel + DL bytes are loaded directly from
// oot.o2r via O2rArchive::LoadFile (no AddArchive), so the data is
// independent of whatever the local user has mounted globally. We hold the
// IResource shared_ptrs to keep the limb-table memory alive for the
// lifetime of the process.
static std::shared_ptr<Ship::IResource> sVanillaAdultSkelHolder;
static std::shared_ptr<Ship::IResource> sVanillaChildSkelHolder;
static void** sVanillaAdultLimbTable = nullptr;
static int sVanillaAdultDListCount = 0;
static void** sVanillaChildLimbTable = nullptr;
static int sVanillaChildDListCount = 0;

// One-shot init flag. The pre-resolve scan can take 1-2 seconds because it
// walks every Player DL in oot.o2r + every override .o2r, so we only run it
// once per app session (called from Harpoon::OnConnected the first time
// the user joins a session). Subsequent reconnects skip the heavy work.
static bool sInitialized = false;

// Vanilla eye + mouth texture cache. These are referenced inside the body
// DL bytecode via segment 0x08 / 0x09, which is set by Player_DrawImpl with
// gSPSegment(0x08, sEyeTextures[age][index]) — and the OTR string in
// sEyeTextures resolves through the global stack at frame-end interpret
// time, leaking the LOCAL user's globally-mounted skin mods. We pre-load
// the raw image data for all 8 eye + 4 mouth textures × 2 ages directly
// from oot.o2r and route PakLoader_GetEyeTexture / GetMouthTexture through
// these pointers when sInRemoteDraw, so segment 0x08 / 0x09 ends up
// pointing at vanilla bytes rather than an OTR string.
static void* sVanillaEyeImageData[2][8] = {};
static void* sVanillaMouthImageData[2][4] = {};
static std::vector<std::shared_ptr<Ship::IResource>> sVanillaFaceTexResources;

// Owned storage for patched DLs. Each entry holds one new Gfx[] array that
// is a modified copy of an OTR-loaded DL where every G_SETTIMG_OTR_FILEPATH
// has been rewritten as a regular G_SETTIMG with a raw pointer to the
// corresponding texture's pixel data (read directly from the local archive,
// not via the global ResourceManager). This is what prevents the LOCAL
// user's globally-mounted skin mods from leaking textures onto the REMOTE
// dummy: the patched bytecode never queries the global stack for textures.
struct PatchedDLEntry {
    std::unique_ptr<Gfx[]> bytes;
    size_t cmdCount = 0;
};
static std::vector<std::shared_ptr<PatchedDLEntry>> sPatchedDLStorage;
// Texture / sub-DL resources kept alive for the lifetime of the patched DLs
// that point into them.
static std::vector<std::shared_ptr<Ship::IResource>> sPatchedDLResources;

// Forward decls — bodies live after LoadO2rOverride / CacheVanillaFromArchive
// for readability but are called from inside them.
struct ArchiveHandle {
    Ship::Archive* archive = nullptr;
    // Local hash → path map built from archive->ListFiles(). CRITICAL:
    // Ship::Archive::LoadFile(hash) consults the GLOBAL HashToString from
    // ArchiveManager — which only knows about archives mounted globally.
    // Override .o2r/.otr files in harpoon/skins/ are deliberately NOT
    // mounted globally, so a hash lookup against the global stack fails
    // for every texture / sub-DL inside the override → patcher can't
    // resolve them → at runtime the interpreter falls back to global
    // (still doesn't have it) → "Texture is null". The fix: populate this
    // map from the archive's own file list at load time and consult it
    // before falling through to the global stack.
    std::unordered_map<uint64_t, std::string> localHashMap;
};

struct PatchContext {
    ArchiveHandle primary;
    // Fallback archives to consult when `primary` doesn't have a referenced
    // texture / sub-DL. Necessary because override .o2rs frequently reuse
    // VANILLA shared textures (gameplay_keep, object_link_*, etc.) without
    // bundling their own copies.
    std::vector<ArchiveHandle*> fallbackArchives;
    Ship::ResourceLoader* loader;
    std::unordered_map<Gfx*, Gfx*> memo;
    // Diagnostic counters per-archive — printed once at the end of the load
    // so we can see whether texture pre-resolution is actually firing.
    uint32_t dlsAttempted = 0;
    uint32_t dlsSkippedNotDL = 0;
    uint32_t dlsPatched = 0;
    uint32_t texturesResolved = 0;
    uint32_t texturesResolvedFromFallback = 0;
    uint32_t texturesFailed = 0;
    uint32_t texturesViaPrefix = 0; // patched via runtime-resolution path
    // When non-empty, the override's archive has been mounted as a
    // HarpoonSyncWrapperArchive with this prefix. Allows the patcher to
    // leave SETTIMG_OTR_FILEPATH intact and rewrite the path string,
    // preserving Fast::Texture metadata at runtime.
    std::string harpoonSyncPrefix;
};
static Gfx* PatchDLForLocalArchive(Gfx* originalDL, size_t maxCmdCount, PatchContext& ctx, int depth);
// Defined after CacheVanillaFromArchive (helper sees same archive); also
// called from LoadO2rOverride to detect override-bundled Link skeletons.
static bool ExtractVanillaSkeletonFromArchive(Ship::Archive* archive, const std::string& path,
                                              std::shared_ptr<Ship::IResource>& outHolder,
                                              void**& outLimbTable, int& outDListCount);

// Safe predicate: is this pointer either (a) NULL, (b) an `__OTR__...` string
// the engine's gSPDisplayList wrapper can resolve, or (c) plausibly a real
// loaded Gfx* in heap territory? Used to sanitize community-packed override
// skeletons whose LodLimb dLists fields sometimes contain ASCII garbage from
// the source archive (e.g. 0x6552202626202928 == "(  ) && Re") — when the
// engine then walks the skeleton, gSPDisplayList → ResourceMgr_OTRSigCheck
// dereferences the garbage as char* and segfaults. Anything that's NEITHER
// a valid OTR string NOR plausible Gfx* memory is rejected here so callers
// can NULL it out (SkelAnime then renders nothing for that limb instead of
// crashing the whole game).
//
// We use VirtualQuery on Windows to verify the page is readable BEFORE we
// dereference; otherwise the validator itself would crash on garbage. On
// other platforms we fall back to a coarse aligned-pointer + magic check.
static bool IsLikelyOtrStringOrGfxPtr(const void* p) {
    if (p == nullptr) return true; // NULL is fine, callers know to skip
    uintptr_t addr = (uintptr_t)p;
    if (addr < 0x10000ull) return false;          // tiny integers
    if (addr & 0x1ull) return false;              // odd → can't be aligned ptr
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                     PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
    if ((mbi.Protect & readable) == 0) return false;
    if (mbi.Protect & PAGE_GUARD) return false;
    // Page is readable. Verify the first 7 bytes are within the same page so
    // we don't straddle into an unreadable region.
    uintptr_t pageEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    if (addr + 7 > pageEnd) return false;
#endif
    const unsigned char* s = (const unsigned char*)p;
    if (s[0] == '_' && s[1] == '_' && s[2] == 'O' && s[3] == 'T' &&
        s[4] == 'R' && s[5] == '_' && s[6] == '_') {
        return true; // __OTR__ string — gSPDisplayList wrapper handles it
    }
    // Not an OTR string. It MIGHT still be a real Gfx* (raw bytecode pointer
    // baked in by an override packer). Accept iff the first byte is a valid
    // F3DEX2 / SOH-extended opcode that's plausibly the START of a DL.
    //
    // Valid first-opcode ranges:
    //   0x00..0x09       — DMA / RSP geometry (NOOP, VTX, TRI1, TRI2, ...)
    //   0x20..0x33       — SOH OTR-extended opcodes (SETTIMG_OTR_*, DL_OTR_*,
    //                      VTX_OTR_*, MTX_OTR_*, etc.)
    //   0xD3..0xFD       — RDP / SP setters (TEXTURE, GEOMETRYMODE, MTX,
    //                      MOVEWORD, DL, ENDDL, RDPPIPESYNC, SETCOMBINE,
    //                      SETTIMG, ...)
    //
    // ASCII text (e.g. 0x65='e', 0x52='R', 0x28='(', 0x29=')') falls in the
    // gap [0x0A..0x1F] / [0x34..0xD2] which we now reject — that's exactly
    // the kind of garbage that landed in MM Young Link's child-limb dLists
    // and crashed the dummy draw.
    uint8_t op = s[0];
    if (op <= 0x09) return true;
    if (op >= 0x20 && op <= 0x33) return true;
    if (op >= 0xD3 && op <= 0xFD) return true;
    return false;
}

// Sanitize a freshly-loaded LodLimb so its dLists fields cannot crash
// SkelAnime_DrawFlexLimbLod. NULLs out any dLists[i] that fails the
// IsLikelyOtrStringOrGfxPtr check; SkelAnime treats NULL as "draw nothing
// for this limb" (line 156 of z_skelanime.c) which is the safe outcome.
static void SanitizeLodLimbDLists(void* limbRaw, const char* skelPath, int limbIndex) {
    if (limbRaw == nullptr) return;
    LodLimb* limb = (LodLimb*)limbRaw;
    for (int s = 0; s < 2; s++) {
        if (!IsLikelyOtrStringOrGfxPtr(limb->dLists[s])) {
            HSS_LOG("skel sanitize: '%s' limb[%d].dLists[%d]=%p is garbage — nulled to prevent crash",
                    skelPath, limbIndex, s, limb->dLists[s]);
            limb->dLists[s] = nullptr;
        }
    }
}

// Several SOH-extended OTR commands span TWO Gfx structs: a header word
// followed by a data word (typically a 64-bit hash or a vtx-data record).
// Naively iterating through the bytecode one Gfx at a time would treat the
// data word as a new command and try to interpret w1 as an OTR string
// pointer — which crashes on strlen of garbage memory. We have to skip past
// the data word for these.
static inline bool IsTwoWordOTRCommand(uint8_t op) {
    switch (op) {
        case G_SETTIMG_OTR_HASH:    // 0x20: header + uint64 hash
        case G_VTX_OTR_FILEPATH:    // 0x24: filename + vtx data record
        case G_VTX_OTR_HASH:        // 0x32: header + uint64 hash
        case G_DL_OTR_HASH:         // 0x31: header + uint64 hash
        case G_MARKER:              // 0x33: header + marker data
        case G_BRANCH_Z_OTR:        // 0x35: header + branch data
        case G_MTX_OTR:             // 0x36: header + uint64 hash
            return true;
        default:
            return false;
    }
}

static bool ShouldNotify(const std::string& key) {
    if (sNotified.count(key)) return false;
    sNotified.insert(key);
    return true;
}

// Resolve and (if needed) create the Harpoon content root, ALWAYS relative
// to the SoH app directory the user launched. Layout (sibling of `mods/`,
// NOT inside it):
//
//     <SoH app dir>/harpoon/
//         skins/        — .o2r files for skin sync (other players' Link skins)
//         gamemodes/    — .o2r packs for gamemodes (manifest + assets)
//
// Both subfolders are auto-created on first run so the user just has to drop
// content in.
static std::filesystem::path EnsureHarpoonRoot() {
    // Preferred: directly resolve `harpoon` if it already exists.
    std::string existingHarpoon = Ship::Context::LocateFileAcrossAppDirs("harpoon", appShortName);
    if (!existingHarpoon.empty()) {
        std::filesystem::path root = existingHarpoon;
        std::error_code ec;
        std::filesystem::create_directories(root / "skins", ec);
        std::filesystem::create_directories(root / "gamemodes", ec);
        return root;
    }

    // Doesn't exist yet — find the SoH app dir by locating `mods/` (always
    // present), then create `harpoon/` as a sibling of it (i.e. directly
    // under the app dir, next to the executable).
    std::string modsPath = Ship::Context::LocateFileAcrossAppDirs("mods", appShortName);
    if (modsPath.empty()) {
        HSS_LOG("Cannot locate SoH app directory — Harpoon content disabled");
        return {};
    }
    std::filesystem::path appDir = std::filesystem::path(modsPath).parent_path();
    std::filesystem::path root = appDir / "harpoon";
    std::error_code ec;
    std::filesystem::create_directories(root / "skins", ec);
    std::filesystem::create_directories(root / "gamemodes", ec);
    if (ec) {
        HSS_LOG("Failed to create %s: %s", root.string().c_str(), ec.message().c_str());
        return {};
    }
    HSS_LOG("Created Harpoon root: %s", root.string().c_str());
    return root;
}

static std::filesystem::path FindSyncFolder() {
    auto root = EnsureHarpoonRoot();
    return root.empty() ? std::filesystem::path{} : root / "skins";
}

static std::filesystem::path FindGamemodesFolder() {
    auto root = EnsureHarpoonRoot();
    return root.empty() ? std::filesystem::path{} : root / "gamemodes";
}

// Open an override archive picking the right concrete type by extension.
// Both `.o2r` (zip) and `.otr` (MPQ) are accepted because community skin
// packs are distributed in either format.
static std::shared_ptr<Ship::Archive> OpenOverrideArchive(const std::filesystem::path& archivePath) {
    std::string ext = archivePath.extension().string();
    for (char& c : ext) c = (char)tolower((unsigned char)c);
    std::shared_ptr<Ship::Archive> archive;
    if (ext == ".o2r") {
        archive = std::make_shared<Ship::O2rArchive>(archivePath.string());
    } else if (ext == ".otr") {
#ifdef INCLUDE_MPQ_SUPPORT
        archive = std::make_shared<Ship::OtrArchive>(archivePath.string());
#else
        HSS_LOG(".otr unsupported in this build (no INCLUDE_MPQ_SUPPORT): %s",
                archivePath.string().c_str());
        return nullptr;
#endif
    } else {
        return nullptr;
    }
    if (!archive->Open()) {
        HSS_LOG("Failed to open archive: %s", archivePath.string().c_str());
        return nullptr;
    }
    return archive;
}

// Parse a single .o2r / .otr as an override. We don't insist on it containing
// a Link skeleton or a package.json — any archive with at least one DL inside
// is fair game (so e.g. equipment-only overrides like "Gilded Sword Over
// Master Sword.o2r" work without special-casing).
static bool LoadO2rOverride(const std::filesystem::path& o2rPath, O2rOverride& out) {
    auto archive = OpenOverrideArchive(o2rPath);
    if (!archive) return false;
    auto allFiles = archive->ListFiles();
    if (!allFiles) return false;

    auto resourceManager = Ship::Context::GetRawInstance()->GetResourceManager();
    if (!resourceManager) return false;
    auto loader = resourceManager->GetResourceLoader();
    if (!loader) return false;

    // Mount this override globally under a NON-CONFLICTING `__hsync__/<modName>/`
    // prefix so the patcher can defer texture-metadata-needing SETTIMG_OTR
    // commands to runtime LoadResourceProcess. Local user is unaffected
    // because nothing in their bytecode ever queries `__hsync__/...` paths.
    // See HarpoonSyncWrapperArchive comment block above for the full rationale.
    std::string overrideName = o2rPath.filename().string();
    {
        // Strip extension for a cleaner prefix (matches `name` field below).
        auto dot = overrideName.find_last_of('.');
        if (dot != std::string::npos) overrideName = overrideName.substr(0, dot);
    }
    std::string syncPrefix = std::string("__hsync__/") + overrideName + "/";
    auto archiveMgr = resourceManager->GetArchiveManager();
    if (archiveMgr) {
        auto wrapper = std::make_shared<HarpoonSyncWrapperArchive>(archive, syncPrefix);
        if (wrapper->Open()) {
            archiveMgr->AddArchive(wrapper);
            out.harpoonSyncPrefix = syncPrefix;
            HSS_LOG("override '%s': mounted with prefix '%s' (%zu files indexed)",
                    overrideName.c_str(), syncPrefix.c_str(), allFiles->size());
        } else {
            HSS_LOG("override '%s': wrapper archive failed to Open() — runtime "
                    "metadata fallback DISABLED for this skin", overrideName.c_str());
        }
    }

    PatchContext patchCtx{};
    patchCtx.primary.archive = archive.get();
    patchCtx.loader = loader.get();
    patchCtx.harpoonSyncPrefix = out.harpoonSyncPrefix;
    // DEBUG: count specifically how many entries contain "_pal_rgba16"
    // — the palette suffix that's failing to resolve. If count > 0 the
    // palette files ARE in this archive but my LoadFile path lookup
    // fails for some other reason (encoding, prefix, etc.). If 0 the
    // archive genuinely doesn't include palette files.
    {
        int palCount = 0;
        std::string firstPalPath;
        for (auto& [hash, path] : *allFiles) {
            if (path.find("_pal_rgba16") != std::string::npos) {
                if (palCount < 5) {
                    HSS_LOG("archive '%s' has palette '%s'",
                            o2rPath.filename().string().c_str(), path.c_str());
                    if (firstPalPath.empty()) firstPalPath = path;
                }
                palCount++;
            }
        }
        HSS_LOG("archive '%s' total _pal_rgba16 entries: %d",
                o2rPath.filename().string().c_str(), palCount);
    }
    // DEBUG: enumerate entries whose path mentions "hand", "fist", "glove"
    // or matches the OOT vanilla hand-DL / hand-Tex naming. Tells us whether
    // Mario.otr (and similar packs) bundle their own custom hand DLs at all,
    // and under what path. If they don't, the dummy ends up walking Player_
    // Draw's vanilla `sPlayerDListGroups` pointers (left/right hand DLs are
    // NOT skeleton limbs in OOT — they're injected by the player code based
    // on leftHandType / rightHandType), bypassing the override registry.
    {
        int count = 0;
        for (auto& [hash, path] : *allFiles) {
            std::string lower = path;
            for (char& c : lower) c = (char)tolower((unsigned char)c);
            if (lower.find("hand") != std::string::npos ||
                lower.find("fist") != std::string::npos ||
                lower.find("glove") != std::string::npos) {
                if (count < 40) {
                    HSS_LOG("archive '%s' hand-related entry: '%s'",
                            o2rPath.filename().string().c_str(), path.c_str());
                }
                count++;
            }
        }
        HSS_LOG("archive '%s' total hand-related entries: %d",
                o2rPath.filename().string().c_str(), count);
    }
    // Build the override's local hash → path map from the archive's file
    // listing. CRC64(path) = hash (matches Archive::IndexFile's encoding).
    // This is what makes hashed texture / sub-DL refs resolve from THIS
    // archive without going through the global ArchiveManager.
    for (auto& [hash, path] : *allFiles) {
        patchCtx.primary.localHashMap[hash] = path;
    }
    // Fall back to the vanilla archives (oot.o2r / oot-mq.o2r) when the
    // override doesn't bundle a referenced texture or sub-DL itself —
    // most skin overrides reuse vanilla shared assets without re-bundling
    // them, so without this every shared-asset reference would leak through
    // the global stack at runtime.
    for (auto& fbHandle : sVanillaArchiveHandles) {
        if (fbHandle && fbHandle->archive) {
            patchCtx.fallbackArchives.push_back(fbHandle.get());
        }
    }
    s32 loadedCount = 0;
    for (auto& [hash, path] : *allFiles) {
        // Try to load EVERY file in the archive — community .o2rs use a wide
        // variety of internal naming conventions (gXxxxDL, *_layer_Opaque,
        // bone${N}_*_mesh, etc.). Filtering by suffix would miss the custom
        // skeleton + mesh chunks some skin packs export. We load via the SOH
        // ResourceLoader which dispatches to whichever factory the file's
        // header identifies (DL, skeleton, texture, vertex, ...) — non-DL
        // resources still get cached but their entries simply never match the
        // gSPDisplayList path lookup at draw time, so they're harmless.
        if (path.find(".meta") != std::string::npos) continue;

        auto file = archive->LoadFile(path);
        if (!file || !file->IsLoaded || !file->Buffer || file->Buffer->empty()) continue;

        std::shared_ptr<Ship::IResource> resource;
        try {
            resource = loader->LoadResource(path, file, nullptr);
        } catch (...) {
            resource = nullptr;
        }
        if (!resource) continue;
        Gfx* dl = (Gfx*)resource->GetRawPointer();
        if (!dl) continue;
        // Patch ONLY DisplayList resources — texture / vertex / palette
        // resources also live in this archive and casting their byte buffer
        // to Gfx* and walking it would run off the end of the allocation.
        // Non-DL resources are stored as the original raw pointer; they
        // simply never match a gSPDisplayList path lookup at draw time.
        patchCtx.dlsAttempted++;
        if (auto dlRes = std::dynamic_pointer_cast<Fast::DisplayList>(resource)) {
            size_t maxCmds = resource->GetPointerSize() / sizeof(Gfx);
            if (maxCmds > 0) {
                Gfx* patched = PatchDLForLocalArchive(dl, maxCmds, patchCtx, 0);
                if (patched && patched != dl) {
                    dl = patched;
                    patchCtx.dlsPatched++;
                }
            }
        } else {
            patchCtx.dlsSkippedNotDL++;
        }

        // Strip leading "__OTR__" / "alt/" prefixes that some packers store —
        // SOH's gXxxxDL symbols expand to "__OTR__objects/...", never to
        // "__OTR__alt/objects/...". Store all four variants so we match
        // whatever the engine ends up querying.
        std::string archivePath = path;
        if (archivePath.compare(0, 7, "__OTR__") == 0) archivePath.erase(0, 7);
        if (archivePath.compare(0, 4, "alt/") == 0) archivePath.erase(0, 4);
        std::string otr = std::string("__OTR__") + archivePath;
        std::string altOtr = std::string("__OTR__alt/") + archivePath;
        std::string altRaw = std::string("alt/") + archivePath;
        out.dlsByPath[otr] = dl;
        out.dlsByPath[archivePath] = dl;
        out.dlsByPath[altOtr] = dl;
        out.dlsByPath[altRaw] = dl;
        out.resourceHolders.push_back(std::move(resource));
        loadedCount++;
    }

    if (loadedCount == 0) {
        HSS_LOG(".o2r ignored (no DL resources inside): %s", o2rPath.filename().string().c_str());
        return false;
    }
    out.archive = archive;
    out.name = o2rPath.stem().string();

    // Try to extract this override's own Link skeletons. We try both vanilla
    // path namespace (`objects/object_link_*/gLink*Skel`) and the alt-prefixed
    // namespace community packers use (`alt/objects/object_link_*/gLink*Skel`).
    // When found, the dummy will walk THIS skeleton instead of vanilla so the
    // override's custom limb-DL paths (often `bone${N}_*_layer_Opaque`) get
    // queried by the engine and resolved against this override's dlsByPath.
    static const char* kAdultSkelPaths[] = {
        "objects/object_link_boy/gLinkAdultSkel",
        "alt/objects/object_link_boy/gLinkAdultSkel",
    };
    static const char* kChildSkelPaths[] = {
        "objects/object_link_child/gLinkChildSkel",
        "alt/objects/object_link_child/gLinkChildSkel",
    };
    for (auto* p : kAdultSkelPaths) {
        if (out.adultLimbTable) break;
        ExtractVanillaSkeletonFromArchive(archive.get(), p, out.adultSkelHolder,
                                          out.adultLimbTable, out.adultDListCount);
    }
    for (auto* p : kChildSkelPaths) {
        if (out.childLimbTable) break;
        ExtractVanillaSkeletonFromArchive(archive.get(), p, out.childSkelHolder,
                                          out.childLimbTable, out.childDListCount);
    }

    // Extract this override's eye + mouth textures (if present at the
    // canonical Link paths). These get returned by HarpoonSkinSync's eye/
    // mouth hooks during dummy draw so the dummy gets the override's face
    // textures instead of vanilla ones bleeding in. Without this every
    // dummy ends up with vanilla Link eyes / mouth on top of the override
    // body — looks weirdly half-vanilla.
    static const char* kEyePathNames[] = {
        "EyesOpenTex", "EyesHalfTex", "EyesClosedfTex",
        "EyesRollLeftTex", "EyesRollRightTex", "EyesShockTex",
        "EyesUnk1Tex", "EyesUnk2Tex"
    };
    static const char* kMouthPathNames[] = {
        "Mouth1Tex", "Mouth2Tex", "Mouth3Tex", "Mouth4Tex"
    };
    auto loadFaceTexFromOverride = [&](const std::string& fullPath, void*& outImageData) {
        if (outImageData != nullptr) return;
        auto file = archive->LoadFile(fullPath);
        if (!file || !file->IsLoaded || !file->Buffer || file->Buffer->empty()) return;
        std::shared_ptr<Ship::IResource> res;
        try {
            res = loader->LoadResource(fullPath, file, nullptr);
        } catch (...) {
            res = nullptr;
        }
        if (!res) return;
        auto tex = std::dynamic_pointer_cast<Fast::Texture>(res);
        if (!tex || !tex->ImageData) return;
        outImageData = tex->ImageData;
        out.faceTexHolders.push_back(std::move(res));
    };
    int faceTexCount = 0;
    for (int age = 0; age < 2; age++) {
        const char* objectPrefix = (age == 0) ? "objects/object_link_boy/gLinkAdult"
                                              : "objects/object_link_child/gLinkChild";
        const char* altPrefix    = (age == 0) ? "alt/objects/object_link_boy/gLinkAdult"
                                              : "alt/objects/object_link_child/gLinkChild";
        for (int i = 0; i < 8; i++) {
            std::string p1 = std::string(objectPrefix) + kEyePathNames[i];
            std::string p2 = std::string(altPrefix)    + kEyePathNames[i];
            loadFaceTexFromOverride(p1, out.eyeImageData[age][i]);
            if (!out.eyeImageData[age][i]) loadFaceTexFromOverride(p2, out.eyeImageData[age][i]);
            if (out.eyeImageData[age][i]) faceTexCount++;
        }
        for (int i = 0; i < 4; i++) {
            std::string p1 = std::string(objectPrefix) + kMouthPathNames[i];
            std::string p2 = std::string(altPrefix)    + kMouthPathNames[i];
            loadFaceTexFromOverride(p1, out.mouthImageData[age][i]);
            if (!out.mouthImageData[age][i]) loadFaceTexFromOverride(p2, out.mouthImageData[age][i]);
            if (out.mouthImageData[age][i]) faceTexCount++;
        }
    }

    HSS_LOG("Loaded override .o2r '%s' (%u entries, dlsAttempted=%u patched=%u skippedNotDL=%u textures=%u "
            "fromFallback=%u viaPrefix=%u failed=%u, adultSkel=%s childSkel=%s, faceTextures=%d)",
            out.name.c_str(), (unsigned)loadedCount,
            (unsigned)patchCtx.dlsAttempted, (unsigned)patchCtx.dlsPatched,
            (unsigned)patchCtx.dlsSkippedNotDL, (unsigned)patchCtx.texturesResolved,
            (unsigned)patchCtx.texturesResolvedFromFallback,
            (unsigned)patchCtx.texturesViaPrefix, (unsigned)patchCtx.texturesFailed,
            out.adultLimbTable ? "yes" : "no", out.childLimbTable ? "yes" : "no",
            faceTexCount);
    return true;
}

// Walks a DL's bytecode and rewrites OTR-style commands to non-OTR commands
// pointing at memory we resolve LOCALLY from the supplied archive.
//
// The interpreter's regular G_SETTIMG handler treats w1 as a raw pointer when
// the upper bytes don't carry the OTR signature, so swapping the opcode +
// putting the texture's ImageData* in w1 makes the interpreter sample our
// pre-loaded bytes without ever touching the global ResourceManager. Same
// pattern applies to G_DL_OTR_FILEPATH (recursively patched into G_DL with a
// pointer into our owned storage).
//
// We don't patch G_VTX_OTR_*, G_MTX_OTR_*, or G_SETTIMG_OTR_HASH for now —
// vertex/matrix data is rarely overridden by skin mods and hash variants are
// uncommon in OOT player DLs. If those become an issue, extend the switch.
//
// Returns a pointer into sPatchedDLStorage (stable for app lifetime). If
// patching fails on a sub-DL the original pointer is returned, which is
// acceptable degradation (that one path may still leak through global, but
// the rest of the patched DL is safe).
static Gfx* PatchDLForLocalArchive(Gfx* originalDL, size_t maxCmdCount, PatchContext& ctx, int depth) {
    if (!originalDL) return nullptr;
    if (maxCmdCount == 0) return originalDL;
    if (depth > 8) return originalDL; // safety against pathological recursion
    auto memoIt = ctx.memo.find(originalDL);
    if (memoIt != ctx.memo.end()) return memoIt->second;

    // Walk to ENDDL (0xDF) to determine bytecode length. Bounded by the
    // resource's actual size (passed in via maxCmdCount) so we never read
    // past the allocated buffer if the resource isn't really a DisplayList
    // or if it's malformed and missing its terminator.
    // Multi-Gfx OTR commands consume an extra slot for their data word; we
    // skip past it so a coincidentally-0xDF byte in the data isn't mistaken
    // for ENDDL.
    size_t cmdCount = 0;
    {
        Gfx* p = originalDL;
        while (cmdCount < maxCmdCount) {
            uint8_t op = (uint8_t)(p->words.w0 >> 24);
            if (op == G_ENDDL) {
                cmdCount++;
                break;
            }
            if (IsTwoWordOTRCommand(op)) {
                if (cmdCount + 2 > maxCmdCount) {
                    // Header+data wouldn't fit — bail without patching.
                    return originalDL;
                }
                p += 2;
                cmdCount += 2;
            } else {
                p++;
                cmdCount++;
            }
        }
    }
    if (cmdCount == 0 || cmdCount > maxCmdCount) {
        return originalDL;
    }

    auto entry = std::make_shared<PatchedDLEntry>();
    entry->cmdCount = cmdCount;
    entry->bytes.reset(new Gfx[cmdCount]);
    memcpy(entry->bytes.get(), originalDL, cmdCount * sizeof(Gfx));
    sPatchedDLStorage.push_back(entry);
    Gfx* outDL = entry->bytes.get();
    ctx.memo[originalDL] = outDL;

    auto LooksLikeValidStringPtr = [](uintptr_t p) -> bool {
        // Reject obvious garbage: null, tiny integers, unaligned pointers.
        // Real OTR strings come from the ResourceManager's archive cache and
        // live in a heap allocation (high address, byte-aligned). This is a
        // belt-and-suspenders defense for cases where IsTwoWordOTRCommand
        // misses an opcode and we'd otherwise dereference data bits.
        return p > 0x10000ull;
    };
    // Tries primary archive first, then each fallback. Returns the loaded
    // resource + whether it came from a fallback (for diagnostics).
    //
    // Hash keys are resolved to a path via the archive's LOCAL hash map
    // (built from ListFiles() at load time). This is essential for
    // un-mounted override archives whose hashes aren't registered in the
    // global ArchiveManager — without local-map lookup, every hashed
    // texture/sub-DL/vertex/matrix in the override fails to resolve and
    // the dummy renders as black silhouette.
    auto loadResourceWithFallback = [&](const auto& key, std::shared_ptr<Ship::IResource>& outRes,
                                        bool& outUsedFallback) -> bool {
        outUsedFallback = false;
        // Try a single archive with the given resolved path. Returns true
        // if loaded.
        auto tryArchiveWithPath = [&](ArchiveHandle& h, const std::string& resolvedPath) -> bool {
            if (!h.archive) return false;
            auto file = h.archive->LoadFile(resolvedPath);
            if (!file || !file->IsLoaded || !file->Buffer || file->Buffer->empty()) return false;
            try {
                outRes = ctx.loader->LoadResource(resolvedPath, file, nullptr);
            } catch (...) {
                outRes = nullptr;
            }
            return outRes != nullptr;
        };
        // Build list of paths to try. Community packers store assets under
        // either the bare `objects/...` namespace OR the `alt/objects/...`
        // namespace; DL bytecode may reference either form. We try both.
        // For hash keys, look up the path in the archive's local hash map
        // (since Archive::LoadFile(hash) consults the GLOBAL stack, useless
        // for our unmounted overrides).
        auto tryArchive = [&](ArchiveHandle& h) -> bool {
            std::string basePath;
            if constexpr (std::is_same_v<std::decay_t<decltype(key)>, std::string>) {
                basePath = key;
            } else {
                uint64_t hash = (uint64_t)key;
                auto it = h.localHashMap.find(hash);
                if (it == h.localHashMap.end()) return false;
                basePath = it->second;
            }
            // Strip __OTR__ prefix if present (some bytecode embeds it).
            if (basePath.compare(0, 7, "__OTR__") == 0) basePath = basePath.substr(7);
            if (tryArchiveWithPath(h, basePath)) return true;
            // Try alt/ prefix variant if not already alt-prefixed. Many
            // mods (MM Young Link, etc.) bundle their assets under alt/
            // even though their DLs reference the bare path.
            if (basePath.compare(0, 4, "alt/") != 0) {
                if (tryArchiveWithPath(h, "alt/" + basePath)) return true;
            } else {
                // Or strip alt/ if reference is alt-prefixed but archive
                // stores bare path.
                if (tryArchiveWithPath(h, basePath.substr(4))) return true;
            }
            return false;
        };
        if (tryArchive(ctx.primary)) return true;
        for (auto* fb : ctx.fallbackArchives) {
            if (tryArchive(*fb)) {
                outUsedFallback = true;
                return true;
            }
        }
        return false;
    };
    for (size_t i = 0; i < cmdCount; ) {
        Gfx& cmd = entry->bytes[i];
        uint8_t op = (uint8_t)(cmd.words.w0 >> 24);
        if (op == G_ENDDL) break;
        size_t advance = IsTwoWordOTRCommand(op) ? 2 : 1;
        switch (op) {
            case G_SETTIMG_OTR_FILEPATH: {
                const char* pathStr = (const char*)cmd.words.w1;
                if (!LooksLikeValidStringPtr((uintptr_t)pathStr)) break;
                std::string pathKey(pathStr);
                std::shared_ptr<Ship::IResource> tex;
                bool usedFallback = false;
                if (!loadResourceWithFallback(pathKey, tex, usedFallback)) {
                    ctx.texturesFailed++;
                    static int sLoggedFails = 0;
                    if (sLoggedFails < 30) {
                        sLoggedFails++;
                        HSS_LOG("patcher MISS: SETTIMG_OTR_FILEPATH path='%s' "
                                "(not in primary archive or fallback)", pathStr);
                    }
                    break;
                }
                auto texPtr = std::dynamic_pointer_cast<Fast::Texture>(tex);
                if (!texPtr || !texPtr->ImageData) {
                    ctx.texturesFailed++;
                    break;
                }
                // Choose patch strategy.
                //
                // When the texture has TEX_FLAG_LOAD_AS_IMG / LOAD_AS_RAW set,
                // ImportTexture branches early into ImportTextureImg /
                // ImportTextureRaw. Those paths use rawTexMetadata (Width,
                // Type, scales) which our raw-pointer pre-patch does NOT
                // populate — runtime sees rawTexMetadata={} → wrong import
                // path → garbage rendering (Gerudo Player's chaotic stripes).
                //
                // So for FLAGGED textures coming from the override's primary
                // archive, leave the OTR opcode intact and rewrite the path
                // string to the wrapper-mounted prefix. Runtime then calls
                // LoadResourceProcess on the prefixed path, hits our globally-
                // mounted wrapper, gets back the full Fast::Texture with
                // populated metadata.
                //
                // Standard (Flags == 0) textures keep the fast raw-pointer
                // path — no metadata needed and one less LoadResourceProcess
                // call per draw.
                //
                // Fallback textures (sourced from oot.o2r) always raw-patch:
                // they're already in the global stack at their canonical
                // paths, so rewriting to a prefix would be wrong.
                bool needsRuntimeMetadata = (texPtr->Flags != 0) && !ctx.harpoonSyncPrefix.empty() && !usedFallback;
                if (needsRuntimeMetadata) {
                    // Determine the inner-archive path that actually loaded.
                    // The Texture resource's InitData->Path tells us. Strip
                    // any leading "alt/" from comparison logic — we always
                    // want the EXACT name the wrapper indexed.
                    std::string innerPath;
                    if (tex->GetInitData() != nullptr) {
                        innerPath = tex->GetInitData()->Path;
                    }
                    if (innerPath.empty()) {
                        // Couldn't recover the inner path — fall through to
                        // raw-pointer patch and accept the risk that this
                        // particular texture renders as garbage.
                    } else {
                        // Build the rewritten OTR string. Format:
                        //   __OTR__<harpoonSyncPrefix><innerPath>
                        // OtrSignatureCheck strips the leading __OTR__,
                        // then LoadResourceProcess walks alt-assets +
                        // hashes — landing on our wrapper archive.
                        std::string newOtrPath = "__OTR__" + ctx.harpoonSyncPrefix + innerPath;
                        sPatchedPathStrings.push_back(std::make_unique<std::string>(std::move(newOtrPath)));
                        cmd.words.w1 = (uintptr_t)sPatchedPathStrings.back()->c_str();
                        // Keep G_SETTIMG_OTR_FILEPATH opcode unchanged in w0.
                        sPatchedDLResources.push_back(std::move(tex));
                        ctx.texturesResolved++;
                        ctx.texturesViaPrefix++;
                        break;
                    }
                }
                cmd.words.w0 = (cmd.words.w0 & 0x00FFFFFFu) | ((uint32_t)G_SETTIMG << 24);
                cmd.words.w1 = (uintptr_t)texPtr->ImageData;
                sPatchedDLResources.push_back(std::move(tex));
                ctx.texturesResolved++;
                if (usedFallback) ctx.texturesResolvedFromFallback++;
                break;
            }
            case G_SETTIMG_OTR_HASH: {
                // Two-Gfx command: header (this cmd) carries fmt/siz/width in
                // the lower bits of w0; the SECOND Gfx packs the 64-bit
                // resource hash split as (w0=hash_hi32, w1=hash_lo32). The
                // OTRExporter that builds oot.o2r emits this variant for
                // every Player limb DL texture, so without patching them
                // every face / hand / chest texture leaks through global at
                // interpret time.
                if (i + 1 >= cmdCount) break;
                Gfx& dataCmd = entry->bytes[i + 1];
                uint64_t hash = ((uint64_t)dataCmd.words.w0 << 32) |
                                (uint64_t)(uint32_t)dataCmd.words.w1;
                std::shared_ptr<Ship::IResource> tex;
                bool usedFallback = false;
                if (!loadResourceWithFallback(hash, tex, usedFallback)) {
                    ctx.texturesFailed++;
                    static int sLoggedHashFails = 0;
                    if (sLoggedHashFails < 30) {
                        sLoggedHashFails++;
                        HSS_LOG("patcher MISS: SETTIMG_OTR_HASH hash=0x%016llx "
                                "(not in primary or fallback hash maps)",
                                (unsigned long long)hash);
                    }
                    break;
                }
                auto texPtr = std::dynamic_pointer_cast<Fast::Texture>(tex);
                if (!texPtr || !texPtr->ImageData) {
                    ctx.texturesFailed++;
                    break;
                }
                // Same metadata-aware decision as the FILEPATH variant — for
                // flagged textures (TEX_FLAG_LOAD_AS_IMG / LOAD_AS_RAW), the
                // raw-pointer pre-patch loses metadata and renders garbage.
                // Convert HASH→FILEPATH with the prefixed string so runtime
                // LoadResourceProcess returns a fully-populated Fast::Texture.
                bool needsRuntimeMetadata = (texPtr->Flags != 0) && !ctx.harpoonSyncPrefix.empty() && !usedFallback;
                if (needsRuntimeMetadata) {
                    std::string innerPath;
                    if (tex->GetInitData() != nullptr) {
                        innerPath = tex->GetInitData()->Path;
                    }
                    if (!innerPath.empty()) {
                        std::string newOtrPath = "__OTR__" + ctx.harpoonSyncPrefix + innerPath;
                        sPatchedPathStrings.push_back(std::make_unique<std::string>(std::move(newOtrPath)));
                        // Convert opcode to G_SETTIMG_OTR_FILEPATH (the
                        // single-Gfx variant). w0 keeps fmt/size/width bits;
                        // we just swap the opcode byte.
                        cmd.words.w0 = (cmd.words.w0 & 0x00FFFFFFu) |
                                       ((uint32_t)G_SETTIMG_OTR_FILEPATH << 24);
                        cmd.words.w1 = (uintptr_t)sPatchedPathStrings.back()->c_str();
                        dataCmd.words.w0 = (uint32_t)G_NOOP << 24;
                        dataCmd.words.w1 = 0;
                        sPatchedDLResources.push_back(std::move(tex));
                        ctx.texturesResolved++;
                        ctx.texturesViaPrefix++;
                        advance = 1;
                        break;
                    }
                }
                cmd.words.w0 = (cmd.words.w0 & 0x00FFFFFFu) | ((uint32_t)G_SETTIMG << 24);
                cmd.words.w1 = (uintptr_t)texPtr->ImageData;
                dataCmd.words.w0 = (uint32_t)G_NOOP << 24;
                dataCmd.words.w1 = 0;
                sPatchedDLResources.push_back(std::move(tex));
                ctx.texturesResolved++;
                if (usedFallback) ctx.texturesResolvedFromFallback++;
                advance = 1;
                break;
            }
            case G_DL_OTR_FILEPATH: {
                const char* pathStr = (const char*)cmd.words.w1;
                if (!LooksLikeValidStringPtr((uintptr_t)pathStr)) break;
                std::string pathKey(pathStr);
                std::shared_ptr<Ship::IResource> sub;
                bool usedFallback = false;
                if (!loadResourceWithFallback(pathKey, sub, usedFallback)) break;
                auto subDL = std::dynamic_pointer_cast<Fast::DisplayList>(sub);
                if (!subDL) break; // not actually a DisplayList resource
                Gfx* subOriginal = (Gfx*)sub->GetRawPointer();
                if (!subOriginal) break;
                size_t subMax = sub->GetPointerSize() / sizeof(Gfx);
                if (subMax == 0) break;
                Gfx* subPatched = PatchDLForLocalArchive(subOriginal, subMax, ctx, depth + 1);
                cmd.words.w0 = (cmd.words.w0 & 0x00FFFFFFu) | ((uint32_t)G_DL << 24);
                cmd.words.w1 = (uintptr_t)subPatched;
                sPatchedDLResources.push_back(std::move(sub));
                break;
            }
            case G_DL_OTR_HASH: {
                // 2-Gfx: header (with C0(16,1) push/branch flag), data = hash.
                // Load sub-DL, recursively patch, replace with regular G_DL
                // preserving the push/branch flag.
                if (i + 1 >= cmdCount) break;
                Gfx& dataCmd = entry->bytes[i + 1];
                uint64_t hash = ((uint64_t)dataCmd.words.w0 << 32) |
                                (uint64_t)(uint32_t)dataCmd.words.w1;
                std::shared_ptr<Ship::IResource> sub;
                bool usedFallback = false;
                if (!loadResourceWithFallback(hash, sub, usedFallback)) break;
                auto subDL = std::dynamic_pointer_cast<Fast::DisplayList>(sub);
                if (!subDL) break;
                Gfx* subOriginal = (Gfx*)sub->GetRawPointer();
                if (!subOriginal) break;
                size_t subMax = sub->GetPointerSize() / sizeof(Gfx);
                if (subMax == 0) break;
                Gfx* subPatched = PatchDLForLocalArchive(subOriginal, subMax, ctx, depth + 1);
                // Preserve push/branch bit (C0(16,1) — bit 16 of w0).
                uint32_t pushBranchBit = cmd.words.w0 & 0x00010000u;
                cmd.words.w0 = ((uint32_t)G_DL << 24) | pushBranchBit;
                cmd.words.w1 = (uintptr_t)subPatched;
                dataCmd.words.w0 = (uint32_t)G_NOOP << 24;
                dataCmd.words.w1 = 0;
                sPatchedDLResources.push_back(std::move(sub));
                advance = 1;
                break;
            }
            case G_VTX_OTR_FILEPATH: {
                // 2-Gfx: w1 = filename, second Gfx packs (vtxCnt, vtxIdxOff,
                // vtxDataOff). Convert to standard F3DEX2 G_VTX (single Gfx,
                // 0x01). Encoding (from interpreter): C0(12,8) = n,
                // C0(1,7) - C0(12,8) = v0  →  bits 1-7 hold (v0 + n).
                if (i + 1 >= cmdCount) break;
                const char* pathStr = (const char*)cmd.words.w1;
                if (!LooksLikeValidStringPtr((uintptr_t)pathStr)) break;
                Gfx& dataCmd = entry->bytes[i + 1];
                uint32_t vtxCnt = (uint32_t)dataCmd.words.w0;
                uint32_t vtxIdxOff = (uint32_t)(dataCmd.words.w1 >> 16);
                uint32_t vtxDataOff = (uint32_t)(dataCmd.words.w1 & 0xFFFFu);
                std::string pathKey(pathStr);
                std::shared_ptr<Ship::IResource> vres;
                bool usedFallback = false;
                if (!loadResourceWithFallback(pathKey, vres, usedFallback)) break;
                auto vtxRes = std::dynamic_pointer_cast<Fast::Vertex>(vres);
                if (!vtxRes) break;
                void* vtxBaseRaw = vres->GetRawPointer();
                if (!vtxBaseRaw) break;
                // SECURITY: vtxDataOff/vtxCnt come from an attacker-controllable
                // downloaded skin .o2r. Reject any range that would read past the
                // vertex resource — otherwise Fast3D performs an OOB GPU read.
                // Leave the original command untouched so the DL stays valid.
                size_t maxVtx = vres->GetPointerSize() / sizeof(Vtx);
                if ((size_t)vtxDataOff + (size_t)vtxCnt > maxVtx) {
                    HSS_LOG("Rejecting out-of-bounds G_VTX (filepath '%s'): off=%u cnt=%u max=%zu",
                            pathKey.c_str(), vtxDataOff, vtxCnt, maxVtx);
                    break;
                }
                uintptr_t vtxAddr = (uintptr_t)vtxBaseRaw + (uintptr_t)vtxDataOff * sizeof(Vtx);
                cmd.words.w0 = ((uint32_t)G_VTX << 24)
                             | ((vtxCnt & 0xFFu) << 12)
                             | (((vtxIdxOff + vtxCnt) & 0x7Fu) << 1);
                cmd.words.w1 = vtxAddr;
                dataCmd.words.w0 = (uint32_t)G_NOOP << 24;
                dataCmd.words.w1 = 0;
                sPatchedDLResources.push_back(std::move(vres));
                advance = 1;
                break;
            }
            case G_VTX_OTR_HASH: {
                // 2-Gfx: w1 of FIRST Gfx unused (vestigial address); SECOND
                // Gfx packs hash_hi32/lo32. Note: this differs from
                // VTX_OTR_FILEPATH — there's no explicit vtxCnt/idx here
                // (it must be embedded in w0's low bits like a regular
                // G_VTX header). We trust the existing w0 contains the
                // count/index encoding already; we just rewrite opcode and
                // resolve the hash to a raw pointer.
                if (i + 1 >= cmdCount) break;
                Gfx& dataCmd = entry->bytes[i + 1];
                uint64_t hash = ((uint64_t)dataCmd.words.w0 << 32) |
                                (uint64_t)(uint32_t)dataCmd.words.w1;
                std::shared_ptr<Ship::IResource> vres;
                bool usedFallback = false;
                if (!loadResourceWithFallback(hash, vres, usedFallback)) break;
                auto vtxRes = std::dynamic_pointer_cast<Fast::Vertex>(vres);
                if (!vtxRes) break;
                void* vtxPtr = vres->GetRawPointer();
                if (!vtxPtr) break;
                // SECURITY: the vertex count is already encoded in the existing
                // w0 (F3DEX2 G_VTX: bits 12-19 = n). w1 is rewritten to the
                // resource base at index 0, so Fast3D reads vertices 0..n-1. A
                // crafted skin .o2r could claim more vertices than the resource
                // holds → OOB GPU read. Reject and leave the command untouched.
                uint32_t hashVtxCnt = (uint32_t)((cmd.words.w0 >> 12) & 0xFFu);
                size_t maxVtx = vres->GetPointerSize() / sizeof(Vtx);
                if ((size_t)hashVtxCnt > maxVtx) {
                    HSS_LOG("Rejecting out-of-bounds G_VTX (hash): cnt=%u max=%zu",
                            hashVtxCnt, maxVtx);
                    break;
                }
                cmd.words.w0 = (cmd.words.w0 & 0x00FFFFFFu) | ((uint32_t)G_VTX << 24);
                cmd.words.w1 = (uintptr_t)vtxPtr;
                dataCmd.words.w0 = (uint32_t)G_NOOP << 24;
                dataCmd.words.w1 = 0;
                sPatchedDLResources.push_back(std::move(vres));
                advance = 1;
                break;
            }
            case G_MTX_OTR_FILEPATH: {
                // 1-Gfx: w0 holds matrix params in bits 0-7 (XOR'd with
                // F3DEX2_G_MTX_PUSH per the OTR handler), w1 = filename.
                // Standard G_MTX has the same param layout, so we just
                // change the opcode byte.
                const char* pathStr = (const char*)cmd.words.w1;
                if (!LooksLikeValidStringPtr((uintptr_t)pathStr)) break;
                std::string pathKey(pathStr);
                std::shared_ptr<Ship::IResource> mres;
                bool usedFallback = false;
                if (!loadResourceWithFallback(pathKey, mres, usedFallback)) break;
                auto mtxRes = std::dynamic_pointer_cast<Fast::Matrix>(mres);
                if (!mtxRes) break;
                void* mtxPtr = mres->GetRawPointer();
                if (!mtxPtr) break;
                cmd.words.w0 = (cmd.words.w0 & 0x00FFFFFFu) | ((uint32_t)G_MTX << 24);
                cmd.words.w1 = (uintptr_t)mtxPtr;
                sPatchedDLResources.push_back(std::move(mres));
                break;
            }
            case G_MTX_OTR: {
                // 2-Gfx hash variant of G_MTX_OTR_FILEPATH. First Gfx has
                // params in low 8 bits; second has hash.
                if (i + 1 >= cmdCount) break;
                Gfx& dataCmd = entry->bytes[i + 1];
                uint64_t hash = ((uint64_t)dataCmd.words.w0 << 32) |
                                (uint64_t)(uint32_t)dataCmd.words.w1;
                std::shared_ptr<Ship::IResource> mres;
                bool usedFallback = false;
                if (!loadResourceWithFallback(hash, mres, usedFallback)) break;
                auto mtxRes = std::dynamic_pointer_cast<Fast::Matrix>(mres);
                if (!mtxRes) break;
                void* mtxPtr = mres->GetRawPointer();
                if (!mtxPtr) break;
                cmd.words.w0 = (cmd.words.w0 & 0x00FFFFFFu) | ((uint32_t)G_MTX << 24);
                cmd.words.w1 = (uintptr_t)mtxPtr;
                dataCmd.words.w0 = (uint32_t)G_NOOP << 24;
                dataCmd.words.w1 = 0;
                sPatchedDLResources.push_back(std::move(mres));
                advance = 1;
                break;
            }
            default:
                break;
        }
        i += advance;
    }
    return outDL;
}

// Pre-load vanilla Link / equipment / item DLs from one main game archive so
// MISSes during a remote-dummy draw can fall back to vanilla instead of the
// globally-mounted local user mods. We only cache paths under object_link_*/,
// object_gi_*, and object_sword*/object_shield* — those are the asset
// namespaces a per-actor skin override could reasonably touch. Other paths
// (scenes, NPCs, environment textures) keep going through the normal global
// resolution so legitimate local-only mods (texture packs, etc.) still apply
// to the world.
static void CacheVanillaFromArchive(const std::string& archivePath) {
    auto archive = std::make_shared<Ship::O2rArchive>(archivePath);
    if (!archive->Open()) {
        HSS_LOG("vanilla cache: failed to open '%s'", archivePath.c_str());
        return;
    }
    auto allFiles = archive->ListFiles();
    if (!allFiles) return;

    auto resourceManager = Ship::Context::GetRawInstance()->GetResourceManager();
    if (!resourceManager) return;
    auto loader = resourceManager->GetResourceLoader();
    if (!loader) return;

    PatchContext vanillaCtx{};
    vanillaCtx.primary.archive = archive.get();
    vanillaCtx.loader = loader.get();
    for (auto& [hash, path] : *allFiles) {
        vanillaCtx.primary.localHashMap[hash] = path;
    }
    // Vanilla DLs reference vanilla textures within the same archive; no
    // fallback needed (any miss is a real error rather than a shared-asset
    // not in this archive).
    s32 added = 0;
    for (auto& [hash, path] : *allFiles) {
        if (path.find(".meta") != std::string::npos) continue;
        // Restrict to player-skin / equipment / get-item namespaces — anything
        // else stays under global resolution so texture packs etc. still work
        // for the world. We DON'T filter by name suffix anymore: community
        // skin .o2rs use a wide variety of conventions (gXxxxDL,
        // *_layer_Opaque, bone${N}_*_mesh, ...) and we want vanilla coverage
        // for every path the engine could query when drawing the dummy.
        bool isInteresting = (path.find("object_link_boy/") != std::string::npos ||
                              path.find("object_link_child/") != std::string::npos ||
                              path.find("object_gi_") != std::string::npos ||
                              path.find("object_sword") != std::string::npos ||
                              path.find("object_shield") != std::string::npos);
        if (!isInteresting) continue;
        // Skip the Link skeleton paths — those are FlexSkeletonHeader, not
        // Gfx*, and they're handled separately below via
        // ExtractVanillaSkeletonFromArchive so the dummy can swap to vanilla
        // limbs at draw time.
        if (path.find("/gLinkAdultSkel") != std::string::npos ||
            path.find("/gLinkChildSkel") != std::string::npos) {
            continue;
        }

        auto file = archive->LoadFile(path);
        if (!file || !file->IsLoaded || !file->Buffer || file->Buffer->empty()) continue;

        std::shared_ptr<Ship::IResource> resource;
        try {
            resource = loader->LoadResource(path, file, nullptr);
        } catch (...) {
            resource = nullptr;
        }
        if (!resource) continue;
        Gfx* dl = (Gfx*)resource->GetRawPointer();
        if (!dl) continue;
        // Same DL-only guard as in LoadO2rOverride: walking a non-DL byte
        // buffer as Gfx commands runs off the end of its allocation.
        vanillaCtx.dlsAttempted++;
        if (auto dlRes = std::dynamic_pointer_cast<Fast::DisplayList>(resource)) {
            size_t maxCmds = resource->GetPointerSize() / sizeof(Gfx);
            if (maxCmds > 0) {
                Gfx* patched = PatchDLForLocalArchive(dl, maxCmds, vanillaCtx, 0);
                if (patched && patched != dl) {
                    dl = patched;
                    vanillaCtx.dlsPatched++;
                }
            }
        } else {
            vanillaCtx.dlsSkippedNotDL++;
        }

        // Match the same key variants we store for override DLs so the lookup
        // is symmetric.
        std::string normalized = path;
        if (normalized.compare(0, 7, "__OTR__") == 0) normalized.erase(0, 7);
        sVanillaDLs[std::string("__OTR__") + normalized] = dl;
        sVanillaDLs[normalized] = dl;
        sVanillaResourceHolders.push_back(std::move(resource));
        added++;
    }
    // While the archive is still open, also pull the vanilla Link skeletons
    // straight out of it. Going through the global ResourceManager doesn't
    // work here because user mods (e.g. ./mods/SM64 Mario Adult.otr) get
    // mounted at app startup BEFORE InitO2rOverrides runs, so a global lookup
    // would return the modded skel. Reading bytes directly from the local
    // archive sidesteps the global stack entirely.
    if (sVanillaAdultLimbTable == nullptr) {
        ExtractVanillaSkeletonFromArchive(archive.get(), "objects/object_link_boy/gLinkAdultSkel",
                                          sVanillaAdultSkelHolder, sVanillaAdultLimbTable,
                                          sVanillaAdultDListCount);
    }
    if (sVanillaChildLimbTable == nullptr) {
        ExtractVanillaSkeletonFromArchive(archive.get(), "objects/object_link_child/gLinkChildSkel",
                                          sVanillaChildSkelHolder, sVanillaChildLimbTable,
                                          sVanillaChildDListCount);
    }

    // Vanilla eye + mouth texture image data — pulled directly from this
    // archive (oot.o2r) so they cannot be intercepted by the local user's
    // globally-mounted skin mods at interpret time.
    static const char* kEyePaths[2][8] = {
        { "objects/object_link_boy/gLinkAdultEyesOpenTex",
          "objects/object_link_boy/gLinkAdultEyesHalfTex",
          "objects/object_link_boy/gLinkAdultEyesClosedfTex",
          "objects/object_link_boy/gLinkAdultEyesRollLeftTex",
          "objects/object_link_boy/gLinkAdultEyesRollRightTex",
          "objects/object_link_boy/gLinkAdultEyesShockTex",
          "objects/object_link_boy/gLinkAdultEyesUnk1Tex",
          "objects/object_link_boy/gLinkAdultEyesUnk2Tex" },
        { "objects/object_link_child/gLinkChildEyesOpenTex",
          "objects/object_link_child/gLinkChildEyesHalfTex",
          "objects/object_link_child/gLinkChildEyesClosedfTex",
          "objects/object_link_child/gLinkChildEyesRollLeftTex",
          "objects/object_link_child/gLinkChildEyesRollRightTex",
          "objects/object_link_child/gLinkChildEyesShockTex",
          "objects/object_link_child/gLinkChildEyesUnk1Tex",
          "objects/object_link_child/gLinkChildEyesUnk2Tex" },
    };
    static const char* kMouthPaths[2][4] = {
        { "objects/object_link_boy/gLinkAdultMouth1Tex",
          "objects/object_link_boy/gLinkAdultMouth2Tex",
          "objects/object_link_boy/gLinkAdultMouth3Tex",
          "objects/object_link_boy/gLinkAdultMouth4Tex" },
        { "objects/object_link_child/gLinkChildMouth1Tex",
          "objects/object_link_child/gLinkChildMouth2Tex",
          "objects/object_link_child/gLinkChildMouth3Tex",
          "objects/object_link_child/gLinkChildMouth4Tex" },
    };
    auto loadFaceTex = [&](const char* path, void*& outImageData) {
        if (outImageData != nullptr) return; // already loaded from a prior archive
        auto file = archive->LoadFile(path);
        if (!file || !file->IsLoaded || !file->Buffer || file->Buffer->empty()) return;
        std::shared_ptr<Ship::IResource> res;
        try {
            res = loader->LoadResource(path, file, nullptr);
        } catch (...) {
            res = nullptr;
        }
        if (!res) return;
        auto tex = std::dynamic_pointer_cast<Fast::Texture>(res);
        if (!tex || !tex->ImageData) return;
        outImageData = tex->ImageData;
        sVanillaFaceTexResources.push_back(std::move(res));
    };
    int eyesLoaded = 0, mouthsLoaded = 0;
    for (int age = 0; age < 2; age++) {
        for (int i = 0; i < 8; i++) {
            loadFaceTex(kEyePaths[age][i], sVanillaEyeImageData[age][i]);
            if (sVanillaEyeImageData[age][i]) eyesLoaded++;
        }
        for (int i = 0; i < 4; i++) {
            loadFaceTex(kMouthPaths[age][i], sVanillaMouthImageData[age][i]);
            if (sVanillaMouthImageData[age][i]) mouthsLoaded++;
        }
    }
    if (eyesLoaded || mouthsLoaded) {
        HSS_LOG("vanilla face cache: eyes=%d/16 mouths=%d/8", eyesLoaded, mouthsLoaded);
    }

    // Build a vanilla ArchiveHandle (with its own hash map) BEFORE moving
    // the archive into sVanillaArchives — so subsequent override loads can
    // use this as a fallback that resolves vanilla shared assets locally.
    auto vanillaHandle = std::make_unique<ArchiveHandle>();
    vanillaHandle->archive = archive.get();
    vanillaHandle->localHashMap = std::move(vanillaCtx.primary.localHashMap);
    sVanillaArchiveHandles.push_back(std::move(vanillaHandle));
    sVanillaArchives.push_back(std::move(archive));
    HSS_LOG("vanilla cache: '%s' loaded %d entries (dlsAttempted=%u patched=%u skippedNotDL=%u textures=%u failed=%u)",
            archivePath.c_str(), added,
            (unsigned)vanillaCtx.dlsAttempted, (unsigned)vanillaCtx.dlsPatched,
            (unsigned)vanillaCtx.dlsSkippedNotDL, (unsigned)vanillaCtx.texturesResolved,
            (unsigned)vanillaCtx.texturesFailed);
}

// Pulls a single skeleton resource straight out of a directly-opened .o2r,
// completely bypassing the global ResourceManager / ArchiveManager — the
// bytes are read from the local `archive` and handed to ResourceLoader
// directly, so any user mods that were already mounted (.otr loaded at app
// startup, .o2r mounted by mod_menu, etc.) cannot intercept the resolution.
// The returned IResource is the holder for limbTable's memory; caller must
// keep the shared_ptr alive for the duration of the cache.
static bool ExtractVanillaSkeletonFromArchive(Ship::Archive* archive, const std::string& path,
                                              std::shared_ptr<Ship::IResource>& outHolder,
                                              void**& outLimbTable, int& outDListCount) {
    if (!archive) return false;
    auto resourceManager = Ship::Context::GetRawInstance()->GetResourceManager();
    if (!resourceManager) return false;
    auto loader = resourceManager->GetResourceLoader();
    if (!loader) return false;

    auto file = archive->LoadFile(path);
    if (!file || !file->IsLoaded || !file->Buffer || file->Buffer->empty()) {
        HSS_LOG("vanilla skel cache: failed to LoadFile '%s'", path.c_str());
        return false;
    }
    std::shared_ptr<Ship::IResource> resource;
    try {
        resource = loader->LoadResource(path, file, nullptr);
    } catch (...) {
        resource = nullptr;
    }
    if (!resource) {
        HSS_LOG("vanilla skel cache: ResourceLoader returned null for '%s'", path.c_str());
        return false;
    }
    // Strict type validation. Player_DrawImpl uses SkelAnime_DrawFlexLod
    // which casts every limb to LodLimb (with `dLists[2]`). Walking a
    // skeleton whose limbType is Standard / Skin / Curve corrupts memory
    // and crashes (rootLimb->dLists[lod] reads past the StandardLimb's
    // 0xC-byte struct). MM Young Link's child skel + Mario adult/child
    // .otr skels all need to pass this check, otherwise we fall back to
    // vanilla skel walking instead of swapping in the override skel.
    auto skelRes = std::dynamic_pointer_cast<SOH::Skeleton>(resource);
    if (!skelRes) {
        HSS_LOG("skel cache: '%s' is not a SOH::Skeleton resource — rejected", path.c_str());
        return false;
    }
    if (skelRes->type != SOH::SkeletonType::Flex) {
        HSS_LOG("skel cache: '%s' type=%d (expected Flex=1) — rejected",
                path.c_str(), (int)skelRes->type);
        return false;
    }
    if (skelRes->limbType != SOH::LimbType::LOD) {
        HSS_LOG("skel cache: '%s' limbType=%d (expected LOD=2) — rejected",
                path.c_str(), (int)skelRes->limbType);
        return false;
    }
    auto* hdr = reinterpret_cast<FlexSkeletonHeader*>(resource->GetRawPointer());
    if (!hdr || !hdr->sh.segment) {
        HSS_LOG("skel cache: '%s' parsed but has null limb table — rejected", path.c_str());
        return false;
    }
    // CRITICAL: SOH's SkeletonFactory at SkeletonFactory.cpp:49-50 resolves
    // each limb via the GLOBAL ResourceManager. This means:
    //   - For overrides loaded from an UNMOUNTED archive: factory returns
    //     nullptr (path not in global) → segment[i] = nullptr → crash.
    //   - For VANILLA loaded from oot.o2r: factory returns whatever's
    //     globally mounted at that path. If the local user has a Link
    //     mod (e.g. Mario.otr) in mods/, vanilla limb paths resolve to
    //     MARIO'S limbs! Result: our "vanilla skel" silently walks Mario
    //     limbs → dummy renders as Mario chimera.
    //
    // Fix: ALWAYS overwrite segment[i] with the limb loaded from OUR
    // local archive, regardless of whether the factory put something
    // there. This makes the skel truly self-contained from `archive`,
    // never inheriting from the global stack.
    //
    // Keep the limb IResource shared_ptrs alive in sPatchedDLResources so
    // the limb structure memory stays valid for the dummy's lifetime.
    int patchedLimbs = 0, factoryHadIt = 0, stillNull = 0;
    for (size_t i = 0; i < skelRes->limbTable.size() && i < skelRes->skeletonHeaderSegments.size(); i++) {
        if (skelRes->skeletonHeaderSegments[i] != nullptr) {
            factoryHadIt++;
            // DON'T continue — fall through and try local. If local works,
            // overwrite with local pointer. If not, keep the existing one
            // (might be from global stack — leak risk but at least non-null).
        }
        // The skel binary may store limb paths with the __OTR__ prefix
        // (SOH style), but the archive's file list doesn't include that
        // prefix. Strip it before calling LoadFile.
        std::string limbPath = skelRes->limbTable[i];
        if (limbPath.compare(0, 7, "__OTR__") == 0) {
            limbPath = limbPath.substr(7);
        }
        // Try the bare path first, then fall back to the `alt/` variant
        // (or the alt-stripped variant if the path already starts with
        // `alt/`). Community packers split skin assets between bare and
        // alt-prefixed namespaces depending on the tooling that produced
        // the .o2r — without trying both, MM-style child skels (whose
        // header path is `alt/.../gLinkChildSkel` but whose limbTable
        // entries can lack the alt prefix on some packers) end up with
        // 21/21 stillNull limbs and the override skel gets rejected,
        // forcing the dummy back to vanilla geometry.
        auto tryLoadLimb = [&](const std::string& p,
                               std::shared_ptr<Ship::IResource>& outRes,
                               std::string& usedPath) -> bool {
            auto f = archive->LoadFile(p);
            if (!f || !f->IsLoaded || !f->Buffer || f->Buffer->empty()) return false;
            try {
                outRes = loader->LoadResource(p, f, nullptr);
            } catch (...) {
                outRes = nullptr;
            }
            if (outRes) usedPath = p;
            return outRes != nullptr;
        };
        std::shared_ptr<Ship::IResource> limbRes;
        std::string usedLimbPath;
        bool ok = tryLoadLimb(limbPath, limbRes, usedLimbPath);
        if (!ok) {
            if (limbPath.compare(0, 4, "alt/") == 0) {
                ok = tryLoadLimb(limbPath.substr(4), limbRes, usedLimbPath);
            } else {
                ok = tryLoadLimb("alt/" + limbPath, limbRes, usedLimbPath);
            }
        }
        if (!ok || !limbRes) {
            if (skelRes->skeletonHeaderSegments[i] == nullptr) stillNull++;
            continue;
        }
        void* limbRaw = limbRes->GetRawPointer();
        if (!limbRaw) {
            if (skelRes->skeletonHeaderSegments[i] == nullptr) stillNull++;
            continue;
        }
        // Sanitize the limb's dLists fields BEFORE publishing the pointer —
        // community packers occasionally produce LodLimbs whose dLists[i]
        // contains ASCII text or other non-pointer garbage. Once those
        // pointers are visible to SkelAnime_DrawFlexLimbLod, the engine will
        // pass them straight to gSPDisplayList and crash on the OTR magic
        // dereference. NULL-ing them here makes that limb render nothing
        // but keeps the rest of the dummy alive.
        SanitizeLodLimbDLists(limbRaw, path.c_str(), (int)i);
        skelRes->skeletonHeaderSegments[i] = limbRaw;
        sPatchedDLResources.push_back(std::move(limbRes));
        patchedLimbs++;
    }
    // Belt-and-suspenders: also sanitize whatever the SkeletonFactory left
    // for us (factoryHadIt path). Some override skeletons get partially
    // populated by the global stack — if the local archive load below didn't
    // overwrite a particular slot, the factory's limb pointer is still in
    // skeletonHeaderSegments[i]. Validate it too so we cover both paths.
    for (size_t i = 0; i < skelRes->skeletonHeaderSegments.size(); i++) {
        SanitizeLodLimbDLists(skelRes->skeletonHeaderSegments[i], path.c_str(), (int)i);
    }
    HSS_LOG("skel cache: '%s' limb patch — factoryHadIt=%d patchedLocally=%d stillNull=%d",
            path.c_str(), factoryHadIt, patchedLimbs, stillNull);
    // Now require root non-null after the local patch attempt.
    if (hdr->sh.segment[0] == nullptr) {
        HSS_LOG("skel cache: '%s' root limb still null after local patch — rejected", path.c_str());
        return false;
    }
    // Vanilla Link is 21 limbs / 18 dLists. Accept anything in a sane
    // range; dummy's jointTable is sized at Init for vanilla so we trust
    // the engine to handle small differences. Strict equality would
    // reject vanilla itself.
    if (hdr->sh.limbCount < 1 || hdr->sh.limbCount > 32) {
        HSS_LOG("skel cache: '%s' limbCount=%u out of sane range — rejected",
                path.c_str(), (unsigned)hdr->sh.limbCount);
        return false;
    }
    if (hdr->dListCount < 1 || hdr->dListCount > 32) {
        HSS_LOG("skel cache: '%s' dListCount=%u out of sane range — rejected",
                path.c_str(), (unsigned)hdr->dListCount);
        return false;
    }
    outHolder = resource;
    outLimbTable = hdr->sh.segment;
    outDListCount = hdr->dListCount;
    HSS_LOG("vanilla skel cache: '%s' limbs=%u dLists=%u (from local archive)",
            path.c_str(), (unsigned)hdr->sh.limbCount, (unsigned)hdr->dListCount);
    return true;
}

} // anonymous namespace

namespace HarpoonSkinSync {

void InitO2rOverrides() {
    // Idempotent: the scan + texture pre-resolve takes 1-2 seconds because
    // it walks every Player DL in oot.o2r plus every override .o2r, so we
    // only run it once per process. Called from Harpoon::OnConnected() the
    // first time the user joins a session — subsequent reconnects are no-ops.
    if (sInitialized) return;
    sInitialized = true;

    sOverrides.clear();
    sActiveOverrideIndices.clear();
    sVanillaDLs.clear();
    sVanillaResourceHolders.clear();
    sVanillaArchives.clear();
    sVanillaArchiveHandles.clear();
    sPatchedDLStorage.clear();
    sPatchedDLResources.clear();
    sPatchedPathStrings.clear();
    sVanillaAdultSkelHolder.reset();
    sVanillaChildSkelHolder.reset();
    sVanillaAdultLimbTable = nullptr;
    sVanillaChildLimbTable = nullptr;
    sVanillaAdultDListCount = 0;
    sVanillaChildDListCount = 0;
    sVanillaFaceTexResources.clear();
    for (int a = 0; a < 2; a++) {
        for (int i = 0; i < 8; i++) sVanillaEyeImageData[a][i] = nullptr;
        for (int i = 0; i < 4; i++) sVanillaMouthImageData[a][i] = nullptr;
    }

    // Pre-cache vanilla Link/equipment DLs AND the vanilla Link skeleton
    // from the main game archive(s). We open each archive directly (no
    // AddArchive) so the global path-resolution stack is bypassed — critical
    // because user mods (.otr files in mods/) are auto-mounted at app
    // startup BEFORE this function runs, and a normal LoadResource() call
    // would return the modded skeleton instead of vanilla.
    for (const char* gameOtr : { "oot.o2r", "oot-mq.o2r" }) {
        std::string p = Ship::Context::LocateFileAcrossAppDirs(gameOtr, appShortName);
        if (!p.empty()) CacheVanillaFromArchive(p);
    }

    // ALSO open mm.o2r and soh.o2r as additional fallback archives for the
    // patcher — community Link mods (Mario, MM Young Link, etc.) often
    // reference textures stored in these shared SOH/MM bundles (e.g.
    // hand_ci8_png_pal_rgba16, gauntlet_*_ci8_png_pal_rgba16). When my
    // hook resolves an override DL's texture path and oot.o2r doesn't
    // have it, falling back to mm.o2r / soh.o2r is what the global stack
    // does for the local user, so we must do the same for the dummy.
    // Note: don't call CacheVanillaFromArchive — these aren't vanilla Link
    // archives (they'd contaminate the skel/face caches with wrong data).
    // Just open + register as ArchiveHandle for the patcher's fallback.
    for (const char* sharedOtr : { "mm.o2r", "soh.o2r" }) {
        std::string p = Ship::Context::LocateFileAcrossAppDirs(sharedOtr, appShortName);
        if (p.empty()) continue;
        auto sharedArchive = std::make_shared<Ship::O2rArchive>(p);
        if (!sharedArchive->Open()) {
            HSS_LOG("shared fallback: failed to open '%s'", p.c_str());
            continue;
        }
        auto allFiles = sharedArchive->ListFiles();
        if (!allFiles) continue;
        auto handle = std::make_unique<ArchiveHandle>();
        handle->archive = sharedArchive.get();
        for (auto& [hash, filePath] : *allFiles) {
            handle->localHashMap[hash] = filePath;
        }
        sVanillaArchiveHandles.push_back(std::move(handle));
        sVanillaArchives.push_back(std::move(sharedArchive));
        HSS_LOG("shared fallback: opened '%s' with %zu entries",
                p.c_str(), allFiles->size());
    }

    auto syncPath = FindSyncFolder();
    if (syncPath.empty()) {
        HSS_LOG("No harpoon/skins/ folder found; override registry empty. "
                "Place .o2r skin packs under harpoon/skins/ to render other players' Link skins.");
        return;
    }
    HSS_LOG("Scanning '%s' for .o2r/.otr overrides", syncPath.string().c_str());

    std::vector<std::filesystem::path> o2rFiles;
    // This runs inside Harpoon::OnConnected() the instant a player joins. A
    // symlink cycle, an unreadable subdir, or a file deleted mid-scan makes the
    // throwing recursive_directory_iterator raise filesystem_error, which would
    // escape the network callback and std::terminate the whole game. Use the
    // non-throwing (error_code) overload AND wrap the body so any failure logs
    // and degrades to an empty/partial registry instead of crashing.
    {
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(
            syncPath, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec) {
            HSS_LOG("Failed to open '%s' for scanning (%s); override registry empty",
                    syncPath.string().c_str(), ec.message().c_str());
        } else {
            const std::filesystem::recursive_directory_iterator end;
            for (; it != end; it.increment(ec)) {
                if (ec) {
                    // Could not advance (e.g. symlink cycle / vanished dir).
                    // Stop walking but keep whatever we already collected.
                    HSS_LOG("Stopped scanning '%s' early: %s",
                            syncPath.string().c_str(), ec.message().c_str());
                    break;
                }
                try {
                    const auto& entry = *it;
                    std::error_code isDirEc;
                    if (entry.is_directory(isDirEc) || isDirEc) continue;
                    std::string ext = entry.path().extension().string();
                    for (char& c : ext) c = (char)tolower((unsigned char)c);
                    if (ext == ".o2r" || ext == ".otr") o2rFiles.push_back(entry.path());
                } catch (const std::exception& e) {
                    // Defensive: any per-entry failure must not kill the join.
                    HSS_LOG("Skipping unreadable entry under '%s': %s",
                            syncPath.string().c_str(), e.what());
                    continue;
                }
            }
        }
    }
    HSS_LOG("Found %d archive files (.o2r/.otr)", (int)o2rFiles.size());

    sOverrides.reserve(o2rFiles.size());
    for (auto& p : o2rFiles) {
        O2rOverride entry;
        if (LoadO2rOverride(p, entry)) {
            sOverrides.push_back(std::move(entry));
        }
    }
    HSS_LOG("Override registry populated with %d skins", (int)sOverrides.size());

    // DEBUG: probe whether canonical Player hand-DL paths landed in the
    // vanilla cache. The user's bug ("dummy renders with LOCAL user's hands")
    // depends on these being in sVanillaDLs — if they are, GbiWrap's
    // gSPDisplayList wrapper hits HarpoonSkinSync_GetDLOverride which returns
    // the patched-vanilla copy and short-circuits the global ArchiveManager
    // lookup. If they're NOT in the cache, we fall through to global, the
    // local user's mods/ wins, and the dummy inherits the local skin's hands.
    static const char* kProbeHandDLs[] = {
        "__OTR__objects/object_link_boy/gLinkAdultLeftHandNearDL",
        "__OTR__objects/object_link_boy/gLinkAdultRightHandNearDL",
        "__OTR__objects/object_link_boy/gLinkAdultLeftHandClosedNearDL",
        "__OTR__objects/object_link_boy/gLinkAdultRightHandClosedNearDL",
        "__OTR__objects/object_link_child/gLinkChildLeftHandNearDL",
        "__OTR__objects/object_link_child/gLinkChildRightHandNearDL",
        "__OTR__objects/object_link_child/gLinkChildLeftFistNearDL",
        "__OTR__objects/object_link_child/gLinkChildRightHandClosedNearDL",
    };
    int handCachedCount = 0;
    for (const char* p : kProbeHandDLs) {
        bool present = sVanillaDLs.count(p) > 0;
        HSS_LOG("vanilla cache probe: %s -> %s",
                p, present ? "PRESENT (would intercept)" : "MISSING (will leak local mod!)");
        if (present) handCachedCount++;
    }
    HSS_LOG("vanilla cache hand-DL probe: %d/%d cached", handCachedCount,
            (int)(sizeof(kProbeHandDLs) / sizeof(kProbeHandDLs[0])));
}

void BeginRemoteOverrides(const std::vector<std::string>& enabledMods) {
    sActiveOverrideIndices.clear();
    sInRemoteDraw = true; // gate the vanilla fallback regardless of match count
    if (sOverrides.empty()) return;
    std::string matched;
    for (const auto& modName : enabledMods) {
        for (size_t i = 0; i < sOverrides.size(); i++) {
            if (sOverrides[i].name == modName) {
                sActiveOverrideIndices.push_back(i);
                if (!matched.empty()) matched += ", ";
                matched += modName;
                break;
            }
        }
    }
    // Once-per-(unique fingerprint) log so we can see exactly which overrides
    // are active without spamming each frame.
    static std::string sLastMatched;
    if (sLastMatched != matched) {
        HSS_LOG("BeginRemoteOverrides: enabledMods=%d active=%d [%s]",
                (int)enabledMods.size(), (int)sActiveOverrideIndices.size(), matched.c_str());
        sLastMatched = matched;
    }
}

void EndRemoteOverrides() {
    sActiveOverrideIndices.clear();
    sInRemoteDraw = false;
}

void NotifyMissingPak(uint32_t clientId, const std::string& playerName, const std::string& skinName) {
    if (skinName.empty()) return;
    std::string key = "missing:" + std::to_string(clientId) + ":" + skinName;
    if (!ShouldNotify(key)) return;

    Notification::Emit({
        .prefix = playerName,
        .prefixColor = ImVec4(0.7f, 0.9f, 1.0f, 1.0f),
        .message = "uses skin",
        .suffix = "'" + skinName + "' (not installed)",
        .suffixColor = ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
        .remainingTime = 6.0f,
        .mute = true,
    });
}

// Returns true when an .o2r matching `modName` exists in our override registry,
// regardless of whether it's currently active. Suppresses the divergence
// notification for that mod because we WILL render the dummy with the
// override applied — the user does have it, just not as a global mod.
static bool HaveOverrideForName(const std::string& modName) {
    for (const auto& o : sOverrides) {
        if (o.name == modName) return true;
    }
    return false;
}

void NotifyO2rDivergence(uint32_t clientId, const std::string& playerName,
                         const std::vector<std::string>& remoteMods,
                         const std::vector<std::string>& remoteSyncMods) {
    const auto& localMods = ModMenu_GetEnabledMods();
    std::set<std::string> localSet(localMods.begin(), localMods.end());
    std::set<std::string> remoteSet(remoteMods.begin(), remoteMods.end());
    std::set<std::string> remoteSyncSet(remoteSyncMods.begin(), remoteSyncMods.end());

    for (const auto& m : remoteSet) {
        if (localSet.count(m)) continue;
        if (HaveOverrideForName(m)) continue; // we can render their dummy with our override
        std::string key = "divR:" + std::to_string(clientId) + ":" + m;
        if (!ShouldNotify(key)) continue;
        Notification::Emit({
            .prefix = playerName,
            .prefixColor = ImVec4(0.7f, 0.9f, 1.0f, 1.0f),
            .message = "has mod",
            .suffix = "'" + m + "' you can't render — visuals may differ",
            .suffixColor = ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
            .remainingTime = 6.0f,
            .mute = true,
        });
    }

    for (const auto& m : localSet) {
        if (remoteSet.count(m)) continue;
        // Suppress when the remote has our mod in their harpoon/skins —
        // they CAN render us correctly even though they haven't enabled it
        // globally. This is the common case the user complained about: the
        // notification was firing despite both clients having each other's
        // mods available for sync rendering.
        if (remoteSyncSet.count(m)) continue;
        std::string key = "divL:" + std::to_string(clientId) + ":" + m;
        if (!ShouldNotify(key)) continue;
        Notification::Emit({
            .prefix = "You",
            .prefixColor = ImVec4(0.7f, 0.9f, 1.0f, 1.0f),
            .message = "have mod",
            .suffix = "'" + m + "' that " + playerName + " can't render",
            .suffixColor = ImVec4(0.7f, 0.8f, 1.0f, 1.0f),
            .remainingTime = 6.0f,
            .mute = true,
        });
    }
}

std::vector<std::string> GetOverrideNames() {
    std::vector<std::string> names;
    names.reserve(sOverrides.size());
    for (const auto& o : sOverrides) {
        names.push_back(o.name);
    }
    return names;
}

// Cached list of installed gamemode pack ids (folder names under
// harpoon/gamemodes/ that contain a gamemode.yaml).
static std::vector<std::string> sInstalledGamemodes;
static bool sGamemodesCached = false;

std::filesystem::path GetGamemodeManifestPath(const std::string& gamemodeId) {
    if (gamemodeId.empty()) return {};
    auto root = FindGamemodesFolder();
    if (root.empty()) return {};
    auto manifest = root / gamemodeId / "gamemode.yaml";
    std::error_code ec;
    if (!std::filesystem::exists(manifest, ec) || !std::filesystem::is_regular_file(manifest, ec)) {
        return {};
    }
    return manifest;
}

std::vector<std::string> GetInstalledGamemodes(bool forceRescan) {
    // Only honour the cache when it actually found packs. A first call that
    // ran before the user dropped any gamemode in (or before the folder was
    // auto-created) would otherwise leave the dropdown permanently empty
    // until they clicked "Refresh" — which the user has no reason to do
    // when the folder visibly contains packs.
    if (sGamemodesCached && !forceRescan && !sInstalledGamemodes.empty()) {
        return sInstalledGamemodes;
    }
    sInstalledGamemodes.clear();

    auto root = FindGamemodesFolder();
    if (root.empty()) {
        sGamemodesCached = true;
        return sInstalledGamemodes;
    }

    std::error_code ec;
    for (auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_directory()) continue;
        auto manifest = entry.path() / "gamemode.yaml";
        std::error_code ec2;
        if (std::filesystem::exists(manifest, ec2) && std::filesystem::is_regular_file(manifest, ec2)) {
            sInstalledGamemodes.push_back(entry.path().filename().string());
        }
    }
    std::sort(sInstalledGamemodes.begin(), sInstalledGamemodes.end());
    sGamemodesCached = true;
    HSS_LOG("Found %d gamemode pack(s) in %s",
            (int)sInstalledGamemodes.size(), root.string().c_str());
    return sInstalledGamemodes;
}

void** GetVanillaLinkLimbTable(bool isAdult) {
    return isAdult ? sVanillaAdultLimbTable : sVanillaChildLimbTable;
}

int GetVanillaLinkDListCount(bool isAdult) {
    return isAdult ? sVanillaAdultDListCount : sVanillaChildDListCount;
}

// Returns the first active override's Link limb table for `isAdult` if any
// override has one, else nullptr. Caller (HarpoonDummyPlayer_Draw) prefers
// this over vanilla so override-specific limb DL paths (custom bone names)
// get queried by the engine — letting overrides like MM Young Link actually
// render with their own mesh structure rather than chimera-blending into
// vanilla.
void** GetActiveOverrideLinkLimbTable(bool isAdult) {
    for (size_t idx : sActiveOverrideIndices) {
        if (idx >= sOverrides.size()) continue;
        const auto& o = sOverrides[idx];
        void** lt = isAdult ? o.adultLimbTable : o.childLimbTable;
        if (lt) return lt;
    }
    return nullptr;
}

int GetActiveOverrideLinkDListCount(bool isAdult) {
    for (size_t idx : sActiveOverrideIndices) {
        if (idx >= sOverrides.size()) continue;
        const auto& o = sOverrides[idx];
        int dl = isAdult ? o.adultDListCount : o.childDListCount;
        if (dl > 0) return dl;
    }
    return 0;
}

void Reset() {
    sNotified.clear();
    sActiveOverrideIndices.clear();
    sInRemoteDraw = false;
}

} // namespace HarpoonSkinSync

// C-linkage hooks called from pak_loader's PakLoader_GetEyeTexture /
// GetMouthTexture so the dummy's segment-0x08 / segment-0x09 references
// resolve against vanilla pixel data we pre-loaded out of oot.o2r rather
// than going through the global ResourceManager (which would pick up the
// LOCAL user's modded eye / mouth textures and paint them on the REMOTE
// dummy's face). Returns NULL when not in a remote-dummy draw block, so
// the local player's own draw still uses its normal pak / vanilla path.
extern "C" void* HarpoonSkinSync_GetVanillaEyeTexture(int32_t eyeIndex, int32_t isAdult) {
    if (!sInRemoteDraw) return nullptr;
    if (eyeIndex < 0 || eyeIndex >= 8) return nullptr;
    int age = isAdult ? 0 : 1;
    // Prefer the active override's eye texture (so Mario dummy gets Mario's
    // eyes, MM Young Link's dummy gets MMYL's eyes, etc.). Fall back to
    // vanilla pre-resolved bytes when no override has it.
    for (size_t idx : sActiveOverrideIndices) {
        if (idx >= sOverrides.size()) continue;
        void* p = sOverrides[idx].eyeImageData[age][eyeIndex];
        if (p) return p;
    }
    return sVanillaEyeImageData[age][eyeIndex];
}

extern "C" void* HarpoonSkinSync_GetVanillaMouthTexture(int32_t mouthIndex, int32_t isAdult) {
    if (!sInRemoteDraw) return nullptr;
    if (mouthIndex < 0 || mouthIndex >= 4) return nullptr;
    int age = isAdult ? 0 : 1;
    for (size_t idx : sActiveOverrideIndices) {
        if (idx >= sOverrides.size()) continue;
        void* p = sOverrides[idx].mouthImageData[age][mouthIndex];
        if (p) return p;
    }
    return sVanillaMouthImageData[age][mouthIndex];
}

// C-linkage hook called from z_player_lib.c's hand / sheath / waist limb
// branches in Player_OverrideLimbDrawGameplayCommon. Those branches do
// `*dList = ResourceMgr_LoadGfxByName(handPath)` which resolves through the
// GLOBAL ArchiveManager — handing the local user's modded Gfx* straight to
// SkelAnime, with no opportunity for PakLoader / GbiWrap to intercept. We
// reroute through our override registry + patched-vanilla cache so a remote
// dummy never wears the local user's hand/sheath/waist textures.
//
// Returns NULL when:
//   - we're not inside a remote dummy draw block (let local user's draw
//     keep its normal global-stack resolution unchanged), or
//   - neither active override nor vanilla cache has the path (caller falls
//     back to ResourceMgr_LoadGfxByName so a missing entry still renders
//     SOMETHING rather than an empty hand).
extern "C" void* HarpoonSkinSync_ResolvePlayerLimbDL(const char* otrPath) {
    if (otrPath == nullptr) return nullptr;
    if (!sInRemoteDraw) return nullptr;
    // First: an active override .o2r the remote has wins. Mirrors
    // GetDLOverride's lookup so per-skin custom hand DLs (when packers
    // bother to bundle them at the canonical path) take precedence.
    for (size_t idx : sActiveOverrideIndices) {
        if (idx >= sOverrides.size()) continue;
        const auto& o = sOverrides[idx];
        auto it = o.dlsByPath.find(otrPath);
        if (it != o.dlsByPath.end() && it->second != nullptr) {
            static std::set<std::string> sLoggedHandOverride;
            if (sLoggedHandOverride.insert(otrPath).second) {
                HSS_LOG("ResolvePlayerLimbDL: '%s' from override '%s'",
                        otrPath, o.name.c_str());
            }
            return (void*)it->second;
        }
    }
    // Second: patched vanilla copy. Bypasses the local user's mods/ entirely
    // — the bytecode here was loaded directly from oot.o2r at startup with
    // every SETTIMG_OTR / DL_OTR / VTX_OTR / MTX_OTR pre-rewritten to raw
    // pointers into vanilla data, so even though the engine walks it during
    // a frame where mods/ is mounted, the global stack is never consulted.
    auto vit = sVanillaDLs.find(otrPath);
    if (vit != sVanillaDLs.end() && vit->second != nullptr) {
        static std::set<std::string> sLoggedHandVanilla;
        if (sLoggedHandVanilla.insert(otrPath).second) {
            HSS_LOG("ResolvePlayerLimbDL: '%s' -> patched vanilla (blocked local mod leak)", otrPath);
        }
        return (void*)vit->second;
    }
    // Cache miss — caller will fall through to ResourceMgr_LoadGfxByName.
    // Log once per path so we know which canonical paths to extend the
    // CacheVanillaFromArchive filter to cover.
    static std::set<std::string> sLoggedHandMiss;
    if (sLoggedHandMiss.insert(otrPath).second) {
        HSS_LOG("ResolvePlayerLimbDL: MISS '%s' (no override / no vanilla cache; "
                "global stack will leak local mod for this path)", otrPath);
    }
    return nullptr;
}

// C-linkage hook called from pak_loader's PakLoader_GetDLOverride during the
// dummy player's gSPDisplayList path resolution. Walks the active overrides
// stack and returns the first matching native Gfx*, or NULL if no match (in
// which case pak_loader continues with its own local .pak / equipment logic).
extern "C" Gfx* HarpoonSkinSync_GetDLOverride(const char* otrPath) {
    if (otrPath == nullptr) return nullptr;

    // First: any active override .o2r the remote has wins.
    for (size_t idx : sActiveOverrideIndices) {
        if (idx >= sOverrides.size()) continue;
        const auto& o = sOverrides[idx];
        auto it = o.dlsByPath.find(otrPath);
        if (it != o.dlsByPath.end() && it->second != nullptr) {
            static u32 sHitLogCount = 0;
            if (sHitLogCount < 16) {
                sHitLogCount++;
                HSS_LOG("HIT '%s' from override '%s'", otrPath, o.name.c_str());
            }
            return it->second;
        }
    }

    // Second: vanilla fallback during a remote-render block. If the dummy
    // queries a Link/equipment/get-item DL path that no override .o2r covers,
    // return the vanilla DL we pre-loaded directly from oot.o2r at startup.
    // This BYPASSES the global ArchiveManager path so the local user's mods —
    // which the engine would otherwise apply to the dummy — cannot leak in.
    // CRITICAL: gated on sInRemoteDraw, NOT on sActiveOverrideIndices being
    // non-empty. When the remote broadcasts mods we don't have installed,
    // the override list is empty but we still need the vanilla fallback to
    // suppress local-mod leak.
    if (sInRemoteDraw) {
        auto vit = sVanillaDLs.find(otrPath);
        if (vit != sVanillaDLs.end() && vit->second != nullptr) {
            // Log ONCE per unique path so the listing covers every distinct
            // DL the dummy queries, not just the first few hits of whichever
            // DL happens to be drawn first (the prior 16-call cap masked
            // hand DL hits behind hundreds of bracelet repeats).
            static std::set<std::string> sLoggedVanillaPaths;
            if (sLoggedVanillaPaths.insert(otrPath).second) {
                HSS_LOG("VANILLA fallback '%s' (no override has it; blocked local-mod leak)", otrPath);
            }
            return vit->second;
        }
        // Cache miss — log but DON'T return an empty DL here. Returning
        // empty would silence rendering for legitimate sub-DLs (textures
        // setup, RDP state, etc.) and produce broken meshes / red
        // triangles. Falling through to the global stack risks leaking
        // the local mod, but partial leak beats no render. The diagnostic
        // shows which paths to add to the vanilla cache filter.
        static u32 sMissLogCount = 0;
        if (sMissLogCount < 64) {
            sMissLogCount++;
            HSS_LOG("MISS '%s' during remote draw (no override / no vanilla cache; "
                    "falling through to global — extend cache filter to include this namespace)",
                    otrPath);
        }
    }
    return nullptr;
}
