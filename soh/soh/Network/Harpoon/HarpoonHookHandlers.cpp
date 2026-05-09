#include "Harpoon.h"
#include <libultraship/libultraship.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/cosmetics/cosmeticsTypes.h"
#include "soh/frame_interpolation.h"
#include "soh/OTRGlobals.h"
#include "soh/Network/Anchor/Anchor.h"

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "objects/gameplay_keep/gameplay_keep.h"
extern PlayState* gPlayState;
extern MapData* gMapData;
float OTRGetDimensionFromLeftEdge(float v);
float OTRGetDimensionFromRightEdge(float v);
}

void Harpoon::RegisterHooks() {

    // Spawn dummy players when entering a new scene. Push a fresh
    // PLAYER.UPDATE_VISUAL_STATE so the server's AOI table knows our new
    // sceneNum — the per-frame TRANSFORM packets are filtered by
    // `same_scene_as=session` server-side, and without an updated VisualState
    // the server still thinks we're in the previous scene (or, on first
    // connect from title, scene 255).
    COND_HOOK(OnSceneSpawnActors, isConnected, [&]() {
        if (IsSaveLoaded()) {
            SendPacket_PlayerVisualState();
            RefreshClientActors();
        }
    });

    // Intercept ACTOR_PLAYER spawns to create Harpoon dummy players
    COND_ID_HOOK(ShouldActorInit, ACTOR_PLAYER, isConnected, [&](void* actorRef, bool* should) {
        Actor* actor = (Actor*)actorRef;

        if (spawningDummyPlayerForClientId != 0) {
            SetDummyPlayerClientId(actor, spawningDummyPlayerForClientId);

            Actor_ChangeCategory(gPlayState, &gPlayState->actorCtx, actor, ACTORCAT_NPC);
            actor->id = ACTOR_EN_OE2;
            actor->category = ACTORCAT_NPC;
            actor->init = HarpoonDummyPlayer_Init;
            actor->update = HarpoonDummyPlayer_Update;
            actor->draw = HarpoonDummyPlayer_Draw;
            actor->destroy = HarpoonDummyPlayer_Destroy;
        }
    });

    // Send player update every frame
    COND_HOOK(OnPlayerUpdate, isConnected, [&]() {
        if (justLoadedSave) {
            justLoadedSave = false;
            // PULL teammates' state into our save (we're a late joiner). The
            // server forwards this to any teammate that responds with their
            // UpdateTeamState. PUSH'ing on load would clobber the team's
            // progress with our (likely empty) save.
            SendPacket_RequestTeamState();
        }

        // Defensive: ensure server knows we're in-game. OnSceneSpawnActors
        // normally fires this on every scene transition, but if the chain
        // breaks (e.g. our save loaded mid-flight from a previous failed
        // hook), the server's session.scene_num stays at 255 and AOI rejects
        // every transform we send → teammates never see us.
        if (!visualStateSentSinceLoad && IsSaveLoaded()) {
            visualStateSentSinceLoad = true;
            SendPacket_PlayerVisualState();
        }

        if (shouldRefreshActors) {
            shouldRefreshActors = false;
            RefreshClientActors();
        }

        // Cutscene trigger detection: when cutsceneIndex transitions from 0
        // (or different) to a fresh value, broadcast it so other players in
        // the same scene can replay it. Skip during incoming-packet apply to
        // avoid loops.
        static s32 lastCutsceneIndex = 0;
        s32 csIdx = gSaveContext.cutsceneIndex;
        if (csIdx != lastCutsceneIndex) {
            lastCutsceneIndex = csIdx;
            if (csIdx != 0 && !isProcessingIncomingPacket && gPlayState != nullptr) {
                SendPacket_CutsceneTrigger(csIdx, gPlayState->sceneNum);
            }
        }

        SendPacket_PlayerUpdate();
    });

    // Process incoming packets on game thread
    COND_HOOK(OnGameFrameUpdate, isConnected, [&]() {
        ProcessIncomingPacketQueue();
        UpdateDecoys();
    });

    // Send SFX to other players
    COND_HOOK(OnPlayerSfx, isConnected, [&](u16 sfxId) { SendPacket_PlayerSfx(sfxId); });

    // Ocarina notes — only forwarded to teammates in the same scene.
    COND_HOOK(OnOcarinaNote, isConnected,
              [&](uint8_t note, float modulator, int8_t bend) { SendPacket_OcarinaSfx(note, modulator, bend); });

    // Load game → request team state (from Anchor)
    COND_HOOK(OnLoadGame, isConnected, [&](s16 fileNum) {
        justLoadedSave = true;
        // Force a fresh VisualState on next OnPlayerUpdate so the server
        // updates our session.scene_num / is_save_loaded promptly.
        visualStateSentSinceLoad = false;
    });

    // Sync full save state on save
    COND_HOOK(OnSaveFile, isConnected, [&](s16 fileNum, int sectionID) {
        if (sectionID == 0) {
            SendPacket_UpdateTeamState();
        }
    });

    // Sync items on receive (from Anchor — handles dungeon items separately)
    COND_HOOK(OnItemReceive, isConnected, [&](GetItemEntry itemEntry) {
        if (itemEntry.modIndex == MOD_NONE &&
            (itemEntry.itemId >= ITEM_KEY_BOSS && itemEntry.itemId <= ITEM_KEY_SMALL)) {
            SendPacket_UpdateDungeonItems();
            return;
        }

        SendPacket_GiveItem(itemEntry.tableId, itemEntry.getItemId);
    });

    // Sync dungeon key usage (from Anchor)
    COND_HOOK(OnDungeonKeyUsed, isConnected, [&](uint16_t mapIndex) { SendPacket_UpdateDungeonItems(); });

    // Flag sync hooks (from Anchor)
    COND_HOOK(OnFlagSet, isConnected,
              [&](s16 flagType, s16 flag) { SendPacket_SetFlag(SCENE_ID_MAX, flagType, flag); });

    COND_HOOK(OnFlagUnset, isConnected,
              [&](s16 flagType, s16 flag) { SendPacket_UnsetFlag(SCENE_ID_MAX, flagType, flag); });

    COND_HOOK(OnSceneFlagSet, isConnected,
              [&](s16 sceneNum, s16 flagType, s16 flag) { SendPacket_SetFlag(sceneNum, flagType, flag); });

    COND_HOOK(OnSceneFlagUnset, isConnected,
              [&](s16 sceneNum, s16 flagType, s16 flag) { SendPacket_UnsetFlag(sceneNum, flagType, flag); });

    // Rando check status sync (from Anchor)
    COND_HOOK(OnRandoSetCheckStatus, isConnected, [&](RandomizerCheck rc, RandomizerCheckStatus status) {
        if (!isHandlingUpdateTeamState) {
            SendPacket_SetCheckStatus(rc);
        }
    });

    COND_HOOK(OnRandoSetIsSkipped, isConnected, [&](RandomizerCheck rc, bool isSkipped) {
        if (!isHandlingUpdateTeamState) {
            SendPacket_SetCheckStatus(rc);
        }
    });

    // Entrance discovery sync (from Anchor)
    COND_HOOK(OnRandoEntranceDiscovered, isConnected,
              [&](u16 entranceIndex, u8 isReversedEntrance) { SendPacket_EntranceDiscovered(entranceIndex); });

    // Boss defeat → game complete (from Anchor). Only fires for the final
    // Ganon (ACTOR_BOSS_GANON2 = Ganondorf phase 2).
    COND_ID_HOOK(OnBossDefeat, ACTOR_BOSS_GANON2, isConnected,
                 [&](void* refActor) { SendPacket_GameComplete(); });

    // Apply tunic color from Harpoon client data
    COND_VB_SHOULD(VB_APPLY_TUNIC_COLOR, isConnected, {
        Actor* myPlayer = (Actor*)GET_PLAYER(gPlayState);
        Actor* actor = va_arg(args, Actor*);
        Color_RGB8* color = va_arg(args, Color_RGB8*);

        if (actor == myPlayer) {
            Color_RGBA8 ownColor = CVarGetColor(CVAR_HARPOON("Color.Value"), { 100, 255, 100 });
            color->r = ownColor.r;
            color->g = ownColor.g;
            color->b = ownColor.b;
            return;
        }

        uint32_t clientId = Harpoon::Instance->GetDummyPlayerClientId(actor);

        if (!Harpoon::Instance->clients.contains(clientId)) {
            return;
        }

        HarpoonClient& client = Harpoon::Instance->clients[clientId];
        color->r = client.color.r;
        color->g = client.color.g;
        color->b = client.color.b;
    });

    // Compass arrows for connected players on the minimap (mirrors Anchor's
    // OnMinimapDrawCompassIcons handler at HookHandlers.cpp:413). Iterates
    // every Harpoon client present in the same scene and draws the vanilla
    // gCompassArrowDL for each, tinted with the client's color. Generic over
    // any skin — we only read pos/rot/color from the client struct, never
    // touch the actor's draw path. Default-on; toggle via CVar.
    struct HarpoonCompassIcon {
        Vec3f pos;
        Vec3s rot;
        float scale;
        Color_RGB8 color;
    };
    COND_HOOK(OnMinimapDrawCompassIcons, isConnected, [&]() {
        if (!CVarGetInteger(CVAR_HARPOON("ShowOtherPlayersOnMinimap"), 1)) {
            return;
        }
        std::vector<HarpoonCompassIcon> icons;
        bool isInDungeon = gPlayState->sceneNum == SCENE_DEKU_TREE ||
                           gPlayState->sceneNum == SCENE_DODONGOS_CAVERN ||
                           gPlayState->sceneNum == SCENE_JABU_JABU ||
                           gPlayState->sceneNum == SCENE_FOREST_TEMPLE ||
                           gPlayState->sceneNum == SCENE_FIRE_TEMPLE ||
                           gPlayState->sceneNum == SCENE_WATER_TEMPLE ||
                           gPlayState->sceneNum == SCENE_SPIRIT_TEMPLE ||
                           gPlayState->sceneNum == SCENE_SHADOW_TEMPLE ||
                           gPlayState->sceneNum == SCENE_BOTTOM_OF_THE_WELL ||
                           gPlayState->sceneNum == SCENE_ICE_CAVERN;
        for (auto& [clientId, client] : Harpoon::Instance->clients) {
            if (client.self || !client.online) continue;
            if (client.sceneNum != gPlayState->sceneNum) continue;
            // Read pos/rot from the broadcast state (`posRot`) instead of
            // dereferencing `client.player`. The dummy actor pointer can be
            // stale across scene transitions / RefreshClientActors cycles
            // and the broadcast state is what the dummy is updated FROM
            // every frame anyway, so it's the same data + safer.
            icons.push_back(HarpoonCompassIcon{
                client.posRot.pos,
                client.posRot.rot,
                0.3f,
                client.color,
            });
        }
        // Local player drawn last so it sits on top of the others.
        Player* localPlayer = GET_PLAYER(gPlayState);
        if (localPlayer != nullptr) {
            Color_RGBA8 ownColor = CVarGetColor(CVAR_HARPOON("Color.Value"), { 100, 255, 100 });
            icons.push_back(HarpoonCompassIcon{
                localPlayer->actor.world.pos,
                localPlayer->actor.shape.rot,
                0.4f,
                { ownColor.r, ownColor.g, ownColor.b },
            });
        }

        // Adapted from Minimap_DrawCompassIcons / Anchor's mirror of it.
        s16 leftMinimapMargin   = CVarGetInteger(CVAR_COSMETIC("HUD.Margin.L"), 0);
        s16 rightMinimapMargin  = CVarGetInteger(CVAR_COSMETIC("HUD.Margin.R"), 0);
        s16 bottomMinimapMargin = CVarGetInteger(CVAR_COSMETIC("HUD.Margin.B"), 0);
        s16 xMarginsMinimap = 0;
        s16 yMarginsMinimap = 0;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.UseMargins"), 0) != 0) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosType"), 0) == ORIGINAL_LOCATION) {
                xMarginsMinimap = rightMinimapMargin;
            }
            yMarginsMinimap = bottomMinimapMargin;
        }
        s16 mapWidth     = isInDungeon ? R_DGN_MINIMAP_X : R_OW_MINIMAP_X;
        s16 mapStartPosX = isInDungeon ? 96 : gMapData->owMinimapWidth[R_MAP_INDEX];

        OPEN_DISPS(gPlayState->state.gfxCtx);
        Gfx_SetupDL_42Overlay(gPlayState->state.gfxCtx);
        for (auto& icon : icons) {
            gSPMatrix(OVERLAY_DISP++, &gMtxClear, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                              PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
            gDPSetEnvColor(OVERLAY_DISP++, 0, 0, 0, 255);
            gDPSetCombineMode(OVERLAY_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);

            s16 mirrorOffset =
                ((mapWidth / 2) - ((R_COMPASS_OFFSET_X / 10) - (mapStartPosX - SCREEN_WIDTH / 2))) * 2 * 10;
            s16 tempX = (s16)icon.pos.x;
            s16 tempZ = (s16)icon.pos.z;
            tempX /= R_COMPASS_SCALE_X * (CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0) ? -1 : 1);
            tempZ /= R_COMPASS_SCALE_Y;
            s16 tempXOffset =
                R_COMPASS_OFFSET_X + (CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0) ? mirrorOffset : 0);
            if (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosType"), 0) != ORIGINAL_LOCATION) {
                if (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosType"), 0) == ANCHOR_LEFT) {
                    if (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.UseMargins"), 0) != 0) {
                        xMarginsMinimap = leftMinimapMargin;
                    }
                    Matrix_Translate(
                        OTRGetDimensionFromLeftEdge((tempXOffset + (xMarginsMinimap * 10) + tempX +
                                                     (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosX"), 0) * 10)) /
                                                    10.0f),
                        (R_COMPASS_OFFSET_Y + ((yMarginsMinimap * 10) * -1) - tempZ +
                         ((CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosY"), 0) * 10) * -1)) /
                            10.0f,
                        0.0f, MTXMODE_NEW);
                } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosType"), 0) == ANCHOR_RIGHT) {
                    if (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.UseMargins"), 0) != 0) {
                        xMarginsMinimap = rightMinimapMargin;
                    }
                    Matrix_Translate(
                        OTRGetDimensionFromRightEdge((tempXOffset + (xMarginsMinimap * 10) + tempX +
                                                      (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosX"), 0) * 10)) /
                                                     10.0f),
                        (R_COMPASS_OFFSET_Y + ((yMarginsMinimap * 10) * -1) - tempZ +
                         ((CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosY"), 0) * 10) * -1)) /
                            10.0f,
                        0.0f, MTXMODE_NEW);
                } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosType"), 0) == ANCHOR_NONE) {
                    Matrix_Translate(
                        (tempXOffset + tempX + (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosX"), 0) * 10) / 10.0f),
                        (R_COMPASS_OFFSET_Y + ((yMarginsMinimap * 10) * -1) - tempZ +
                         ((CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosY"), 0) * 10) * -1)) /
                            10.0f,
                        0.0f, MTXMODE_NEW);
                }
            } else {
                Matrix_Translate(OTRGetDimensionFromRightEdge((tempXOffset + (xMarginsMinimap * 10) + tempX) / 10.0f),
                                 (R_COMPASS_OFFSET_Y + ((yMarginsMinimap * 10) * -1) - tempZ) / 10.0f, 0.0f,
                                 MTXMODE_NEW);
            }
            Matrix_Scale(icon.scale, icon.scale, icon.scale, MTXMODE_APPLY);
            Matrix_RotateX(-1.6f, MTXMODE_APPLY);
            s16 rotation = ((0x7FFF - icon.rot.y) / 0x400) *
                           (CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0) ? -1 : 1);
            Matrix_RotateY(rotation / 10.0f, MTXMODE_APPLY);
            gSPMatrix(OVERLAY_DISP++, MATRIX_NEWMTX(gPlayState->state.gfxCtx),
                      G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

            gDPSetPrimColor(OVERLAY_DISP++, 0, 0xFF, icon.color.r, icon.color.g, icon.color.b, 255);
            gSPDisplayList(OVERLAY_DISP++, (Gfx*)gCompassArrowDL);
        }
        CLOSE_DISPS(gPlayState->state.gfxCtx);
    });
}
