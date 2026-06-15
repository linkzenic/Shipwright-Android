#include "z_arms_hook.h"
#include "objects/object_link_boy/object_link_boy.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern u8 TransformMasks_IsTransformed(void);

#define FLAGS (ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED)

void ArmsHook_Init(Actor* thisx, PlayState* play);
void ArmsHook_Destroy(Actor* thisx, PlayState* play);
void ArmsHook_Update(Actor* thisx, PlayState* play);
void ArmsHook_Draw(Actor* thisx, PlayState* play);

void ArmsHook_Wait(ArmsHook* this, PlayState* play);
void ArmsHook_Shoot(ArmsHook* this, PlayState* play);

const ActorInit Arms_Hook_InitVars = {
    ACTOR_ARMS_HOOK,
    ACTORCAT_ITEMACTION,
    FLAGS,
    OBJECT_LINK_BOY,
    sizeof(ArmsHook),
    (ActorFunc)ArmsHook_Init,
    (ActorFunc)ArmsHook_Destroy,
    (ActorFunc)ArmsHook_Update,
    (ActorFunc)ArmsHook_Draw,
    NULL,
};

static ColliderQuadInit sQuadInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_PLAYER,
        AC_NONE,
        OC1_NONE,
        OC2_TYPE_PLAYER,
        COLSHAPE_QUAD,
    },
    {
        ELEMTYPE_UNK2,
        { 0x00000080, 0x00, 0x01 },
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_ON | TOUCH_NEAREST | TOUCH_SFX_NORMAL,
        BUMP_NONE,
        OCELEM_NONE,
    },
    { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
};

static Vec3f sUnusedVec1 = { 0.0f, 0.5f, 0.0f };
static Vec3f sUnusedVec2 = { 0.0f, 0.5f, 0.0f };

static Color_RGB8 sUnusedColors[] = {
    { 255, 255, 100 },
    { 255, 255, 50 },
};

static Vec3f D_80865B70 = { 0.0f, 0.0f, 0.0f };
static Vec3f D_80865B7C = { 0.0f, 0.0f, 900.0f };
static Vec3f D_80865B88 = { 0.0f, 500.0f, -3000.0f };
static Vec3f D_80865B94 = { 0.0f, -500.0f, -3000.0f };
static Vec3f D_80865BA0 = { 0.0f, 500.0f, 1200.0f };
static Vec3f D_80865BAC = { 0.0f, -500.0f, 1200.0f };

void ArmsHook_SetupAction(ArmsHook* this, ArmsHookActionFunc actionFunc) {
    this->actionFunc = actionFunc;
}

void ArmsHook_Init(Actor* thisx, PlayState* play) {
    ArmsHook* this = (ArmsHook*)thisx;

    Collider_InitQuad(play, &this->collider);
    Collider_SetQuad(play, &this->collider, &this->actor, &sQuadInit);
    ArmsHook_SetupAction(this, ArmsHook_Wait);
    this->unk_1E8 = this->actor.world.pos;
}

void ArmsHook_Destroy(Actor* thisx, PlayState* play) {
    ArmsHook* this = (ArmsHook*)thisx;

    if (this->grabbed != NULL) {
        this->grabbed->flags &= ~ACTOR_FLAG_HOOKSHOT_ATTACHED;
    }
    Collider_DestroyQuad(play, &this->collider);
}

void ArmsHook_Wait(ArmsHook* this, PlayState* play) {
    if (this->actor.parent == NULL) {
        Player* player = GET_PLAYER(play);
        // get correct timer length for hookshot or longshot
        s32 length = ((player->heldItemAction == PLAYER_IA_HOOKSHOT) ? 13 : 26) *
                     CVarGetFloat(CVAR_CHEAT("HookshotReachMultiplier"), 1.0f);

        ArmsHook_SetupAction(this, ArmsHook_Shoot);
        Actor_SetProjectileSpeed(&this->actor, 20.0f);
        this->actor.parent = &GET_PLAYER(play)->actor;
        this->timer = length;

        // Clawshot Bullet Time — clear the "last hit was surface" flag at
        // every new shot so the previous hit doesn't accidentally re-trigger
        // bullet time on a miss.
        extern void ClawshotBT_NoteShotFired(void);
        ClawshotBT_NoteShotFired();
    }
}

void ArmsHook_PullPlayer(ArmsHook* this) {
    this->actor.child = this->actor.parent;
    this->actor.parent->parent = &this->actor;
}

s32 ArmsHook_AttachToPlayer(ArmsHook* this, Player* player) {
    player->actor.child = &this->actor;
    player->heldActor = &this->actor;
    if (this->actor.child != NULL) {
        player->actor.parent = NULL;
        this->actor.child = NULL;
        return true;
    }
    return false;
}

void ArmsHook_DetachHookFromActor(ArmsHook* this) {
    if (this->grabbed != NULL) {
        this->grabbed->flags &= ~ACTOR_FLAG_HOOKSHOT_ATTACHED;
        this->grabbed = NULL;
    }
}

s32 ArmsHook_CheckForCancel(ArmsHook* this) {
    Player* player = (Player*)this->actor.parent;

    if (Player_HoldsHookshot(player)) {
        if ((player->itemAction != player->heldItemAction) || (player->actor.flags & ACTOR_FLAG_TALK) ||
            ((player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_DAMAGED)))) {
            this->timer = 0;
            ArmsHook_DetachHookFromActor(this);
            Math_Vec3f_Copy(&this->actor.world.pos, &player->unk_3C8);
            return 1;
        }
    }
    return 0;
}

void ArmsHook_AttachHookToActor(ArmsHook* this, Actor* actor) {
    actor->flags |= ACTOR_FLAG_HOOKSHOT_ATTACHED;
    this->grabbed = actor;
    Math_Vec3f_Diff(&actor->world.pos, &this->actor.world.pos, &this->grabbedDistDiff);
}

void ArmsHook_Shoot(ArmsHook* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    Actor* touchedActor;
    Actor* grabbed;
    Vec3f bodyDistDiffVec;
    Vec3f newPos;
    f32 bodyDistDiff;
    f32 phi_f16;
    DynaPolyActor* dynaPolyActor;
    f32 sp94;
    f32 sp90;
    s32 pad;
    CollisionPoly* poly;
    s32 bgId;
    Vec3f sp78;
    Vec3f prevFrameDiff;
    Vec3f sp60;
    f32 sp5C;
    f32 sp58;
    f32 velocity;
    s32 pad1;

    if ((this->actor.parent == NULL) || (!Player_HoldsHookshot(player))) {
        ArmsHook_DetachHookFromActor(this);
        Actor_Kill(&this->actor);
        return;
    }

    func_8002F8F0(&player->actor, NA_SE_IT_HOOKSHOT_CHAIN - SFX_FLAG);
    ArmsHook_CheckForCancel(this);

    if ((this->timer != 0) && (this->collider.base.atFlags & AT_HIT) &&
        (this->collider.info.atHitInfo->elemType != ELEMTYPE_UNK4)) {
        touchedActor = this->collider.base.at;
        // Twilight Upgrade — Clawshot mode: in clawshot mode we want to grab
        // ANY hit actor (enemies, etc.) and drag it back to Link, not just
        // ones flagged with HOOKSHOT_PULLS_*. Vanilla hookshot only stuns
        // most enemies because they don't have either flag — so the attach
        // never fires and the timer-0 dragback branch has nothing to move.
        // Relax the flag gate when clawshot mode is on; the bumperFlags
        // BUMP_HOOKABLE check is still required so only actually-hookshottable
        // surfaces respond. Stun damage continues to apply via the collider
        // path regardless.
        extern u8 TwilightUpgrade_IsClawshotActive(void);
        u8 clawshotInvert = TwilightUpgrade_IsClawshotActive();
        u8 hasPullFlag = (touchedActor->flags & (ACTOR_FLAG_HOOKSHOT_PULLS_ACTOR | ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER)) != 0;
        u8 isHookable = (this->collider.info.atHitInfo->bumperFlags & BUMP_HOOKABLE) != 0;
        if ((touchedActor->update != NULL) && (hasPullFlag || clawshotInvert)) {
            // Vanilla requires BUMP_HOOKABLE on the bumper. In clawshot mode we
            // also bypass that — many heavy enemies (Armos, Beamos, etc.) have
            // BUMP_ON without BUMP_HOOKABLE, so the vanilla path leaves them
            // un-attached and only stunned. Clawshot grabs them too.
            if (isHookable || clawshotInvert) {
                ArmsHook_AttachHookToActor(this, touchedActor);
                // Clawshot Bullet Time — this is an ACTOR hit, NOT a surface.
                // Note it so arrival doesn't trigger bullet time for enemy
                // pulls.
                extern void ClawshotBT_NoteHitActor(void);
                ClawshotBT_NoteHitActor();
                // When active, INVERT the pull direction on actors that would
                // normally pull Link (ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER). Instead
                // of attaching Link to the hook (which would yank Link forward),
                // we leave the actor flagged ATTACHED so the standard "drag
                // target back to Link" branch at the bottom of this function
                // moves the target each frame — exactly the Twilight Princess
                // clawshot grab.
                if (CHECK_FLAG_ALL(touchedActor->flags, ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER) && !clawshotInvert) {
                    ArmsHook_PullPlayer(this);
                }
            }
        }
        this->timer = 0;
        Audio_PlaySoundGeneral(NA_SE_IT_ARROW_STICK_CRE, &this->actor.projectedPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    } else if (DECR(this->timer) == 0) {
        grabbed = this->grabbed;
        if (grabbed != NULL) {
            if ((grabbed->update == NULL) || !CHECK_FLAG_ALL(grabbed->flags, ACTOR_FLAG_HOOKSHOT_ATTACHED)) {
                grabbed = NULL;
                this->grabbed = NULL;
            } else if (this->actor.child != NULL) {
                sp94 = Actor_WorldDistXYZToActor(&this->actor, grabbed);
                sp90 = sqrtf(SQ(this->grabbedDistDiff.x) + SQ(this->grabbedDistDiff.y) + SQ(this->grabbedDistDiff.z));
                Math_Vec3f_Diff(&grabbed->world.pos, &this->grabbedDistDiff, &this->actor.world.pos);
                if (50.0f < (sp94 - sp90)) {
                    ArmsHook_DetachHookFromActor(this);
                    grabbed = NULL;
                }
            }
        }

        bodyDistDiff = Math_Vec3f_DistXYZAndStoreDiff(&player->unk_3C8, &this->actor.world.pos, &bodyDistDiffVec);
        if (bodyDistDiff < 30.0f) {
            velocity = 0.0f;
            phi_f16 = 0.0f;
        } else {
            if (this->actor.child != NULL) {
                velocity = 30.0f;
            } else if (grabbed != NULL) {
                velocity = 50.0f;
            } else {
                velocity = 200.0f;
            }
            phi_f16 = bodyDistDiff - velocity;
            if (bodyDistDiff <= velocity) {
                phi_f16 = 0.0f;
            }
            velocity = phi_f16 / bodyDistDiff;
        }

        newPos.x = bodyDistDiffVec.x * velocity;
        newPos.y = bodyDistDiffVec.y * velocity;
        newPos.z = bodyDistDiffVec.z * velocity;

        if (this->actor.child == NULL) {
            if ((grabbed != NULL) && (grabbed->id == ACTOR_BG_SPOT06_OBJECTS)) {
                Math_Vec3f_Diff(&grabbed->world.pos, &this->grabbedDistDiff, &this->actor.world.pos);
                phi_f16 = 1.0f;
            } else {
                Math_Vec3f_Sum(&player->unk_3C8, &newPos, &this->actor.world.pos);
                if (grabbed != NULL) {
                    Math_Vec3f_Sum(&this->actor.world.pos, &this->grabbedDistDiff, &grabbed->world.pos);
                    // Clawshot mode — force the grabbed actor's own velocity
                    // to zero so its AI (gravity, walk, attack pursuit) can't
                    // fight against the drag we're applying via world.pos.
                    // Without this, enemies wobble in place or partially
                    // resist being pulled. Vanilla grabbable targets (pots,
                    // spider webs) are already inanimate so no harm there.
                    extern u8 TwilightUpgrade_IsClawshotActive(void);
                    if (TwilightUpgrade_IsClawshotActive()) {
                        grabbed->velocity.x = 0.0f;
                        grabbed->velocity.y = 0.0f;
                        grabbed->velocity.z = 0.0f;
                        grabbed->speedXZ = 0.0f;
                        grabbed->gravity = 0.0f;
                    }
                }
            }
        } else {
            Math_Vec3f_Diff(&bodyDistDiffVec, &newPos, &player->actor.velocity);
            player->actor.world.rot.x =
                Math_Atan2S(sqrtf(SQ(bodyDistDiffVec.x) + SQ(bodyDistDiffVec.z)), -bodyDistDiffVec.y);
        }

        if (phi_f16 < 50.0f) {
            ArmsHook_DetachHookFromActor(this);
            if (phi_f16 == 0.0f) {
                ArmsHook_SetupAction(this, ArmsHook_Wait);
                if (ArmsHook_AttachToPlayer(this, player)) {
                    Math_Vec3f_Diff(&this->actor.world.pos, &player->actor.world.pos, &player->actor.velocity);
                    // Clawshot Bullet Time — when the upgrade is active AND
                    // the last hit was a surface (not an enemy), enter bullet
                    // time at arrival. Skip the vanilla -20 downward kick so
                    // Link doesn't immediately drop off the anchor; the bullet
                    // time hold pins him in place. Returns 1 on entry.
                    extern u8 ClawshotBT_TryStartOnArrival(Player* player, PlayState* play);
                    if (!ClawshotBT_TryStartOnArrival(player, play)) {
                        player->actor.velocity.y -= 20.0f;
                    }
                }
            }
        }
    } else {
        Actor_MoveXZGravity(&this->actor);
        Math_Vec3f_Diff(&this->actor.world.pos, &this->actor.prevPos, &prevFrameDiff);
        Math_Vec3f_Sum(&this->unk_1E8, &prevFrameDiff, &this->unk_1E8);
        this->actor.shape.rot.x = Math_Atan2S(this->actor.speedXZ, -this->actor.velocity.y);
        sp60.x = this->unk_1F4.x - (this->unk_1E8.x - this->unk_1F4.x);
        sp60.y = this->unk_1F4.y - (this->unk_1E8.y - this->unk_1F4.y);
        sp60.z = this->unk_1F4.z - (this->unk_1E8.z - this->unk_1F4.z);
        u16 buttonsToCheck = BTN_A | BTN_B | BTN_R | BTN_CUP | BTN_CLEFT | BTN_CRIGHT | BTN_CDOWN;
        if (CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0) != 0) {
            buttonsToCheck |= BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT;
        }
        if (BgCheck_EntityLineTest1(&play->colCtx, &sp60, &this->unk_1E8, &sp78, &poly, true, true, true, true,
                                    &bgId) &&
            !func_8002F9EC(play, &this->actor, poly, bgId, &sp78)) {
            sp5C = COLPOLY_GET_NORMAL(poly->normal.x);
            sp58 = COLPOLY_GET_NORMAL(poly->normal.z);
            Math_Vec3f_Copy(&this->actor.world.pos, &sp78);
            this->actor.world.pos.x += 10.0f * sp5C;
            this->actor.world.pos.z += 10.0f * sp58;
            this->timer = 0;
            if (SurfaceType_IsHookshotSurface(&play->colCtx, poly, bgId)) {
                // Clawshot Bullet Time — mark this hit as a surface so the
                // arrival branch above (phi_f16 == 0.0f) can trigger bullet
                // time. Pass the surface normal so bullet time can distinguish
                // wall (|nY| < 0.5) from ceiling (nY < -0.5) and position Link
                // accordingly: walls keep him at hook pos with back to wall;
                // ceilings drop him ~70u below so he hangs from the anchor
                // (TP-style) instead of being stuck inside the ceiling.
                extern void ClawshotBT_NoteHitSurface(f32 nx, f32 ny, f32 nz);
                f32 nY = COLPOLY_GET_NORMAL(poly->normal.y);
                ClawshotBT_NoteHitSurface(sp5C, nY, sp58);
                if (bgId != BGCHECK_SCENE) {
                    dynaPolyActor = DynaPoly_GetActor(&play->colCtx, bgId);
                    if (dynaPolyActor != NULL) {
                        ArmsHook_AttachHookToActor(this, &dynaPolyActor->actor);
                    }
                }
                ArmsHook_PullPlayer(this);
                Audio_PlaySoundGeneral(NA_SE_IT_HOOKSHOT_STICK_OBJ, &this->actor.projectedPos, 4,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            } else {
                CollisionCheck_SpawnShieldParticlesMetal(play, &this->actor.world.pos);
                Audio_PlaySoundGeneral(NA_SE_IT_HOOKSHOT_REFLECT, &this->actor.projectedPos, 4,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
        } else if (CHECK_BTN_ANY(play->state.input[0].press.button, (buttonsToCheck))) {
            this->timer = 0;
        }
    }
}

void ArmsHook_Update(Actor* thisx, PlayState* play) {
    ArmsHook* this = (ArmsHook*)thisx;

    this->actionFunc(this, play);
    this->unk_1F4 = this->unk_1E8;
}

void ArmsHook_Draw(Actor* thisx, PlayState* play) {
    s32 pad;
    ArmsHook* this = (ArmsHook*)thisx;
    Player* player = GET_PLAYER(play);
    Vec3f sp78;
    Vec3f sp6C;
    Vec3f sp60;
    f32 sp5C;
    f32 sp58;

    if ((player->actor.draw != NULL) && (player->rightHandType == PLAYER_MODELTYPE_RH_HOOKSHOT)) {
        // Transformed: OOT's right-hand limb matrix isn't set up (MM form draws instead),
        // so Matrix_MultVec3f would use stale data → chain/tip draw at wrong position.
        // Only draw when the hookshot is actively shooting (matrices set during first-person).
        // In Wait state, the hookshot is invisible (held in hand) anyway in vanilla.
        if (TransformMasks_IsTransformed() && (ArmsHook_Shoot != this->actionFunc)) {
            return;
        }

        OPEN_DISPS(play->state.gfxCtx);

        if ((ArmsHook_Shoot != this->actionFunc) || (this->timer <= 0)) {
            Matrix_MultVec3f(&D_80865B70, &this->unk_1E8);
            Matrix_MultVec3f(&D_80865B88, &sp6C);
            Matrix_MultVec3f(&D_80865B94, &sp60);
            this->hookInfo.active = 0;
        } else {
            Matrix_MultVec3f(&D_80865B7C, &this->unk_1E8);
            Matrix_MultVec3f(&D_80865BA0, &sp6C);
            Matrix_MultVec3f(&D_80865BAC, &sp60);
        }

        func_80090480(play, &this->collider, &this->hookInfo, &sp6C, &sp60);
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        if (CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0) &&
            CVarGetInteger(CVAR_ENHANCEMENT("ScaleAdultEquipmentAsChild"), 0) && LINK_IS_CHILD) {
            Matrix_Scale(0.8, 0.8, 0.8, MTXMODE_APPLY);
        }
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        if (GameInteractor_Should(VB_DRAW_HOOKSHOT_TIP, true, player, play)) {
            gSPDisplayList(POLY_OPA_DISP++, gLinkAdultHookshotTipDL);
        }
        Matrix_Translate(this->actor.world.pos.x, this->actor.world.pos.y, this->actor.world.pos.z, MTXMODE_NEW);
        Math_Vec3f_Diff(&player->unk_3C8, &this->actor.world.pos, &sp78);
        sp58 = SQ(sp78.x) + SQ(sp78.z);
        sp5C = sqrtf(sp58);
        Matrix_RotateY(Math_FAtan2F(sp78.x, sp78.z), MTXMODE_APPLY);
        Matrix_RotateX(Math_FAtan2F(-sp78.y, sp5C), MTXMODE_APPLY);
        if (CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0) &&
            CVarGetInteger(CVAR_ENHANCEMENT("ScaleAdultEquipmentAsChild"), 0) && LINK_IS_CHILD) {
            Matrix_Scale(0.012f, 0.012f, sqrtf(SQ(sp78.y) + sp58) * 0.01f, MTXMODE_APPLY);
        } else {
            Matrix_Scale(0.015f, 0.015f, sqrtf(SQ(sp78.y) + sp58) * 0.01f, MTXMODE_APPLY);
        }
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        // Twilight Upgrade — Clawshot mode: swap chain DL to MM's gameplay_keep
        // hookshot chain so the visual matches the body DL swap in z_player_lib.c.
        // Fallback to OOT chain when mm.o2r isn't loaded.
        {
            extern u8 TwilightUpgrade_IsClawshotActive(void);
            extern void* MmAssets_LoadHookshotChainDL(void);
            Gfx* chainDL = gLinkAdultHookshotChainDL;
            if (TwilightUpgrade_IsClawshotActive()) {
                Gfx* mmChain = (Gfx*)MmAssets_LoadHookshotChainDL();
                if (mmChain != NULL) {
                    chainDL = mmChain;
                }
            }
            // upstream: alt-asset hookshot models can suppress the vanilla chain draw
            if (GameInteractor_Should(VB_DRAW_HOOKSHOT_CHAIN, true, player, play)) {
                gSPDisplayList(POLY_OPA_DISP++, chainDL);
            }
        }

        CLOSE_DISPS(play->state.gfxCtx);
    }
}
