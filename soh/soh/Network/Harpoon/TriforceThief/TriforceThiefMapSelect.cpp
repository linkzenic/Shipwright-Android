// Triforce Thief map select overlay — 3x2 grid for 6 maps. Visual port of
// Scooter's TriforceThiefMapSelectWindow, sharing the same map_select/*.png
// assets as Prop Hunt (background, navi icons). Triggered by
// `gameState == HARPOON_STATE_MAP_SELECT` when room gamemode is triforce_thief.

#include "TriforceThief.h"
#include "../Harpoon.h"

#include <libultraship/libultraship.h>
#include <spdlog/spdlog.h>
#include <imgui.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

namespace {

struct TTMapDef {
    const char* name;
    const char* description;
    s32 entranceIndex;
    const char* thumbnailPath;
};

constexpr TTMapDef kTTMaps[] = {
    { "Hyrule Field",         "Wide open field — lots of room to run.",
      205, "map_select/thumbnail_kakariko_village.png" },
    { "Zora's River",         "Winding river with cliffs and waterfalls.",
      234, "map_select/thumbnail_zora_river.png" },
    { "Gerudo Fortress",      "Desert compound with rooftops and corridors.",
      297, "map_select/thumbnail_gerudo_fortress.png" },
    { "Kokiri Forest",        "Peaceful village with bridges and trees.",
      238, "map_select/thumbnail_kokiri_forest.png" },
    { "Kakariko Village",     "Mountain village with rooftops and alleys.",
      219, "map_select/thumbnail_kakariko_village.png" },
    { "Sacred Forest Meadow", "Forest maze with open clearings.",
      252, "map_select/thumbnail_forest_temple.png" },
};
constexpr s32 kTTMapCount = (s32)(sizeof(kTTMaps) / sizeof(kTTMaps[0]));
constexpr int kTTGridCols = 3;
constexpr int kTTGridRows = 2;

bool sTTTexturesLoaded = false;
s32  sTTStickDebounce  = 0;
bool sTTHasVoted       = false;

void SafeLoadTexture(const char* name, const char* path) {
    auto archMgr = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager();
    if (!archMgr->HasFile(path)) return;
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    gui->LoadTextureFromRawImage(name, path);
}

void LoadTTTextures() {
    SafeLoadTexture("tt-bg",         "map_select/bg.png");
    SafeLoadTexture("tt-navi",       "map_select/navi.png");
    SafeLoadTexture("tt-navi-white", "map_select/navi_white.png");
    for (int i = 0; i < kTTMapCount; i++) {
        if (kTTMaps[i].thumbnailPath) {
            SafeLoadTexture(kTTMaps[i].thumbnailPath, kTTMaps[i].thumbnailPath);
        }
    }
    sTTTexturesLoaded = true;
}

class TriforceThiefMapSelectWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override {}
    void DrawElement() override {}
    void UpdateElement() override {}

    void Draw() override {
        auto harpoon = Harpoon::Instance;
        if (!harpoon || !harpoon->isConnected) return;
        if (harpoon->gameState != HARPOON_STATE_MAP_SELECT) return;
        if (harpoon->currentRoomGameMode != "triforce_thief") return;

        bool isHost = (harpoon->ownClientId == harpoon->hostClientId);
        // Everybody sees the overlay regardless of mode. Non-hosts in
        // HOST_CHOOSES mode can spectate the host's cursor (it broadcasts
        // MAP_HOVER) but can't press A to confirm — the input handler at
        // the bottom of Draw() gates the confirm on `isHost`.

        static HarpoonGameState sPrevState = HARPOON_STATE_LOBBY;
        if (sPrevState != HARPOON_STATE_MAP_SELECT) sTTHasVoted = false;
        sPrevState = harpoon->gameState;

        if (!sTTTexturesLoaded) LoadTTTextures();

        auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
        auto vp = ImGui::GetMainViewport();
        float vpW = vp->Size.x, vpH = vp->Size.y;
        float vpX = vp->Pos.x,  vpY = vp->Pos.y;

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground;

        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::SetNextWindowViewport(vp->ID);

        ImGui::Begin("##TriforceThiefMapSelect", nullptr, flags);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        s32 localSel = harpoon->selectedMapIndex;
        if (localSel < 0 || localSel >= kTTMapCount) localSel = 0;

        // Background
        ImTextureID bgTex = gui->GetTextureByName("tt-bg");
        if (bgTex) dl->AddImage(bgTex, vp->Pos, ImVec2(vpX + vpW, vpY + vpH));
        else       dl->AddRectFilled(vp->Pos, ImVec2(vpX + vpW, vpY + vpH),
                                     IM_COL32(10, 10, 30, 245));

        // Layout: 3x2 grid top
        float gridW = vpW * 0.96f, gridH = vpH * 0.54f;
        float gridStartX = vpX + vpW * 0.02f, gridStartY = vpY + vpH * 0.02f;
        float cellW = gridW / kTTGridCols, cellH = gridH / kTTGridRows;
        float pad = 4.0f;
        float bottomY = gridStartY + gridH + vpH * 0.02f;
        float prevW = vpW * 0.42f, prevH = (vpH - (bottomY - vpY)) - vpH * 0.02f;
        float prevX = vpX + vpW * 0.02f, prevY = bottomY;
        float rcCenterX = vpX + vpW * 0.72f;

        // Thumbnail grid 3x2
        for (int i = 0; i < kTTMapCount; i++) {
            int col = i % kTTGridCols;
            int row = i / kTTGridCols;
            float x = gridStartX + col * cellW;
            float y = gridStartY + row * cellH;
            ImVec2 tl(x + pad, y + pad);
            ImVec2 br(x + cellW - pad, y + cellH - pad);

            if (kTTMaps[i].thumbnailPath) {
                ImTextureID thumb = gui->GetTextureByName(kTTMaps[i].thumbnailPath);
                if (thumb) dl->AddImage(thumb, tl, br);
                else       dl->AddRectFilled(tl, br, IM_COL32(40, 40, 60, 200));
            } else {
                dl->AddRectFilled(tl, br, IM_COL32(40, 40, 60, 200));
            }
            if (i != localSel) dl->AddRectFilled(tl, br, IM_COL32(0, 0, 0, 120));

            ImGui::SetWindowFontScale(0.85f);
            const char* name = kTTMaps[i].name;
            ImVec2 ns = ImGui::CalcTextSize(name);
            float nx = x + (cellW - ns.x) * 0.5f, ny = br.y - ns.y - 3.0f;
            dl->AddRectFilled(ImVec2(tl.x, ny - 2), ImVec2(br.x, br.y),
                              IM_COL32(0, 0, 0, 170));
            dl->AddText(ImVec2(nx + 1, ny + 1), IM_COL32(0, 0, 0, 255), name);
            dl->AddText(ImVec2(nx, ny), IM_COL32(255, 255, 255, 255), name);
            ImGui::SetWindowFontScale(1.0f);

            // Selection border — gold (TT theme).
            if (i == localSel) {
                dl->AddRect(tl, br, IM_COL32(255, 215, 0, 255), 0, 0, 3.5f);
                dl->AddRect(ImVec2(tl.x - 2, tl.y - 2), ImVec2(br.x + 2, br.y + 2),
                            IM_COL32(255, 230, 50, 120), 0, 0, 2.0f);
            }
        }

        // Preview (bottom-left)
        ImVec2 pTL(prevX, prevY), pBR(prevX + prevW, prevY + prevH);
        if (localSel < kTTMapCount && kTTMaps[localSel].thumbnailPath) {
            ImTextureID prev = gui->GetTextureByName(kTTMaps[localSel].thumbnailPath);
            if (prev) dl->AddImage(prev, pTL, pBR);
            dl->AddRect(pTL, pBR, IM_COL32(200, 170, 50, 200), 0, 0, 2.0f);
        }
        if (localSel < kTTMapCount) {
            ImGui::SetWindowFontScale(0.85f);
            const char* desc = kTTMaps[localSel].description;
            ImVec2 ds = ImGui::CalcTextSize(desc);
            float dx = prevX + (prevW - ds.x) * 0.5f;
            float dy = pBR.y - ds.y - 6.0f;
            dl->AddRectFilled(ImVec2(pTL.x, dy - 3), ImVec2(pBR.x, pBR.y),
                              IM_COL32(0, 0, 0, 180));
            dl->AddText(ImVec2(dx + 1, dy + 1), IM_COL32(0, 0, 0, 200), desc);
            dl->AddText(ImVec2(dx, dy), IM_COL32(255, 255, 200, 255), desc);
            ImGui::SetWindowFontScale(1.0f);
        }

        // Right column: title + golden banner
        ImGui::SetWindowFontScale(1.5f);
        const char* sel = "Select Map";
        ImVec2 ss = ImGui::CalcTextSize(sel);
        float stX = rcCenterX - ss.x * 0.5f, stY = bottomY;
        dl->AddText(ImVec2(stX + 1, stY + 1), IM_COL32(0, 0, 0, 200), sel);
        dl->AddText(ImVec2(stX, stY), IM_COL32(255, 215, 0, 255), sel);
        ImGui::SetWindowFontScale(1.0f);
        float afterTitleY = stY + ss.y + 20.0f;

        if (localSel < kTTMapCount) {
            ImGui::SetWindowFontScale(1.4f);
            const char* sn = kTTMaps[localSel].name;
            ImVec2 sns = ImGui::CalcTextSize(sn);
            float bpX = 35.0f, bpY = 8.0f;
            float bW = sns.x + bpX * 2, bH = sns.y + bpY * 2;
            float bX = rcCenterX - bW * 0.5f, bY = afterTitleY;
            ImVec2 bTL(bX, bY), bBR(bX + bW, bY + bH);
            dl->AddRectFilled(bTL, bBR, IM_COL32(160, 120, 20, 235), 8.0f);
            dl->AddRectFilled(ImVec2(bX + 3, bY + 3),
                              ImVec2(bX + bW - 3, bY + bH - 3),
                              IM_COL32(210, 170, 50, 210), 6.0f);
            dl->AddRect(bTL, bBR, IM_COL32(240, 200, 80, 255), 8.0f, 0, 2.5f);
            float aw = 16.0f, amY = bY + bH * 0.5f;
            dl->AddTriangleFilled(ImVec2(bX - aw, amY),
                                   ImVec2(bX + 2, bY + 2),
                                   ImVec2(bX + 2, bY + bH - 2),
                                   IM_COL32(200, 160, 40, 230));
            dl->AddTriangleFilled(ImVec2(bX + bW + aw, amY),
                                   ImVec2(bX + bW - 2, bY + 2),
                                   ImVec2(bX + bW - 2, bY + bH - 2),
                                   IM_COL32(200, 160, 40, 230));
            float nX = bX + bpX, nY = bY + bpY;
            dl->AddText(ImVec2(nX + 1, nY + 1), IM_COL32(80, 50, 0, 200), sn);
            dl->AddText(ImVec2(nX, nY), IM_COL32(255, 255, 255, 255), sn);
            ImGui::SetWindowFontScale(1.0f);

            const char* instr;
            if (harpoon->mapSelectMode == MAP_SELECT_EVERYONE_CHOOSES) {
                instr = isHost ? "Press A to confirm" : "Hold START + press A to vote";
            } else {
                instr = isHost ? "Press A to confirm" : "Waiting for host...";
            }
            ImGui::SetWindowFontScale(0.9f);
            ImVec2 is = ImGui::CalcTextSize(instr);
            float iX = rcCenterX - is.x * 0.5f, iY = bBR.y + 4.0f;
            dl->AddText(ImVec2(iX + 1, iY + 1), IM_COL32(0, 0, 0, 200), instr);
            dl->AddText(ImVec2(iX, iY), IM_COL32(255, 255, 100, 255), instr);
            ImGui::SetWindowFontScale(1.0f);
        }

        // Player navi cursors on grid
        ImTextureID naviTex      = gui->GetTextureByName("tt-navi");
        ImTextureID naviWhiteTex = gui->GetTextureByName("tt-navi-white");
        float naviSize = cellH * 0.30f;
        for (auto& [cid, c] : harpoon->clients) {
            if (!c.online) continue;
            if (harpoon->mapSelectMode == MAP_SELECT_HOST_CHOOSES &&
                cid != harpoon->hostClientId) continue;
            s32 idx = c.self ? localSel : c.mapSelectIndex;
            if (idx < 0 || idx >= kTTMapCount) idx = 0;
            int col = idx % kTTGridCols, row = idx / kTTGridCols;
            float cx = gridStartX + col * cellW + cellW * 0.5f;
            float cy = gridStartY + row * cellH + cellH * 0.30f;
            float off = (float)((int)(cid % 5) - 2) * (naviSize * 0.7f);
            cx += off;
            ImU32 tint = IM_COL32(c.color.r, c.color.g, c.color.b, 230);
            ImVec2 nTL(cx - naviSize * 0.5f, cy - naviSize * 0.5f);
            ImVec2 nBR(cx + naviSize * 0.5f, cy + naviSize * 0.5f);
            if (naviWhiteTex) {
                dl->AddImage(naviWhiteTex, nTL, nBR, ImVec2(0, 0), ImVec2(1, 1), tint);
            } else if (naviTex) {
                dl->AddImage(naviTex, nTL, nBR, ImVec2(0, 0), ImVec2(1, 1), tint);
            } else {
                dl->AddCircleFilled(ImVec2(cx, cy), 9.0f, tint);
            }
        }

        // Post-vote dim overlay for everyone-votes
        if (sTTHasVoted && harpoon->mapSelectMode == MAP_SELECT_EVERYONE_CHOOSES) {
            dl->AddRectFilled(ImVec2(vpX, vpY), ImVec2(vpX + vpW, vpY + vpH),
                              IM_COL32(0, 0, 0, 160));
            ImGui::SetWindowFontScale(2.0f);
            const char* w = "Waiting for other players...";
            ImVec2 ws = ImGui::CalcTextSize(w);
            dl->AddText(ImVec2(vpX + (vpW - ws.x) * 0.5f, vpY + (vpH - ws.y) * 0.5f),
                        IM_COL32(255, 230, 100, 255), w);
            ImGui::SetWindowFontScale(1.0f);
        }

        ImGui::End();

        // Stick navigation + A to confirm/vote. Non-hosts in HOST_CHOOSES
        // mode see the overlay (spectator view) but can't interact — only
        // the host's cursor / A press counts.
        bool canInteract = (harpoon->mapSelectMode != MAP_SELECT_HOST_CHOOSES) || isHost;
        if (gPlayState != nullptr && !sTTHasVoted && canInteract) {
            Input* input = &gPlayState->state.input[0];
            s8 sx = input->cur.stick_x;
            s8 sy = input->cur.stick_y;
            if (sTTStickDebounce > 0) sTTStickDebounce--;
            if (sTTStickDebounce == 0 && (abs(sx) > 30 || abs(sy) > 30)) {
                int col = localSel % kTTGridCols;
                int row = localSel / kTTGridCols;
                if (sx >  30) col = (col + 1) % kTTGridCols;
                if (sx < -30) col = (col + kTTGridCols - 1) % kTTGridCols;
                if (sy >  30) row = (row + kTTGridRows - 1) % kTTGridRows;
                if (sy < -30) row = (row + 1) % kTTGridRows;
                s32 newSel = row * kTTGridCols + col;
                if (newSel >= kTTMapCount) newSel = kTTMapCount - 1;
                if (newSel != harpoon->selectedMapIndex) {
                    harpoon->selectedMapIndex = newSel;
                    nlohmann::json env;
                    env["type"]       = "ROOM.BROADCAST_EVENT";
                    env["event_name"] = "TRIFORCE_THIEF.MAP_HOVER";
                    env["data"]       = { {"mapIndex", newSel} };
                    harpoon->SendJsonToRemote(env);
                }
                sTTStickDebounce = 9;
            }
            // Confirm / vote intent. Same rules as PropHunt overlay:
            //   - Host (any mode): A press.
            //   - Peer in EVERYONE_CHOOSES: A press alone (was A+START;
            //     parity with PropHunt's simplified vote input).
            //   - Peer in HOST_CHOOSES: ignored.
            bool everyoneMode = (harpoon->mapSelectMode == MAP_SELECT_EVERYONE_CHOOSES);
            bool aPress       = CHECK_BTN_ALL(input->press.button, BTN_A);
            bool trigger      = isHost ? aPress
                                       : (everyoneMode ? aPress : false);
            if (trigger) {
                s32 idx = harpoon->selectedMapIndex;
                if (everyoneMode) {
                    sTTHasVoted = true;
                    nlohmann::json env;
                    env["type"]       = "ROOM.BROADCAST_EVENT";
                    env["event_name"] = "TRIFORCE_THIEF.MAP_VOTE";
                    env["data"]       = { {"mapIndex", idx} };
                    harpoon->SendJsonToRemote(env);
                    auto myIt = harpoon->clients.find(harpoon->ownClientId);
                    if (myIt != harpoon->clients.end()) {
                        myIt->second.hasVoted = true;
                        myIt->second.mapSelectIndex = idx;
                    }
                } else if (isHost) {
                    // Shared host-confirm path: applies the map locally and
                    // broadcasts MAP_CONFIRMED + ROUND_CONFIG + TRIFORCE_SPAWN.
                    HarpoonTriforceThief::HostConfirmMap(idx);
                }
            }
        }
    }
};

std::shared_ptr<TriforceThiefMapSelectWindow> sTTWindow;

}  // namespace

namespace HarpoonTriforceThief {

void RegisterMapSelectWindow() {
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    if (gui == nullptr) return;
    static const char* kName = "TriforceThiefMapSelect";
    if (gui->GetGuiWindow(kName) != nullptr) return;
    sTTWindow = std::make_shared<TriforceThiefMapSelectWindow>(
        "gOpenWindows.TriforceThiefMapSelect", kName);
    gui->AddGuiWindow(sTTWindow);
    sTTWindow->Show();
    SPDLOG_INFO("[Harpoon][TriforceThief] map select window registered");
}

}  // namespace HarpoonTriforceThief
