#include "animationViewer.h"

#include "soh/SohGui/UIWidgets.hpp"
#include "soh/SohGui/SohGui.hpp"
#include "soh/OTRGlobals.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/resource/type/PlayerAnimation.h"
#include "soh/resource/type/SohResourceType.h"

#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>

#include "soh/Extractor/portable-file-dialogs.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

extern "C" {
#include <z64.h>
#include "z64animation.h"
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace {

char sAnimSearchString[64] = "";
std::vector<std::string> sAnimList;
std::string sSelectedAnim = "";
int16_t sAnimSearchDebounce = -1;
bool sAnimDoSearch = false;

bool sPreviewActive = false;
int sAnimMode = ANIMMODE_LOOP;
float sPlaySpeed = 1.0f;
bool sScrubMode = false;
float sScrubFrame = 0.0f;
int sCachedFrameCount = 0;

std::string sLastAppliedAnim = "";
int sLastAppliedMode = -1;
bool sLastAppliedScrub = false;
bool sForceRestart = false;

uint32_t sOnPlayerUpdateHook = 0;

const char* GetDisplayName(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return path.c_str();
    }
    return path.c_str() + pos + 1;
}

bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                          [](char a, char b) { return std::tolower(a) == std::tolower(b); });
    return it != haystack.end();
}

void PerformAnimationSearch() {
    sAnimList.clear();

    auto archiveManager = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
    if (archiveManager == nullptr) {
        return;
    }

    auto results = archiveManager->ListFiles("*PlayerAnim*");
    if (results == nullptr) {
        return;
    }

    std::string filter(sAnimSearchString);

    for (size_t i = 0; i < results->size(); i++) {
        const std::string& path = results->at(i);
        if (path.find("PlayerAnim_") == std::string::npos) {
            continue;
        }
        if (!filter.empty() && !ContainsCaseInsensitive(path, filter)) {
            continue;
        }
        sAnimList.push_back(path);
    }

    std::sort(sAnimList.begin(), sAnimList.end(), [](const std::string& a, const std::string& b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                            [](char c1, char c2) { return std::tolower(c1) < std::tolower(c2); });
    });
}

// Stable wrappers for PlayerAnimation resources. The resource manager returns
// only the raw int16 payload; LinkAnimation_Change needs a header with a
// proper frameCount and a `segment` pointer, so we cache one wrapper per path.
// Pointer stability matters because SkelAnime holds onto the address across
// frames. std::map (not unordered_map) is used so the LinkAnimationHeader
// addresses never move under rehash.
std::map<std::string, LinkAnimationHeader> sPlayerAnimWrappers;

// Raw player-anim layout: 22 Vec3s per frame + 1 trailing appearanceInfo s16.
//   3 root_xyz + 21 limb rotations * 3 + 1 appearanceInfo = 67 s16.
// This matches Human Link / Garo (21-limb skeleton). MM forms with different
// limb counts (Goron 17, Zora 23, Deku 12) would need different stride; not a
// concern for the viewer since vanilla OOT only ships Human Link.
constexpr s32 PLAYER_ANIM_S16_PER_FRAME = 67;

LinkAnimationHeader* LoadSelectedAnim() {
    if (sSelectedAnim.empty()) {
        return nullptr;
    }

    auto res = ResourceMgr_GetResourceByNameHandlingMQ(sSelectedAnim.c_str());
    if (res == nullptr) {
        return nullptr;
    }

    uint32_t type = res->GetInitData()->Type;
    if (type == static_cast<uint32_t>(SOH::ResourceType::SOH_PlayerAnimation)) {
        // PlayerAnimation: raw s16 payload, no header struct. Wrap it.
        auto playerAnim = std::static_pointer_cast<SOH::PlayerAnimation>(res);
        LinkAnimationHeader& wrapper = sPlayerAnimWrappers[sSelectedAnim];

        size_t totalS16 = playerAnim->GetPointerSize() / sizeof(int16_t);
        wrapper.common.frameCount = (s16)(totalS16 / PLAYER_ANIM_S16_PER_FRAME);
        wrapper.segment = (void*)playerAnim->GetPointer();
        return &wrapper;
    }

    // Animation (indexed) or other type: keep the legacy naive cast — works
    // because AnimationHeader and LinkAnimationHeader share a common prefix.
    return (LinkAnimationHeader*)ResourceMgr_LoadAnimByName(sSelectedAnim.c_str());
}

void ApplyAnimationToPlayer() {
    if (!sPreviewActive || gPlayState == nullptr || sSelectedAnim.empty()) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) {
        return;
    }

    LinkAnimationHeader* anim = LoadSelectedAnim();
    if (anim == nullptr) {
        return;
    }

    s16 lastFrame = Animation_GetLastFrame(anim);
    sCachedFrameCount = lastFrame;

    // We only call LinkAnimation_Change when something requires a real restart.
    // Otherwise we just keep skelAnime->animation pinned to our anim so the player's
    // own SkelAnime_Update advances curFrame naturally. Calling LinkAnimation_Change
    // every frame freezes the pose because it re-queues a load at startFrame=0.
    bool needRestart = sForceRestart || (sLastAppliedAnim != sSelectedAnim) ||
                       (sLastAppliedMode != sAnimMode) || (sLastAppliedScrub != sScrubMode) ||
                       (player->skelAnime.animation != (void*)anim);

    sLastAppliedAnim = sSelectedAnim;
    sLastAppliedMode = sAnimMode;
    sLastAppliedScrub = sScrubMode;
    sForceRestart = false;

    if (sScrubMode) {
        float frame = sScrubFrame;
        if (frame < 0.0f) frame = 0.0f;
        if (frame > (float)lastFrame) frame = (float)lastFrame;

        if (needRestart) {
            LinkAnimation_Change(gPlayState, &player->skelAnime, anim, 0.0f, frame, frame, ANIMMODE_ONCE, 0.0f);
        }
        // Freeze: keep curFrame pinned and stop the player's natural advance.
        player->skelAnime.curFrame = frame;
        player->skelAnime.playSpeed = 0.0f;
    } else {
        if (needRestart) {
            LinkAnimation_Change(gPlayState, &player->skelAnime, anim, sPlaySpeed, 0.0f, (f32)lastFrame,
                                 (u8)sAnimMode, 0.0f);
        } else {
            // Keep these in sync in case the user adjusted the slider mid-playback.
            player->skelAnime.playSpeed = sPlaySpeed;
            player->skelAnime.endFrame = (f32)lastFrame;
        }
    }
}

} // namespace

AnimationViewerWindow::~AnimationViewerWindow() {
    if (sOnPlayerUpdateHook != 0) {
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnPlayerUpdate>(sOnPlayerUpdateHook);
        sOnPlayerUpdateHook = 0;
    }
}

void AnimationViewerWindow::InitElement() {
    PerformAnimationSearch();

    if (sOnPlayerUpdateHook == 0) {
        sOnPlayerUpdateHook =
            GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>(ApplyAnimationToPlayer);
    }
}

void AnimationViewerWindow::DrawElement() {
    ImGui::BeginDisabled(CVarGetInteger(CVAR_SETTING("DisableChanges"), 0));
    UIWidgets::PushStyleInput(THEME_COLOR);

    if (ImGui::InputText("Search Animations", sAnimSearchString, ARRAY_COUNT(sAnimSearchString))) {
        sAnimDoSearch = true;
        sAnimSearchDebounce = 30;
    }
    UIWidgets::PopStyleInput();

    if (sAnimDoSearch) {
        if (sAnimSearchDebounce == 0) {
            sAnimDoSearch = false;
            PerformAnimationSearch();
        }
        sAnimSearchDebounce--;
    }

    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        PerformAnimationSearch();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load .o2r…")) {
        auto selection =
            pfd::open_file("Select an .o2r archive", ".", { "Shipwright archives", "*.o2r *.zip" }).result();
        if (!selection.empty()) {
            auto archiveManager = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
            if (archiveManager != nullptr) {
                auto archive = archiveManager->AddArchive(selection[0]);
                if (archive != nullptr) {
                    SPDLOG_INFO("[AnimationViewer] loaded archive: {}", selection[0]);
                    PerformAnimationSearch();
                } else {
                    SPDLOG_WARN("[AnimationViewer] failed to load archive: {}", selection[0]);
                }
            }
        }
    }

    ImGui::Text("Matches: %zu", sAnimList.size());

    UIWidgets::PushStyleCombobox(THEME_COLOR);
    const char* selectedLabel = sSelectedAnim.empty() ? "<none>" : GetDisplayName(sSelectedAnim);
    if (ImGui::BeginCombo("Active Animation", selectedLabel)) {
        for (size_t i = 0; i < sAnimList.size(); i++) {
            const char* label = GetDisplayName(sAnimList[i]);
            bool isSelected = (sAnimList[i] == sSelectedAnim);
            if (ImGui::Selectable(label, isSelected)) {
                sSelectedAnim = sAnimList[i];
                // Refresh frameCount from the newly selected anim.
                LinkAnimationHeader* anim = LoadSelectedAnim();
                if (anim != nullptr) {
                    sCachedFrameCount = Animation_GetLastFrame(anim);
                    if (sScrubFrame > (float)sCachedFrameCount) {
                        sScrubFrame = 0.0f;
                    }
                }
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    UIWidgets::PopStyleCombobox();

    if (!sSelectedAnim.empty()) {
        ImGui::TextWrapped("Path: %s", sSelectedAnim.c_str());
        ImGui::Text("Last Frame: %d", sCachedFrameCount);
    }

    ImGui::Separator();

    UIWidgets::PushStyleCheckbox(THEME_COLOR);
    bool prevPreview = sPreviewActive;
    ImGui::Checkbox("Preview on Link (live)", &sPreviewActive);
    UIWidgets::PopStyleCheckbox();
    if (sPreviewActive && !prevPreview) {
        sForceRestart = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        sForceRestart = true;
    }
    ImGui::TextWrapped(
        "While enabled, the selected animation is re-applied to the player every frame, "
        "overriding the normal state machine. Disable to return Link to normal behavior.");

    ImGui::Separator();

    ImGui::Text("Mode:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Loop", sAnimMode == ANIMMODE_LOOP)) {
        sAnimMode = ANIMMODE_LOOP;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Once", sAnimMode == ANIMMODE_ONCE)) {
        sAnimMode = ANIMMODE_ONCE;
    }

    ImGui::SliderFloat("Play Speed", &sPlaySpeed, 0.0f, 3.0f, "%.2fx");

    ImGui::Separator();

    UIWidgets::PushStyleCheckbox(THEME_COLOR);
    ImGui::Checkbox("Manual Frame Scrub", &sScrubMode);
    UIWidgets::PopStyleCheckbox();

    ImGui::BeginDisabled(!sScrubMode);
    float maxFrame = (sCachedFrameCount > 0) ? (float)sCachedFrameCount : 1.0f;
    ImGui::SliderFloat("Frame", &sScrubFrame, 0.0f, maxFrame, "%.1f");
    ImGui::EndDisabled();

    ImGui::Separator();

    if (ImGui::Button("Stop / Release Link")) {
        sPreviewActive = false;
    }

    if (gPlayState == nullptr) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "No active gameplay — load a save to preview.");
    }

    ImGui::EndDisabled();
}
