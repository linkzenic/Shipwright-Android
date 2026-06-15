#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/ShipInit.hpp"
#include "expansions/sw97/sw97_config.h"

extern "C" {
#include "overlays/actors/ovl_Bg_Breakwall/z_bg_breakwall.h"
#include "overlays/actors/ovl_Bg_Ice_Shelter/z_bg_ice_shelter.h"
#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h"
#include "mods/items/custom_items.h"

extern PlayState* gPlayState;
}

static void UpdateBlueFireCollidersBgBreakwall(void* actorPtr) {
    BgBreakwall* thisx = (BgBreakwall*)actorPtr;
    thisx->collider.info.bumper.dmgFlags |= DMG_ARROW_ICE;
}

static void UpdateBlueFireCollidersBgIceShelter(void* actorPtr) {
    BgIceShelter* thisx = (BgIceShelter*)actorPtr;
    thisx->cylinder1.base.acFlags |= AC_TYPE_PLAYER;
    thisx->cylinder1.info.bumper.dmgFlags |= DMG_ARROW_ICE;
    thisx->cylinder2.base.acFlags |= AC_TYPE_PLAYER;
    thisx->cylinder2.info.bumper.dmgFlags |= DMG_ARROW_ICE;
}

static bool CheckAC(Actor* ac) {
    if (ac == NULL || ac->id != ACTOR_EN_ARROW) return false;
    s16 p = (s16)ac->params;
    return p == ARROW_ICE || p == ARROW_SW97_ICE || p == ARROW_SEED_ICE;
}

static bool IsSw97IceArrow(Actor* ac) {
    if (ac == NULL || ac->id != ACTOR_EN_ARROW) return false;
    s16 p = (s16)ac->params;
    return p == ARROW_SW97_ICE || p == ARROW_SEED_ICE;
}

// The Gust Jar's BLOW collider is owned by the Player (ac->id == ACTOR_PLAYER),
// not by an EnArrow — so CheckAC/IsSw97IceArrow alone would reject it even
// though the dmgFlag is DMG_ARROW_ICE. Treat an Ice-element BLOW as an SW97
// ice arrow for the red-ice melt check so the Water medallion gustjar also
// melts BgIceShelter, the same way the SW97 bow / slingshot ice already do.
static bool IsGustJarIceBlow(Actor* ac) {
    if (ac == NULL || ac->id != ACTOR_PLAYER) return false;
    return gCustomItemState.gustJarEquipped &&
           gCustomItemState.gustJarMode == 3 /* GUST_MODE_BLOW */ &&
           gCustomItemState.gustJarElement == 2 /* GUST_ELEMENT_ICE */;
}

void RegisterBlueFireArrowsHooks() {
    bool cheatOn =
        CVarGetInteger(CVAR_ENHANCEMENT("BlueFireArrows"), 0) || (IS_RANDO && RAND_GET_OPTION(RSK_BLUE_FIRE_ARROWS));
    bool shouldRegister = cheatOn || SW97_MEDALLIONS_ENABLED();

    COND_ID_HOOK(OnActorInit, ACTOR_BG_BREAKWALL, shouldRegister, UpdateBlueFireCollidersBgBreakwall);
    COND_ID_HOOK(OnActorInit, ACTOR_BG_ICE_SHELTER, shouldRegister, UpdateBlueFireCollidersBgIceShelter);

    // fix bug where cylinder2 never checks acFlags
    COND_VB_SHOULD(VB_BG_ICE_SHELTER_HIT, shouldRegister, {
        BgIceShelter* thisx = va_arg(args, BgIceShelter*);

        if (thisx->cylinder2.base.acFlags & AC_HIT) {
            thisx->cylinder2.base.acFlags &= ~AC_HIT;
            *should = true;
        }
    });

    COND_VB_SHOULD(VB_BG_ICE_SHELTER_MELT, shouldRegister, {
        BgIceShelter* thisx = va_arg(args, BgIceShelter*);
        bool meltCheatOn =
            CVarGetInteger(CVAR_ENHANCEMENT("BlueFireArrows"), 0) || (IS_RANDO && RAND_GET_OPTION(RSK_BLUE_FIRE_ARROWS));

        Actor* ac1 = thisx->cylinder1.base.ac;
        Actor* ac2 = thisx->cylinder2.base.ac;

        if (IsSw97IceArrow(ac1) || IsSw97IceArrow(ac2) ||
            IsGustJarIceBlow(ac1) || IsGustJarIceBlow(ac2)) {
            // SW97 ice (bow or slingshot) and Water-medallion gustjar BLOW
            // always melt red ice regardless of cheat state.
            *should = true;
        } else if (meltCheatOn && (CheckAC(ac1) || CheckAC(ac2))) {
            // Vanilla ice only melts when the cheat is on
            *should = true;
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterBlueFireArrowsHooks,
                                     { "IS_RANDO", CVAR_ENHANCEMENT("BlueFireArrows"), SW97_MEDALLIONS_CVAR });
