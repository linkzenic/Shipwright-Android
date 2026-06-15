/**
 * item_dekuleaf.c - Deku Leaf from Wind Waker
 *
 * Controls:
 *   C Button (ground): Swing leaf to create wind gust, pushes objects/enemies
 *   C Button (air):    Hold to glide, consumes magic over time
 *
 * Features:
 *   - Wind blow pushes enemies and certain objects
 *   - Gliding reduces fall speed and allows horizontal movement
 *   - Uses skeletal animation (39 frames) for blow attack
 */

#include "z64.h"
#include "item_dekuleaf.h"
#include "../custom_items.h"
#include "../helpers/movement_helper.h"
#include "../helpers/equip_helper.h"
#include "../helpers/fx_helper.h"
#include "transformation_masks/transformation_masks.h"
#include "transformation_masks/assets/mm_asset_loader.h"
#include "sound_translator/mm_sfx_ids.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "objects/gameplay_keep/gameplay_keep.h"

// Include animation
#include "../anim/deku_leaf/dekuleaf_anim.c"
#include "../anim/deku_leaf/dekuleaf_anim_data.c"

static s8 sDekuLeafPrevInvinc = 0;
static u8 sDekuLeafBlowEffectFired = 0;

// MM Deku SFX play helper — 100% MM verbatim.
// MM calls `Player_PlaySfx` (z_actor.c:2355) for FLOWER_OPEN/CLOSE/STRUGGLE
// and `Audio_PlaySfx_AtPosWithTimer` (audio/code_8019AF00.c:4347) for
// FLOWER_ROLL. Both ultimately invoke `AudioSfx_PlaySfx` with:
//     freqScale = sSfxAdjustedFreq  (= 1.0f in MM; "modified in OoT, but
//                                     remains 1.0f in MM" per the comment
//                                     at code_8019AF00.c:4343)
//     volume    = gSfxDefaultFreqAndVolScale (= 1.0f, sfx.c:78)
//     reverb    = gSfxDefaultReverb          (= 0,    sfx.c:82)
//
// So MM is DRY: no pitch shift, no extra reverb, no volume boost. We route
// through MmSfx_PlayAtPos which calls MmSfx_PlayEx with all three params as
// nullptr (asset_loader.cpp:3871) — the bank engine falls back to its own
// defaults that match MM's. Any remaining "feels off" perception against
// real MM is now a bridge / sample-bank issue, not a call-site issue.
static void DekuLeaf_PlayMmSfx(u16 sfxId, Vec3f* pos) {
    MmSfx_PlayAtPos(sfxId, pos);
}

static void DekuLeaf_Stop(Player* p, PlayState* play) {
    if (!dlActive)
        return;

    u8 wasGliding = dlGliding;

    dlActive = 0;
    dlMode = DEKULEAF_MODE_INACTIVE;
    dlGliding = 0;
    dlBlowing = 0;
    dlAnimTimer = 0;
    dlBlowTimer = 0;
    sDekuLeafBlowEffectFired = 0;

    // Stop looping sounds — legacy OOT wind + the MM Deku propeller hum.
    Audio_StopSfxById(DEKULEAF_SOUND_WIND);
    Audio_StopSfxById(DEKULEAF_SOUND_BLOW);
    MmSfx_Stop(MM_NA_SE_IT_DEKUNUTS_FLOWER_ROLL);

    // MM verbatim: closing the Deku flower at end-of-flight fires a one-shot
    // "flower close" SFX (mm_player_form.cpp:8684, 2Ship z_player.c:6401).
    // Only fire when leaving a glide — blow-mode stop shouldn't play it.
    if (wasGliding) {
        DekuLeaf_PlayMmSfx(MM_NA_SE_IT_DEKUNUTS_FLOWER_CLOSE, &p->actor.projectedPos);
    }

    p->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
    ItemEquip_PlayUnequipSFX(play, p);
}

static void DekuLeaf_StartGlide(Player* p, PlayState* play) {
    dlActive = 1;
    dlMode = DEKULEAF_MODE_GLIDING;
    dlGliding = 1;
    dlBlowing = 0;
    dlAnimTimer = 0;

    // MM's FLOWER_OPEN fires during the Deku flower LAUNCH sequence (Link
    // pops out of the ground bud). For the Deku Leaf glide context Link
    // isn't launching from a flower — the equip SFX alone covers the entry.
    ItemEquip_PlayEquipSFX(play, p);
}

static void DekuLeaf_StartBlow(Player* p, PlayState* play) {
    if (gSaveContext.magic < DEKULEAF_BLOW_MAGIC_COST)
        return;

    dlActive = 1;
    dlMode = DEKULEAF_MODE_BLOWING;
    dlGliding = 0;
    dlBlowing = 1;
    dlAnimTimer = 0;
    dlBlowTimer = 0;
    sDekuLeafBlowEffectFired = 0;

    // Start the skeletal animation on upperSkelAnime
    LinkAnimation_PlayOnce(play, &p->upperSkelAnime, &gDekuLeafBlowAnim);

    ItemEquip_PlayEquipSFX(play, p);
}

static void DekuLeaf_UpdateGlide(Player* p, PlayState* play) {
    if (p->skelAnime.animation != &DEKULEAF_ANIM_GLIDE) {
        LinkAnimation_Change(play, &p->skelAnime, &DEKULEAF_ANIM_GLIDE, 1.0f, 0.0f,
                             Animation_GetLastFrame(&DEKULEAF_ANIM_GLIDE), ANIMMODE_LOOP, -4.0f);
    }

    if (p->actor.velocity.y < DEKULEAF_FALL_VELOCITY) {
        p->actor.velocity.y = DEKULEAF_FALL_VELOCITY;
    }

    if (play->gameplayFrames % DEKULEAF_GLIDE_MAGIC_INTERVAL == 0) {
        ItemMagic_Consume(play, DEKULEAF_GLIDE_MAGIC_COST);
    }

    // === MM Deku-flower propeller hum ===
    // Source SFX: mm_player_form.cpp:9100-9129 → MM_NA_SE_IT_DEKUNUTS_FLOWER_ROLL.
    //
    // In MM Deku flight the cadence between pulses tracks the angular speed of
    // the flower's petals (range 2..6 frames). For human-form Deku Leaf there's
    // no petalSpeed equivalent — the leaf either is open and gliding or it
    // isn't, with no acceleration profile. Using fall velocity as a proxy made
    // the cadence dance during the velocity ramp-up at glide start and felt
    // "accelerated" mid-glide. We just hold MM's SLOW end of the range (6
    // frames) for the whole glide — discrete, stable pulses that don't pile
    // up on each other and don't shift tempo unexpectedly.
    {
        static s32 sPropellerTimer = 1;
        sPropellerTimer--;
        if (sPropellerTimer <= 0) {
            DekuLeaf_PlayMmSfx(MM_NA_SE_IT_DEKUNUTS_FLOWER_ROLL, &p->actor.projectedPos);
            sPropellerTimer = 6;
        }
    }

    // === MM Deku flutter struggle ===
    // Source: mm_player_form.cpp:9082, 2Ship z_player.c:19194.
    // In MM the flutter anim fires this on frame 6 of its cycle. We don't have
    // a flutter anim driving us, so we fire it on a fixed ~24-frame cadence
    // (rough match to MM flutter loop length) starting after a small delay so
    // it doesn't double-up with FLOWER_OPEN.
    if ((play->gameplayFrames - dlAnimTimer) % 24 == 18) {
        DekuLeaf_PlayMmSfx(MM_NA_SE_PL_DEKUNUTS_STRUGGLE, &p->actor.projectedPos);
    }
}

static void DekuLeaf_SpawnWindParticles(Player* p, PlayState* play) {
    Vec3f windPos = p->actor.world.pos;
    s16 facingYaw = p->actor.shape.rot.y;

    windPos.y += 30.0f;

    FX_SpawnWindBlow(play, &windPos, facingYaw, DEKULEAF_BLOW_RANGE);
}

static void DekuLeaf_BlowEnemies(Player* p, PlayState* play) {
    Vec3f blowOrigin = p->actor.world.pos;
    s16 facingYaw = p->actor.shape.rot.y;

    blowOrigin.y += 30.0f;

    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].head;
    while (actor != NULL) {
        if (actor->update != NULL) {
            f32 dx = actor->world.pos.x - blowOrigin.x;
            f32 dz = actor->world.pos.z - blowOrigin.z;
            f32 dist = sqrtf(SQ(dx) + SQ(dz));
            f32 dy = fabsf(actor->world.pos.y - blowOrigin.y);

            if (dist < DEKULEAF_BLOW_RANGE && dy < 60.0f) {
                s16 angleToEnemy = Math_Atan2S(dx, dz);
                s16 angleDiff = angleToEnemy - facingYaw;

                if (angleDiff > -0x4000 && angleDiff < 0x4000) {
                    f32 forceMult = 1.0f - (dist / DEKULEAF_BLOW_RANGE);
                    f32 force = DEKULEAF_BLOW_FORCE * forceMult;

                    f32 norm = dist > 0.1f ? dist : 0.1f;
                    actor->world.pos.x += (dx / norm) * force;
                    actor->world.pos.z += (dz / norm) * force;

                    if (actor->bgCheckFlags & BGCHECKFLAG_GROUND) {
                        actor->world.pos.y += 3.0f * forceMult;
                    }
                }
            }
        }
        actor = actor->next;
    }
}

// ============================================================================
// UPPER ACTION - Drives the blow animation via upperSkelAnime
// ============================================================================

s32 Player_UpperAction_DekuLeaf(Player* player, PlayState* play) {
    if (!dlActive)
        return 0;
    if (!dlBlowing)
        return 0;

    // Update the skeletal animation
    if (LinkAnimation_Update(play, &player->upperSkelAnime)) {
        // Animation finished
        DekuLeaf_Stop(player, play);
        return 0;
    }

    // Track frame
    dlAnimTimer++;

    // Stop movement during blow animation
    player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
    player->actor.speedXZ = 0.0f;
    player->linearVelocity = 0.0f;

    // Fire the wind blow effect at the designated frame
    if (!sDekuLeafBlowEffectFired && dlAnimTimer == DEKULEAF_BLOW_EFFECT_FRAME) {
        sDekuLeafBlowEffectFired = 1;
        ItemMagic_Consume(play, DEKULEAF_BLOW_MAGIC_COST);
        Player_PlaySfx(player, DEKULEAF_SOUND_BLOW);
        dlBlowTimer = DEKULEAF_BLOW_DURATION;
    }

    // During active blow frames, spawn wind and push enemies
    if (dlAnimTimer >= DEKULEAF_ATTACK_FRAME_START && dlAnimTimer <= DEKULEAF_ATTACK_FRAME_END) {
        if (play->gameplayFrames % DEKULEAF_WIND_SPAWN_RATE == 0) {
            DekuLeaf_SpawnWindParticles(player, play);
        }
        DekuLeaf_BlowEnemies(player, play);
    }

    // Return 1 to indicate upper body is busy (use upperSkelAnime)
    return 1;
}

// ============================================================================
// MAIN HANDLER
// ============================================================================

void Handle_DekuLeaf(Player* p, PlayState* play) {
    // Deku form has its own Deku Leaf handling (flower burrow + flight)
    if (TransformMasks_IsTransformed() && MmPlayer_GetForm() == MM_PLAYER_FORM_DEKU)
        return;

    ItemInputState in;
    ItemInput_Update(&in, ITEM_DEKU_LEAF, p, play);

    if (!in.wasEquipped) {
        if (dlActive)
            DekuLeaf_Stop(p, play);
        return;
    }

    if (ItemInput_IsBlocked(p, play)) {
        if (dlActive)
            DekuLeaf_Stop(p, play);
        return;
    }

    if (ItemInput_CheckDamage(p, &sDekuLeafPrevInvinc)) {
        DekuLeaf_Stop(p, play);
        return;
    }

    // If blowing, the upper action handles everything
    if (dlMode == DEKULEAF_MODE_BLOWING) {
        return;
    }

    if (dlMode == DEKULEAF_MODE_GLIDING) {
        if (Movement_IsOnGround(p)) {
            DekuLeaf_Stop(p, play);
            return;
        }

        if (!in.isHeld || gSaveContext.magic <= 0 || in.otherButtonPressed) {
            DekuLeaf_Stop(p, play);
            return;
        }

        DekuLeaf_UpdateGlide(p, play);
        return;
    }

    if (!dlActive && in.isPressed) {
        if (!Movement_IsOnGround(p)) {
            if (gSaveContext.magic > 0) {
                DekuLeaf_StartGlide(p, play);
            }
        } else {
            if (p->stateFlags1 & PLAYER_STATE1_IN_WATER)
                return;
            if (p->meleeWeaponState != 0)
                return;
            DekuLeaf_StartBlow(p, play);
        }
        return;
    }

    if (dlMode == DEKULEAF_MODE_GLIDING && !in.isHeld) {
        DekuLeaf_Stop(p, play);
    }
}
