// Prop Hunt map select screen — visual port of Scooter's PropHuntMapSelectWindow.
// Renders a fullscreen ImGui overlay with thumbnail grid, preview, golden
// banner, and player cursors. Triggered by `gameState == HARPOON_STATE_MAP_SELECT`.
//
// Assets live in soh/assets/custom/map_select/ (baked into soh.o2r at build).
// Reused from Scooter verbatim.

#include "PropHunt.h"
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

struct MapDef {
    const char* name;
    const char* description;
    s32 entranceIndex;
    const char* thumbnailPath;
    const char* iconPath;
};

constexpr MapDef kMaps[] = {
    { "Kakariko Village",   "Village streets, rooftops, and the graveyard.",
      0x0DB, "map_select/thumbnail_kakariko_village.png", "map_select/sheikah_icon.png" },
    { "Death Mountain",     "Volcanic trail with boulders and narrow paths.",
      0x13D, "map_select/thumbnail_death_mountain.png", "map_select/goron_icon.png" },
    { "Bottom of the Well", "Dark dungeon beneath Kakariko Village.",
      0x0098, "map_select/thumbnail_bottom_of_the_well.png", "map_select/sheikah_icon.png" },
    { "Gerudo Fortress",    "Desert compound with courtyards and corridors.",
      0x129, "map_select/thumbnail_gerudo_fortress.png", "map_select/gerudo_icon.png" },
    { "Forest Temple",      "Twisted corridors in the Sacred Forest Meadow.",
      0x169, "map_select/thumbnail_forest_temple.png", "map_select/kokori_icon.png" },
    { "Zora's River",       "Winding river through Zora's Domain.",
      0x0EA, "map_select/thumbnail_zora_river.png", "map_select/zora_icon.png" },
    { "Dodongo's Cavern",   "Dark dungeon corridors. Cramped and deadly.",
      0x004, "map_select/thumbnail_dodongo_cavern.png", "map_select/goron_icon.png" },
    { "Ganon's Castle",     "Final dungeon. Lava and shadow.",
      0x467, "map_select/thumbnail_ganon_castle.png", "map_select/hyrule_icon.png" },
    { "Kokiri Forest",      "Village, Lost Woods, and Sacred Forest Meadow.",
      0x0EE, "map_select/thumbnail_kokiri_forest.png", "map_select/kokori_icon.png" },
    { "RANDOM",             "Pick a random map!",
      -1, nullptr, nullptr },
};
constexpr s32 kMapCount = (s32)(sizeof(kMaps) / sizeof(kMaps[0]));
constexpr int kGridCols = 5;
constexpr int kGridRows = 2;

bool sTexturesLoaded = false;
s32  sStickDebounce  = 0;
bool sHasVoted       = false;

void SafeLoadTexture(const char* name, const char* path) {
    auto archMgr = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager();
    if (!archMgr->HasFile(path)) return;
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    gui->LoadTextureFromRawImage(name, path);
}

void LoadTextures() {
    SafeLoadTexture("ph-bg",         "map_select/bg.png");
    SafeLoadTexture("ph-sign",       "map_select/select_sign.png");
    SafeLoadTexture("ph-navi",       "map_select/navi.png");
    SafeLoadTexture("ph-navi-white", "map_select/navi_white.png");
    for (int i = 0; i < kMapCount; i++) {
        if (kMaps[i].thumbnailPath) {
            SafeLoadTexture(kMaps[i].thumbnailPath, kMaps[i].thumbnailPath);
        }
        if (kMaps[i].iconPath) {
            SafeLoadTexture(kMaps[i].iconPath, kMaps[i].iconPath);
        }
    }
    sTexturesLoaded = true;
}

class PropHuntMapSelectWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override {}
    void DrawElement() override {}
    void UpdateElement() override {}

    void Draw() override {
        auto harpoon = Harpoon::Instance;
        if (!harpoon || !harpoon->isConnected) return;
        if (harpoon->gameState != HARPOON_STATE_MAP_SELECT) return;
        if (harpoon->currentRoomGameMode != "prop_hunt") return;

        // Every player sees the overlay so they can watch the host's
        // cursor in HOST_CHOOSES mode. The input handler below gates
        // confirmation / navigation on `isHost`, so non-hosts can only
        // spectate.
        bool isHost = (harpoon->ownClientId == harpoon->hostClientId);

        // Reset vote state on first frame in MAP_SELECT.
        static HarpoonGameState sPrevState = HARPOON_STATE_LOBBY;
        if (sPrevState != HARPOON_STATE_MAP_SELECT) sHasVoted = false;
        sPrevState = harpoon->gameState;

        if (!sTexturesLoaded) LoadTextures();

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

        ImGui::Begin("##PropHuntMapSelect", nullptr, flags);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        s32 localSel = harpoon->selectedMapIndex;
        if (localSel < 0 || localSel >= kMapCount) localSel = 0;

        // Background
        ImTextureID bgTex = gui->GetTextureByName("ph-bg");
        if (bgTex) dl->AddImage(bgTex, vp->Pos, ImVec2(vpX + vpW, vpY + vpH));
        else       dl->AddRectFilled(vp->Pos, ImVec2(vpX + vpW, vpY + vpH),
                                     IM_COL32(10, 10, 30, 245));

        // Layout
        float gridW = vpW * 0.96f, gridH = vpH * 0.54f;
        float gridStartX = vpX + vpW * 0.02f, gridStartY = vpY + vpH * 0.02f;
        float cellW = gridW / kGridCols, cellH = gridH / kGridRows;
        float pad = 4.0f;
        float bottomY = gridStartY + gridH + vpH * 0.02f;
        float prevW = vpW * 0.42f, prevH = (vpH - (bottomY - vpY)) - vpH * 0.02f;
        float prevX = vpX + vpW * 0.02f, prevY = bottomY;
        float rcCenterX = vpX + vpW * 0.72f;

        // Thumbnail grid 5x2
        for (int i = 0; i < kMapCount; i++) {
            int col = i % kGridCols;
            int row = i / kGridCols;
            float x = gridStartX + col * cellW;
            float y = gridStartY + row * cellH;
            ImVec2 tl(x + pad, y + pad);
            ImVec2 br(x + cellW - pad, y + cellH - pad);

            if (kMaps[i].thumbnailPath) {
                ImTextureID thumb = gui->GetTextureByName(kMaps[i].thumbnailPath);
                if (thumb) dl->AddImage(thumb, tl, br);
                else       dl->AddRectFilled(tl, br, IM_COL32(40, 40, 60, 200));
                if (i != localSel) dl->AddRectFilled(tl, br, IM_COL32(0, 0, 0, 120));

                ImGui::SetWindowFontScale(0.85f);
                const char* name = kMaps[i].name;
                ImVec2 ns = ImGui::CalcTextSize(name);
                float nx = x + (cellW - ns.x) * 0.5f, ny = br.y - ns.y - 3.0f;
                dl->AddRectFilled(ImVec2(tl.x, ny - 2), ImVec2(br.x, br.y),
                                  IM_COL32(0, 0, 0, 170));
                dl->AddText(ImVec2(nx + 1, ny + 1), IM_COL32(0, 0, 0, 255), name);
                dl->AddText(ImVec2(nx, ny), IM_COL32(255, 255, 255, 255), name);
                ImGui::SetWindowFontScale(1.0f);
            } else {
                // RANDOM cell
                dl->AddRectFilled(tl, br, IM_COL32(35, 35, 50, 220));
                if (i != localSel) dl->AddRectFilled(tl, br, IM_COL32(0, 0, 0, 60));
                ImGui::SetWindowFontScale(1.2f);
                const char* rnd = "RANDOM";
                ImVec2 rs = ImGui::CalcTextSize(rnd);
                dl->AddText(ImVec2(x + (cellW - rs.x) * 0.5f + 1, tl.y + 5 + 1),
                            IM_COL32(0, 0, 0, 200), rnd);
                dl->AddText(ImVec2(x + (cellW - rs.x) * 0.5f, tl.y + 5),
                            IM_COL32(255, 215, 0, 255), rnd);
                ImGui::SetWindowFontScale(2.8f);
                const char* q = "?";
                ImVec2 qs = ImGui::CalcTextSize(q);
                dl->AddText(ImVec2(x + (cellW - qs.x) * 0.5f + 1,
                                    y + (cellH - qs.y) * 0.55f + 1),
                            IM_COL32(0, 0, 0, 150), q);
                dl->AddText(ImVec2(x + (cellW - qs.x) * 0.5f,
                                    y + (cellH - qs.y) * 0.55f),
                            IM_COL32(255, 255, 255, 180), q);
                ImGui::SetWindowFontScale(1.0f);
            }

            if (i == localSel) {
                dl->AddRect(tl, br, IM_COL32(255, 30, 30, 255), 0, 0, 3.5f);
                dl->AddRect(ImVec2(tl.x - 2, tl.y - 2), ImVec2(br.x + 2, br.y + 2),
                            IM_COL32(255, 50, 50, 120), 0, 0, 2.0f);
            }
        }

        // Preview (bottom-left)
        ImVec2 pTL(prevX, prevY), pBR(prevX + prevW, prevY + prevH);
        if (localSel < kMapCount && kMaps[localSel].thumbnailPath) {
            ImTextureID prev = gui->GetTextureByName(kMaps[localSel].thumbnailPath);
            if (prev) dl->AddImage(prev, pTL, pBR);
            dl->AddRect(pTL, pBR, IM_COL32(80, 80, 100, 200), 0, 0, 2.0f);
        } else if (localSel == kMapCount - 1) {
            dl->AddRectFilled(pTL, pBR, IM_COL32(35, 35, 50, 200));
            dl->AddRect(pTL, pBR, IM_COL32(80, 80, 100, 200), 0, 0, 2.0f);
            ImGui::SetWindowFontScale(5.0f);
            const char* bq = "?";
            ImVec2 bqs = ImGui::CalcTextSize(bq);
            dl->AddText(ImVec2(prevX + (prevW - bqs.x) * 0.5f,
                                prevY + (prevH - bqs.y) * 0.5f),
                        IM_COL32(255, 215, 0, 200), bq);
            ImGui::SetWindowFontScale(1.0f);
        }

        // Right column: title -> medallion -> banner
        ImGui::SetWindowFontScale(1.5f);
        const char* sel = "Select Map";
        ImVec2 ss = ImGui::CalcTextSize(sel);
        float stX = rcCenterX - ss.x * 0.5f, stY = bottomY;
        dl->AddText(ImVec2(stX + 1, stY + 1), IM_COL32(0, 0, 0, 200), sel);
        dl->AddText(ImVec2(stX, stY), IM_COL32(255, 230, 160, 255), sel);
        ImGui::SetWindowFontScale(1.0f);
        float afterTitleY = stY + ss.y + 4.0f;

        // Medallion + icon
        float signSize = vpW * 0.14f;
        float signX = rcCenterX - signSize * 0.5f, signY = afterTitleY;
        ImTextureID signTex = gui->GetTextureByName("ph-sign");
        if (signTex) {
            dl->AddImage(signTex, ImVec2(signX, signY),
                          ImVec2(signX + signSize, signY + signSize));
        }
        if (localSel < kMapCount && kMaps[localSel].iconPath) {
            ImTextureID iconTex = gui->GetTextureByName(kMaps[localSel].iconPath);
            if (iconTex) {
                float iSz = signSize * 0.50f;
                float iX = signX + (signSize - iSz) * 0.5f;
                float iY = signY + (signSize - iSz) * 0.5f;
                dl->AddImage(iconTex, ImVec2(iX, iY), ImVec2(iX + iSz, iY + iSz));
            }
        }
        float afterMedY = signY + signSize + 6.0f;

        // Golden banner with map name
        if (localSel < kMapCount) {
            ImGui::SetWindowFontScale(1.4f);
            const char* sn = kMaps[localSel].name;
            ImVec2 sns = ImGui::CalcTextSize(sn);
            float bpX = 35.0f, bpY = 8.0f;
            float bW = sns.x + bpX * 2, bH = sns.y + bpY * 2;
            float bX = rcCenterX - bW * 0.5f, bY = afterMedY;
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

            // Host instruction
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
        ImTextureID naviTex      = gui->GetTextureByName("ph-navi");
        ImTextureID naviWhiteTex = gui->GetTextureByName("ph-navi-white");
        float naviSize = cellH * 0.30f;
        for (auto& [cid, c] : harpoon->clients) {
            if (!c.online) continue;
            if (harpoon->mapSelectMode == MAP_SELECT_HOST_CHOOSES &&
                cid != harpoon->hostClientId) continue;
            s32 idx = c.self ? localSel : c.mapSelectIndex;
            if (idx < 0 || idx >= kMapCount) idx = 0;
            int col = idx % kGridCols, row = idx / kGridCols;
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

        // Post-vote dim overlay
        if (sHasVoted && harpoon->mapSelectMode == MAP_SELECT_EVERYONE_CHOOSES) {
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

        // Stick navigation + A to confirm — only when this client has authority
        // over the cursor (host in host_chooses, anyone in everyone_votes).
        bool canInteract = (harpoon->mapSelectMode != MAP_SELECT_HOST_CHOOSES) || isHost;
        if (gPlayState != nullptr && !sHasVoted && canInteract) {
            Input* input = &gPlayState->state.input[0];
            s8 sx = input->cur.stick_x;
            s8 sy = input->cur.stick_y;
            if (sStickDebounce > 0) sStickDebounce--;
            if (sStickDebounce == 0 && (abs(sx) > 30 || abs(sy) > 30)) {
                int col = localSel % kGridCols;
                int row = localSel / kGridCols;
                if (sx >  30) col = (col + 1) % kGridCols;
                if (sx < -30) col = (col + kGridCols - 1) % kGridCols;
                if (sy >  30) row = (row + kGridRows - 1) % kGridRows;
                if (sy < -30) row = (row + 1) % kGridRows;
                s32 newSel = row * kGridCols + col;
                if (newSel >= kMapCount) newSel = kMapCount - 1;
                if (newSel != harpoon->selectedMapIndex) {
                    harpoon->selectedMapIndex = newSel;
                    // Broadcast cursor position so peers see our navi move.
                    nlohmann::json env;
                    env["type"]       = "ROOM.BROADCAST_EVENT";
                    env["event_name"] = "PROP_HUNT.MAP_CURSOR";
                    env["data"]       = { {"mapIndex", newSel} };
                    harpoon->SendJsonToRemote(env);
                }
                sStickDebounce = 9;
            }
            // Confirm / vote intent. Trigger rules:
            //   - Host (any mode): A press alone.
            //   - Peer in EVERYONE_CHOOSES: A press alone (was A+START;
            //     user requested simple A so the vote feels responsive
            //     and doesn't require holding pause).
            //   - Peer in HOST_CHOOSES: ignored — only host confirms.
            bool everyoneMode = (harpoon->mapSelectMode == MAP_SELECT_EVERYONE_CHOOSES);
            bool aPress       = CHECK_BTN_ALL(input->press.button, BTN_A);
            bool trigger      = isHost ? aPress
                                       : (everyoneMode ? aPress : false);
            if (trigger) {
                s32 idx = harpoon->selectedMapIndex;
                if (everyoneMode) {
                    sHasVoted = true;
                    nlohmann::json env;
                    env["type"]       = "ROOM.BROADCAST_EVENT";
                    env["event_name"] = "PROP_HUNT.MAP_VOTE";
                    env["data"]       = { {"mapIndex", idx} };
                    harpoon->SendJsonToRemote(env);
                    auto myIt = harpoon->clients.find(harpoon->ownClientId);
                    if (myIt != harpoon->clients.end()) {
                        myIt->second.hasVoted = true;
                        myIt->second.mapSelectIndex = idx;
                    }
                } else if (isHost) {
                    // HOST_CHOOSES → full round start.
                    HarpoonPropHunt::HostStartRound(idx);
                }
            }
        }
    }
};

std::shared_ptr<PropHuntMapSelectWindow> sWindow;

}  // namespace

namespace HarpoonPropHunt {

void RegisterMapSelectWindow() {
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    if (gui == nullptr) return;
    static const char* kName = "PropHuntMapSelect";
    if (gui->GetGuiWindow(kName) != nullptr) return;
    sWindow = std::make_shared<PropHuntMapSelectWindow>(
        "gOpenWindows.PropHuntMapSelect", kName);
    gui->AddGuiWindow(sWindow);
    sWindow->Show();
    SPDLOG_INFO("[Harpoon][PropHunt] map select window registered");
}

}  // namespace HarpoonPropHunt
