/**
 * sm64_mario_items.c — Mario-mode item handlers
 *
 * Direct port of Ivan the Fairy's item system from
 * soh/src/overlays/actors/ovl_En_Partner/z_en_partner.c (lines 192-578 + 681-737).
 * Items spawn at the player actor's position (which is synced to Mario every
 * frame in Sm64Mario_Update) using Mario's facing yaw — no first-person aim,
 * no Link action-func involvement, Mario's mesh stays visible throughout.
 *
 * Differences from EnPartner port:
 *   - No companion-actor movement / sparkles / glow lights / camera-relative
 *     stick handling. Mario already has its own movement via libsm64.
 *   - Reads from `player->actor` directly instead of `this->actor`.
 *   - Stick AT collider is a separate cylinder (sMarioStickCollider) so it
 *     doesn't collide with the existing Mario punch collider in sm64_mario.c.
 *   - Lens-of-truth visibility is exposed via Sm64Mario_LensActive() so
 *     Sm64Mario_HasMesh() can return false while lens is up.
 *
 * Pure C, #included into z_player.c after sm64_mario.c so it can reach the
 * Sm64Mario_* helpers.
 */

#include "overlays/actors/ovl_En_Bom/z_en_bom.h"
#include "overlays/actors/ovl_En_Boom/z_en_boom.h"
#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h"
#include "overlays/actors/ovl_En_Partner/z_en_partner.h"
#include "objects/object_link_child/object_link_child.h"

// Forward decls — these are defined in z_player.c (Player_RequestQuake) and
// in z_player.c too (spawn_boomerang_ivan) but never declared in any public
// header. Without these forward decls C falls back to implicit-int return
// type, which conflicts with the actual `void` and `s32` definitions and
// produces C2371 "redefinition; differing basic types" at the def sites.
void Player_RequestQuake(PlayState* play, s32 speed, s32 y, s32 countdown);
s32  spawn_boomerang_ivan(EnPartner* this, PlayState* play);

// Mirror of Ivan's per-arrow-type magic cost (z_en_partner.c:190).
static u8 sMarioMagicArrowCosts[] = { 0, 4, 4, 8 };

// State machine — direct field equivalents of EnPartner.
static u8     sMarioUsedItem       = 0xFF;   // 0xFF = none
static u8     sMarioUsedItemButton = 0xFF;
static u8     sMarioUsedSpell      = 0;
static s16    sMarioItemTimer      = 0;
static s16    sMarioMagicTimer     = 0;
static s16    sMarioStickDamageTimer = 0;
static Actor* sMarioHookshotTarget = NULL;
static u8     sMarioLensActive     = 0;

// Stick flame-position vector (mirrors EnPartner.stickWeaponInfo.tip).
static Vec3f sMarioStickTipPos;

// Separate AT collider for the lit deku stick. Don't reuse sSm64AttackCollider
// (the punch collider) — they'd both fire AT when the stick is held during a
// punch frame, which is double-damage and weird state.
static ColliderCylinder sMarioStickCollider;
static u8 sMarioStickColliderInited = 0;

// AT collider for the lit deku stick. Damage flags = DMG_DEKU_STICK | DMG_FIRE
// so cobwebs burn, unlit torches catch fire, deku babas/scrubs flinch, etc.
// Larger radius (24) + taller height (40) than the punch collider so it
// reaches cobwebs and torch flames without the user having to walk into
// them. yShift 0 → cylinder bottom = collider pos.y, top = pos.y + 40.
// We position the collider at chest height each frame, so the cylinder
// spans [chest, chest+40] — covers everything from waist to overhead.
static ColliderCylinderInit sMarioStickColliderInit = {
    { COLTYPE_NONE, AT_ON | AT_TYPE_PLAYER, AC_NONE,
      OC1_NONE, OC2_TYPE_PLAYER, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0,
      { DMG_DEKU_STICK | DMG_FIRE, 0x00, 0x08 },
      { 0x00000000, 0x00, 0x00 },
      TOUCH_ON | TOUCH_NEAREST, BUMP_NONE, OCELEM_NONE },
    { 24, 40, 0, { 0, 0, 0 } }
};

// Stick flame visual constants — copied verbatim from z_en_partner.c:314-318.
static Vec3f sMarioFlameVelocity = { 0.0f, 0.5f, 0.0f };
static Vec3f sMarioFlameAccel    = { 0.0f, 0.5f, 0.0f };
static Color_RGBA8 sMarioFlamePrim = { 255, 255, 100, 255 };
static Color_RGBA8 sMarioFlameEnv  = { 255, 50, 0, 0 };

// Visible-stick-DL state — set true while the user holds the C-button so
// Sm64Mario_DrawHeldStick (called from Sm64Mario_Draw in sm64_mario.c)
// renders the gLinkChildLinkDekuStickDL at Mario's hand each frame.
// "Como con Link" — a freestanding stick model that follows Mario's
// position + facing instead of floating invisibly.
static u8    sMarioStickDrawActive = 0;
static Vec3f sMarioStickDrawPos;     // World pos for stick draw matrix
static s16   sMarioStickDrawYaw;     // Mario's facing for stick orientation

// =============================================================================
// Public getters
// =============================================================================

u8 Sm64Mario_LensActive(void) {
    return sMarioLensActive;
}

// =============================================================================
// Per-item handlers — port of z_en_partner.c:192-509 with pos/yaw read from
// player->actor instead of this->actor. State machine values are identical:
//   started == 1  → press (rising edge)
//   started == 2  → held  (current)
//   started == 0  → release (falling edge)
// =============================================================================

// Mirrors Ivan's UseBow (z_en_partner.c:192-231) but with one corrected
// behavior: pass the elemental params at spawn time instead of overriding
// post-spawn. EnArrow_Init reads `params` to register the visual blure
// trail and to set the collider damage flags (z_en_arrow.c:174-200) —
// post-spawn override is too late for both. Ivan inherits this bug; in
// vanilla play it manifests as elemental arrows shooting with normal
// trail + normal damage. The behavior the user actually wants ("fire
// arrows do fire damage") only works with spawn-time params.
static void MarioItem_UseBow(PlayState* play, Player* player, u8 started, u8 arrowType) {
    if (started == 1) {
        Player_PlaySfx(&player->actor, NA_SE_PL_CHANGE_ARMS);
    } else if (started == 0) {
        if (sMarioItemTimer <= 0) {
            if (AMMO(ITEM_BOW) > 0) {
                if (arrowType >= 1 && !Magic_RequestChange(play, sMarioMagicArrowCosts[arrowType], MAGIC_CONSUME_NOW)) {
                    Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
                    return;
                }

                sMarioItemTimer = 10;

                s16 spawnParams;
                switch (arrowType) {
                    case 1:  spawnParams = ARROW_FIRE;   break;
                    case 2:  spawnParams = ARROW_ICE;    break;
                    case 3:  spawnParams = ARROW_LIGHT;  break;
                    default: spawnParams = ARROW_NORMAL; break;
                }

                Actor* newarrow = Actor_SpawnAsChild(
                    &play->actorCtx, &player->actor, play, ACTOR_EN_ARROW,
                    player->actor.world.pos.x,
                    player->actor.world.pos.y + 7,
                    player->actor.world.pos.z,
                    0, player->actor.shape.rot.y, 0, spawnParams);

                if (newarrow != NULL) {
                    player->unk_A73 = 4;
                    newarrow->parent = NULL;
                }
                Inventory_ChangeAmmo(ITEM_BOW, -1);
            }
        }
    }
}

static void MarioItem_UseSlingshot(PlayState* play, Player* player, u8 started) {
    if (started == 0) {
        if (sMarioItemTimer <= 0) {
            if (AMMO(ITEM_SLINGSHOT) > 0) {
                sMarioItemTimer = 10;
                Actor* newpellet = Actor_SpawnAsChild(
                    &play->actorCtx, &player->actor, play, ACTOR_EN_ARROW,
                    player->actor.world.pos.x,
                    player->actor.world.pos.y + 7.0f,
                    player->actor.world.pos.z,
                    0, player->actor.shape.rot.y, 0, ARROW_SEED);
                if (newpellet != NULL) {
                    player->unk_A73 = 4;
                    newpellet->parent = NULL;
                }
                Inventory_ChangeAmmo(ITEM_SLINGSHOT, -1);
            } else {
                Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
            }
        }
    }
}

static void MarioItem_UseBombs(PlayState* play, Player* player, u8 started) {
    if (sMarioItemTimer > 0) return;
    if (started != 1) return;

    if (AMMO(ITEM_BOMB) > 0 && play->actorCtx.actorLists[ACTORCAT_EXPLOSIVE].length < 3) {
        sMarioItemTimer = 10;
        Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOM,
                    player->actor.world.pos.x,
                    player->actor.world.pos.y + 7.0f,
                    player->actor.world.pos.z, 0, 0, 0, 0);
        Inventory_ChangeAmmo(ITEM_BOMB, -1);
    } else {
        Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
    }
}

static void MarioItem_UseBombchus(PlayState* play, Player* player, u8 started) {
    if (sMarioItemTimer > 0) return;
    if (started != 1) return;

    if (AMMO(ITEM_BOMBCHU) > 0) {
        sMarioItemTimer = 10;
        EnBom* bomb = (EnBom*)Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOM,
                                          player->actor.world.pos.x,
                                          player->actor.world.pos.y + 7.0f,
                                          player->actor.world.pos.z, 0, 0, 0, 0);
        if (bomb != NULL) {
            bomb->timer = 0;
        }
        Inventory_ChangeAmmo(ITEM_BOMBCHU, -1);
    } else {
        Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
    }
}

static void MarioItem_UseHammer(PlayState* play, Player* player, u8 started) {
    if (sMarioItemTimer > 0) return;
    if (started != 1) return;

    static Vec3f zeroVec = { 0.0f, 0.0f, 0.0f };
    sMarioItemTimer = 10;
    Vec3f shockwavePos = player->actor.world.pos;

    Player_RequestQuake(play, 27767, 7, 20);
    Player_PlaySfx(&player->actor, NA_SE_IT_HAMMER_HIT);
    EffectSsBlast_SpawnWhiteShockwave(play, &shockwavePos, &zeroVec, &zeroVec);

    // Knockback-like impulse on nearby actors. xzDistToPlayer/yDistToPlayer
    // are measured from Player, which is co-located with Mario.
    if (player->actor.xzDistToPlayer < 100.0f && player->actor.yDistToPlayer < 35.0f) {
        func_8002F71C(play, &player->actor, 8.0f, player->actor.yawTowardsPlayer, 8.0f);
    }
}

static void MarioItem_UseNuts(PlayState* play, Player* player, u8 started) {
    if (sMarioItemTimer > 0) return;
    if (started != 1) return;

    if (AMMO(ITEM_NUT) > 0) {
        sMarioItemTimer = 10;
        Actor_Spawn(&play->actorCtx, play, ACTOR_EN_ARROW,
                    player->actor.world.pos.x,
                    player->actor.world.pos.y + 7.0f,
                    player->actor.world.pos.z,
                    0x1000, player->actor.shape.rot.y, 0, ARROW_NUT);
        Inventory_ChangeAmmo(ITEM_NUT, -1);
    } else {
        Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
    }
}

static void MarioItem_UseDekuStick(PlayState* play, Player* player, u8 started) {
    if (sMarioItemTimer > 0) return;

    if (!sMarioStickColliderInited) {
        Collider_InitCylinder(play, &sMarioStickCollider);
        Collider_SetCylinder(play, &sMarioStickCollider, &player->actor, &sMarioStickColliderInit);
        sMarioStickColliderInited = 1;
    }

    if (started == 1) {
        if (AMMO(ITEM_STICK) > 0) {
            Player_PlaySfx(&player->actor, NA_SE_EV_FLAME_IGNITION);
            sMarioStickDrawActive = 1;     // turn on the floating stick render
        } else {
            Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
        }
    }

    if (started == 2 && AMMO(ITEM_STICK) > 0) {
        // Stick floats at Mario's right-hand height, in front of him by ~10
        // OOT units. Mario is ~37 OOT tall; +18 Y is mid-chest where his
        // hand would be in a passive idle pose.
        f32 sinY = Math_SinS(player->actor.shape.rot.y);
        f32 cosY = Math_CosS(player->actor.shape.rot.y);
        sMarioStickDrawPos.x = player->actor.world.pos.x + sinY * 10.0f;
        sMarioStickDrawPos.y = player->actor.world.pos.y + 18.0f;
        sMarioStickDrawPos.z = player->actor.world.pos.z + cosY * 10.0f;
        sMarioStickDrawYaw = player->actor.shape.rot.y;

        // Flame at the stick tip — slightly above the stick's grip.
        sMarioStickTipPos = sMarioStickDrawPos;
        sMarioStickTipPos.y += 6.0f;
        func_8002836C(play, &sMarioStickTipPos, &sMarioFlameVelocity, &sMarioFlameAccel,
                      &sMarioFlamePrim, &sMarioFlameEnv, 200.0f, 0, 8);

        // AT collider centered on the flame, at chest height — covers
        // from Mario's waist (chest - 0) up to overhead (chest + 40).
        // Cobwebs and torches sit at this height typically; previous
        // pos at Mario's feet missed them entirely.
        sMarioStickCollider.dim.pos.x = (s16)sMarioStickTipPos.x;
        sMarioStickCollider.dim.pos.y = (s16)(player->actor.world.pos.y + 5.0f);
        sMarioStickCollider.dim.pos.z = (s16)sMarioStickTipPos.z;
        // Re-arm AT each frame; SetAT can only register once per call.
        sMarioStickCollider.base.atFlags |= AT_ON | AT_TYPE_PLAYER;
        sMarioStickCollider.base.atFlags &= ~AT_HIT;
        CollisionCheck_SetAT(play, &play->colChkCtx, &sMarioStickCollider.base);

        if (sMarioStickDamageTimer <= 0) {
            Inventory_ChangeAmmo(ITEM_STICK, -1);
            sMarioStickDamageTimer = 20;
        } else {
            sMarioStickDamageTimer--;
        }
    }

    if (started == 0) {
        sMarioStickDrawActive = 0;     // hide the stick when released
    }
}

// Public — called from Sm64Mario_Draw in sm64_mario.c each frame.
// Renders gLinkChildLinkDekuStickDL at Mario's hand position when the
// player is holding a deku stick C-button. Pattern lifted from
// EffectSsStick_Draw (z_eff_ss_stick.c:51-73): set up a fresh world-space
// matrix, scale to OOT actor scale (0.01), bind segment 0x06 to LINK_CHILD
// object so the DL's texture/data references resolve, then draw the DL.
void Sm64Mario_DrawHeldStick(PlayState* play) {
    if (!sMarioStickDrawActive) return;
    if (play == NULL) return;

    s32 objIdx = Object_GetIndex(&play->objectCtx, OBJECT_LINK_CHILD);
    if (objIdx < 0) return;     // object isn't loaded in this scene

    OPEN_DISPS(play->state.gfxCtx);

    Matrix_Translate(sMarioStickDrawPos.x, sMarioStickDrawPos.y, sMarioStickDrawPos.z, MTXMODE_NEW);
    Matrix_Scale(0.01f, 0.01f, 0.01f, MTXMODE_APPLY);
    Matrix_RotateZYX(0, sMarioStickDrawYaw, 0, MTXMODE_APPLY);

    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x06, play->objectCtx.status[objIdx].segment);
    gSPSegment(POLY_OPA_DISP++, 0x0C, gCullBackDList);
    gSPDisplayList(POLY_OPA_DISP++, gLinkChildLinkDekuStickDL);

    CLOSE_DISPS(play->state.gfxCtx);
}

static void MarioItem_UseHookshot(PlayState* play, Player* player, u8 started) {
    if (sMarioItemTimer > 0) return;

    if (started == 1) {
        Player_PlaySfx(&player->actor, NA_SE_PL_CHANGE_ARMS);
        sMarioHookshotTarget = Actor_SpawnAsChild(
            &play->actorCtx, &player->actor, play, ACTOR_OBJ_HSBLOCK,
            player->actor.world.pos.x,
            player->actor.world.pos.y + 7.5f,
            player->actor.world.pos.z,
            player->actor.world.rot.x, player->actor.world.rot.y, player->actor.world.rot.z, 2);
        if (sMarioHookshotTarget != NULL) {
            sMarioHookshotTarget->scale.x = 0.05f;
            sMarioHookshotTarget->scale.y = 0.05f;
            sMarioHookshotTarget->scale.z = 0.05f;
        }
    } else if (started == 0) {
        if (sMarioHookshotTarget != NULL) {
            Actor_Kill(sMarioHookshotTarget);
            sMarioHookshotTarget = NULL;
        }
        Player_PlaySfx(&player->actor, NA_SE_PL_CHANGE_ARMS);
    } else if (started == 2) {
        if (sMarioHookshotTarget != NULL) {
            sMarioHookshotTarget->shape.rot.y = player->actor.shape.rot.y;
        }
    }
}

static void MarioItem_UseOcarina(PlayState* play, Player* player, u8 started) {
    if (sMarioItemTimer > 0) return;
    if (started == 1) {
        Audio_PlaySoundTransposed(&player->actor.projectedPos, NA_SE_VO_NA_HELLO_2, -6);
    }
}

static void MarioItem_UseBoomerang(PlayState* play, Player* player, u8 started) {
    if (sMarioItemTimer > 0) return;
    if (started != 1) return;
    sMarioItemTimer = 20;
    // spawn_boomerang_ivan internally checks IvanCoopModeEnabled || gIvanPossessActive
    // — we extend that gate in z_player.c:407-410 to also accept Sm64Mario_IsReady().
    spawn_boomerang_ivan((EnPartner*)&player->actor, play);
}

static void MarioItem_UseLens(PlayState* play, Player* player, u8 started) {
    if (sMarioItemTimer > 0) return;
    if (started == 1) {
        Sfx_PlaySfxCentered(NA_SE_SY_GLASSMODE_ON);
        sMarioLensActive = 1;
    } else if (started == 0) {
        Sfx_PlaySfxCentered(NA_SE_SY_GLASSMODE_OFF);
        sMarioLensActive = 0;
    }
}

static void MarioItem_UseBeans(PlayState* play, Player* player, u8 started) {
    if (sMarioItemTimer > 0) return;
    if (started != 1) return;

    GetItemEntry beanEntry = ItemTable_Retrieve(GI_BEAN);
    if (play->actorCtx.titleCtx.alpha <= 0) {
        if (gSaveContext.rupees >= 100 && GiveItemEntryWithoutActor(play, beanEntry)) {
            Rupees_ChangeBy(-100);
        } else {
            Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
        }
    }
}

// Spell magic cost (per cast). 12 magic = half a normal magic bar.
static u8 sMarioSpellCost = 12;

// Cap durations in libsm64 ticks (30 fps). 600 ticks = 20 seconds = the
// vanilla SM64 cap timer; matches the feel of grabbing a Wing/Metal/Vanish
// Cap powerup box.
#define SM64_CAP_TIME_FRAMES 600

// All three OOT spells map to SM64 caps via libsm64's interact_cap API:
//   Din's Fire     → Vanish Cap   (Mario translucent, phases through cage walls)
//   Nayru's Love   → Metal Cap    (invincible, sinks in water, metallic SFX)
//   Farore's Wind  → Wing Cap     (triple-jump → flap to fly)
// Single press = one cast = ~20 seconds of cap. libsm64's internal timer
// handles expiry, music, and visual transitions. No per-frame Player field
// mutation needed.
static void MarioItem_UseSpell(PlayState* play, Player* player, u8 started, u8 spellType) {
    if (started != 1) return;
    // No cooldown gating for spells — user explicitly wants override-on-press
    // every time. The patched libsm64 interact_cap clears any previous cap
    // type before applying the new one, so re-cast cleanly transitions
    // Vanish → Metal → Wing → etc. without timer wait.

    // DIRECT magic deduction — bypassing Magic_RequestChange. The vanilla
    // API runs through a state machine (IDLE → CONSUME_SETUP → CONSUME →
    // METER_FLASH_1 → RESET → IDLE) that takes ~1+ second to settle.
    // Until it returns to IDLE, RequestChange returns false and blocks new
    // casts — which the user observed as "can't re-cast until I reload the
    // scene". Log evidence: `Magic_RequestChange failed (state=3 ...)` was
    // returned for every press after the first cast.
    //
    // Direct deduction snaps the magic value, no flash/drain animation.
    // Matches the user's request: "solo cobra magia cómo las elemental
    // arrows por ejemplo" — single up-front charge, instant re-cast.
    if (gSaveContext.magic < sMarioSpellCost) {
        Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
        lusprintf(__FILE__, __LINE__, 2,
            "[SM64Items] Spell press: BLOCKED — not enough magic (have=%d need=%d)",
            gSaveContext.magic, sMarioSpellCost);
        return;
    }
    gSaveContext.magic -= sMarioSpellCost;
    // Force-reset the state machine so any lingering flash/reset cycle
    // from a previous cast doesn't leak. Without this, meter visuals can
    // lock in odd states.
    gSaveContext.magicState = MAGIC_STATE_IDLE;

    if (!p_sm64_mario_interact_cap || sSm64MarioId < 0) {
        // libsm64 export missing or Mario not yet created — magic was
        // consumed but cap can't activate. Silent fallback: spell is a no-op
        // for this cast.
        sMarioItemTimer = 10;
        return;
    }

    u32 capFlag = 0;
    s32 sfx = 0;
    switch (spellType) {
        case 1: // Din's Fire → Vanish Cap
            capFlag = SM64_MARIO_VANISH_CAP;
            sfx = NA_SE_PL_MAGIC_FIRE;
            break;
        case 2: // Nayru's Love → Metal Cap
            capFlag = SM64_MARIO_METAL_CAP;
            sfx = NA_SE_PL_MAGIC_SOUL_NORMAL;
            break;
        case 3: // Farore's Wind → Wing Cap
            capFlag = SM64_MARIO_WING_CAP;
            sfx = NA_SE_PL_MAGIC_WIND_NORMAL;
            break;
        default:
            return;
    }

    // Snapshot flags BEFORE the call so we can diagnose whether libsm64's
    // patched interact_cap actually clears + applies the new cap. If the
    // post-call flags don't have only the new bit, the libsm64 rebuild
    // didn't take or there's another path overriding.
    u32 flagsBefore = sSm64OutState.flags;
    p_sm64_mario_interact_cap(sSm64MarioId, capFlag, SM64_CAP_TIME_FRAMES, 1);
    Sfx_PlaySfxCentered(sfx);
    sMarioUsedSpell = spellType;
    // No cooldown — user wants immediate re-cast / override on every press.

    lusprintf(__FILE__, __LINE__, 2,
        "[SM64Items] Cap cast: type=%u capFlag=0x%X flagsBefore=0x%08X (V=%d M=%d W=%d) magic=%d→%d",
        spellType, capFlag, flagsBefore,
        (flagsBefore & SM64_MARIO_VANISH_CAP) ? 1 : 0,
        (flagsBefore & SM64_MARIO_METAL_CAP) ? 1 : 0,
        (flagsBefore & SM64_MARIO_WING_CAP) ? 1 : 0,
        gSaveContext.magic + sMarioSpellCost, gSaveContext.magic);
}

// =============================================================================
// Dispatcher — verbatim copy of z_en_partner.c:511-578 switch statement.
// =============================================================================

static void MarioItem_Use(u8 usedItem, u8 started, PlayState* play, Player* player) {
    if (sMarioUsedItem != 0xFF && sMarioItemTimer <= 0) {
        switch (usedItem) {
            case ITEM_STICK:        MarioItem_UseDekuStick(play, player, started); break;
            case ITEM_BOMB:         MarioItem_UseBombs(play, player, started);     break;
            case ITEM_BOMBCHU:      MarioItem_UseBombchus(play, player, started);  break;
            case ITEM_NUT:          MarioItem_UseNuts(play, player, started);      break;
            case ITEM_BOW:                MarioItem_UseBow(play, player, started, 0); break;
            case ITEM_ARROW_FIRE:                                                       /* 0x04 — never appears in buttonItems but kept for completeness */
            case ITEM_BOW_ARROW_FIRE:     MarioItem_UseBow(play, player, started, 1); break;  /* 0x38 — actual stored value when user equips fire arrows on C-slot */
            case ITEM_ARROW_ICE:
            case ITEM_BOW_ARROW_ICE:      MarioItem_UseBow(play, player, started, 2); break;  /* 0x39 */
            case ITEM_ARROW_LIGHT:
            case ITEM_BOW_ARROW_LIGHT:    MarioItem_UseBow(play, player, started, 3); break;  /* 0x3A */
            case ITEM_SLINGSHOT:    MarioItem_UseSlingshot(play, player, started); break;
            case ITEM_OCARINA_FAIRY:
            case ITEM_OCARINA_TIME: MarioItem_UseOcarina(play, player, started);   break;
            case ITEM_HOOKSHOT:
            case ITEM_LONGSHOT:     MarioItem_UseHookshot(play, player, started);  break;
            case ITEM_DINS_FIRE:    MarioItem_UseSpell(play, player, started, 1);  break;
            case ITEM_NAYRUS_LOVE:  MarioItem_UseSpell(play, player, started, 2);  break;
            case ITEM_FARORES_WIND: MarioItem_UseSpell(play, player, started, 3);  break;
            case ITEM_HAMMER:       MarioItem_UseHammer(play, player, started);    break;
            case ITEM_BOOMERANG:    MarioItem_UseBoomerang(play, player, started); break;
            case ITEM_LENS:         MarioItem_UseLens(play, player, started);      break;
            case ITEM_BEAN:         MarioItem_UseBeans(play, player, started);     break;
        }
    }

    if (started == 0) {
        sMarioUsedItem = 0xFF;
    }
}

// =============================================================================
// Main entry — input state machine. Port of z_en_partner.c:681-737.
// =============================================================================

void Sm64Mario_HandleItems(PlayState* play, Player* player) {
    if (play == NULL || player == NULL) return;

    // Cap-expiry detector — when the SM64 cap (Vanish/Metal/Wing) drops out
    // of sSm64OutState.flags, libsm64's internal cap timer expired. Clear
    // sMarioUsedSpell so the next press can fire a fresh cast. Without this,
    // sMarioUsedSpell stayed set forever after the first cast (only reset
    // by Sm64Mario_ItemsReset on detransform / scene change), which is why
    // the user could only cast one cap per scene.
    {
        u32 capFlags = (1U << 1) | (1U << 2) | (1U << 3);  // VANISH|METAL|WING
        if (sMarioUsedSpell != 0 && (sSm64OutState.flags & capFlags) == 0) {
            sMarioUsedSpell = 0;
        }
    }

    // Cooldown tick.
    if (sMarioItemTimer > 0) {
        sMarioItemTimer--;
    }

    Input* input = &play->state.input[0];

    // Cutscene-entry safety: cancel any in-flight item use cleanly so a held
    // Din's Fire doesn't leak ivanDamageMultiplier=2 across the cutscene.
    if (Player_InCsMode(play)) {
        if (sMarioUsedItem != 0xFF) {
            MarioItem_Use(sMarioUsedItem, 0, play, player);
        }
        sMarioUsedItem = 0xFF;
        sMarioItemTimer = 10;
        return;
    }

    static u16 sMarioPartnerButtons[7] = {
        BTN_CLEFT, BTN_CDOWN, BTN_CRIGHT, BTN_DUP, BTN_DDOWN, BTN_DLEFT, BTN_DRIGHT
    };
    u8 buttonMax = 3;
    if (CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0) != 0) {
        buttonMax = ARRAY_COUNT(gSaveContext.equips.cButtonSlots);
    }

    u8 pressed  = 0;
    u8 current  = 0;
    u8 released = 0;

    if (sMarioUsedItem == 0xFF && sMarioItemTimer <= 0) {
        for (u8 i = 0; i < buttonMax; i++) {
            if (CHECK_BTN_ALL(input->press.button, sMarioPartnerButtons[i])) {
                sMarioUsedItem = gSaveContext.equips.buttonItems[i + 1];
                sMarioUsedItemButton = i;
                pressed = 1;
                break;
            }
        }
    }

    if (sMarioUsedItem != 0xFF) {
        for (u8 i = 0; i < buttonMax; i++) {
            if (CHECK_BTN_ALL(input->cur.button, sMarioPartnerButtons[i]) && sMarioUsedItemButton == i) {
                current = 1;
            }
            if (CHECK_BTN_ALL(input->rel.button, sMarioPartnerButtons[i]) && sMarioUsedItemButton == i) {
                released = 1;
            }
        }
    }

    if (pressed) {
        MarioItem_Use(sMarioUsedItem, 1, play, player);
    } else if (released) {
        MarioItem_Use(sMarioUsedItem, 0, play, player);
        sMarioUsedItemButton = 0xFF;
    } else if (current) {
        MarioItem_Use(sMarioUsedItem, 2, play, player);
    }
}

// =============================================================================
// Reset — called from Sm64Mario_Reset on detransform / scene-suspend / CVAR off.
// =============================================================================

void Sm64Mario_ItemsReset(void) {
    // Best-effort kill of leaked hookshot target. No play context here, so
    // we use Actor_Kill which only writes update=NULL — safe if the actor
    // pool already collected it (it'll be a no-op write to whatever's there).
    if (sMarioHookshotTarget != NULL) {
        if (sMarioHookshotTarget->update != NULL) {
            Actor_Kill(sMarioHookshotTarget);
        }
        sMarioHookshotTarget = NULL;
    }

    sMarioUsedItem = 0xFF;
    sMarioUsedItemButton = 0xFF;
    sMarioUsedSpell = 0;
    sMarioItemTimer = 0;
    sMarioMagicTimer = 0;
    sMarioStickDamageTimer = 0;
    sMarioLensActive = 0;

    sMarioStickCollider.base.atFlags &= ~(AT_ON | AT_HIT);

    // Restore the Player struct fields that UseSpell mutated. If we leave
    // ivanFloating=1 across detransform, Link will float infinitely on
    // hover boots once Mario re-transforms or Ivan/coop mode runs.
    Player* player = NULL;
    // We'd need play context to get the player — defer the field reset to
    // the Sm64Mario_Reset caller in sm64_mario.c (it has access to play
    // via the suspend cascade, though only indirectly). For safety, call
    // sites that need the fields back to defaults can do it explicitly.
    (void)player;
}

// Externally callable variant when we have a Player* (called from
// Sm64Mario_Reset's caller chain when a Player is available).
void Sm64Mario_ItemsResetWithPlayer(Player* player) {
    Sm64Mario_ItemsReset();
    if (player != NULL) {
        player->ivanDamageMultiplier = 1;
        player->ivanFloating = 0;
        player->hoverBootsTimer = 0;
    }
}
