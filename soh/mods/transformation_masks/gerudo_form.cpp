/**
 * gerudo_form.cpp — Gerudo Form (OOT Gerudo Mask, Garo-style hybrid).
 *
 * See gerudo_form.h for the design overview.
 *
 * What this file owns:
 *   - Mask-edge-detect: polls player->currentMask each frame; on a rising
 *     edge (mask just equipped AND cheat on), calls O2rLoader_ForceModel
 *     ("gerudo"). On a falling edge, clears the forced model.
 *   - GameInteractor VB hooks: VB_GERUDOS_BE_FRIENDLY → true while active,
 *     VB_GIVE_ITEM_GERUDO_MEMBERSHIP_CARD → false (no card autograntee).
 *   - Sandstorm-OFF enforcement in Haunted Wasteland (per-frame + on
 *     transition end).
 *   - GerudoForm_GetTunicColor helper used by the hybrid render to recolor
 *     the gerudo outfit with Link's current tunic.
 *
 * The skeleton swap, null-body PostLimbDraw, and gerudo body draw are
 * handled by:
 *   - O2rLoader_SwapSkeleton / O2rLoader_RestoreSkeleton  (o2r_loader.cpp)
 *   - GerudoForm_DrawNullBody                              (gerudo_post_limb.cpp)
 *   - GerudoHybrid_Update / GerudoHybrid_Draw              (gerudo_hybrid_render.cpp)
 *
 * Replaces the older skin-pack approach (alt/-pathed Link skeleton + idle
 * pose override on a sFormSkelAnime). That approach is now retired.
 */

#include "gerudo_form.h"
#include "mods/o2r_loader/o2r_loader.h"

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

#include <libultraship/bridge.h>
#include <spdlog/spdlog.h>
#include <cstring>

extern "C" {
#include "macros.h" // GET_PLAYER
}

extern "C" PlayState* gPlayState;
extern "C" Color_RGB8 sTunicColors[];

#define CVAR_GERUDO_TRANSFORM "gMods.GerudoMaskTransform"

namespace {

bool IsWearingGerudoMask() {
    if (gPlayState == nullptr) {
        return false;
    }
    Player* player = GET_PLAYER(gPlayState);
    return player != nullptr && player->currentMask == PLAYER_MASK_GERUDO;
}

// Edge-detection state for the mask toggle. Rising edge → ForceModel,
// falling edge → ClearForcedModel. Polled in OnPlayerUpdate.
bool sPrevWantGerudo = false;

void GerudoForm_OnPlayerUpdate() {
    if (gPlayState == nullptr) {
        return;
    }

    bool cheatOn = CVarGetInteger(CVAR_GERUDO_TRANSFORM, 0) != 0;
    bool want = cheatOn && IsWearingGerudoMask();

    if (want != sPrevWantGerudo) {
        if (want) {
            O2rLoader_ForceModel("gerudo");
            SPDLOG_INFO("[GerudoForm] mask equipped — ForceModel(\"gerudo\")");
        } else {
            // Only clear if WE forced "gerudo" — leave alone if user has a
            // different model (e.g., garo from another mask).
            const char* cur = O2rLoader_GetForcedName();
            if (cur != nullptr && std::strcmp(cur, "gerudo") == 0) {
                O2rLoader_ClearForcedModel();
                SPDLOG_INFO("[GerudoForm] mask removed — ClearForcedModel");
            }
        }
        sPrevWantGerudo = want;
    }

    // Sandstorm OFF in Haunted Wasteland (per-frame, so toggling the mask
    // mid-scene clears the sandstorm without re-entering the area).
    if (GerudoForm_IsActive() && gPlayState->sceneNum == SCENE_HAUNTED_WASTELAND) {
        gPlayState->envCtx.sandstormState = SANDSTORM_OFF;
    }
}

void GerudoForm_OnTransitionEnd(int16_t sceneNum) {
    if (sceneNum == SCENE_HAUNTED_WASTELAND && GerudoForm_IsActive() && gPlayState != nullptr) {
        gPlayState->envCtx.sandstormState = SANDSTORM_OFF;
    }
}

} // namespace

extern "C" u8 GerudoForm_IsActive(void) {
    if (!CVarGetInteger(CVAR_GERUDO_TRANSFORM, 0)) {
        return 0;
    }
    const char* cur = O2rLoader_GetForcedName();
    return (cur != nullptr && std::strcmp(cur, "gerudo") == 0) ? 1 : 0;
}

// No-op now — the previous architecture used GerudoHybrid_Update/Draw to
// render the gerudo body through a separate skel. Current pipeline uses a
// Link-rigged gerudo skin so Player_DrawImpl handles everything. Kept so
// z_player.c's older callsite (if any survives) still links cleanly.
extern "C" s32 GerudoForm_TryDrawSmoothSkin(PlayState* play, Player* player) {
    (void)play;
    (void)player;
    return 0;
}

extern "C" void GerudoForm_GetTunicColor(s32 tunic, Color_RGB8* out) {
    if (out == nullptr) {
        return;
    }
    if (tunic < PLAYER_TUNIC_KOKIRI || tunic > PLAYER_TUNIC_ZORA) {
        tunic = PLAYER_TUNIC_KOKIRI;
    }
    Color_RGB8 c = sTunicColors[tunic];

    if (tunic == PLAYER_TUNIC_KOKIRI && CVarGetInteger(CVAR_COSMETIC("Link.KokiriTunic.Changed"), 0)) {
        c = CVarGetColor24(CVAR_COSMETIC("Link.KokiriTunic.Value"), sTunicColors[PLAYER_TUNIC_KOKIRI]);
    } else if (tunic == PLAYER_TUNIC_GORON && CVarGetInteger(CVAR_COSMETIC("Link.GoronTunic.Changed"), 0)) {
        c = CVarGetColor24(CVAR_COSMETIC("Link.GoronTunic.Value"), sTunicColors[PLAYER_TUNIC_GORON]);
    } else if (tunic == PLAYER_TUNIC_ZORA && CVarGetInteger(CVAR_COSMETIC("Link.ZoraTunic.Changed"), 0)) {
        c = CVarGetColor24(CVAR_COSMETIC("Link.ZoraTunic.Value"), sTunicColors[PLAYER_TUNIC_ZORA]);
    }
    *out = c;
}

extern "C" void GerudoForm_Init(void) {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    // Gerudo NPCs treat the player as a friendly Gerudo while the mask is worn.
    REGISTER_VB_SHOULD(VB_GERUDOS_BE_FRIENDLY, {
        if (GerudoForm_IsActive()) {
            *should = true;
        }
    });

    // Skip the forced card-giving path while the mask is worn — access is
    // temporary, no QUEST_GERUDO_CARD is granted.
    REGISTER_VB_SHOULD(VB_GIVE_ITEM_GERUDO_MEMBERSHIP_CARD, {
        if (GerudoForm_IsActive()) {
            *should = false;
        }
    });

    COND_HOOK(OnTransitionEnd, true, GerudoForm_OnTransitionEnd);
    COND_HOOK(OnPlayerUpdate, true, GerudoForm_OnPlayerUpdate);
}

// ShipInit-registered entry point. Hooks check the cheat CVar at call time,
// so no need to register/unregister on CVar changes.
static void GerudoForm_RegisterShipInit() {
    GerudoForm_Init();
}
static RegisterShipInitFunc sGerudoFormInitFunc(GerudoForm_RegisterShipInit, {});
