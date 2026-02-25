#include "PvPAnchor.h"
#include <libultraship/libultraship.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Network/Anchor/Anchor.h"

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

void PvPAnchor::RegisterHooks() {

    // Spawn dummy players when entering a new scene
    COND_HOOK(OnSceneSpawnActors, isConnected, [&]() {
        if (IsSaveLoaded()) {
            // Notify server of our scene change
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

    // Intercept ACTOR_PLAYER spawns to create PvP dummy players
    COND_ID_HOOK(ShouldActorInit, ACTOR_PLAYER, isConnected, [&](void* actorRef, bool* should) {
        Actor* actor = (Actor*)actorRef;

        if (spawningDummyPlayerForClientId != 0) {
            SetDummyPlayerClientId(actor, spawningDummyPlayerForClientId);

            Actor_ChangeCategory(gPlayState, &gPlayState->actorCtx, actor, ACTORCAT_NPC);
            actor->id = ACTOR_EN_OE2;
            actor->category = ACTORCAT_NPC;
            actor->init = PvPDummyPlayer_Init;
            actor->update = PvPDummyPlayer_Update;
            actor->draw = PvPDummyPlayer_Draw;
            actor->destroy = PvPDummyPlayer_Destroy;
        }
    });

    // Send player update every frame
    COND_HOOK(OnPlayerUpdate, isConnected, [&]() {
        if (shouldRefreshActors) {
            shouldRefreshActors = false;
            RefreshClientActors();
        }

        SendPacket_PlayerUpdate();
    });

    // Process incoming packets on game thread
    COND_HOOK(OnGameFrameUpdate, isConnected, [&]() {
        ProcessIncomingPacketQueue();
    });

    // Send SFX to other players
    COND_HOOK(OnPlayerSfx, isConnected, [&](u16 sfxId) {
        SendPacket_PlayerSfx(sfxId);
    });

    // Apply tunic color from PvP client data
    COND_VB_SHOULD(VB_APPLY_TUNIC_COLOR, isConnected, {
        Actor* myPlayer = (Actor*)GET_PLAYER(gPlayState);
        Actor* actor = va_arg(args, Actor*);
        Color_RGB8* color = va_arg(args, Color_RGB8*);

        if (actor == myPlayer) {
            Color_RGBA8 ownColor = CVarGetColor(CVAR_PVP_ANCHOR("Color.Value"), { 100, 255, 100 });
            color->r = ownColor.r;
            color->g = ownColor.g;
            color->b = ownColor.b;
            return;
        }

        uint32_t clientId = PvPAnchor::Instance->GetDummyPlayerClientId(actor);

        if (!PvPAnchor::Instance->clients.contains(clientId)) {
            return;
        }

        PvPClient& client = PvPAnchor::Instance->clients[clientId];
        color->r = client.color.r;
        color->g = client.color.g;
        color->b = client.color.b;
    });
}
