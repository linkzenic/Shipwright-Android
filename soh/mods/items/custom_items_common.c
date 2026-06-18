/**
 * custom_items_common.c - Shared state and utilities for custom items
 *
 * Contains:
 *   - Global CustomItemState struct instance
 *   - Common utility functions used by multiple items
 *   - Frame update handlers for active items
 */

#include "custom_items.h"
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include <math.h>
#include "helpers/fx_helper.h"
#include "helpers/camera_helper.h"
#include "logic/item_postman_hat.h"
#include "../extended_inventory.h" // ExtInv_GetItemSlot — custom items must NOT use vanilla SLOT()/INV_CONTENT()
#include "overlays/actors/ovl_En_Boom/z_en_boom.h" // EnBoom struct for Gale Boomerang multi-target override

// Forward declarations for items included after this file in unity build
extern void Handle_Pending3(Player* p, PlayState* play);

// Global custom items state
CustomItemState gCustomItemState = { .timer1 = 0,
                                     .timer2 = 0,
                                     .globalCooldownTimer = 0,
                                     .rocsFeatherJumpActive = 0,
                                     .rocsJumpCount = 0,
                                     .dekuLeafActive = 0,
                                     .dekuLeafMode = 0,
                                     .dekuLeafGliding = 0,
                                     .dekuLeafBlowing = 0,
                                     .dekuLeafAnimTimer = 0,
                                     .dekuLeafBlowTimer = 0,
                                     .ballAndChainThrown = 0,
                                     .ballAndChainFirstPersonActive = 0,
                                     .spinnerActive = 0,
                                     .spinnerSpinAttackTimer = 0,
                                     .spinnerWallBumpTimer = 0,
                                     .gustJarEquipped = 0,
                                     .gustJarMode = 0,
                                     .gustJarElement = 0,
                                     .gustJarBlowActive = 0,
                                     .gustJarHeatTimer = 0,
                                     .gustJarBlowTimer = 0,
                                     .gustJarCooldownTimer = 0,
                                     .gustJarTimer = 0,
                                     .gustJarFirstPersonActive = 0,
                                     .gustJarAimMode = 0,
                                     .gustJarPrevCameraMode = 0,
                                     .gustJarButtonMask = 0,
                                     .gustJarPrevInvincibility = 0,
                                     .gustJarPotActor = NULL,
                                     .shovelActive = 0,
                                     .shovelAnimating = 0,
                                     .shovelAnimTimer = 0,
                                     .shovelHoleActor = NULL,
                                     .demiseDestructionActive = 0,
                                     .beetleActive = 0,
                                     .beetleState = 0,
                                     .beetleFirstPersonActive = 0,
                                     .beetlePos = { 0, 0, 0 },
                                     .beetleRot = { 0, 0, 0 },
                                     .beetleGrabbed = NULL,
                                     .beetleWingScale = 1.0f,
                                     .beetleWingDir = -1,
                                     .beetleTimer = 0,
                                     .beetleStartPos = { 0, 0, 0 },
                                     .beetleSubCamId = SUBCAM_FREE,
                                     .bombArrowActive = 0,
                                     .bombArrowState = 0,
                                     .bombArrowBombActor = NULL,
                                     .bombArrowArrowActor = NULL,
                                     .bombArrowFirstPersonActive = 0,
                                     .bombArrowButtonMask = 0,
                                     .fireRodActive = 0,
                                     .fireRodState = 0,
                                     .fireRodPrevSword = 0,
                                     .fireRodMatrixValid = 0,
                                     .fireRodProjActive = 0,
                                     .fireRodProjCount = 0,
                                     .fireRodFlameActive = 0,
                                     .fireRodFlameTimer = 0,
                                     .fireRodFirstPerson = 0,
                                     .fireRodButtonMask = 0,
                                     // Ice Rod
                                     .iceRodActive = 0,
                                     .iceRodState = 0,
                                     .iceRodPrevSword = 0,
                                     .iceRodMatrixValid = 0,
                                     .iceRodProjActive = 0,
                                     .iceRodProjCount = 0,
                                     .iceRodWaveActive = 0,
                                     .iceRodWaveTimer = 0,
                                     .iceRodFirstPerson = 0,
                                     .iceRodButtonMask = 0,
                                     // Light Rod
                                     .lightRodActive = 0,
                                     .lightRodState = 0,
                                     .lightRodPrevSword = 0,
                                     .lightRodMatrixValid = 0,
                                     .lightRodProjActive = 0,
                                     .lightRodProjCount = 0,
                                     .lightRodBeamActive = 0,
                                     .lightRodBeamTimer = 0,
                                     .lightRodFirstPerson = 0,
                                     .lightRodButtonMask = 0,
                                     // Dominion Rod
                                     .dominionRodActive = 0,
                                     .dominionRodState = 0,
                                     .dominionRodFirstPersonActive = 0,
                                     .dominionRodOrbPos = { 0, 0, 0 },
                                     .dominionRodOrbRot = { 0, 0, 0 },
                                     .dominionRodControlledActor = NULL,
                                     .dominionRodTimer = 0,
                                     .dominionRodStartPos = { 0, 0, 0 },
                                     .dominionRodLightNode = NULL,
                                     .dominionRodButtonMask = 0,
                                     .dominionRodControlType = 0,
                                     .dominionRodControlVel = { 0, 0, 0 },
                                     .dominionRodDamagePaused = 0,
                                     .dominionRodPrevInvincibility = 0,
                                     // Dominion Rod Actor-Specific
                                     .dominionRodActorHomePos = { 0, 0, 0 },
                                     .dominionRodFlameTimer = 0,
                                     .dominionRodAttackCooldown = 0,
                                     .dominionRodSpikeInvulnerable = 0,
                                     .dominionRodFlameActor = NULL,
                                     // Cane of Somaria
                                     .somariaActive = 0,
                                     .somariaBlocks = { NULL, NULL, NULL },
                                     .somariaBlockCount = 0,
                                     .somariaOldestSlot = 0,
                                     .somariaButtonMask = 0,
                                     .somariaCooldown = 0,
                                     .somariaSelectedType = 0,
                                     .somariaAnimating = 0,
                                     .somariaAnimTimer = 0,
                                     .somariaActionType = 0,
                                     // Hylia's Grace
                                     .hyliasGraceActive = 0,
                                     .hyliasGraceState = 0,
                                     .hyliasGraceSubPhase = 0,
                                     .hyliasGraceTimer = 0,
                                     .hyliasGraceCooldown = 0,
                                     .hyliasGraceFairy = NULL,
                                     // Zonai Permafrost
                                     .zonaiPermafrostActive = 0,
                                     .zonaiPermafrostState = 0,
                                     .zonaiPermafrostSubPhase = 0,
                                     .zonaiPermafrostTimer = 0,
                                     .zonaiPermafrostSavedTimeIncr = 0,
                                     // Time Gate
                                     .timeGateActive = 0,
                                     .timeGateState = 0,
                                     .timeGateSubPhase = 0,
                                     .timeGateTimer = 0,
                                     .timeGatePromptShown = 0,
                                     .timeGateItemVisible = 0,
                                     .timeGatePortalActive = 0,
                                     .timeGatePortalAlpha = 0.0f,
                                     .timeGatePortalScale = 0.0f,
                                     // Mogma Mitts
                                     .mogmaMittsActive = 0,
                                     .mogmaMittsDrainTick = 0,
                                     // Whip
                                     .whipActive = 0,
                                     .whipState = 0,
                                     .whipTipPos = { 0, 0, 0 },
                                     .whipAttachPos = { 0, 0, 0 },
                                     .whipAttachNormal = { 0, 0, 0 },
                                     .whipTimer = 0,
                                     .whipSwingAngle = 0.0f,
                                     .whipSwingVel = 0.0f,
                                     .whipSwingYaw = 0,
                                     .whipRopeLength = 0.0f,
                                     .whipAttachedBgId = 0,
                                     .whipPullTarget = NULL,
                                     .whipRageTarget = NULL,
                                     .whipRageTimer = 0,
                                     .whipRageOrigSpeed = 0.0f,
                                     .whipPrevInvinc = 0,
                                     .whipExtendYaw = 0,
                                     .whipExtendPitch = 0,
                                     .whipFirstPersonActive = 0,
                                     // Desire Sensor
                                     .desireSensorActive = 0,
                                     .desireSensorState = 0,
                                     .desireSensorTimer = 0,
                                     .desireSensorResult = 0,
                                     // Switch Hook
                                     .switchHookActive = 0,
                                     .switchHookState = 0,
                                     .switchHookFirstPerson = 0,
                                     .switchHookProjPos = { 0, 0, 0 },
                                     .switchHookProjYaw = 0,
                                     .switchHookProjPitch = 0,
                                     .switchHookTimer = 0,
                                     .switchHookTarget = NULL,
                                     .switchHookLinkStartPos = { 0, 0, 0 },
                                     .switchHookTargetStartPos = { 0, 0, 0 },
                                     .switchHookSwapTimer = 0,
                                     .switchHookButtonMask = 0,
                                     .switchHookVortexTimer = 0,
                                     .sharedProjectilePos = { 0, 0, 0 } };

s32 CustomItems_IsBlocked(Player* p, PlayState* play) {
    if (p == NULL || play == NULL)
        return true;
    if (p->stateFlags1 & CUSTOM_BLOCKING_STATE1_FLAGS)
        return true;
    if (p->csAction != 0)
        return true;
    if (play->transitionTrigger == TRANS_TRIGGER_START)
        return true;
    if (p->stateFlags3 & PLAYER_STATE3_FLYING_WITH_HOOKSHOT)
        return true;
    return false;
}

// Quick check if item is in any C-button slot
static u8 IsItemEquipped(u8 itemId) {
    for (u8 i = 1; i <= 8; i++) {
        if (gSaveContext.equips.buttonItems[i] == itemId)
            return 1;
    }
    return 0;
}

// Cleanup items that were unequipped while active - only runs if needed
static void CustomItems_CleanupUnequipped(Player* p, PlayState* play) {
    if (gCustomItemState.spinnerActive && !IsItemEquipped(ITEM_SPINNER))
        Handle_Spinner(p, play);
    if (gCustomItemState.gustJarEquipped && !IsItemEquipped(ITEM_GUST_JAR))
        Handle_GustJar(p, play);
    if (gCustomItemState.ballAndChainThrown && !IsItemEquipped(ITEM_BALL_AND_CHAIN))
        Handle_BallAndChain(p, play);
    if (gCustomItemState.shovelActive && !IsItemEquipped(ITEM_SHOVEL))
        Handle_Shovel(p, play);
    if (gCustomItemState.demiseDestructionActive && !IsItemEquipped(ITEM_DEMISE_DESTRUCTION))
        Handle_DemiseDestruction(p, play);
    if (gCustomItemState.dekuLeafGliding && !IsItemEquipped(ITEM_DEKU_LEAF))
        Handle_DekuLeaf(p, play);
    if (gCustomItemState.beetleActive && !IsItemEquipped(ITEM_BEETLE))
        Handle_Beetle(p, play);
    if (gCustomItemState.bombArrowActive && !IsItemEquipped(ITEM_BOMB_ARROWS))
        Handle_BombArrows(p, play);
    if ((gCustomItemState.fireRodActive || gCustomItemState.fireRodFirstPerson) && !IsItemEquipped(ITEM_ROD_FIRE))
        Handle_FireRod(p, play);
    if ((gCustomItemState.iceRodActive || gCustomItemState.iceRodFirstPerson) && !IsItemEquipped(ITEM_ROD_ICE))
        Handle_IceRod(p, play);
    if ((gCustomItemState.lightRodActive || gCustomItemState.lightRodFirstPerson) && !IsItemEquipped(ITEM_ROD_LIGHT))
        Handle_LightRod(p, play);
    if (gCustomItemState.dominionRodActive && !IsItemEquipped(ITEM_DOMINION_ROD))
        Handle_DominionRod(p, play);
    if (gCustomItemState.somariaActive && !IsItemEquipped(ITEM_CANE_OF_SOMARIA))
        Handle_CaneOfSomaria(p, play);
    if (gCustomItemState.hyliasGraceActive && !IsItemEquipped(ITEM_HYLIAS_GRACE))
        Handle_HyliasGrace(p, play);
    if (gCustomItemState.zonaiPermafrostActive && !IsItemEquipped(ITEM_ZONAI_PERMAFROST))
        Handle_ZonaiPermafrost(p, play);
    if (gCustomItemState.timeGateActive && !IsItemEquipped(ITEM_TIME_GATE))
        Handle_TimeGate(p, play);
    if (gCustomItemState.mogmaMittsActive && !IsItemEquipped(ITEM_MOGMA_MITTS))
        Handle_MogmaMitts(p, play);
    if (gCustomItemState.whipActive && !IsItemEquipped(ITEM_WHIP))
        Handle_Whip(p, play);
    if (gCustomItemState.desireSensorActive && !IsItemEquipped(ITEM_DESIRE_SENSOR))
        Handle_DesireSensor(p, play);
    if (gCustomItemState.switchHookActive && !IsItemEquipped(ITEM_SWITCH_HOOK))
        Handle_SwitchHook(p, play);
}

void CustomItems_Update(Player* p, PlayState* play) {
    // Minish tiny-mode upkeep runs ALWAYS (scene-load auto-reset, per-frame scale
    // guard, shrink/grow animation) — even while blocked or with the cap unequipped
    {
        extern void MinishTiny_Update(Player* p, PlayState* play);
        MinishTiny_Update(p, play);
    }

    // Lantern passive systems run ALWAYS (even when other items block, even when unequipped)
    {
        extern void Lantern_UpdateFlames(PlayState * play);
        extern void Lantern_UpdateBurning(PlayState * play);
        extern void Lantern_UpdateLens(PlayState * play);
        Lantern_UpdateFlames(play);
        Lantern_UpdateBurning(play);
        Lantern_UpdateLens(play);
    }

    // Bomb-arrows auto-grant: when CVar is on, hand the player ITEM_BOMB_ARROWS
    // the moment any bomb bag is owned. Idempotent — only writes when needed.
    // ALSO grants automatically when the Twilight Upgrade has been obtained
    // (bomb arrows is one of its three unlocks).
    {
        extern u8 TwilightUpgrade_HasBombArrows(void);
        u8 autoGrant = CVarGetInteger("gMods.BombArrows.AutoGrantOnBag", 0) != 0;
        u8 twilightGrant = TwilightUpgrade_HasBombArrows();
        // ITEM_BOMB_ARROWS is a NEI custom item (0xAE) and is NOT a valid index into
        // gItemSlots[56]; INV_CONTENT()/SLOT() would read OOB and corrupt SaveContext.
        // Resolve the real extended-inventory slot (returns SLOT_BOMB_ARROWS == 27).
        u8 baSlot = ExtInv_GetItemSlot(ITEM_BOMB_ARROWS);
        if ((autoGrant || twilightGrant) && CUR_UPG_VALUE(UPG_BOMB_BAG) > 0 && baSlot != 0xFF &&
            gSaveContext.inventory.items[baSlot] == ITEM_NONE) {
            gSaveContext.inventory.items[baSlot] = ITEM_BOMB_ARROWS;
        }
    }

    // Postman Hat: unlock-on-visit + Mail Dash state machine always run.
    Handle_PostmanHat(p, play);

    // Twilight Upgrade — L-tap toggle for the Clawshot mode. Press L ONLY
    // while the hookshot/longshot is the actually-held item (or actively
    // aiming it). DELIBERATELY narrow: when the player has both hookshot
    // and boomerang equipped on C-buttons, the previous broad fallback
    // (firing on any focusActor != NULL while hookshot was equipped on a
    // C-slot) stole the L press during boomerang aim and prevented the
    // gale multi-target block below from ever receiving it. Now the
    // toggle requires either heldItemAction/Id matching hookshot/longshot,
    // OR ready-to-fire while NOT using the boomerang.
    {
        extern u8 TwilightUpgrade_HasClawshot(void);
        extern u8 TwilightUpgrade_IsClawshotActive(void);
        extern void TwilightUpgrade_SetClawshotActive(u8 active);
        if (TwilightUpgrade_HasClawshot() &&
            CHECK_BTN_ALL(play->state.input[0].press.button, BTN_L) &&
            !(p->stateFlags1 & PLAYER_STATE1_USING_BOOMERANG)) {
            s8 act = p->heldItemAction;
            s16 itemId = p->heldItemId;
            u8 wielding = (act == PLAYER_IA_HOOKSHOT || act == PLAYER_IA_LONGSHOT) ||
                          (itemId == ITEM_HOOKSHOT || itemId == ITEM_LONGSHOT);
            if (wielding) {
                u8 newMode = TwilightUpgrade_IsClawshotActive() ? 0 : 1;
                TwilightUpgrade_SetClawshotActive(newMode);
                // Distinct sound per mode so the player gets audible
                // confirmation of WHICH direction the toggle went.
                Audio_PlaySoundGeneral(newMode ? NA_SE_SY_GET_ITEM : NA_SE_SY_DECIDE,
                                       &p->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                // Swallow L so the gust-jar / shield / boomerang multi-target
                // block below doesn't also consume the same press.
                play->state.input[0].cur.button &= ~BTN_L;
                play->state.input[0].press.button &= ~BTN_L;
            }
        }
    }

    // Gale Boomerang — multi-target route tracking.
    // Gated on IsGaleBoomerangActive() (the persistent A-toggle in kaleido),
    // not just the upgrade bit. When the player toggles the mode OFF in
    // kaleido, the boomerang behaves vanilla even if they own the upgrade.
    // L tap during aim adds the focusActor to the route (up to 4 points,
    // 500 units max between consecutive points).
    {
        extern u8 TwilightUpgrade_IsGaleBoomerangActive(void);
        u8 galeActive = TwilightUpgrade_IsGaleBoomerangActive();
        u8 isUsingBoomerang = (p->stateFlags1 & PLAYER_STATE1_USING_BOOMERANG) != 0;
        u8 isThrown = (p->stateFlags1 & PLAYER_STATE1_BOOMERANG_THROWN) != 0;

        if (!galeActive || !isUsingBoomerang) {
            // Reset route when not using boomerang or upgrade isn't owned.
            gCustomItemState.galeBoomerangTargetCount = 0;
            gCustomItemState.galeBoomerangCurrentTargetIdx = 0;
            gCustomItemState.galeBoomerangLockHeld = 0;
            for (u8 i = 0; i < 4; i++) {
                gCustomItemState.galeBoomerangTargets[i] = NULL;
            }
        } else if (!isThrown) {
            // Aim phase — L press adds the focusActor (Z-target) to the route.
            // Debounced so a held L doesn't spam-add.
            u8 lHeld = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_L) != 0;
            u8 lJustPressed = CHECK_BTN_ALL(play->state.input[0].press.button, BTN_L) != 0;

            // Audible feedback when L is tapped during aim but the player
            // isn't Z-targeting — silent failures were confusing.
            if (lJustPressed && !gCustomItemState.galeBoomerangLockHeld &&
                (p->focusActor == NULL || p->focusActor->update == NULL)) {
                Audio_PlaySoundGeneral(NA_SE_SY_ERROR, &p->actor.world.pos, 4,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultReverb);
                gCustomItemState.galeBoomerangLockHeld = 1; // debounce
            }

            if (lJustPressed && !gCustomItemState.galeBoomerangLockHeld &&
                gCustomItemState.galeBoomerangTargetCount < 4 && p->focusActor != NULL &&
                p->focusActor->update != NULL) {
                gCustomItemState.galeBoomerangLockHeld = 1;

                // Reject if already in the route.
                u8 already = 0;
                for (u8 i = 0; i < gCustomItemState.galeBoomerangTargetCount; i++) {
                    if (gCustomItemState.galeBoomerangTargets[i] == p->focusActor) {
                        already = 1;
                        break;
                    }
                }

                if (!already) {
                    // Distance constraint: new target must be within 500
                    // units of the previous target (or of Link for the
                    // first slot). Z-target lock-on range.
                    Vec3f* prevPos;
                    if (gCustomItemState.galeBoomerangTargetCount == 0) {
                        prevPos = &p->actor.world.pos;
                    } else {
                        prevPos = &gCustomItemState
                                       .galeBoomerangTargets[gCustomItemState.galeBoomerangTargetCount - 1]
                                       ->world.pos;
                    }
                    f32 dx = p->focusActor->world.pos.x - prevPos->x;
                    f32 dy = p->focusActor->world.pos.y - prevPos->y;
                    f32 dz = p->focusActor->world.pos.z - prevPos->z;
                    f32 distSq = dx * dx + dy * dy + dz * dz;
                    if (distSq <= (500.0f * 500.0f)) {
                        gCustomItemState
                            .galeBoomerangTargets[gCustomItemState.galeBoomerangTargetCount++] =
                            p->focusActor;
                        Audio_PlaySoundGeneral(NA_SE_SY_LOCK_ON_HUMAN, &p->actor.world.pos, 4,
                                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                               &gSfxDefaultReverb);
                    } else {
                        // Out of chain range — error chirp so the user knows
                        // the press registered but the target was rejected.
                        Audio_PlaySoundGeneral(NA_SE_SY_ERROR, &p->actor.world.pos, 4,
                                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                               &gSfxDefaultReverb);
                    }
                }
            } else if (!lHeld) {
                gCustomItemState.galeBoomerangLockHeld = 0;
            }
        } else if (isThrown && p->boomerangActor != NULL && p->boomerangActor->update != NULL &&
                   gCustomItemState.galeBoomerangTargetCount > 0) {
            // Flight phase — override En_Boom's moveTo to walk through the route.
            u8 idx = gCustomItemState.galeBoomerangCurrentTargetIdx;
            if (idx < gCustomItemState.galeBoomerangTargetCount) {
                Actor* cur = gCustomItemState.galeBoomerangTargets[idx];
                if (cur == NULL || cur->update == NULL) {
                    // Target died/despawned — skip to next.
                    gCustomItemState.galeBoomerangCurrentTargetIdx++;
                } else {
                    EnBoom* boom = (EnBoom*)p->boomerangActor;
                    boom->moveTo = cur;

                    // Advance when boomerang is within 80 units of the current target.
                    f32 dx = cur->world.pos.x - p->boomerangActor->world.pos.x;
                    f32 dy = cur->world.pos.y - p->boomerangActor->world.pos.y;
                    f32 dz = cur->world.pos.z - p->boomerangActor->world.pos.z;
                    if ((dx * dx + dy * dy + dz * dz) < (80.0f * 80.0f)) {
                        gCustomItemState.galeBoomerangCurrentTargetIdx++;
                    }
                }
            } else {
                // All targets visited — release moveTo so vanilla return logic
                // (returnTimer countdown) brings the boomerang back to Link.
                EnBoom* boom = (EnBoom*)p->boomerangActor;
                boom->moveTo = NULL;
            }
        }
    }

    // Gale Boomerang — B-boost-to-boomerang (TP clawshot-jump style).
    // When the player has the Gale Boomerang mode active AND a boomerang is in
    // flight AND Z-targeting is engaged, pressing B launches Link toward the
    // boomerang's current position with a small upward arc. Lets the player
    // chain mobility off thrown boomerangs.
    {
        extern u8 TwilightUpgrade_IsGaleBoomerangActive(void);
        if (TwilightUpgrade_IsGaleBoomerangActive() &&
            (p->stateFlags1 & PLAYER_STATE1_BOOMERANG_THROWN) &&
            p->boomerangActor != NULL && p->boomerangActor->update != NULL &&
            Player_IsZTargeting(p) && CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B)) {
            // Vector from Link to boomerang
            f32 dx = p->boomerangActor->world.pos.x - p->actor.world.pos.x;
            f32 dy = p->boomerangActor->world.pos.y - p->actor.world.pos.y;
            f32 dz = p->boomerangActor->world.pos.z - p->actor.world.pos.z;
            f32 dist = sqrtf(dx * dx + dy * dy + dz * dz);
            if (dist > 1.0f) {
                f32 speed = 18.0f; // horizontal launch speed
                f32 invNorm = speed / dist;
                p->actor.velocity.x = dx * invNorm;
                p->actor.velocity.z = dz * invNorm;
                p->actor.velocity.y = 8.0f; // upward kick, gravity pulls Link in arc
                // Disable ground flag briefly so velocity actually takes effect.
                p->actor.bgCheckFlags &= ~1;
                Audio_PlaySoundGeneral(NA_SE_PL_SKIP, &p->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
        }
    }

    // Clawshot Bullet Time per-frame update — runs every frame so the slow
    // factor stays applied, exits get detected, joystick aim integrates,
    // and gravity stays suspended while Link is hanging from the anchor.
    // The state machine itself lives in the ClawshotBT_* block below.
    {
        extern void ClawshotBT_Update(Player* player, PlayState* play);
        ClawshotBT_Update(p, play);
    }

    if (gCustomItemState.demiseDestructionActive) {
        Handle_DemiseDestruction(p, play);
        return;
    }

    // Hylia's Grace fairy mode blocks all other custom items
    if (gCustomItemState.hyliasGraceActive) {
        Handle_HyliasGrace(p, play);
        return;
    }

    // Desire Sensor blocks all other custom items during sensing/result
    if (gCustomItemState.desireSensorActive) {
        Handle_DesireSensor(p, play);
        return;
    }

    // Beetle flying blocks all other custom items
    if (gCustomItemState.beetleActive && (gCustomItemState.beetleState == 2 || gCustomItemState.beetleState == 3)) {
        Handle_Beetle(p, play);
        return;
    }

    // Switch Hook blocks all other custom items while active
    if (gCustomItemState.switchHookActive) {
        Handle_SwitchHook(p, play);
        return;
    }

    // Time Gate blocks all other custom items during casting/hovering
    if (gCustomItemState.timeGateActive) {
        Handle_TimeGate(p, play);
        return;
    }

    // Zonai Permafrost must run before IsBlocked check (CASTING sets IN_ITEM_CS)
    // During CASTING: block other items. During ACTIVE: let other items also update.
    if (gCustomItemState.zonaiPermafrostActive) {
        Handle_ZonaiPermafrost(p, play);
        if (gCustomItemState.zonaiPermafrostState != 2 /* ZPERM_STATE_ACTIVE */) {
            return;
        }
    }

    if (CustomItems_IsBlocked(p, play))
        return;

    if (gCustomItemState.globalCooldownTimer > 0)
        gCustomItemState.globalCooldownTimer--;
    if (gCustomItemState.hyliasGraceCooldown > 0)
        gCustomItemState.hyliasGraceCooldown--;

    CustomItems_CleanupUnequipped(p, play);

    // Zora Mask: allow use in water (like custom items, bypasses Player_UseItem water block).
    // Scans all equipped buttons for Zora mask and calls transform handler on press.
    if (p->stateFlags1 & PLAYER_STATE1_IN_WATER) {
        static const u16 sMaskBtns[] = { BTN_CLEFT, BTN_CDOWN, BTN_CRIGHT, BTN_DUP, BTN_DDOWN, BTN_DLEFT, BTN_DRIGHT };
        Input* ctrl = &play->state.input[0];
        for (s32 mi = 0; mi < 7; mi++) {
            if (mi >= 3 && !CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0))
                break;
            if (CHECK_BTN_ALL(ctrl->press.button, sMaskBtns[mi])) {
                u8 slot = (mi < 3) ? (mi + 1) : (mi - 3 + 5); // C-buttons: slots 1-3, D-pad: slots 5-8
                u8 maskItem = gSaveContext.equips.buttonItems[slot];
                if (maskItem == ITEM_MM_MASK_ZORA || maskItem == ITEM_MASK_ZORA) {
                    extern void TransformMasks_HandleMaskUse(PlayState*, Player*, s32);
                    TransformMasks_HandleMaskUse(play, p, maskItem);
                    break;
                }
            }
        }
    }

    // Walk every equip slot, including slot 0 = B button. The previous
    // range (1..8) skipped B entirely AND overflowed the 8-element
    // `buttonItems` array at index 8 — so custom items like Roc's
    // Feather equipped to B never had their handler dispatched. Fix:
    // i = 0..7 covers B + 3 C-buttons + 4 D-pad slots cleanly.
    for (u8 i = 0; i < 8; i++) {
        u8 item = gSaveContext.equips.buttonItems[i];
        if (item < ITEM_ROCS_FEATHER_SKIJER || item > ITEM_POKEBALL)
            continue;

        switch (item) {
            case ITEM_ROCS_FEATHER_SKIJER:
                Handle_RocsFeather(p, play);
                break;
            case ITEM_ROCS_CAPE:
                Handle_RocsCape(p, play);
                break;
            case ITEM_DEKU_LEAF:
                Handle_DekuLeaf(p, play);
                break;
            case ITEM_SPINNER:
                Handle_Spinner(p, play);
                break;
            case ITEM_GUST_JAR:
                Handle_GustJar(p, play);
                break;
            case ITEM_BALL_AND_CHAIN:
                Handle_BallAndChain(p, play);
                break;
            case ITEM_SHOVEL:
                Handle_Shovel(p, play);
                break;
            case ITEM_DEMISE_DESTRUCTION:
                Handle_DemiseDestruction(p, play);
                break;
            case ITEM_BEETLE:
                Handle_Beetle(p, play);
                break;
            case ITEM_BOMB_ARROWS:
                Handle_BombArrows(p, play);
                break;
            case ITEM_ROD_FIRE:
                Handle_FireRod(p, play);
                break;
            case ITEM_ROD_ICE:
                Handle_IceRod(p, play);
                break;
            case ITEM_ROD_LIGHT:
                Handle_LightRod(p, play);
                break;
            case ITEM_DOMINION_ROD:
                Handle_DominionRod(p, play);
                break;
            case ITEM_CANE_OF_SOMARIA:
                Handle_CaneOfSomaria(p, play);
                break;
            case ITEM_MOGMA_MITTS:
                Handle_MogmaMitts(p, play);
                break;
            case ITEM_HYLIAS_GRACE:
                Handle_HyliasGrace(p, play);
                break;
            case ITEM_ZONAI_PERMAFROST:
                // Only call from switch when idle; early-run handles active states
                if (!gCustomItemState.zonaiPermafrostActive)
                    Handle_ZonaiPermafrost(p, play);
                break;
            case ITEM_TIME_GATE:
                Handle_TimeGate(p, play);
                break;
            case ITEM_WHIP:
                Handle_Whip(p, play);
                break;
            case ITEM_DESIRE_SENSOR:
                Handle_DesireSensor(p, play);
                break;
            case ITEM_SWITCH_HOOK:
                Handle_SwitchHook(p, play);
                break;
            case ITEM_MINISH_CAP:
                Handle_MinishCap(p, play);
                break;
            case ITEM_LANTERN:
                Handle_Lantern(p, play);
                break;
            case ITEM_POKEBALL:
                Handle_Pending3(p, play);
                break;
            default:
                break;
        }
    }
}

s32 CustomItems_OverrideDraw(Player* p, PlayState* play) {
    CustomItems_DrawDekuLeaf(p, play);
    CustomItems_DrawSpinner(p, play);
    CustomItems_DrawFireRod(p, play);  // Call unconditionally like spinner
    CustomItems_DrawIceRod(p, play);   // Ice Rod draw
    CustomItems_DrawLightRod(p, play); // Light Rod draw

    if (gCustomItemState.gustJarMode > 0 || p->heldItemAction == ITEM_GUST_JAR) {
        CustomItems_DrawGustJar(p, play);
    }
    if (gCustomItemState.ballAndChainThrown) {
        CustomItems_DrawBallChain(p, play);
    }
    if (gCustomItemState.shovelActive || gCustomItemState.shovelAnimating) {
        CustomItems_DrawShovel(p, play);
    }
    if (gCustomItemState.beetleActive) {
        CustomItems_DrawBeetle(p, play);
    }
    if (gCustomItemState.dominionRodActive) {
        CustomItems_DrawDominionRod(p, play);
    }
    if (gCustomItemState.somariaActive) {
        CustomItems_DrawCaneOfSomaria(p, play);
    }
    if (gCustomItemState.mogmaMittsActive) {
        CustomItems_DrawMogmaMitts(p, play);
    }
    if (gCustomItemState.whipActive) {
        CustomItems_DrawWhip(p, play);
    }
    if (gCustomItemState.timeGateActive) {
        CustomItems_DrawTimeGate(p, play);
        CustomItems_DrawTimeGatePortal(p, play);
    }
    if (gCustomItemState.switchHookActive) {
        CustomItems_DrawSwitchHookInHand(p, play);
        CustomItems_DrawSwitchHook(p, play);
    }
    // Lantern draws in hand ONLY during/after swing AND while lantern is still on a C-button.
    if (gCustomItemState.lanternEquipped || gCustomItemState.lanternSwinging) {
        // Check if lantern is still on any C-button
        u8 lanternOnC = 0;
        for (u8 btn = 1; btn <= 8; btn++) {
            if (gSaveContext.equips.buttonItems[btn] == ITEM_LANTERN) {
                lanternOnC = 1;
                break;
            }
        }
        if (!lanternOnC) {
            // Removed from C-buttons — force hide
            gCustomItemState.lanternEquipped = 0;
            gCustomItemState.lanternSwinging = 0;
        } else {
            s32 heldIA = p->heldItemAction;
            if (heldIA == PLAYER_IA_NONE || heldIA == PLAYER_IA_LANTERN) {
                CustomItems_DrawLantern(p, play);
            } else {
                gCustomItemState.lanternEquipped = 0;
            }
        }
    }

    // Draw reticle for items using first-person aiming mode
    // Color scheme: RED = expel/attack, BLUE = pull/suck, GREEN = control

    // Bomb Arrows - RED (expel)
    if (gCustomItemState.bombArrowActive && gCustomItemState.bombArrowFirstPersonActive) {
        FirstPerson_DrawReticle(p, play, 0.0f, 255, 0, 0);
    }
    // Gust Jar - BLUE when absorbing, element color when blowing, WHITE when idle
    if (gCustomItemState.gustJarFirstPersonActive) {
        if (gCustomItemState.gustJarMode == 2) { // GUST_MODE_ABSORB
            FirstPerson_DrawReticle(p, play, 0.0f, 0, 100, 255);
        } else if (gCustomItemState.gustJarMode == 3) { // GUST_MODE_BLOW
            FirstPerson_DrawReticle(p, play, 0.0f, 255, 100, 0);
        } else {
            FirstPerson_DrawReticle(p, play, 0.0f, 200, 200, 200);
        }
    }
    // Ball and Chain - RED (expel)
    if (gCustomItemState.ballAndChainFirstPersonActive) {
        FirstPerson_DrawReticle(p, play, 0.0f, 255, 0, 0);
    }
    // Beetle - GREEN (control) - only during aiming state
    if (gCustomItemState.beetleFirstPersonActive && gCustomItemState.beetleState == 1) {
        FirstPerson_DrawReticle(p, play, 0.0f, 0, 255, 0);
    }
    // Fire Rod - RED (expel)
    if (gCustomItemState.fireRodFirstPerson) {
        FirstPerson_DrawReticle(p, play, 0.0f, 255, 0, 0);
    }
    // Ice Rod - RED (expel)
    if (gCustomItemState.iceRodFirstPerson) {
        FirstPerson_DrawReticle(p, play, 0.0f, 255, 0, 0);
    }
    // Light Rod - RED (expel)
    if (gCustomItemState.lightRodFirstPerson) {
        FirstPerson_DrawReticle(p, play, 0.0f, 255, 0, 0);
    }
    // Whip - RED (expel)
    if (gCustomItemState.whipFirstPersonActive) {
        FirstPerson_DrawReticle(p, play, 0.0f, 255, 0, 0);
    }
    // Dominion Rod - GREEN (control)
    if (gCustomItemState.dominionRodFirstPersonActive) {
        FirstPerson_DrawReticle(p, play, 0.0f, 0, 255, 0);
    }
    // Switch Hook - BLUE (pull/swap)
    if (gCustomItemState.switchHookFirstPerson) {
        FirstPerson_DrawReticle(p, play, 0.0f, 0, 100, 255);
    }

    return 0;
}

// =============================================================================
// Clawshot Hang (Twilight Upgrade)
// =============================================================================
// When the player lands a clawshot on a hookshot SURFACE (wall/ceiling — NOT
// an enemy or actor), Link is pinned in place at the impact point so he can
// re-aim and fire another clawshot from there. NO slow motion, NO Z-target
// requirement — Z-targeting was suspending Link unintentionally. Instead we
// just keep Link's physics frozen (zero gravity, zero velocity, ground flag
// forced) so the game treats him as standing on solid ground — that is the
// "invisible platform" the player requested, implemented via physics flags
// rather than a separate collision actor.
//
// Wall hit:    Link rotates so his back is to the wall (TP side grapple).
// Ceiling hit: Link drops ~70u so he hangs below the anchor (TP ceiling
//              hang, hands gripping the target above).
//
// Exit:
//  - A press (drop and fall normally — normal physics resume next frame).
//  - Firing another hookshot/clawshot (ArmsHook_Wait → Shoot calls
//    ClawshotBT_NoteShotFired which clears the hang flag; if the new shot
//    lands on another hookshot surface, the hang re-enters on arrival).
//  - Cutscene / damage / loading / etc. (safety unhook).
//
// Enemy pulls (the BUMP_HOOKABLE-bypass branch above) DON'T trigger the
// hang. Only surface hits do. Surface kind is set externally by
// z_arms_hook.c via ClawshotBT_NoteHit*.

// Surface kind detected at hit time — drives where Link hangs and how he
// faces during bullet time. Wall = Link's back glued to the wall (TP side
// grapple); Ceiling = Link dangles ~70u below the anchor (TP ceiling
// hang); else = stick at hook pos with no rotation adjustment.
typedef enum {
    CLAWSHOT_BT_HIT_NONE = 0,
    CLAWSHOT_BT_HIT_WALL,
    CLAWSHOT_BT_HIT_CEILING,
    CLAWSHOT_BT_HIT_OTHER,
} ClawshotBTHitKind;

static u8  sClawshotBTActive = 0;
static u8  sClawshotBTLastHitKind = CLAWSHOT_BT_HIT_NONE;
static Vec3f sClawshotBTLastHitNormal = { 0.0f, 0.0f, 0.0f };
static s16 sClawshotBTLockedYaw = 0;
static Vec3f sClawshotBTAnchorPos = { 0.0f, 0.0f, 0.0f };

// TP ceiling hang: Link's hands grip the anchor, body dangles below. ~70u
// is roughly Link's torso+upper-body height (matches the visual where his
// head sits below the anchor with arms extended up).
#define CLAWSHOT_BT_CEILING_DROP 70.0f

// z_arms_hook.c calls these from the surface- and actor-hit branches so we
// can discriminate when arrival triggers bullet time. Surface variant takes
// the surface normal (XYZ) so we can detect wall (|nY|<0.5) vs ceiling
// (nY<-0.5) and orient Link properly.
void ClawshotBT_NoteHitSurface(f32 nx, f32 ny, f32 nz) {
    sClawshotBTLastHitNormal.x = nx;
    sClawshotBTLastHitNormal.y = ny;
    sClawshotBTLastHitNormal.z = nz;
    if (ny < -0.5f) {
        sClawshotBTLastHitKind = CLAWSHOT_BT_HIT_CEILING;
    } else if (ny > -0.5f && ny < 0.5f) {
        sClawshotBTLastHitKind = CLAWSHOT_BT_HIT_WALL;
    } else {
        // Floor or near-floor — clawshot from above (rare). Treat as a
        // generic anchor; Link stops at hook pos with no special pose.
        sClawshotBTLastHitKind = CLAWSHOT_BT_HIT_OTHER;
    }
}
void ClawshotBT_NoteHitActor(void)    { sClawshotBTLastHitKind = CLAWSHOT_BT_HIT_NONE; }
// Called when a new hookshot leaves Link's hand. Cancels any active hang so
// the new shot's vanilla pull isn't fighting against the pin. Gravity will
// self-restore via Player_UpdateCommon next frame.
void ClawshotBT_NoteShotFired(void) {
    sClawshotBTLastHitKind = CLAWSHOT_BT_HIT_NONE;
    sClawshotBTActive = 0;
}

u8 ClawshotBT_IsActive(void) { return sClawshotBTActive; }

// Called by z_arms_hook.c at the arrival moment (phi_f16 == 0.0f) so we can
// suppress the vanilla -20 velocity.y kick AND enter bullet time when the
// upgrade applies. Returns 1 if bullet time started (caller should skip the
// kick), 0 otherwise.
u8 ClawshotBT_TryStartOnArrival(Player* player, PlayState* play) {
    extern u8 TwilightUpgrade_IsClawshotActive(void);
    if (!TwilightUpgrade_IsClawshotActive())
        return 0;
    if (sClawshotBTLastHitKind == CLAWSHOT_BT_HIT_NONE)
        return 0;
    if (sClawshotBTActive)
        return 1; // already active — still suppress the kick

    sClawshotBTActive = 1;
    sClawshotBTAnchorPos = player->actor.world.pos;

    // Surface-kind-specific positioning + facing:
    //
    //   Wall:    Push Link OUT from the wall by an extra 30u along the surface
    //            normal. Vanilla pull stops Link within ~30u of the hook (which
    //            itself is 10u off the wall) — for thin walls or steep angles
    //            Link can overshoot and end up clipped through the wall geometry.
    //            The extra offset keeps his whole body on the safe side.
    //            Rotate his back into the wall (forward = surface normal).
    //
    //   Ceiling: Drop him CLAWSHOT_BT_CEILING_DROP units below the impact so he
    //            hangs from the anchor TP-style. Facing stays as he was.
    //
    //   Other:   Keep current pose.
    switch (sClawshotBTLastHitKind) {
        case CLAWSHOT_BT_HIT_WALL: {
            sClawshotBTAnchorPos.x += 30.0f * sClawshotBTLastHitNormal.x;
            sClawshotBTAnchorPos.z += 30.0f * sClawshotBTLastHitNormal.z;
            sClawshotBTLockedYaw =
                Math_Atan2S(sClawshotBTLastHitNormal.z, sClawshotBTLastHitNormal.x);
            break;
        }
        case CLAWSHOT_BT_HIT_CEILING: {
            sClawshotBTAnchorPos.y -= CLAWSHOT_BT_CEILING_DROP;
            sClawshotBTLockedYaw = player->actor.shape.rot.y;
            break;
        }
        default:
            sClawshotBTLockedYaw = player->actor.shape.rot.y;
            break;
    }

    // Stop Link cold, zero gravity, force ground flag, and clear all the
    // airborne / hookshot-falling state. Force his action to Player_Action_Idle
    // so the engine treats him as a stationary grounded player — the aim
    // subsystem (bow/hookshot first-person) only engages from idle-ish actions;
    // FreeFall / HookshotFly etc. refuse to enter aim, which is why pressing
    // the hookshot C-button did nothing while hanging.
    extern void Player_Action_Idle(Player* this, PlayState* play);
    extern s32 Player_SetupAction(PlayState* play, Player* this,
                                  PlayerActionFunc actionFunc, s32 flags);

    player->actor.velocity.x = 0.0f;
    player->actor.velocity.y = 0.0f;
    player->actor.velocity.z = 0.0f;
    player->actor.speedXZ = 0.0f;
    player->linearVelocity = 0.0f;
    player->actor.gravity = 0.0f;
    player->actor.bgCheckFlags |= 1;
    player->stateFlags1 &= ~(PLAYER_STATE1_HOOKSHOT_FALLING | PLAYER_STATE1_JUMPING | PLAYER_STATE1_FREEFALL);
    player->stateFlags3 &= ~(PLAYER_STATE3_FLYING_WITH_HOOKSHOT | PLAYER_STATE3_MIDAIR);
    player->actor.world.pos = sClawshotBTAnchorPos;
    player->actor.shape.rot.y = sClawshotBTLockedYaw;
    player->yaw = sClawshotBTLockedYaw;
    // The hookshot reel-in writes a pitch into shape.rot.x to align Link with
    // the chain (Math_Atan2S on bodyDistDiffVec.y vs xz dist at z_arms_hook.c
    // ~line 257) — if the anchor is above Link he ends up tilted upward when
    // the pull completes. Zero ONLY shape.rot.x (the user specifically asked
    // for X only; rot.z left alone). Aim look-up/down lives on upperLimbRot,
    // not the body rotation, so zeroing here is safe.
    player->actor.shape.rot.x = 0;

    // No platform actor spawn — Setting Obj_Hsblock's draw=NULL after spawn
    // didn't actually suppress its rendering (the hookshottable-target square
    // was still visible below Link's feet), and most scenes do have a real
    // scene-collision floor somewhere below the wall anchor, which the
    // bgCheck raycast in Player_ProcessSceneCollision finds. That non-NULL
    // floorPoly is enough for our hook there to safely force the ground flag
    // and unblock the first-person aim subsystem. In pure-pit scenes (no
    // floor at all below Link) aim will still glitch, but at least we don't
    // visually spawn the target actor.

    Audio_PlaySoundGeneral(NA_SE_SY_ATTENTION_ON, &player->actor.world.pos, 4,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    return 1;
}

static void ClawshotBT_End(Player* player) {
    if (!sClawshotBTActive)
        return;
    sClawshotBTActive = 0;
    // Gravity self-restores via Player_UpdateCommon next frame — no need to
    // pick a value here that might fight whatever state Link transitions to.
}

void ClawshotBT_Update(Player* player, PlayState* play) {
    if (!sClawshotBTActive)
        return;

    Input* input = &play->state.input[0];

    // Exit on A press (drop & fall). Other buttons (B / R / C-buttons /
    // Z toggle) stay LIVE so Link can swing the sword, raise the shield,
    // aim items, change C-button equipment, etc. without losing the hang.
    // The hookshot specifically self-exits via ClawshotBT_NoteShotFired
    // when a new shot leaves Link's hand.
    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        ClawshotBT_End(player);
        return;
    }

    // Safety: cutscene / damage / loading / etc.
    u32 blockedFlags = PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_LOADING |
                       PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_DAMAGED;
    if (player->stateFlags1 & blockedFlags) {
        ClawshotBT_End(player);
        return;
    }

    // Hang pin: lock position + zero physics every frame so Player_UpdateCommon's
    // velocity writes from joystick / gravity / etc. don't drift Link off the
    // anchor. Forcing bgCheckFlags|=1 plus clearing the airborne stateFlags
    // keeps the engine in "Link is grounded" mode so the hookshot aim subsystem
    // engages from the hanging position.
    //
    // We DON'T re-call Player_SetupAction(Idle) here — Idle naturally handles
    // its own transitions (aim, sword, etc.). If the engine kicks Link out of
    // Idle into FreeFall because the raycast finds no floor (which can happen
    // since we don't have a real platform), we only reassert Idle when it's
    // specifically a falling state — anything else (aim/READY_TO_FIRE,
    // first-person, etc.) is exactly what the player wants and we leave alone.
    player->actor.world.pos = sClawshotBTAnchorPos;
    player->actor.velocity.x = 0.0f;
    player->actor.velocity.y = 0.0f;
    player->actor.velocity.z = 0.0f;
    player->actor.speedXZ = 0.0f;
    player->linearVelocity = 0.0f;
    player->actor.gravity = 0.0f;
    player->actor.bgCheckFlags |= 1;
    player->stateFlags1 &= ~(PLAYER_STATE1_HOOKSHOT_FALLING | PLAYER_STATE1_JUMPING | PLAYER_STATE1_FREEFALL);
    player->stateFlags3 &= ~(PLAYER_STATE3_FLYING_WITH_HOOKSHOT | PLAYER_STATE3_MIDAIR);

    // For a wall hang, keep Link's back glued to the wall WHILE IDLE — but
    // once he enters first-person aim / ready-to-fire, release the yaw lock
    // so the player can rotate freely to look at new targets. Otherwise the
    // pin glues the body to the original facing and the user can't sweep
    // the camera during aim.
    u8 isAiming = (player->stateFlags1 &
                   (PLAYER_STATE1_FIRST_PERSON | PLAYER_STATE1_READY_TO_FIRE)) != 0;
    if (sClawshotBTLastHitKind == CLAWSHOT_BT_HIT_WALL && !isAiming) {
        player->actor.shape.rot.y = sClawshotBTLockedYaw;
        player->yaw = sClawshotBTLockedYaw;
    }
    // Keep Link upright across the entire hang — shape.rot.x picks up tilt
    // from the reel-in chain alignment and the engine occasionally re-asserts
    // it during transitions. ONLY rot.x is zeroed (per user request — rot.z
    // stays untouched). Aim look-up/down lives on upperLimbRot, not the body
    // rotation, so zeroing here is safe.
    player->actor.shape.rot.x = 0;
}

// ─────────────────────────────────────────────────────────────────────────
// VisualSync field lists (single source of truth shared by Build & Apply)
//
// These X-macro lists drive both CustomItems_BuildVisualSync (sender) and
// CustomItems_ApplyVisualSync (receiver) so the two directions can no longer
// drift apart. They intentionally do NOT redeclare the CustomItemVisualSync
// struct or the CI_FLAG_* bits — those are referenced by name/value from the
// networking layer (Harpoon.cpp / HarpoonDummyPlayer.cpp) and are kept as the
// authoritative hand-written definitions in custom_items.h.
//
// CI_VISUAL_SCALARS / CI_VISUAL_ARRAYS: plain value copies that are symmetric
// in both directions (Build: out->x = s->x; Apply: s->x = sync->x).
//
// CI_VISUAL_FLAGS(F): one entry per activeFlags bit. Each entry carries its
// complete two-way logic so it stays in sync:
//   F(flag, buildCond, buildExtra, applyStmts)
//     buildCond  — expression; when true the flag bit is OR'd into activeFlags
//     buildExtra — extra Build-only statements (e.g. value copies whose Apply
//                  is gated by the flag); empty (0) when none
//     applyStmts — statements run by Apply for this flag; empty (0) when none
// ─────────────────────────────────────────────────────────────────────────

// clang-format off
#define CI_VISUAL_SCALARS(F)        \
    F(dekuLeafGliding)              \
    F(dekuLeafBlowing)              \
    F(dekuLeafAnimTimer)            \
    F(gustJarElement)               \
    F(gustJarBlowActive)            \
    F(gustJarHeatTimer)             \
    F(timer2)                       \
    F(sharedProjectilePos)          \
    F(beetleState)                  \
    F(beetlePos)                    \
    F(beetleRot)                    \
    F(beetleWingScale)              \
    F(fireRodProjActive)            \
    F(fireRodProjCount)             \
    F(fireRodProjType)              \
    F(fireRodProjPos)               \
    F(fireRodProjPos2)              \
    F(fireRodProjPos3)              \
    F(fireRodProjScale)             \
    F(fireRodMatrix)                \
    F(fireRodMatrixValid)           \
    F(iceRodProjActive)             \
    F(iceRodProjCount)              \
    F(iceRodProjPos)                \
    F(iceRodProjPos2)               \
    F(iceRodProjPos3)               \
    F(iceRodProjScale)              \
    F(iceRodMatrix)                 \
    F(iceRodMatrixValid)            \
    F(lightRodProjActive)           \
    F(lightRodProjCount)            \
    F(lightRodProjPos)              \
    F(lightRodProjPos2)             \
    F(lightRodProjPos3)             \
    F(lightRodMatrix)               \
    F(lightRodMatrixValid)          \
    F(dominionRodState)             \
    F(dominionRodOrbPos)            \
    F(whipState)                    \
    F(whipTipPos)                   \
    F(whipAttachPos)                \
    F(whipAttachNormal)             \
    F(timeGateItemVisible)          \
    F(timeGatePortalActive)         \
    F(timeGatePortalAlpha)          \
    F(timeGatePortalScale)          \
    F(switchHookState)              \
    F(switchHookProjPos)            \
    F(rocsJumpCount)                \
    F(rocsMmAnimTimer)              \
    F(bombArrowState)               \
    F(hyliasGraceState)             \
    F(hyliasGraceSubPhase)          \
    F(hyliasGraceTimer)             \
    F(hyliasGraceForcedBySpell)     \
    F(zonaiPermafrostState)         \
    F(zonaiPermafrostSubPhase)      \
    F(zonaiPermafrostTimer)         \
    F(lanternFireType)              \
    F(lanternSwinging)              \
    F(lanternEquipped)              \
    F(lanternSwingFrame)            \
    F(minishCapWarpMode)            \
    F(minishCapShrinking)           \
    F(minishCapGrowing)             \
    F(postmanHatDashing)            \
    F(postmanHatArriving)           \
    F(postmanHatTransitionTimer)    \
    F(desireSensorState)            \
    F(desireSensorTimer)            \
    F(desireSensorResult)

#define CI_VISUAL_ARRAYS(F)         \
    F(fireRodProjTrail)             \
    F(iceRodProjTrail)

#define CI_VISUAL_FLAGS(F)                                                                                   \
    F(CI_FLAG_SPINNER,            s->spinnerActive,                          0, s->spinnerActive = present;)  \
    F(CI_FLAG_GUSTJAR,            s->gustJarMode > 0,    out->gustJarMode = s->gustJarMode;,                  \
                                                         s->gustJarMode = present ? sync->gustJarMode : 0;)  \
    F(CI_FLAG_BALLCHAIN,          s->ballAndChainThrown, out->ballAndChainThrown = s->ballAndChainThrown;,   \
                                                         s->ballAndChainThrown = present;)                   \
    F(CI_FLAG_SHOVEL,             s->shovelAnimating,    out->shovelAnimating = s->shovelAnimating;,          \
                                                         s->shovelAnimating = present ? sync->shovelAnimating : 0; \
                                                         s->shovelActive = present;)                         \
    F(CI_FLAG_BEETLE,             s->beetleActive,                           0, s->beetleActive = present;)  \
    F(CI_FLAG_DOMINION_ROD,       s->dominionRodActive,                      0, s->dominionRodActive = present;) \
    F(CI_FLAG_SOMARIA,            s->somariaActive,                          0, s->somariaActive = present;) \
    F(CI_FLAG_MOGMA_MITTS,        s->mogmaMittsActive,                       0, s->mogmaMittsActive = present;) \
    F(CI_FLAG_WHIP,               s->whipActive,                             0, s->whipActive = present;)    \
    F(CI_FLAG_TIME_GATE,          s->timeGateActive,                         0, s->timeGateActive = present;) \
    F(CI_FLAG_SWITCH_HOOK,        s->switchHookActive,                       0, s->switchHookActive = present;) \
    F(CI_FLAG_DEKU_LEAF,          s->dekuLeafGliding || s->dekuLeafBlowing,  0, 0)                           \
    F(CI_FLAG_FIRE_ROD,           s->fireRodActive,                          0, s->fireRodActive = present;) \
    F(CI_FLAG_ICE_ROD,            s->iceRodActive,                           0, s->iceRodActive = present;)  \
    F(CI_FLAG_LIGHT_ROD,          s->lightRodActive,                         0, s->lightRodActive = present;) \
    F(CI_FLAG_ROCS_FEATHER,       s->rocsFeatherJumpActive,                                                       \
                                  out->rocsFeatherJumpActive = s->rocsFeatherJumpActive;,                        \
                                                         s->rocsFeatherJumpActive = present;)                    \
    F(CI_FLAG_BOMB_ARROW,         s->bombArrowActive,                        0, s->bombArrowActive = present;) \
    F(CI_FLAG_DEMISE_DESTRUCTION, s->demiseDestructionActive,                0, s->demiseDestructionActive = present;) \
    F(CI_FLAG_HYLIAS_GRACE,       s->hyliasGraceActive,                      0, s->hyliasGraceActive = present;) \
    F(CI_FLAG_ZONAI_PERMAFROST,   s->zonaiPermafrostActive,                  0, s->zonaiPermafrostActive = present;) \
    F(CI_FLAG_LANTERN,            s->lanternEquipped || s->lanternSwinging,  0, 0)                           \
    F(CI_FLAG_MINISH_CAP,         s->minishCapShrinking || s->minishCapGrowing || s->minishCapWarpMode ||    \
                                  s->minishTinyActive || s->minishTinyAnim,  0, 0)                           \
    F(CI_FLAG_POSTMAN_HAT,        s->postmanHatDashing || s->postmanHatArriving, 0, 0)                       \
    F(CI_FLAG_DESIRE_SENSOR,      s->desireSensorActive,                     0, s->desireSensorActive = present;)
// clang-format on

void CustomItems_BuildVisualSync(CustomItemVisualSync* out) {
    CustomItemState* s = &gCustomItemState;
    memset(out, 0, sizeof(CustomItemVisualSync));

    // Build active flags bitfield + any flag-gated value copies
    u32 flags = 0;
#define CI_BUILD_FLAG(flag, buildCond, buildExtra, applyStmts) \
    if (buildCond)                                             \
        flags |= (flag);                                       \
    buildExtra;
    CI_VISUAL_FLAGS(CI_BUILD_FLAG)
#undef CI_BUILD_FLAG
    out->activeFlags = flags;

    // Plain symmetric value copies
#define CI_BUILD_SCALAR(name) out->name = s->name;
    CI_VISUAL_SCALARS(CI_BUILD_SCALAR)
#undef CI_BUILD_SCALAR

    // Array copies
#define CI_BUILD_ARRAY(name) memcpy(out->name, s->name, sizeof(s->name));
    CI_VISUAL_ARRAYS(CI_BUILD_ARRAY)
#undef CI_BUILD_ARRAY
}

void CustomItems_ApplyVisualSync(const CustomItemVisualSync* sync) {
    CustomItemState* s = &gCustomItemState;

    // Apply active flags from bitfield (+ flag-gated value restores)
#define CI_APPLY_FLAG(flag, buildCond, buildExtra, applyStmts) \
    {                                                          \
        u32 present = (sync->activeFlags & (flag)) ? 1 : 0;    \
        (void)present;                                         \
        applyStmts;                                            \
    }
    CI_VISUAL_FLAGS(CI_APPLY_FLAG)
#undef CI_APPLY_FLAG

    // Plain symmetric value copies
#define CI_APPLY_SCALAR(name) s->name = sync->name;
    CI_VISUAL_SCALARS(CI_APPLY_SCALAR)
#undef CI_APPLY_SCALAR

    // Array copies
#define CI_APPLY_ARRAY(name) memcpy(s->name, sync->name, sizeof(s->name));
    CI_VISUAL_ARRAYS(CI_APPLY_ARRAY)
#undef CI_APPLY_ARRAY

    // Disable first-person reticles (never draw for remote players)
    s->bombArrowFirstPersonActive = 0;
    s->fireRodFirstPerson = 0;
    s->iceRodFirstPerson = 0;
    s->lightRodFirstPerson = 0;
    s->dominionRodFirstPersonActive = 0;
    s->switchHookFirstPerson = 0;
}
