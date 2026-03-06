#include "Harpoon.h"
#include <libultraship/libultraship.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Network/Anchor/Anchor.h"

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

void Harpoon::RegisterHooks() {

    // Spawn dummy players when entering a new scene
    COND_HOOK(OnSceneSpawnActors, isConnected, [&]() {
        if (IsSaveLoaded()) {
            nlohmann::json payload;
            payload["type"] = "PVP_UPDATE_CLIENT_STATE";
            payload["isSaveLoaded"] = true;
            payload["sceneNum"] = gPlayState->sceneNum;
            payload["entranceIndex"] = gSaveContext.entranceIndex;
            payload["linkAge"] = gSaveContext.linkAge;
            SendJsonToRemote(payload);

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
            SendPacket_UpdateTeamState();
        }

        if (shouldRefreshActors) {
            shouldRefreshActors = false;
            RefreshClientActors();
        }

        SendPacket_PlayerUpdate();
    });

    // Process incoming packets on game thread
    COND_HOOK(OnGameFrameUpdate, isConnected, [&]() {
        ProcessIncomingPacketQueue();
        UpdateDecoys();
    });

    // Send SFX to other players
    COND_HOOK(OnPlayerSfx, isConnected, [&](u16 sfxId) {
        SendPacket_PlayerSfx(sfxId);
    });

    // Load game → request team state (from Anchor)
    COND_HOOK(OnLoadGame, isConnected, [&](s16 fileNum) {
        justLoadedSave = true;
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
    COND_HOOK(OnDungeonKeyUsed, isConnected, [&](uint16_t mapIndex) {
        SendPacket_UpdateDungeonItems();
    });

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

    // Boss defeat → game complete (from Anchor)
    COND_ID_HOOK(OnBossDefeat, ACTOR_BOSS_GANON2, isConnected, [&](void* refActor) {
        // Could send a game complete packet if needed
    });

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
}
