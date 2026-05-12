/**
 * o2r_loader.cpp - Generalist .o2r player-model loader
 *
 * See o2r_loader.h for design notes.
 */

#include "o2r_loader.h"
#include "soh/ResourceManagerHelpers.h"

#include <cstring>
#include <vector>
#include <spdlog/spdlog.h>

#define O2R_LOG(...) SPDLOG_INFO("[O2rLoader] " __VA_ARGS__)

namespace {

struct O2rEntry {
    char name[32];
    char skelOtrPath[128];
    FlexSkeletonHeader* skel; // lazy-loaded on first force
    bool loaded;
};

std::vector<O2rEntry> sModels;
s32 sForcedIdx = -1;
bool sInitialized = false;

// Saved player skeleton state during swap.
void** sSavedSkeleton = nullptr;
s32 sSavedDListCount = 0;

// Forward decl so EnsureInit can call the public Register.
void RegisterImpl(const char* name, const char* skelOtrPath);

void EnsureInit() {
    if (sInitialized) return;
    sInitialized = true;
    // Register known o2r-based models. Add additional entries here as needed.
    RegisterImpl("garo", "__OTR__objects/garo/gGaroSkel");
}

s32 FindByName(const char* name) {
    if (!name || !*name) return -1;
    for (size_t i = 0; i < sModels.size(); i++) {
        if (std::strcmp(sModels[i].name, name) == 0) {
            return (s32)i;
        }
    }
    return -1;
}

// Attempt to resolve the skeleton resource. Returns true on success.
bool LazyLoad(O2rEntry& e) {
    if (e.loaded) return true;
    SkeletonHeader* hdr = ResourceMgr_LoadSkeletonByName(e.skelOtrPath, nullptr);
    if (hdr == nullptr) {
        O2R_LOG("LazyLoad FAIL: '{}' could not resolve '{}' (is the .o2r in nei/?)",
                e.name, e.skelOtrPath);
        return false;
    }
    e.skel = (FlexSkeletonHeader*)hdr;
    e.loaded = true;
    O2R_LOG("LazyLoad OK: '{}' (limbCount={}, dListCount={})",
            e.name, e.skel->sh.limbCount, e.skel->dListCount);
    return true;
}

void RegisterImpl(const char* name, const char* skelOtrPath) {
    if (!name || !*name || !skelOtrPath || !*skelOtrPath) return;
    if (FindByName(name) >= 0) return; // already registered

    O2rEntry e{};
    std::strncpy(e.name, name, sizeof(e.name) - 1);
    std::strncpy(e.skelOtrPath, skelOtrPath, sizeof(e.skelOtrPath) - 1);
    e.skel = nullptr;
    e.loaded = false;
    sModels.push_back(e);
}

} // namespace

extern "C" void O2rLoader_Init(void) {
    // Idempotent — defaults register lazily anyway, but allow explicit init.
    EnsureInit();
}

extern "C" void O2rLoader_Register(const char* name, const char* skelOtrPath) {
    EnsureInit();
    RegisterImpl(name, skelOtrPath);
}

extern "C" void O2rLoader_ForceModel(const char* name) {
    EnsureInit();
    O2R_LOG("ForceModel('{}')", name ? name : "<null>");
    if (!name || !*name) {
        sForcedIdx = -1;
        return;
    }
    s32 idx = FindByName(name);
    if (idx < 0) {
        O2R_LOG("ForceModel FAIL: no registered entry named '{}'", name);
        return;
    }
    if (!LazyLoad(sModels[idx])) return;
    sForcedIdx = idx;
    O2R_LOG("ForceModel ACTIVE: '{}' (idx={})", name, idx);
}

extern "C" void O2rLoader_ClearForcedModel(void) {
    O2R_LOG("ClearForcedModel");
    sForcedIdx = -1;
}

extern "C" u8 O2rLoader_HasActiveModel(void) {
    return (sForcedIdx >= 0 && sForcedIdx < (s32)sModels.size() && sModels[sForcedIdx].loaded) ? 1 : 0;
}

extern "C" const char* O2rLoader_GetForcedName(void) {
    if (!O2rLoader_HasActiveModel()) return nullptr;
    return sModels[sForcedIdx].name;
}

extern "C" void O2rLoader_SwapSkeleton(Player* player) {
    if (!O2rLoader_HasActiveModel() || !player) return;
    FlexSkeletonHeader* flex = sModels[sForcedIdx].skel;
    if (!flex || !flex->sh.segment) return;

    sSavedSkeleton = player->skelAnime.skeleton;
    sSavedDListCount = player->skelAnime.dListCount;

    player->skelAnime.skeleton = flex->sh.segment;
    player->skelAnime.dListCount = flex->dListCount;
}

extern "C" void O2rLoader_RestoreSkeleton(Player* player) {
    if (!sSavedSkeleton || !player) return;
    player->skelAnime.skeleton = sSavedSkeleton;
    player->skelAnime.dListCount = sSavedDListCount;
    sSavedSkeleton = nullptr;
    sSavedDListCount = 0;
}
