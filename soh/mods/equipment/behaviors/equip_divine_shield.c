/**
 * equip_divine_shield.c - Divine Shield (Ext Shield 1)
 *
 * Behavior: Deku Shield (COLTYPE_WOOD) that doesn't burn from fire.
 * Parry: If shield blocks within first 10 frames of raising, stun ALL nearby
 *        enemies like a Deku Nut (freezeTimer on every ACTORCAT_ENEMY).
 *
 * Included by ext_equip_behavior.c (unity build from extended_equipment.c -> z_player.c)
 */

// ---------------------------------------------------------------------------
// Parry tracking
// ---------------------------------------------------------------------------
static s16 sDivineShieldRaiseTimer = 0;
static u8 sDivineShieldWasShielding = 0;

static void DivineShield_Behavior(Player* player, PlayState* play) {
    u8 isShielding = (player->stateFlags1 & PLAYER_STATE1_SHIELDING) ? 1 : 0;

    // Rising edge: reset timer when shield is first raised
    if (isShielding && !sDivineShieldWasShielding) {
        sDivineShieldRaiseTimer = 0;
    }
    sDivineShieldWasShielding = isShielding;

    if (isShielding) {
        sDivineShieldRaiseTimer++;
    }
}

// ---------------------------------------------------------------------------
// Called DIRECTLY from func_808382DC in z_player.c the EXACT moment a shield
// bounce is detected (AC_BOUNCED on shieldQuad). This runs BEFORE
// Collider_ResetQuadAC clears the flags, so everything is still valid.
// ---------------------------------------------------------------------------
void DivineShield_OnShieldBlock(Player* player, PlayState* play) {
    if (!ExtEquip_IsEnabled()) return;
    if (ExtEquip_GetCurrent(EQUIP_TYPE_SHIELD) != 1) return;

    // Perfect parry: within first 10 frames of raising shield
    if (sDivineShieldRaiseTimer <= 10) {
        // Freeze ALL enemies + VFX on each one
        Actor* actor;
        actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].head;
        while (actor != NULL) {
            actor->freezeTimer = 40;

            // Blue/white color filter (like ice arrow hit) — 0x0000 = blue, 0x8000 = white
            Actor_SetColorFilter(actor, 0x0000, 0xF8, 0x0000, 40);

            // Ice crystal particles around the enemy
            Vec3f icePos;
            Vec3f iceVel = { 0.0f, 1.0f, 0.0f };
            Vec3f iceAccel = { 0.0f, 0.0f, 0.0f };
            for (s32 i = 0; i < 6; i++) {
                icePos.x = actor->world.pos.x + Rand_CenteredFloat(60.0f);
                icePos.y = actor->world.pos.y + 20.0f + Rand_ZeroFloat(40.0f);
                icePos.z = actor->world.pos.z + Rand_CenteredFloat(60.0f);
                iceVel.x = Rand_CenteredFloat(3.0f);
                iceVel.y = Rand_ZeroFloat(2.0f) + 1.0f;
                iceVel.z = Rand_CenteredFloat(3.0f);
                // White-blue sparkles: prim white, env light blue
                EffectSsKiraKira_SpawnSmall(play, &icePos, &iceVel, &iceAccel,
                                            &(Color_RGBA8){ 200, 220, 255, 255 },
                                            &(Color_RGBA8){ 100, 150, 255, 0 });
            }

            actor = actor->next;
        }

        // Parry sound
        Audio_PlaySoundGeneral(NA_SE_IT_SHIELD_REFLECT_SW, &player->actor.world.pos, 4,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultReverb);
    }
}

// ---------------------------------------------------------------------------
// Shield type override — called from z_player_lib.c
// Returns 1 if Divine Shield should use COLTYPE_WOOD
// ---------------------------------------------------------------------------
u8 DivineShield_IsWoodType(void) {
    if (!ExtEquip_IsEnabled()) return 0;
    return (ExtEquip_GetCurrent(EQUIP_TYPE_SHIELD) == 1) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Fire immunity — called from z_player.c func_8083819C
// Returns 1 if fire should NOT destroy the shield
// ---------------------------------------------------------------------------
u8 DivineShield_IsFireproof(void) {
    if (!ExtEquip_IsEnabled()) return 0;
    return (ExtEquip_GetCurrent(EQUIP_TYPE_SHIELD) == 1) ? 1 : 0;
}
