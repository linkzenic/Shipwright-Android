/**
 * ssbb_companion.c — Pikachu Companion AI (Pokemon-Style)
 *
 * Autonomous Pikachu that follows Link, detects enemies, and attacks with
 * Pokemon-style move selection. Can Gigantamax during boss fights.
 *
 * AI: IDLE → FOLLOW → CHASE → ATTACK → RETURN
 *     FAINT (10s revive) | GIGANTAMAX (boss + Giant's Mask)
 */

#include "expansions/ssbb/ssbb_companion.h"
#include "expansions/ssbb/ssbb_skin.h"
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"

// ── Collider inits ──
static ColliderCylinderInit sAtCylInit = {
    { COLTYPE_NONE, AT_ON | AT_TYPE_PLAYER, AC_NONE, OC1_NONE, OC2_NONE, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0,
      { 0x00000000, 0x00, 0x00 },
      { 0x00000000, 0x00, 0x00 },
      TOUCH_ON | TOUCH_SFX_WOOD,
      BUMP_NONE,
      OCELEM_NONE },
    { 20, 20, 0, { 0, 0, 0 } },
};

static ColliderCylinderInit sBodyCylInit = {
    { COLTYPE_HIT0, AT_NONE, AC_ON | AC_TYPE_ENEMY, OC1_ON | OC1_TYPE_ALL, OC2_TYPE_1, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0, { 0x00000000, 0x00, 0x00 }, { 0xFFCFFFFF, 0x00, 0x00 }, TOUCH_NONE, BUMP_ON, OCELEM_ON },
    { 15, 25, 0, { 0, 0, 0 } },
};

// ── Helper: set companion action/anim ──
static void PikaComp_SetAction(PikachuCompanion* comp, SSBBActionId action) {
    const SSBBActionDef* def = SSBBAction_Get(action);
    if (!def)
        return;
    const struct SSBBAnim* anim = SSBBAction_GetAnim(action);
    if (!anim)
        return;

    comp->currentAction = action;
    comp->actionFrame = 0;
    comp->charInst.ssbbAnim = anim;
    comp->charInst.curFrame = 0.0f;
    comp->charInst.animLength = (f32)anim->numFrames;
    comp->charInst.playSpeed = 1.5f; // Slightly faster than normal
}

static u8 PikaComp_ActionFinished(PikachuCompanion* comp) {
    if (!comp->charInst.ssbbAnim)
        return 1;
    return (comp->actionFrame >= comp->charInst.ssbbAnim->numFrames);
}

// ── Find nearest enemy ──
static Actor* PikaComp_FindNearestEnemy(PikachuCompanion* comp, PlayState* play, f32 range) {
    Actor* best = NULL;
    f32 bestDist = range;

    for (s32 cat = ACTORCAT_ENEMY; cat <= ACTORCAT_BOSS; cat += (ACTORCAT_BOSS - ACTORCAT_ENEMY)) {
        Actor* actor = play->actorCtx.actorLists[cat].head;
        while (actor != NULL) {
            if (actor->update != NULL) {
                f32 dx = comp->pos.x - actor->world.pos.x;
                f32 dz = comp->pos.z - actor->world.pos.z;
                f32 dist = sqrtf(dx * dx + dz * dz);
                if (dist < bestDist) {
                    bestDist = dist;
                    best = actor;
                }
            }
            actor = actor->next;
        }
    }
    return best;
}

// ── Check if any boss is nearby ──
static Actor* PikaComp_FindBoss(PikachuCompanion* comp, PlayState* play) {
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_BOSS].head;
    while (actor != NULL) {
        if (actor->update != NULL) {
            f32 dx = comp->pos.x - actor->world.pos.x;
            f32 dz = comp->pos.z - actor->world.pos.z;
            if (sqrtf(dx * dx + dz * dz) < 500.0f)
                return actor;
        }
        actor = actor->next;
    }
    return NULL;
}

// ── Choose best attack based on distance and cooldowns ──
static u8 PikaComp_ChooseAttack(PikachuCompanion* comp, f32 distToEnemy, u8 isBoss) {
    // Gigantamax: always G-Max Volt Crash
    if (comp->gigantamax && comp->gmaxCD <= 0)
        return PCOMP_ATK_GMAX_CRASH;

    // Boss + close: Thunder
    if (isBoss && distToEnemy < 100.0f && comp->thunderCD <= 0)
        return PCOMP_ATK_THUNDER;

    // Far: Thunder Jolt (projectile)
    if (distToEnemy > 200.0f && comp->thunderJoltCD <= 0)
        return PCOMP_ATK_THUNDER_JOLT;

    // Mid: Quick Attack (dash)
    if (distToEnemy > 80.0f && comp->quickAtkCD <= 0)
        return PCOMP_ATK_QUICK;

    // Close: Jab combo
    if (distToEnemy < 100.0f && comp->jabCD <= 0)
        return PCOMP_ATK_JAB;

    // Nothing off cooldown
    return PCOMP_ATK_NONE;
}

// ── Init ──
void PikaCompanion_Init(PikachuCompanion* comp, PlayState* play) {
    lusprintf(__FILE__, __LINE__, 2, "PCOMP_INIT: start\n");

    memset(comp, 0, sizeof(PikachuCompanion));

    lusprintf(__FILE__, __LINE__, 2, "PCOMP_INIT: memset done\n");

    // Register SSBB character
    extern s32 pikachu_ssbb_Register_Extern(void);
    s32 defIdx = pikachu_ssbb_Register_Extern();
    lusprintf(__FILE__, __LINE__, 2, "PCOMP_INIT: register=%d\n", defIdx);
    if (defIdx < 0)
        return;

    SSBBChar_Init(&comp->charInst, defIdx, play);
    lusprintf(__FILE__, __LINE__, 2, "PCOMP_INIT: SSBBChar_Init done, def=%p skin=%p\n", comp->charInst.def,
              comp->charInst.def ? comp->charInst.def->skinMesh : NULL);

    comp->hp = PCOMP_HP_MAX;
    comp->giantScale = 1.0f;
    comp->aiState = PCOMP_AI_ENTRY;
    comp->stateTimer = 30;

    // Init colliders (use Player actor as owner since companion has no real Actor)
    Player* player = GET_PLAYER(play);
    lusprintf(__FILE__, __LINE__, 2, "PCOMP_INIT: player=%p\n", player);

    Collider_InitCylinder(play, &comp->atCyl);
    Collider_SetCylinder(play, &comp->atCyl, &player->actor, &sAtCylInit);
    Collider_InitCylinder(play, &comp->bodyCyl);
    Collider_SetCylinder(play, &comp->bodyCyl, &player->actor, &sBodyCylInit);
    comp->colliderReady = 1;
    lusprintf(__FILE__, __LINE__, 2, "PCOMP_INIT: colliders done\n");

    // Start with idle anim
    PikaComp_SetAction(comp, SSBB_ACT_WAIT1);
    lusprintf(__FILE__, __LINE__, 2, "PCOMP_INIT: action set, ssbbAnim=%p\n", comp->charInst.ssbbAnim);

    comp->initialized = 1;
    comp->active = 1;
}

// ── Update ──
void PikaCompanion_Update(PikachuCompanion* comp, PlayState* play, Player* player) {
    if (!comp->initialized || !comp->active)
        return;
    static s32 sUpdatePrintCD = 0;
    if (sUpdatePrintCD <= 0) {
        lusprintf(__FILE__, __LINE__, 2, "PCOMP_UPDATE: state=%d pos=(%.0f,%.0f,%.0f)\n", comp->aiState, comp->pos.x,
                  comp->pos.y, comp->pos.z);
        sUpdatePrintCD = 60;
    }
    sUpdatePrintCD--;

    Vec3f linkPos = player->actor.world.pos;
    f32 dx = linkPos.x - comp->pos.x;
    f32 dz = linkPos.z - comp->pos.z;
    f32 distToLink = sqrtf(dx * dx + dz * dz);

    // Decrement cooldowns
    if (comp->jabCD > 0)
        comp->jabCD--;
    if (comp->quickAtkCD > 0)
        comp->quickAtkCD--;
    if (comp->thunderCD > 0)
        comp->thunderCD--;
    if (comp->thunderJoltCD > 0)
        comp->thunderJoltCD--;
    if (comp->gmaxCD > 0)
        comp->gmaxCD--;

    // Teleport if too far
    if (distToLink > PCOMP_TELEPORT_DIST) {
        f32 yaw = player->actor.shape.rot.y * (M_PI / 32768.0f);
        comp->pos.x = linkPos.x - sinf(yaw) * PCOMP_FOLLOW_DIST;
        comp->pos.y = linkPos.y;
        comp->pos.z = linkPos.z - cosf(yaw) * PCOMP_FOLLOW_DIST;
        distToLink = PCOMP_FOLLOW_DIST;
    }

    // Check for Gigantamax conditions (skip in first few frames to let scene load)
    if (comp->stateTimer > 0 && comp->aiState == PCOMP_AI_ENTRY) {
        comp->stateTimer--;
        comp->actionFrame++;
        SSBBChar_Update(&comp->charInst);
        return;
    }
    Actor* boss = PikaComp_FindBoss(comp, play);
    u8 hasGiantMask = (gSaveContext.inventory.items[SLOT_MM_MASK_GIANT] == ITEM_MM_MASK_GIANT);
    if (boss && hasGiantMask && !comp->gigantamax && comp->aiState != PCOMP_AI_FAINT) {
        comp->gigantamax = 1;
        comp->aiState = PCOMP_AI_GIGANTAMAX;
    }
    if (!boss && comp->gigantamax) {
        comp->gigantamax = 0;
    }

    // Scale lerp for Gigantamax
    f32 targetScale = comp->gigantamax ? 3.0f : 1.0f;
    Math_SmoothStepToF(&comp->giantScale, targetScale, 0.3f, 0.5f, 0.01f);

    // ── AI State Machine ──
    switch (comp->aiState) {
        case PCOMP_AI_ENTRY:
            comp->stateTimer--;
            if (comp->stateTimer <= 0) {
                comp->aiState = PCOMP_AI_IDLE;
                PikaComp_SetAction(comp, SSBB_ACT_WAIT2);
            }
            break;

        case PCOMP_AI_IDLE: {
            // Face Link
            if (distToLink > 10.0f)
                comp->yaw = Math_Atan2S(dx, dz);

            // Check for enemies
            Actor* enemy = PikaComp_FindNearestEnemy(comp, play, PCOMP_DETECT_RANGE);
            if (enemy) {
                comp->targetEnemy = enemy;
                comp->aiState = PCOMP_AI_CHASE;
                PikaComp_SetAction(comp, SSBB_ACT_RUN);
                break;
            }
            // Too far from Link → follow
            if (distToLink > PCOMP_FOLLOW_DIST * 1.5f) {
                comp->aiState = PCOMP_AI_FOLLOW;
                PikaComp_SetAction(comp, SSBB_ACT_WALK_MIDDLE);
            }
            // Idle anim cycle
            if (PikaComp_ActionFinished(comp)) {
                PikaComp_SetAction(comp, (play->gameplayFrames % 2) ? SSBB_ACT_WAIT2 : SSBB_ACT_WAIT3);
            }
            break;
        }

        case PCOMP_AI_FOLLOW: {
            // Move toward Link
            f32 speed = (distToLink > PCOMP_FOLLOW_DIST * 3.0f) ? PCOMP_RUN_SPEED : PCOMP_WALK_SPEED;
            if (distToLink > PCOMP_FOLLOW_DIST) {
                f32 inv = 1.0f / distToLink;
                comp->pos.x += dx * inv * speed;
                comp->pos.z += dz * inv * speed;
                comp->yaw = Math_Atan2S(dx, dz);
                // Set walk/run anim
                if (speed > PCOMP_WALK_SPEED && comp->currentAction != SSBB_ACT_RUN)
                    PikaComp_SetAction(comp, SSBB_ACT_RUN);
                else if (speed <= PCOMP_WALK_SPEED && comp->currentAction != SSBB_ACT_WALK_MIDDLE)
                    PikaComp_SetAction(comp, SSBB_ACT_WALK_MIDDLE);
            } else {
                comp->aiState = PCOMP_AI_IDLE;
                PikaComp_SetAction(comp, SSBB_ACT_WAIT2);
            }
            // Check for enemies while following
            Actor* enemy = PikaComp_FindNearestEnemy(comp, play, PCOMP_DETECT_RANGE);
            if (enemy) {
                comp->targetEnemy = enemy;
                comp->aiState = PCOMP_AI_CHASE;
                PikaComp_SetAction(comp, SSBB_ACT_RUN);
            }
            break;
        }

        case PCOMP_AI_CHASE: {
            if (!comp->targetEnemy || comp->targetEnemy->update == NULL) {
                comp->targetEnemy = NULL;
                comp->aiState = PCOMP_AI_RETURN;
                break;
            }
            f32 edx = comp->targetEnemy->world.pos.x - comp->pos.x;
            f32 edz = comp->targetEnemy->world.pos.z - comp->pos.z;
            f32 eDist = sqrtf(edx * edx + edz * edz);
            comp->yaw = Math_Atan2S(edx, edz);

            // Move toward enemy
            if (eDist > PCOMP_ATTACK_RANGE) {
                f32 inv = 1.0f / eDist;
                comp->pos.x += edx * inv * PCOMP_RUN_SPEED;
                comp->pos.z += edz * inv * PCOMP_RUN_SPEED;
                if (comp->currentAction != SSBB_ACT_RUN)
                    PikaComp_SetAction(comp, SSBB_ACT_RUN);
            } else {
                // In range — choose attack
                u8 isBoss = (comp->targetEnemy->category == ACTORCAT_BOSS);
                u8 atk = PikaComp_ChooseAttack(comp, eDist, isBoss);
                if (atk != PCOMP_ATK_NONE) {
                    comp->attackType = atk;
                    comp->aiState = PCOMP_AI_ATTACK;
                    comp->attackTimer = 0;
                    // Set attack anim
                    switch (atk) {
                        case PCOMP_ATK_JAB:
                            PikaComp_SetAction(comp, SSBB_ACT_ATTACK_JAB);
                            comp->attackTimer = 20;
                            comp->jabCD = PCOMP_CD_JAB;
                            break;
                        case PCOMP_ATK_QUICK:
                            PikaComp_SetAction(comp, SSBB_ACT_SPECIAL_HI_START);
                            comp->attackTimer = 15;
                            comp->quickAtkCD = PCOMP_CD_QUICK;
                            break;
                        case PCOMP_ATK_THUNDER_JOLT:
                            PikaComp_SetAction(comp, SSBB_ACT_SPECIAL_N);
                            comp->attackTimer = 20;
                            comp->thunderJoltCD = PCOMP_CD_THUNDER_JOLT;
                            break;
                        case PCOMP_ATK_THUNDER:
                            PikaComp_SetAction(comp, SSBB_ACT_SPECIAL_LW_START);
                            comp->attackTimer = 40;
                            comp->thunderCD = PCOMP_CD_THUNDER;
                            break;
                        case PCOMP_ATK_GMAX_CRASH:
                            PikaComp_SetAction(comp, SSBB_ACT_SPECIAL_HI_START);
                            comp->attackTimer = 20;
                            comp->gmaxCD = PCOMP_CD_GMAX;
                            break;
                    }
                }
            }
            break;
        }

        case PCOMP_AI_ATTACK: {
            comp->attackTimer--;

            // Quick Attack: dash toward enemy during attack
            if ((comp->attackType == PCOMP_ATK_QUICK || comp->attackType == PCOMP_ATK_GMAX_CRASH) &&
                comp->targetEnemy && comp->targetEnemy->update) {
                f32 edx = comp->targetEnemy->world.pos.x - comp->pos.x;
                f32 edz = comp->targetEnemy->world.pos.z - comp->pos.z;
                f32 eDist = sqrtf(edx * edx + edz * edz);
                if (eDist > 10.0f) {
                    f32 inv = 1.0f / eDist;
                    comp->pos.x += edx * inv * 20.0f;
                    comp->pos.z += edz * inv * 20.0f;
                    comp->yaw = Math_Atan2S(edx, edz);
                }
            }

            // Register AT collider during attack
            if (comp->colliderReady) {
                s16 radius = 20, height = 20;
                u32 dmgFlags = DMG_SLASH_MASTER;
                u8 damage = 2;

                switch (comp->attackType) {
                    case PCOMP_ATK_JAB:
                        radius = 20;
                        height = 20;
                        damage = 2;
                        dmgFlags = DMG_SLASH_MASTER;
                        break;
                    case PCOMP_ATK_QUICK:
                        radius = 30;
                        height = 30;
                        damage = 4;
                        dmgFlags = DMG_BOOMERANG | DMG_SLASH_MASTER;
                        break;
                    case PCOMP_ATK_THUNDER_JOLT:
                        radius = 25;
                        height = 25;
                        damage = 3;
                        dmgFlags = DMG_SLINGSHOT | DMG_SLASH_KOKIRI;
                        break;
                    case PCOMP_ATK_THUNDER:
                        radius = 80;
                        height = 100;
                        damage = 8;
                        dmgFlags = DMG_MAGIC_LIGHT | DMG_ARROW_LIGHT;
                        break;
                    case PCOMP_ATK_GMAX_CRASH:
                        radius = 100;
                        height = 100;
                        damage = 8;
                        dmgFlags = DMG_UNBLOCKABLE | DMG_SLASH_MASTER | DMG_BOOMERANG | DMG_ARROW_LIGHT;
                        break;
                }

                // Scale for Gigantamax
                radius = (s16)(radius * comp->giantScale);
                height = (s16)(height * comp->giantScale);

                comp->atCyl.dim.radius = radius;
                comp->atCyl.dim.height = height;
                comp->atCyl.dim.yShift = 0;
                comp->atCyl.dim.pos.x = (s16)comp->pos.x;
                comp->atCyl.dim.pos.y = (s16)comp->pos.y;
                comp->atCyl.dim.pos.z = (s16)comp->pos.z;
                comp->atCyl.info.toucher.dmgFlags = dmgFlags;
                comp->atCyl.info.toucher.damage = damage;
                comp->atCyl.info.toucherFlags = TOUCH_ON | TOUCH_SFX_WOOD;
                comp->atCyl.base.atFlags = AT_ON | AT_TYPE_PLAYER;
                comp->atCyl.base.atFlags &= ~AT_HIT;
                CollisionCheck_SetAT(play, &play->colChkCtx, &comp->atCyl.base);
            }

            // Attack finished
            if (comp->attackTimer <= 0) {
                // Check if enemy still alive
                if (comp->targetEnemy && comp->targetEnemy->update) {
                    comp->aiState = PCOMP_AI_CHASE; // Re-engage
                } else {
                    comp->targetEnemy = NULL;
                    comp->aiState = PCOMP_AI_RETURN;
                }
                PikaComp_SetAction(comp, SSBB_ACT_WAIT1);
            }
            break;
        }

        case PCOMP_AI_RETURN:
            // Run back to Link
            if (distToLink > PCOMP_FOLLOW_DIST) {
                f32 inv = 1.0f / distToLink;
                comp->pos.x += dx * inv * PCOMP_RUN_SPEED;
                comp->pos.z += dz * inv * PCOMP_RUN_SPEED;
                comp->yaw = Math_Atan2S(dx, dz);
                if (comp->currentAction != SSBB_ACT_RUN)
                    PikaComp_SetAction(comp, SSBB_ACT_RUN);
            } else {
                comp->aiState = PCOMP_AI_IDLE;
                PikaComp_SetAction(comp, SSBB_ACT_WAIT2);
            }
            break;

        case PCOMP_AI_FAINT:
            comp->faintTimer--;
            if (comp->faintTimer <= 0) {
                comp->hp = PCOMP_HP_MAX;
                comp->aiState = PCOMP_AI_IDLE;
                PikaComp_SetAction(comp, SSBB_ACT_WAIT2);
            }
            break;

        case PCOMP_AI_GIGANTAMAX: {
            // Same as CHASE/ATTACK but with Gigantamax scale
            Actor* enemy = comp->targetEnemy;
            if (!enemy || enemy->update == NULL)
                enemy = PikaComp_FindNearestEnemy(comp, play, 500.0f);
            if (!enemy) {
                comp->aiState = PCOMP_AI_RETURN;
                break;
            }
            comp->targetEnemy = enemy;
            f32 edx = enemy->world.pos.x - comp->pos.x;
            f32 edz = enemy->world.pos.z - comp->pos.z;
            f32 eDist = sqrtf(edx * edx + edz * edz);
            comp->yaw = Math_Atan2S(edx, edz);

            if (eDist > 60.0f) {
                f32 inv = 1.0f / eDist;
                comp->pos.x += edx * inv * PCOMP_RUN_SPEED;
                comp->pos.z += edz * inv * PCOMP_RUN_SPEED;
                if (comp->currentAction != SSBB_ACT_WALK_SLOW)
                    PikaComp_SetAction(comp, SSBB_ACT_WALK_SLOW);
            } else {
                u8 atk = PikaComp_ChooseAttack(comp, eDist, 1);
                if (atk != PCOMP_ATK_NONE && comp->aiState != PCOMP_AI_ATTACK) {
                    comp->attackType = atk;
                    comp->aiState = PCOMP_AI_ATTACK;
                    comp->attackTimer = 30;
                    PikaComp_SetAction(comp, SSBB_ACT_ATTACK_JAB);
                }
            }
            // No boss anymore → revert
            if (!PikaComp_FindBoss(comp, play)) {
                comp->gigantamax = 0;
                comp->aiState = PCOMP_AI_RETURN;
            }
            break;
        }

        case PCOMP_AI_DODGE:
            comp->stateTimer--;
            if (comp->stateTimer <= 0) {
                comp->aiState = PCOMP_AI_CHASE;
            }
            break;
    }

    // ── Receive damage (body collider AC check) ──
    if (comp->bodyCyl.base.acFlags & AC_HIT) {
        comp->bodyCyl.base.acFlags &= ~AC_HIT;
        if (comp->aiState != PCOMP_AI_FAINT) {
            comp->hp -= 4;
            if (comp->hp <= 0) {
                comp->hp = 0;
                comp->aiState = PCOMP_AI_FAINT;
                comp->faintTimer = PCOMP_FAINT_DURATION;
                PikaComp_SetAction(comp, SSBB_ACT_FURA_SLEEP_START);
            } else {
                // 50% chance to dodge
                if ((play->gameplayFrames % 2) == 0 && comp->aiState != PCOMP_AI_ATTACK) {
                    comp->aiState = PCOMP_AI_DODGE;
                    comp->stateTimer = 15;
                    PikaComp_SetAction(comp, SSBB_ACT_ESCAPE_B);
                    // Move away from enemy
                    if (comp->targetEnemy) {
                        f32 edx = comp->pos.x - comp->targetEnemy->world.pos.x;
                        f32 edz = comp->pos.z - comp->targetEnemy->world.pos.z;
                        f32 eDist = sqrtf(edx * edx + edz * edz);
                        if (eDist > 1.0f) {
                            comp->pos.x += (edx / eDist) * 30.0f;
                            comp->pos.z += (edz / eDist) * 30.0f;
                        }
                    }
                } else {
                    PikaComp_SetAction(comp, SSBB_ACT_DAMAGE_N1);
                }
            }
        }
    }

    // ── Register body collider ──
    if (comp->colliderReady && comp->aiState != PCOMP_AI_FAINT) {
        comp->bodyCyl.dim.pos.x = (s16)comp->pos.x;
        comp->bodyCyl.dim.pos.y = (s16)comp->pos.y;
        comp->bodyCyl.dim.pos.z = (s16)comp->pos.z;
        CollisionCheck_SetAC(play, &play->colChkCtx, &comp->bodyCyl.base);
        CollisionCheck_SetOC(play, &play->colChkCtx, &comp->bodyCyl.base);
    }

    // ── Match Y to ground ──
    comp->pos.y = player->actor.world.pos.y;

    // ── Advance animation ──
    comp->actionFrame++;
    SSBBChar_Update(&comp->charInst);
}

// ── Draw ──
void PikaCompanion_Draw(PikachuCompanion* comp, PlayState* play) {
    if (!comp->initialized || !comp->active)
        return;
    static s32 sDrawPrintCD = 0;
    if (sDrawPrintCD <= 0) {
        lusprintf(__FILE__, __LINE__, 2, "PCOMP_DRAW: scale=%.3f giantScale=%.1f\n", PCOMP_SCALE * comp->giantScale,
                  comp->giantScale);
        sDrawPrintCD = 60;
    }
    sDrawPrintCD--;
    if (comp->aiState == PCOMP_AI_FAINT && comp->faintTimer > (PCOMP_FAINT_DURATION - 30))
        return; // Hide briefly on faint

    if (!comp->charInst.def || !comp->charInst.def->skinMesh || !comp->charInst.def->skinMesh->vtxBuf[0] ||
        !comp->charInst.def->skinMesh->vtxBuf[1] || !comp->charInst.ssbbAnim) {
        return;
    }

    Vec3s rot = { 0, comp->yaw, 0 };
    f32 savedScale = comp->charInst.def->scale;
    comp->charInst.def->scale = PCOMP_SCALE * comp->giantScale;

    SSBBSkin_Draw(&comp->charInst, play, &comp->pos, &rot);

    comp->charInst.def->scale = savedScale;
}

// ── Destroy ──
void PikaCompanion_Destroy(PikachuCompanion* comp) {
    comp->active = 0;
    comp->initialized = 0;
}
