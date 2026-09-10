#include "FleetShipCombo.h"
#include "FleetComboRando.h"               // FleetCombo_IsRunning: a generating guest is busy, not hung
#include "soh/Notification/Notification.h" // tell the player WHY the combo is closing

#include <libultraship/libultraship.h>
#include <libultraship/bridge.h>
#include <ship/resource/archive/O2rArchive.h> // ReadO2rMajor: "portVersion" of an .o2r
#include <ship/utils/binarytools/MemoryStream.h>
#include <ship/utils/binarytools/BinaryReader.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <csignal>
#include <cerrno>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

// Ship's own build major (soh/src/boot/build.c) — the owner version for oot.o2r. Same declaration
// shape as variables.h (which this lean TU does not include).
extern "C" uint16_t gBuildVersionMajor;

namespace {

// Candidate 2ship executable file names, in priority order, per platform.
// 2ship's CMake target is "2ship" (-> 2ship.exe on Windows, 2s2h.elf on Linux,
// 2ship/2s2h-macos on macOS).
const std::vector<std::string>& TwoShipExeNames() {
#ifdef _WIN32
    static const std::vector<std::string> names = { "2ship.exe" };
#elif defined(__APPLE__)
    static const std::vector<std::string> names = { "2ship", "2s2h-macos" };
#else
    static const std::vector<std::string> names = { "2s2h.elf", "2ship" };
#endif
    return names;
}

// Directory containing the currently running Ship executable.
std::filesystem::path SelfExeDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return {};
    }
    return std::filesystem::path(std::wstring(buf, len)).parent_path();
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) {
        return {};
    }
    return std::filesystem::weakly_canonical(std::filesystem::path(buf)).parent_path();
#else
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        return {};
    }
    return p.parent_path();
#endif
}

// 2ship lives at Ship/2ship/, so look in <shipDir>/2ship first, then <shipDir> as
// a fallback (in case both exes share a folder during development).
std::filesystem::path Locate2ShipExe() {
#if defined(__ANDROID__)
    // A separate Android APK is not a desktop executable in this directory.
    return {};
#endif
    std::filesystem::path selfDir = SelfExeDir();
    if (selfDir.empty()) {
        return {};
    }

    std::vector<std::filesystem::path> searchDirs = { selfDir / "2ship", selfDir };

    std::error_code ec;
    for (const auto& dir : searchDirs) {
        for (const auto& name : TwoShipExeNames()) {
            std::filesystem::path candidate = dir / name;
            if (std::filesystem::exists(candidate, ec)) {
                return candidate;
            }
        }
    }
    return {};
}

bool HasArg(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && std::string(argv[i]) == flag) {
            return true;
        }
    }
    return false;
}

// The 2ship child we launched, kept so we can WATCH it. Ship draws MM's shared texture, so a guest
// that died (or hung) leaves this process showing a frame that will never update again -- the black
// screen. Holding the handle is what lets us notice.
#ifdef _WIN32
HANDLE sChildProcess = nullptr;
#else
pid_t sChildPid = 0;
#endif

// Launch 2ship as a child, telling it --fleet-child so it does not bounce back.
// The host keeps running (no replace/exit). Returns true on success.
bool Launch2ShipChild(const std::filesystem::path& twoShipExe) {
    std::filesystem::path workDir = twoShipExe.parent_path();
#ifdef _WIN32
    // Pass our PID so 2ship can exit itself if this host process dies (no orphan).
    std::wstring cmd =
        L"\"" + twoShipExe.wstring() + L"\" --fleet-child --fleet-host-pid=" + std::to_wstring(GetCurrentProcessId());
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');

    std::wstring workDirW = workDir.wstring();

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    BOOL ok = CreateProcessW(twoShipExe.wstring().c_str(), mutableCmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                             workDirW.empty() ? nullptr : workDirW.c_str(), &si, &pi);
    if (!ok) {
        SPDLOG_ERROR("[FleetShipCombo] CreateProcessW failed for '{}' (error {})", twoShipExe.string(), GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    // NOT CloseHandle(pi.hProcess): the watchdog needs it to tell "2ship is still running" from
    // "2ship died and Ship is now rendering a frozen copy of it". Held for the process lifetime.
    sChildProcess = pi.hProcess;
    return true;
#else
    pid_t pid = fork();
    if (pid < 0) {
        SPDLOG_ERROR("[FleetShipCombo] fork failed for '{}'", twoShipExe.string());
        return false;
    }
    if (pid == 0) {
        // Child: move into the 2ship folder so it finds its own assets, then exec.
        std::string exe = twoShipExe.string();
        std::string dir = workDir.string();
        if (!dir.empty()) {
            (void)chdir(dir.c_str());
        }
        std::string hostPid = "--fleet-host-pid=" + std::to_string((long)getppid());
        char* args[] = { exe.data(), const_cast<char*>("--fleet-child"), hostPid.data(), nullptr };
        execv(exe.c_str(), args);
        _exit(127); // exec failed
    }
    sChildPid = pid; // parent: remember the guest so the watchdog can probe it
    return true;     // parent (host) keeps running
#endif
}

// ===================== Shared-memory coordination (Frente B) =====================

// THIS_GAME identity for this build: 0 = Ocarina of Time (Ship).
constexpr int kThisGame = 0;
constexpr uint32_t kFscMagic = 0x46534331u; // 'FSC1'
constexpr uint32_t kFscVersion = 4u;

// ---- Anchor-style packet channel (version 3) ----
// One JSON message per slot, same shape as an Anchor packet. A slot must hold the LARGEST single
// thing a delta can carry, and that is a whole array: arrays are sent as one JSON value precisely
// so they can never be split across packets (a split one unflattens with null gaps and corrupts
// the save). comboObtainedFc is 512 entries, ~2KB dumped, so 1KB slots were too small -- it would
// have been silently dropped, taking the cross-game item grants with it. Anything bigger than a
// delta (a whole save) still goes through the temp file and is only ANNOUNCED here.
constexpr uint32_t kFscPacketBytes = 4096;
constexpr uint32_t kFscRingSlots = 256;

struct FscPacket {
    uint32_t len;                  // bytes used in payload (0 = never written)
    uint32_t kind;                 // reserved fast-path opcode; 0 = plain JSON
    char payload[kFscPacketBytes]; // JSON text, NUL-terminated
};

// Layout shared between Ship and 2ship. MUST stay byte-identical to the 2ship copy.
struct FscShared {
    uint32_t magic;
    uint32_t version;
    int32_t activeGame; // 0 = Ocarina of Time (Ship), 1 = Majora's Mask (2ship)
    // Picture-in-picture: 2ship publishes its game image as a D3D11 shared texture.
    uint64_t texHandle; // OS shared handle from IDXGIResource::GetSharedHandle (0 = none)
    uint32_t texWidth;
    uint32_t texHeight;
    uint32_t texFormat;     // DXGI_FORMAT value
    uint32_t texFrameIndex; // bumped each publish (lets the consumer detect new frames)
    int32_t uiFocus;        // which window is front for CONFIG: 0 = Ship, 1 = 2ship
    // Cross-game loading-zone WARP request. The trigger side writes the target (in the TARGET
    // game's id/space), bumps warpSeq, and flips activeGame; whichever game BECOMES active applies
    // it once per new seq (load scene + override Link pos/rot). MUST stay byte-identical to 2ship.
    int32_t warpSeq;   // bumped per request (0 = none yet)
    int32_t warpScene; // target scene id in the target game's space
    float warpX;       // land position override (target game world coords)
    float warpY;
    float warpZ;
    int32_t warpRotY;        // land Y rotation (s16 binary angle stored in int32)
    int32_t warpSaveFileNum; // save SLOT the warp came from; the target game loads its own same slot (-1 = unset)
    int32_t
        sendFadeAlpha; // 0..255 sending-fade overlay, written by the ACTIVE (sending) game, drawn by the host consumer
    int32_t doorDLIndex; // DEV: which Lost Woods room-DL the MM door tunnel tool is showing (for the on-screen readout)
    uint64_t reservedU[12];
    // ---- Anchor-style packet rings (version 3) ----
    // TWO one-way rings, so neither side ever writes the ring it reads: no lock is needed. The
    // writer fills slot (head % kFscRingSlots) and THEN bumps head; the reader keeps its own
    // PRIVATE tail (process-local, not shared) and consumes up to head. If the reader falls more
    // than kFscRingSlots behind, the oldest packets are overwritten and it jumps forward -- a
    // stalled or frozen game can drop deltas but can never deadlock the writer. That is exactly
    // why the periodic hash validation exists: it repairs anything a drop lost.
    // Appended AFTER reservedU so every version-1 field keeps its old offset.
    uint32_t ringToMmHead;  // written ONLY by Ship (OoT)
    uint32_t ringToOotHead; // written ONLY by 2ship (MM)
    FscPacket ringToMm[kFscRingSlots];
    FscPacket ringToOot[kFscRingSlots];
    // ---- Combo seed identity (version 4) ----
    // The Rando finalSeed OoT generated for this combo. MM compares its paired save's finalSeed
    // against this and REBUILDS the slot when they disagree -- a save from an older seed silently
    // loaded next to a new OoT seed is a desync you would only notice hours later, when a check
    // gives the wrong item. 0 = unset (no combo seed generated yet; validation is skipped).
    uint32_t comboSeed;
    uint32_t comboSeedPad; // keeps the struct 8-byte aligned on both sides
};

#ifdef _WIN32
std::wstring sShmName = L"Local\\FleetShipComboShared";
HANDLE sShmHandle = nullptr;
#else
std::string sShmName = "/FleetShipComboShared";
int sShmFd = -1;
#endif
FscShared* sShared = nullptr;
bool sLazyOpenTried = false;

// Make the shared-memory region name UNIQUE per combo instance, keyed by the host Ship's PID,
// so SEVERAL combos can run on one machine without colliding on a single region. Ship (host)
// keys it by its own PID and launches 2ship with --fleet-host-pid=<that PID>, so the child
// derives the SAME name and the pair is matched. Key 0 keeps the legacy unsuffixed name.
void SetInstanceKey(unsigned long key) {
    if (key == 0) {
        return;
    }
#ifdef _WIN32
    sShmName = L"Local\\FleetShipComboShared_" + std::to_wstring(key);
#else
    sShmName = "/FleetShipComboShared_" + std::to_string(key);
#endif
}

void InitFreshRegion() {
    if (!sShared) {
        return;
    }
    sShared->magic = kFscMagic;
    sShared->version = kFscVersion;
    sShared->activeGame = 0; // default to OoT until host/child sets it
    sShared->texHandle = 0;
    sShared->texWidth = 0;
    sShared->texHeight = 0;
    sShared->texFormat = 0;
    sShared->texFrameIndex = 0;
    sShared->uiFocus = 0;
    sShared->warpSeq = 0;
    sShared->warpScene = 0;
    sShared->warpX = sShared->warpY = sShared->warpZ = 0.0f;
    sShared->warpRotY = 0;
    sShared->warpSaveFileNum = -1;
    sShared->sendFadeAlpha = 0;
    sShared->doorDLIndex = 0;
    for (int i = 0; i < 12; ++i) {
        sShared->reservedU[i] = 0;
    }
    sShared->ringToMmHead = 0;
    sShared->ringToOotHead = 0;
    sShared->comboSeed = 0;
    sShared->comboSeedPad = 0;
    // Only the len/kind headers need clearing; a slot's payload is never read unless its len says so.
    for (uint32_t i = 0; i < kFscRingSlots; ++i) {
        sShared->ringToMm[i].len = sShared->ringToMm[i].kind = 0;
        sShared->ringToOot[i].len = sShared->ringToOot[i].kind = 0;
    }
}

// create=true: create-or-open (FleetShipCombo_SharedInit). create=false: open an
// EXISTING region only (lazy), so standalone play never allocates one.
FscShared* MapShared(bool create) {
    if (sShared) {
        return sShared;
    }
#ifdef _WIN32
    if (create) {
        sShmHandle =
            CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(FscShared), sShmName.c_str());
        bool existed = (sShmHandle != nullptr && GetLastError() == ERROR_ALREADY_EXISTS);
        if (sShmHandle != nullptr) {
            sShared = (FscShared*)MapViewOfFile(sShmHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FscShared));
            if (sShared != nullptr && !existed) {
                InitFreshRegion();
            }
        }
    } else {
        sShmHandle = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, sShmName.c_str());
        if (sShmHandle != nullptr) {
            sShared = (FscShared*)MapViewOfFile(sShmHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FscShared));
        }
    }
#elif defined(__ANDROID__)
    // Desktop combo mode launches a second executable and shares a named POSIX
    // mapping. Android does not provide shm_open or that process model. Leave
    // standalone NEI active; an Android combo needs its own explicit integration.
    return nullptr;
#else
    int flags = create ? (O_CREAT | O_RDWR) : O_RDWR;
    sShmFd = shm_open(sShmName.c_str(), flags, 0666);
    if (sShmFd >= 0) {
        if (create) {
            (void)ftruncate(sShmFd, sizeof(FscShared));
        }
        void* p = mmap(nullptr, sizeof(FscShared), PROT_READ | PROT_WRITE, MAP_SHARED, sShmFd, 0);
        if (p != MAP_FAILED) {
            sShared = (FscShared*)p;
            if (create && sShared->magic != kFscMagic) {
                InitFreshRegion();
            }
        }
    }
#endif
    return sShared;
}

// Attach to an existing region once (used by the per-frame freeze check). If none
// exists (standalone), leaves sShared null so IsThisGameActive() returns true.
FscShared* LazyOpen() {
    if (sShared) {
        return sShared;
    }
    if (sLazyOpenTried) {
        return nullptr;
    }
    sLazyOpenTried = true;
    return MapShared(false);
}

// ---- .o2r version reading ----
// Every archive LUS produces carries a "portVersion" entry: [endianness u8][major u16][minor u16][patch
// u16], stamped with the build that made it. A game only accepts a ROM archive whose MAJOR equals its
// own build major (see OTRGlobals::RunExtract / 2ship's RunExtract: mismatch = delete + re-extract).
// The port archive (soh.o2r / 2ship.o2r) is stamped the same way, so "does mm.o2r match the 2ship
// build?" is answerable from Ship without knowing anything about 2ship: compare the two majors.
constexpr int kO2rMajorUnknown = -1;

int ReadO2rMajor(const std::filesystem::path& archivePath) {
    std::error_code ec;
    if (archivePath.empty() || !std::filesystem::exists(archivePath, ec)) {
        return kO2rMajorUnknown;
    }
    try {
        auto archive = std::make_shared<Ship::O2rArchive>(archivePath.string());
        if (!archive->Open()) {
            return kO2rMajorUnknown;
        }
        auto t = archive->LoadFile("portVersion");
        if (t == nullptr || !t->IsLoaded || t->Buffer == nullptr || t->Buffer->size() < 7) {
            return kO2rMajorUnknown;
        }
        auto stream = std::make_shared<Ship::MemoryStream>(t->Buffer->data(), t->Buffer->size());
        auto reader = std::make_shared<Ship::BinaryReader>(stream);
        Ship::Endianness endianness = (Ship::Endianness)reader->ReadUByte();
        reader->SetEndianness(endianness);
        return (int)reader->ReadUInt16();
    } catch (...) {
        return kO2rMajorUnknown; // unreadable = treat as "no usable copy here"
    }
}

// Reconcile ONE archive between the two combo dirs given the major its OWNER build expects.
//   ownerMajor == kO2rMajorUnknown -> we can't judge; fall back to plain existence-mirroring.
// A copy is VALID when its major equals ownerMajor. Valid copies are the only source; the preferred
// source is the copy in `preferDir` (the owner's dir: it is the one that game just extracted). The
// destination is (over)written when it is missing, invalid, or a different size than the source.
// Never deletes anything: each game deletes its own outdated archive itself, in its own extractor.
void ReconcileO2r(const std::filesystem::path& preferDir, const std::filesystem::path& otherDir, const char* name,
                  int ownerMajor) {
    std::error_code ec;
    std::filesystem::path a = preferDir / name;
    std::filesystem::path b = otherDir / name;
    bool ae = std::filesystem::exists(a, ec);
    bool be = std::filesystem::exists(b, ec);
    if (!ae && !be) {
        return;
    }
    if (ownerMajor == kO2rMajorUnknown) {
        // Old behavior: fill the missing side only.
        if (ae && !be) {
            std::filesystem::copy_file(a, b, std::filesystem::copy_options::overwrite_existing, ec);
        } else if (be && !ae) {
            std::filesystem::copy_file(b, a, std::filesystem::copy_options::overwrite_existing, ec);
        }
        return;
    }
    int am = ae ? ReadO2rMajor(a) : kO2rMajorUnknown;
    int bm = be ? ReadO2rMajor(b) : kO2rMajorUnknown;
    bool aValid = ae && am == ownerMajor;
    bool bValid = be && bm == ownerMajor;
    if (!aValid && !bValid) {
        SPDLOG_WARN("[FleetShipCombo] {}: no copy matches its owner build (major {}; have {} / {}) -> the owner "
                    "game will re-extract it",
                    name, ownerMajor, am, bm);
        return; // nothing worth copying; the owner game's extractor takes it from here
    }
    const std::filesystem::path& src = aValid ? a : b;
    const std::filesystem::path& dst = aValid ? b : a;
    bool dstValid = aValid ? bValid : aValid;
    if (dstValid) {
        auto sSize = std::filesystem::file_size(src, ec);
        auto dSize = std::filesystem::file_size(dst, ec);
        if (sSize == dSize) {
            return; // both valid and same size: nothing to do
        }
    }
    if (std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec)) {
        SPDLOG_INFO("[FleetShipCombo] {}: mirrored {} -> {} (owner major {})", name, src.string(), dst.string(),
                    ownerMajor);
    } else {
        SPDLOG_WARN("[FleetShipCombo] {}: could not mirror {} -> {} ({})", name, src.string(), dst.string(),
                    ec.message());
    }
}

struct ComboDirs {
    std::filesystem::path shipDir;
    std::filesystem::path guestDir;
    bool valid = false;
};

ComboDirs GetComboDirs() {
    ComboDirs d;
    d.shipDir = SelfExeDir();
    std::filesystem::path guestExe = Locate2ShipExe();
    if (d.shipDir.empty() || guestExe.empty()) {
        return d;
    }
    d.guestDir = guestExe.parent_path();
    if (d.guestDir.empty() || d.guestDir == d.shipDir) {
        return d;
    }
    d.valid = true;
    return d;
}

// The major 2ship expects from mm.o2r = the major stamped in ITS port archive (2ship.o2r), which
// 2ship itself refuses to run without. Unknown when 2ship.o2r is missing/unreadable.
int GuestBuildMajor(const ComboDirs& d) {
    return ReadO2rMajor(d.guestDir / "2ship.o2r");
}

bool MmArchiveValidAt(const std::filesystem::path& dir, int guestMajor) {
    std::error_code ec;
    std::filesystem::path p = dir / "mm.o2r";
    if (!std::filesystem::exists(p, ec)) {
        return false;
    }
    if (guestMajor == kO2rMajorUnknown) {
        return true; // can't judge the version: existence is the best we can do
    }
    return ReadO2rMajor(p) == guestMajor;
}

// The visible `2ship.exe --fleet-extract` child (MM archive gate).
#ifdef _WIN32
HANDLE sGuestExtractProcess = nullptr;
#else
pid_t sGuestExtractPid = 0;
#endif

} // namespace

void FleetShipCombo_ProvisionO2rBothDirs(void) {
    // Combo layout: <root>/soh.exe + <root>/2ship/2ship.exe. Both mm.o2r and oot.o2r should sit next to
    // BOTH exes, so mirror whichever exists into the dir missing it — an extraction done by EITHER game
    // (soh -> oot.o2r, 2ship -> mm.o2r) then provisions both. No-op when there's no sibling (standalone).
    ComboDirs d = GetComboDirs();
    if (!d.valid) {
        return;
    }
    // mm.o2r is 2ship's: its copy is preferred and must match 2ship's build.
    ReconcileO2r(d.guestDir, d.shipDir, "mm.o2r", GuestBuildMajor(d));
    // oot.o2r is ours: our copy is preferred and must match THIS build.
    ReconcileO2r(d.shipDir, d.guestDir, "oot.o2r", (int)gBuildVersionMajor);
}

bool FleetShipCombo_HaveValidMmArchive(void) {
    ComboDirs d = GetComboDirs();
    if (!d.valid) {
        return false;
    }
    int guestMajor = GuestBuildMajor(d);
    return MmArchiveValidAt(d.guestDir, guestMajor) || MmArchiveValidAt(d.shipDir, guestMajor);
}

bool FleetShipCombo_GuestExtractStart(void) {
    if (!CVarGetInteger("isFleetShipCombo.Enabled", 0)) {
        return false;
    }
    ComboDirs d = GetComboDirs();
    if (!d.valid) {
        return false;
    }
    if (FleetShipCombo_HaveValidMmArchive()) {
        return false;
    }
    std::filesystem::path twoShipExe = Locate2ShipExe();
    SPDLOG_WARN("[FleetShipCombo] no mm.o2r matching the 2ship build (2ship.o2r major {}) -> running 2ship's "
                "extractor VISIBLY first: '{}' --fleet-extract",
                GuestBuildMajor(d), twoShipExe.string());
#ifdef _WIN32
    std::wstring cmd = L"\"" + twoShipExe.wstring() + L"\" --fleet-extract";
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    std::wstring workDirW = twoShipExe.parent_path().wstring();
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(twoShipExe.wstring().c_str(), mutableCmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                             workDirW.empty() ? nullptr : workDirW.c_str(), &si, &pi);
    if (!ok) {
        SPDLOG_ERROR("[FleetShipCombo] CreateProcessW (--fleet-extract) failed for '{}' (error {})",
                     twoShipExe.string(), GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    sGuestExtractProcess = pi.hProcess;
    return true;
#else
    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        std::string exe = twoShipExe.string();
        std::string dir = twoShipExe.parent_path().string();
        if (!dir.empty()) {
            (void)chdir(dir.c_str());
        }
        char* args[] = { exe.data(), const_cast<char*>("--fleet-extract"), nullptr };
        execv(exe.c_str(), args);
        _exit(127);
    }
    sGuestExtractPid = pid;
    return true;
#endif
}

bool FleetShipCombo_GuestExtractRunning(void) {
#ifdef _WIN32
    if (sGuestExtractProcess == nullptr) {
        return false;
    }
    if (WaitForSingleObject(sGuestExtractProcess, 0) == WAIT_TIMEOUT) {
        return true;
    }
    CloseHandle(sGuestExtractProcess);
    sGuestExtractProcess = nullptr;
    SPDLOG_INFO("[FleetShipCombo] 2ship extractor child finished; valid mm.o2r now = {}",
                FleetShipCombo_HaveValidMmArchive());
    return false;
#else
    if (sGuestExtractPid == 0) {
        return false;
    }
    int status = 0;
    pid_t r = waitpid(sGuestExtractPid, &status, WNOHANG);
    if (r == 0) {
        return true;
    }
    sGuestExtractPid = 0;
    return false;
#endif
}

void FleetShipCombo_HostBootstrap(int argc, char** argv) {
#ifdef COMBO_BUILD
    return; // ComboShip loads mm.dll in-process; spawning a 2ship.exe child here would run a second MM
#endif
    // For picture-in-picture BOTH games run whenever the combo is enabled; isPlayerIn2Ship
    // only decides which one is ACTIVE (unfrozen). Bring up 2ship when:
    //  - the combo is enabled (master toggle), OR
    //  - 2ship handed off to us with --boot=mm.
    bool bootMm = HasArg(argc, argv, "--boot=mm");
    bool enabled = CVarGetInteger("isFleetShipCombo.Enabled", 0);

    // Combo just switched ON (it was off the last time we booted). Every MM save sitting on disk
    // predates the combo, so not one of them is the other half of an OoT file -- and a stray MM
    // file is exactly what lets MM play one seed while OoT plays another. Wipe them all; from here
    // on the pairing (create / delete / ensure) keeps both sides matched. The request itself waits
    // for the guest to be up, so it is only FLAGGED here (see the maintenance queue).
    if (enabled && !CVarGetInteger("gFleetCombo.WasEnabled", 0)) {
        SPDLOG_INFO("[FleetShipCombo] combo turned ON since the last boot -> queuing an MM save wipe");
        CVarSetInteger("gFleetCombo.WipeMmSavesPending", 1);
    }
    CVarSetInteger("gFleetCombo.WasEnabled", enabled ? 1 : 0);
    Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();

    if (!bootMm && !enabled) {
        return; // combo off -> plain OoT
    }

    std::filesystem::path twoShipExe = Locate2ShipExe();
    if (twoShipExe.empty()) {
        SPDLOG_WARN("[FleetShipCombo] 2ship executable not found under Ship/2ship/; staying in OoT only.");
        return;
    }

    // Never bring up the HIDDEN child without a usable MM archive: 2ship would only sit in its own
    // "No O2R Files - Generate one now?" prompt, drawn off-screen where nobody can answer it, and the
    // combo would look dead. The MM archive gate (FleetShipCombo_GuestExtractStart, run before Ship's
    // own extractor) is where that prompt is shown VISIBLY; landing here without the archive means the
    // player skipped/cancelled it, so say so and play OoT alone.
    if (!FleetShipCombo_HaveValidMmArchive()) {
        SPDLOG_ERROR("[FleetShipCombo] no mm.o2r matching the 2ship build -> NOT launching the hidden 2ship child "
                     "(combo unavailable this session)");
        Notification::Emit({
            .prefix = "[Fleet] ",
            .message = "Majora's Mask archive (mm.o2r) is missing or outdated",
            .suffix = " - restart to run 2ship's extractor; playing OoT alone",
            .remainingTime = 15.0f,
            .mute = true, // boot time: audio isn't up yet
        });
        return;
    }

    // THE COMBO ALWAYS BOOTS INTO OoT. (User decision, 2026-07-31.)
    //
    // It used to resume wherever the player last saved, and booting straight into MM turned out to
    // be a genuinely different startup than the one every other path exercises: OoT never leaves its
    // title/file select, so it sits there with NO file loaded while the player is off in MM — the
    // save sync had to be taught to shut up in that state, the warp back lands in the cold-boot
    // branch instead of the normal in-game one, and testers hit crashes on both sides that nothing
    // else reproduces. OoT is the combo's entry point (its file select is where you pick the combo
    // file), so making that the ONLY startup removes an entire class of states instead of hardening
    // each one. The cost is small and honest: after a session that ended in MM you come back in OoT
    // and walk through the portal.
    //
    // `bootMm` (the 2ship.exe -> Ship bounce) and isPlayerIn2Ship still decide that the combo comes
    // up at all — just not who is active. isPlayerIn2Ship keeps tracking the active game at runtime.
    (void)bootMm;
    const int active = 0;

    SPDLOG_INFO("[FleetShipCombo] Host bringing up 2ship child at '{}' (--fleet-child). active={}.",
                twoShipExe.string(), active);

    // Key the shared region by OUR pid; the child is launched with --fleet-host-pid=<our pid>
    // (see Launch2ShipChild) and attaches to the same name, so multiple combos on one machine
    // stay isolated. Create the region + set the active game BEFORE launching the child, so the
    // child sees the correct active game immediately (no freeze race).
#ifdef _WIN32
    unsigned long selfPid = GetCurrentProcessId();
#else
    unsigned long selfPid = static_cast<unsigned long>(getpid());
#endif
    FleetShipCombo_SharedInit(selfPid);
    FleetShipCombo_SetActiveGame(active);
    FleetShipCombo_SetUiFocus(active); // front window follows the boot active game (0=OoT, 1=MM)

    if (Launch2ShipChild(twoShipExe)) {
        CVarSetInteger("isPlayerIn2Ship", active);
        Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
    }
}

void FleetShipCombo_SharedInit(unsigned long instanceKey) {
    SetInstanceKey(instanceKey);
    MapShared(true);
}

int FleetCombo_BeatBothBosses(void) {
    return CVarGetInteger("isFleetShipCombo.Enabled", 0) != 0 && CVarGetInteger("gFleetCombo.GoalMode", 0) == 0;
}

#ifdef COMBO_BUILD
// ComboShip: one process, and comboui knows which game is in front. The shared-memory region belongs
// to the two-process layout and is never created here, so answering -1 ("standalone") would make
// every caller drop its combo gating.
bool Combo_OotIsForeground(void);

int FleetShipCombo_GetActiveGame(void) {
    return Combo_OotIsForeground() ? 0 : 1;
}

void FleetShipCombo_SetActiveGame(int game) {
    (void)game; // the launcher switches games at scene seams; there is nothing to write
}
#else
int FleetShipCombo_GetActiveGame(void) {
    FscShared* s = LazyOpen();
    return s ? s->activeGame : -1;
}

void FleetShipCombo_SetActiveGame(int game) {
    FscShared* s = LazyOpen();
    if (s) {
        s->activeGame = game;
    }
}
#endif

// Per-process seq of the last warp we issued/consumed, so the REQUESTER never re-consumes its own.
static int sLastWarpSeq = 0;

void FleetShipCombo_RequestWarp(int targetGame, int scene, float x, float y, float z, int rotY, int saveFile) {
    FscShared* s = LazyOpen();
    if (!s) {
        return;
    }
    s->warpScene = scene;
    s->warpX = x;
    s->warpY = y;
    s->warpZ = z;
    s->warpRotY = rotY;
    s->warpSaveFileNum = saveFile; // tell the target game which save slot to be in (set before the seq bump)
    s->warpSeq += 1;               // mark a new request
    sLastWarpSeq = s->warpSeq;     // we issued it; don't let THIS process consume its own warp
    s->activeGame = targetGame;    // flip: the target game becomes active (unfrozen) and applies it
    s->uiFocus = targetGame;       // front window follows the active game (0=OoT, 1=MM); a peek tab may
                                   // override it afterwards without touching activeGame
}

int FleetShipCombo_ConsumePendingWarp(int* scene, float* x, float* y, float* z, int* rotY) {
    FscShared* s = LazyOpen();
    if (!s) {
        return 0;
    }
    if (s->warpSeq == sLastWarpSeq) {
        return 0; // nothing new
    }
    if (s->activeGame != kThisGame) {
        return 0; // not addressed to this game yet
    }
    sLastWarpSeq = s->warpSeq;
    if (scene) {
        *scene = s->warpScene;
    }
    if (x) {
        *x = s->warpX;
    }
    if (y) {
        *y = s->warpY;
    }
    if (z) {
        *z = s->warpZ;
    }
    if (rotY) {
        *rotY = s->warpRotY;
    }
    return 1;
}

int FleetShipCombo_GetWarpSaveFile(void) {
    FscShared* s = LazyOpen();
    return s ? s->warpSaveFileNum : -1;
}

// Cross-game arrival blackout: while > 0, THIS game's render path paints the screen BLACK (empty DL)
// so the stale frame of the OTHER game and the warp scene-load are never shown during a flip. Set on
// warp arrival; the render path calls ...Active() exactly once per frame (it decrements).
static int sArrivalBlackout = 0;
void FleetShipCombo_BeginArrivalBlackout(int frames) {
    sArrivalBlackout = frames;
}
int FleetShipCombo_ArrivalBlackoutActive(void) {
    if (sArrivalBlackout > 0) {
        sArrivalBlackout--;
        return 1;
    }
    return 0;
}

// Sending-side fade overlay (0..255). The active game's warp logic ramps this up while Link keeps
// walking into the door (no scene transition -> no reload, not frozen); the host PiP consumer draws a
// black overlay at this alpha over the scene, giving a real fade-out, then we flip at full black.
void FleetShipCombo_SetSendFadeAlpha(int alpha) {
    FscShared* s = LazyOpen();
    if (!s) {
        return;
    }
    s->sendFadeAlpha = alpha < 0 ? 0 : (alpha > 255 ? 255 : alpha);
}
int FleetShipCombo_GetSendFadeAlpha(void) {
    FscShared* s = LazyOpen();
    return s ? s->sendFadeAlpha : 0;
}

void FleetShipCombo_SetDoorDLIndex(int index) {
    FscShared* s = LazyOpen();
    if (s) {
        s->doorDLIndex = index;
    }
}
int FleetShipCombo_GetDoorDLIndex(void) {
    FscShared* s = LazyOpen();
    return s ? s->doorDLIndex : 0;
}

void FleetShipCombo_SetComboSeed(unsigned int seed) {
    FscShared* s = LazyOpen();
    if (s) {
        s->comboSeed = (uint32_t)seed;
    }
}

unsigned int FleetShipCombo_GetComboSeed(void) {
    FscShared* s = LazyOpen();
    return (s && s->version >= 4) ? (unsigned int)s->comboSeed : 0u;
}

// ================== Anchor-style packet channel (version 2) ==================
// This block is TEXTUALLY IDENTICAL in Ship and 2ship -- kThisGame picks which ring is ours at
// compile time, so there is no "which side am I" branch to get wrong. We only ever WRITE the ring
// the other game reads, and only ever READ the ring it writes, so the channel needs no lock.

int FleetShipCombo_PushPacket(const char* json) {
    FscShared* s = LazyOpen();
    if (!s || !json || s->version < 2) {
        return 0; // no combo, or the other exe is an old build without the rings
    }
    const size_t len = strlen(json);
    if (len + 1 > kFscPacketBytes) {
        SPDLOG_WARN("[FleetNet] packet dropped: {} bytes does not fit the {}-byte slot", len, kFscPacketBytes);
        return 0;
    }
    FscPacket* ring = (kThisGame == 0) ? s->ringToMm : s->ringToOot;
    uint32_t* head = (kThisGame == 0) ? &s->ringToMmHead : &s->ringToOotHead;

    FscPacket& slot = ring[*head % kFscRingSlots];
    memcpy(slot.payload, json, len + 1);
    slot.kind = 0;
    slot.len = (uint32_t)len;
    // Publish the head LAST: the reader must never see a bumped head pointing at a half-written slot.
    std::atomic_thread_fence(std::memory_order_release);
    ++(*head);
    return 1;
}

int FleetShipCombo_PopPacket(char* out, int cap) {
    FscShared* s = LazyOpen();
    if (!s || !out || cap <= 0 || s->version < 2) {
        return 0;
    }
    FscPacket* ring = (kThisGame == 0) ? s->ringToOot : s->ringToMm;
    const uint32_t head = (kThisGame == 0) ? s->ringToOotHead : s->ringToMmHead;
    std::atomic_thread_fence(std::memory_order_acquire);

    // The tail is PROCESS-LOCAL on purpose: it is ours alone, so the writer can never touch it and
    // a crashed/restarted peer cannot rewind us.
    static uint32_t sTail = 0;
    const uint32_t pending = head - sTail; // unsigned: wraps correctly
    if (pending == 0) {
        return 0;
    }
    if (pending > kFscRingSlots) {
        // We fell more than a whole ring behind (frozen game, long scene load) and the oldest
        // packets were overwritten. Jump to what is still intact; the periodic hash validation is
        // what repairs the deltas lost here -- that is the whole reason it exists.
        // Land TWO slots inside the window, not exactly on head - kFscRingSlots: that slot is the
        // oldest one, i.e. precisely the one the writer is about to reuse, so reading it races a
        // slot being rewritten under us. Two slots of margin costs two dropped packets (already
        // lost anyway) and removes the race.
        SPDLOG_WARN("[FleetNet] ring overrun: dropped {} packets", pending - kFscRingSlots);
        sTail = head - (kFscRingSlots - 2);
    }
    const FscPacket& slot = ring[sTail % kFscRingSlots];
    ++sTail;

    // LENGTH VALIDATION — this length comes out of memory ANOTHER PROCESS writes, so it is hostile
    // input, not data. The old test was `slot.len + 1 > (uint32_t)cap`, which OVERFLOWS: len =
    // 0xFFFFFFFF makes len + 1 == 0, sails past the check, and the memcpy copies 4 GB — an instant,
    // untrappable process kill with nothing in the log. Compare without arithmetic, and bound by the
    // payload's REAL size as well as the caller's buffer.
    const uint32_t len = slot.len;
    if (len == 0 || len >= kFscPacketBytes || len >= (uint32_t)cap) {
        if (len != 0) {
            SPDLOG_WARN("[FleetNet] packet dropped: implausible length {} (payload {} bytes, buffer {})", len,
                        kFscPacketBytes, cap);
        }
        return 0; // empty, corrupt, or too big for the caller's buffer: skip, never truncate JSON
    }
    memcpy(out, slot.payload, len);
    out[len] = '\0';
    return 1;
}

#ifdef COMBO_BUILD
bool FleetShipCombo_IsThisGameActive(void) {
    return Combo_OotIsForeground();
}
#else
bool FleetShipCombo_IsThisGameActive(void) {
    FscShared* s = LazyOpen();
    if (!s) {
        return true; // standalone / no combo: always active, never freeze
    }
    return s->activeGame == kThisGame;
}
#endif

int FleetShipCombo_GetSharedTexture(unsigned long long* handle, unsigned int* width, unsigned int* height,
                                    unsigned int* dxgiFormat, unsigned int* frameIndex) {
    FscShared* s = LazyOpen();
    if (!s) {
        return 0;
    }
    if (handle) {
        *handle = s->texHandle;
    }
    if (width) {
        *width = s->texWidth;
    }
    if (height) {
        *height = s->texHeight;
    }
    if (dxgiFormat) {
        *dxgiFormat = s->texFormat;
    }
    if (frameIndex) {
        *frameIndex = s->texFrameIndex;
    }
    return s->texHandle != 0 ? 1 : 0;
}

int FleetShipCombo_GetUiFocus(void) {
    FscShared* s = LazyOpen();
    return s ? s->uiFocus : -1;
}

bool FleetShipCombo_ShowMenuUi(void) {
    return CVarGetInteger("isFleetShipCombo.DevUi", 0) != 0;
}

void FleetShipCombo_SetUiFocus(int focus) {
    FscShared* s = LazyOpen();
    if (s) {
        s->uiFocus = focus;
    }
#ifdef _WIN32
    // Handing the foreground to 2ship for config: as the process that currently holds the
    // foreground (Ship), bless ANY process to take it. This is the documented way to let
    // 2ship's SetForegroundWindow succeed WITHOUT the user clicking the window first;
    // otherwise Windows' foreground-lock blocks cross-process focus changes and the
    // keyboard (ESC) keeps going to Ship instead of 2ship's BenGui.
    if (focus == 1) {
        AllowSetForegroundWindow(ASFW_ANY);
    }
#endif
}

// ---- FleetSync save-sync handshake over reservedU (layout unchanged — MUST match 2ship) ----
// reservedU[0] = the guest (2ship) heartbeat, bumped by MM every frame. [1] = syncSaveSeq (bumped by the game that
// just SAVED), [2] = syncSaveAck (set by the OTHER game once it applied the shared overlay and
// wrote its own file), [3] = syncSaveSlot.
void FleetShipCombo_SignalSyncSave(int slot) {
    FscShared* s = LazyOpen();
    if (!s) {
        return;
    }
    s->reservedU[3] = (uint64_t)(uint32_t)slot;
    s->reservedU[1] = s->reservedU[1] + 1;
}

unsigned long long FleetShipCombo_GetSyncSaveSeq(void) {
    FscShared* s = LazyOpen();
    return s ? s->reservedU[1] : 0;
}

int FleetShipCombo_GetSyncSaveSlot(void) {
    FscShared* s = LazyOpen();
    return s ? (int)(uint32_t)s->reservedU[3] : -1;
}

void FleetShipCombo_AckSyncSave(unsigned long long seq) {
    FscShared* s = LazyOpen();
    if (s) {
        s->reservedU[2] = seq;
    }
}

unsigned long long FleetShipCombo_GetSyncSaveAck(void) {
    FscShared* s = LazyOpen();
    return s ? s->reservedU[2] : 0;
}

// ---- Fleet Oracle (combo randomizer) handshake over reservedU[4..5] (layout unchanged — MUST match 2ship) ----
// [4] = oracleReqSeq (bumped by the HOST after writing fleet_oracle_req.json), [5] = oracleRespAck
// (set by the MM oracle to the req seq it answered, after writing fleet_oracle_resp.json).
void FleetShipCombo_SignalOracleRequest(void) {
    FscShared* s = LazyOpen();
    if (s) {
        s->reservedU[4] = s->reservedU[4] + 1;
    }
}

unsigned long long FleetShipCombo_GetOracleRequestSeq(void) {
    FscShared* s = LazyOpen();
    return s ? s->reservedU[4] : 0;
}

void FleetShipCombo_AckOracleResponse(unsigned long long seq) {
    FscShared* s = LazyOpen();
    if (s) {
        s->reservedU[5] = seq;
    }
}

unsigned long long FleetShipCombo_GetOracleResponseAck(void) {
    FscShared* s = LazyOpen();
    return s ? s->reservedU[5] : 0;
}

// ---- Shared-window open request over reservedU[6] (layout unchanged — MUST match 2ship) ----
// 2ship's "Shared" tab bumps this counter; our client pump (FleetOracleClient) opens the
// Fleet Shared window when it sees the change.
void FleetShipCombo_RequestSharedWindowOpen(void) {
    FscShared* s = LazyOpen();
    if (s) {
        s->reservedU[6] = s->reservedU[6] + 1;
    }
}

unsigned long long FleetShipCombo_GetSharedWindowOpenSeq(void) {
    FscShared* s = LazyOpen();
    return s ? s->reservedU[6] : 0;
}

// ---- Cross-game RESTART over reservedU[10] (layout unchanged — MUST match the other repo) ----
// A reset in one game bumps reservedU[10]; the other game's per-frame pump sees the new value and
// resets itself too. sRestartSelfSeq marks the value we ourselves bumped/observed so we never respond
// to our own reset (which would ping-pong forever).
static unsigned long long sRestartSelfSeq = 0;
static bool sRestartSeqInit = false;

void FleetShipCombo_SignalRestart(void) {
    FscShared* s = LazyOpen();
    if (s) {
        s->reservedU[10] = s->reservedU[10] + 1;
        sRestartSelfSeq = s->reservedU[10]; // our own bump; our pump must not respond to it
        sRestartSeqInit = true;
    }
}

int FleetShipCombo_ConsumeRestartRequest(void) {
    FscShared* s = LazyOpen();
    if (!s) {
        return 0;
    }
    if (!sRestartSeqInit) {
        sRestartSeqInit = true;
        sRestartSelfSeq = s->reservedU[10]; // don't fire on any pre-existing value at attach
        return 0;
    }
    if (s->reservedU[10] == sRestartSelfSeq) {
        return 0; // nothing new, or our own reset
    }
    sRestartSelfSeq = s->reservedU[10];
    return 1;
}

// A restart puts the player back on OCARINA OF TIME's title screen, always. MM's title/file select
// are OoT-driven and must stay unreachable, so whichever game was active before the reset, the
// combo comes back up with OoT in front and MM frozen off-screen on its logo.
void FleetShipCombo_YieldToOoT(void) {
    FscShared* s = LazyOpen();
    if (!s) {
        return; // standalone Ship: nothing to yield
    }
    s->activeGame = 0;
    s->uiFocus = 0;
    CVarSetInteger("isPlayerIn2Ship", 0);
    Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
}

// ---- Guest (2ship) watchdog over reservedU[0] ----

unsigned long long FleetShipCombo_GetGuestHeartbeat(void) {
    FscShared* s = LazyOpen();
    return s ? s->reservedU[0] : 0;
}

namespace {

// A guest that stopped turning frames for this long is hung, not busy. Generously above any normal
// stall (a scene load, a save write): the cost of being wrong is killing a working session.
//
// Raised 10s -> 30s on 2026-07-31. Healthy sessions really do go quiet for a while: a warp's scene
// load plus the save handshake leaves gaps of ~9s in the logs, and that was with everything working.
// This timer TEARS THE COMBO DOWN, so it has to sit far above the worst legitimate stall — a hung
// guest noticed 20s later costs nothing, a working session killed by a slow scene load costs the
// player their game. The warp uses its own, much shorter probe below, because "wait a moment before
// travelling" is cheap in a way that "close everything" is not.
constexpr uint64_t kGuestHangMs = 30000;
// "Not turning frames right now" for the warp probe. Short on purpose: it only makes the warp HOLD
// at black and re-test, never gives up on its own.
constexpr uint64_t kGuestQuietMs = 6000;
// Time the "MM went down" notice stays up before Ship closes, so the player reads WHY the window
// vanished instead of watching it disappear on them.
constexpr uint64_t kGuestShutdownGraceMs = 2000;

uint64_t sLastBeat = 0;
uint64_t sLastBeatMs = 0;
// RAW liveness, tracked with NO "it's busy" excuse applied: the last time the guest's frame counter
// actually moved. The pair above is the shutdown timer (and gets its clock reset while the guest is
// legitimately busy); this pair is what answers "would flipping to MM right now strand the player?"
uint64_t sRawBeat = 0;
uint64_t sRawBeatMs = 0;
uint64_t sShutdownAtMs = 0; // != 0: shutdown scheduled for this timestamp
bool sGuestDown = false;

uint64_t NowMs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// The oracle runs INSIDE 2ship's frame, and a fill turn can legitimately chew through tens of
// seconds without yielding. During generation (or with any request still unanswered) a still
// heartbeat means "MM is thinking", not "MM is gone" -- so the hang test simply doesn't apply.
//
// BUT THE EXCUSE MUST EXPIRE. An unanswered oracle request is exactly what a CRASHED or HUNG guest
// leaves behind: it can never ack, so `responseAck < requestSeq` stays true forever and the hang
// watchdog below is disabled for the rest of the session. That is not theory — it is what the
// 2026-07-31 logs show: 2ship stopped beating at 11:43:47, the player asked for a generation twice
// (both timed out), and from then on Ship believed MM was "thinking" for four straight minutes,
// right up to the portal jump that flipped the player into a game that no longer existed. So the
// excuse is capped: past this, a silent guest is a dead guest no matter what it owes us.
constexpr uint64_t kGuestBusyMaxMs = 180000; // 3 min — well past any real fill; not "forever"

bool GuestIsLegitimatelyBusy() {
    static uint64_t sBusySinceMs = 0;
    static bool sExpiredLogged = false;

    const bool busy =
        FleetCombo_IsRunning() || (FleetShipCombo_GetOracleResponseAck() < FleetShipCombo_GetOracleRequestSeq());
    if (!busy) {
        sBusySinceMs = 0;
        sExpiredLogged = false;
        return false;
    }
    if (sBusySinceMs == 0) {
        sBusySinceMs = NowMs();
    }
    if (NowMs() - sBusySinceMs > kGuestBusyMaxMs) {
        if (!sExpiredLogged) {
            sExpiredLogged = true;
            SPDLOG_ERROR("[FleetWatchdog] 2ship has owed us an oracle answer for {} s — that is not 'busy' any "
                         "more; re-enabling the hang watchdog",
                         (NowMs() - sBusySinceMs) / 1000);
        }
        return false;
    }
    return true;
}

void BeginGuestShutdown(const char* why) {
    if (sGuestDown) {
        return;
    }
    sGuestDown = true;
    sShutdownAtMs = NowMs() + kGuestShutdownGraceMs;
    SPDLOG_ERROR("[FleetShipCombo] guest (2ship) went down: {} -- closing the combo", why);
    Notification::Emit({
        .prefix = "[Fleet] ",
        .message = std::string("Majora's Mask went down (") + why + ")",
        .suffix = " - closing the combo",
        .remainingTime = 5.0f,
    });
}

} // namespace

void FleetShipCombo_PollGuestAlive(void) {
    if (sShutdownAtMs != 0) {
        if (NowMs() < sShutdownAtMs) {
            return; // let the notice sit on screen for a moment first
        }
        sShutdownAtMs = 0;
#ifdef _WIN32
        // A HUNG guest is still a live process, and it would outlive us as an orphan with no window
        // (we hid it) and no way for the player to reach it. Whatever is left of it goes with us.
        if (sChildProcess != nullptr && WaitForSingleObject(sChildProcess, 0) != WAIT_OBJECT_0) {
            TerminateProcess(sChildProcess, 0);
        }
#else
        if (sChildPid != 0 && kill(sChildPid, 0) == 0) {
            kill(sChildPid, SIGKILL);
        }
#endif
        if (auto window = Ship::Context::GetRawInstance()->GetWindow()) {
            window->Close();
        }
        return;
    }

#ifdef _WIN32
    if (sChildProcess == nullptr) {
        return; // no child of ours: standalone Ship, or the launch failed (already reported)
    }
    if (WaitForSingleObject(sChildProcess, 0) == WAIT_OBJECT_0) {
        BeginGuestShutdown("its process exited");
        return;
    }
#else
    if (sChildPid == 0) {
        return;
    }
    // Reap first: a child nobody waited on stays a zombie, and a zombie still answers kill(pid, 0)
    // happily -- the signal probe alone would never notice 2ship died.
    int status = 0;
    if (waitpid(sChildPid, &status, WNOHANG) == sChildPid) {
        sChildPid = 0; // reaped: nothing left to terminate at shutdown
        BeginGuestShutdown("its process exited");
        return;
    }
    if (kill(sChildPid, 0) != 0 && errno == ESRCH) {
        BeginGuestShutdown("its process exited");
        return;
    }
#endif

    // Raw liveness FIRST, before any excuse: this is the number IsGuestResponsive answers from, and
    // it must keep an honest clock even while the guest is allowed to be slow.
    {
        const uint64_t raw = FleetShipCombo_GetGuestHeartbeat();
        if (raw != sRawBeat || sRawBeatMs == 0) {
            sRawBeat = raw;
            sRawBeatMs = NowMs();
        }
    }

    if (GuestIsLegitimatelyBusy()) {
        sLastBeatMs = NowMs(); // the clock only runs while MM is supposed to be turning frames
        return;
    }
    uint64_t beat = FleetShipCombo_GetGuestHeartbeat();
    if (beat == 0) {
        return; // 2ship hasn't reached its frame loop yet (a first-run asset extraction takes
                // minutes). Until it beats once there is nothing to time out; a guest that dies
                // while starting up is caught by the process check above.
    }
    if (beat != sLastBeat || sLastBeatMs == 0) {
        sLastBeat = beat;
        sLastBeatMs = NowMs();
        return;
    }
    if (NowMs() - sLastBeatMs > kGuestHangMs) {
        BeginGuestShutdown("it stopped responding");
    }
}

// "Is it safe to hand the player to MM right now?" — asked by the warp on the frame it would flip.
//
// The flip is a one-way door: it makes MM the active game and freezes OoT. If MM is dead or hung
// when that happens, the player is left staring at a window that will never update again, with no
// way back — the 2026-07-31 report. Everything else in the warp can be retried; this cannot, so it
// gets checked BEFORE we commit, not after.
//
// Deliberately NOT routed through GuestIsLegitimatelyBusy: a guest that is busy is also a guest that
// cannot receive a player. The only question here is whether it is turning frames.
int FleetShipCombo_IsGuestResponsive(void) {
    if (sGuestDown || sShutdownAtMs != 0) {
        return 0; // already on its way out
    }
#ifdef _WIN32
    if (sChildProcess != nullptr && WaitForSingleObject(sChildProcess, 0) == WAIT_OBJECT_0) {
        return 0; // the process is gone
    }
#else
    if (sChildPid != 0 && kill(sChildPid, 0) != 0 && errno == ESRCH) {
        return 0;
    }
#endif
    if (FleetShipCombo_GetGuestHeartbeat() == 0) {
        return 0; // never reached its frame loop (still extracting assets, or died starting up)
    }
    if (sRawBeatMs != 0 && (NowMs() - sRawBeatMs) > kGuestQuietMs) {
        return 0; // alive as a process, but not turning frames — flipping there is a black screen
    }
    return 1;
}

// Told to the player from the C side of the warp (custom_items_common.c), which has no access to
// Notification. Says WHY the door did nothing, so a refused warp never looks like a bug.
void FleetShipCombo_ReportGuestUnavailable(void) {
    SPDLOG_ERROR("[FleetWatchdog] warp to MM REFUSED: 2ship is not turning frames (last beat {} ms ago) — "
                 "staying in OoT instead of flipping into a dead game",
                 sRawBeatMs == 0 ? 0 : (NowMs() - sRawBeatMs));
    Notification::Emit({
        .prefix = "[Fleet] ",
        .message = "Majora's Mask is not responding — stayed in Ocarina of Time",
        .suffix = " (the portal will work once MM is back)",
        .remainingTime = 5.0f,
    });
}

// ---- UI-overlay texture over reservedU[7..9] (layout unchanged — MUST match 2ship) ----
// A SECOND shared texture with ONLY 2ship's ImGui windows (trackers etc.) over a transparent
// background, so we can draw MM's UI on top of whichever game is active (both trackers at once).
// [7] = D3D11 shared handle (0 = none), [8] = (width<<32)|height, [9] = (dxgiFormat<<32)|frameIndex.
void FleetShipCombo_PublishUiTexture(unsigned long long handle, unsigned int width, unsigned int height,
                                     unsigned int dxgiFormat, unsigned int frameIndex) {
    FscShared* s = LazyOpen();
    if (!s) {
        return;
    }
    s->reservedU[8] = ((unsigned long long)width << 32) | height;
    s->reservedU[9] = ((unsigned long long)dxgiFormat << 32) | frameIndex;
    s->reservedU[7] = handle; // handle last: the consumer keys re-opens off it
}

int FleetShipCombo_GetUiTexture(unsigned long long* handle, unsigned int* width, unsigned int* height,
                                unsigned int* dxgiFormat, unsigned int* frameIndex) {
    FscShared* s = LazyOpen();
    if (!s) {
        return 0;
    }
    if (handle) {
        *handle = s->reservedU[7];
    }
    if (width) {
        *width = (unsigned int)(s->reservedU[8] >> 32);
    }
    if (height) {
        *height = (unsigned int)(s->reservedU[8] & 0xFFFFFFFFu);
    }
    if (dxgiFormat) {
        *dxgiFormat = (unsigned int)(s->reservedU[9] >> 32);
    }
    if (frameIndex) {
        *frameIndex = (unsigned int)(s->reservedU[9] & 0xFFFFFFFFu);
    }
    return s->reservedU[7] != 0 ? 1 : 0;
}

// ---- Combo active SLOT over reservedU[11] (layout unchanged — MUST match the other repo) ----
// OoT publishes which save slot the current combo file is (0..2 stored as 1..3; 0 = unset) when a
// combo file is created/loaded, so MM auto-loads the SAME slot when it becomes active (its own
// per-process gFleetCombo.LastSlot may not match on a fresh combo).
void FleetShipCombo_SetComboSlot(int slot) {
    FscShared* s = LazyOpen();
    if (s && slot >= 0 && slot <= 2) {
        s->reservedU[11] = (uint64_t)(slot + 1);
    }
}

int FleetShipCombo_GetComboSlot(void) {
    FscShared* s = LazyOpen();
    if (!s || s->reservedU[11] == 0) {
        return -1;
    }
    return (int)s->reservedU[11] - 1;
}
