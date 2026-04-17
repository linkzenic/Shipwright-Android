// ---------------------------------------------------------------------------
// Tomahawk Throw — C-Up to aim (FirstPerson camera + reticle), B to throw
//
// Flow:
// 1. Press C-Up → FirstPerson aim mode (same as Ice Rod / bow)
// 2. Aim with stick, reticle drawn on screen
// 3. Press B → spawn En_Boom with params=99 (tomahawk arc) in aimed direction
// 4. Axe flies, hits, returns to Link
// 5. Exit aim automatically after throw, or press C-Up again to cancel
// ---------------------------------------------------------------------------

// FirstPerson aim functions (camera_helper.c, compiled separately)
extern void FirstPerson_Init(Player* player, PlayState* play);
extern void FirstPerson_Update(Player* player, PlayState* play);
extern void FirstPerson_Exit(Player* player, PlayState* play);
extern void FirstPerson_DrawReticle(Player* player, PlayState* play, f32 range, u8 r, u8 g, u8 b);

static void IKAxe_UpdateThrow(Player* player, PlayState* play) {
    u8 pressedCUp = CHECK_BTN_ALL(play->state.input[0].press.button, BTN_CUP);
    u8 pressedB = CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B);

    switch (sIKAxeThrowState) {
        case IKAXE_THROW_IDLE:
            // C-Up → enter aim mode
            if (pressedCUp && player->meleeWeaponState == 0 &&
                !(player->stateFlags1 & (PLAYER_STATE1_IN_WATER | PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_IN_CUTSCENE |
                                         PLAYER_STATE1_DEAD | PLAYER_STATE1_BOOMERANG_THROWN))) {
                sIKAxeThrowState = IKAXE_THROW_CHARGING;
                sIKAxeAimActive = 1;
                FirstPerson_Init(player, play);
                player->linearVelocity = 0.0f;
            }
            break;

        case IKAXE_THROW_CHARGING:
            // Lock movement, update aim camera
            player->linearVelocity = 0.0f;
            player->actor.speedXZ = 0.0f;
            FirstPerson_Update(player, play);

            // B → throw tomahawk in aimed direction
            if (pressedB) {
                s16 yaw = FirstPerson_GetAimYaw(player);
                s16 pitch = FirstPerson_GetAimPitch(player);

                f32 posX = player->actor.world.pos.x + 20.0f * Math_SinS(yaw);
                f32 posZ = player->actor.world.pos.z + 20.0f * Math_CosS(yaw);

                EnBoom* axe =
                    (EnBoom*)Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOOM, posX, player->actor.world.pos.y + 40.0f,
                                         posZ, pitch, yaw, 0, IKAXE_THROW_PARAMS);

                if (axe != NULL) {
                    axe->moveTo = NULL;
                    axe->returnTimer = IKAXE_THROW_RETURN;
                    player->stateFlags1 |= PLAYER_STATE1_BOOMERANG_THROWN;
                    player->boomerangActor = &axe->actor;
                    sIKAxeThrown = 1;
                    sIKAxeThrowState = IKAXE_THROW_FLYING;
                    Audio_PlaySoundGeneral(NA_SE_IT_SWORD_SWING_HARD, &player->actor.world.pos, 4,
                                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                           &gSfxDefaultReverb);
                }

                FirstPerson_Exit(player, play);
                sIKAxeAimActive = 0;
            }
            // Any button press EXCEPT B, or Z held → cancel aim
            else if ((play->state.input[0].press.button & ~BTN_B) ||
                     CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_Z)) {
                FirstPerson_Exit(player, play);
                sIKAxeAimActive = 0;
                sIKAxeThrowState = IKAXE_THROW_IDLE;
            }
            break;

        case IKAXE_THROW_FLYING:
            player->meleeWeaponState = 0;
            if (!(player->stateFlags1 & PLAYER_STATE1_BOOMERANG_THROWN)) {
                sIKAxeThrown = 0;
                sIKAxeThrowState = IKAXE_THROW_IDLE;
                sIKAxeAimActive = 0;
            }
            break;
    }
}

// Draw reticle during aim (called from ExtEquip_DrawDispatch or similar)
void IKAxe_DrawReticle(Player* player, PlayState* play) {
    if (sIKAxeThrowState != IKAXE_THROW_CHARGING)
        return;
    // Orange reticle
    FirstPerson_DrawReticle(player, play, 0.0f, 255, 150, 50);
}
