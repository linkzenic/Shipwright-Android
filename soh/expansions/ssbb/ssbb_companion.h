#ifndef SSBB_COMPANION_H
#define SSBB_COMPANION_H

#include "z64.h"
#include "expansions/ssbb/ssbb_character.h"
#include "expansions/ssbb/ssbb_anim.h"
#include "expansions/ssbb/ssbb_action_defs.h"

// ── AI States ──
#define PCOMP_AI_IDLE 0       // Near Link, no enemy, Wait anim
#define PCOMP_AI_FOLLOW 1     // Walking/running to Link
#define PCOMP_AI_CHASE 2      // Running toward enemy
#define PCOMP_AI_ATTACK 3     // Executing attack
#define PCOMP_AI_RETURN 4     // Returning to Link after attack
#define PCOMP_AI_FAINT 5      // Knocked out, reviving
#define PCOMP_AI_GIGANTAMAX 6 // Giant mode near boss
#define PCOMP_AI_ENTRY 7      // Spawn animation (EntryL)
#define PCOMP_AI_DODGE 8      // Dodge roll

// ── Attack Types ──
#define PCOMP_ATK_NONE 0
#define PCOMP_ATK_JAB 1          // Close range, 3-hit combo
#define PCOMP_ATK_QUICK 2        // Mid range, dash to enemy
#define PCOMP_ATK_THUNDER_JOLT 3 // Long range, projectile
#define PCOMP_ATK_THUNDER 4      // Boss AoE
#define PCOMP_ATK_GMAX_CRASH 5   // Gigantamax Quick Attack

// ── Constants ──
#define PCOMP_HP_MAX 20
#define PCOMP_FAINT_DURATION 600 // 10 seconds at 60fps
#define PCOMP_FOLLOW_DIST 80.0f
#define PCOMP_TELEPORT_DIST 600.0f
#define PCOMP_DETECT_RANGE 300.0f
#define PCOMP_ATTACK_RANGE 60.0f
#define PCOMP_WALK_SPEED 4.0f
#define PCOMP_RUN_SPEED 10.0f
#define PCOMP_SCALE 0.014f

// ── Cooldowns (frames) ──
#define PCOMP_CD_JAB 30
#define PCOMP_CD_QUICK 90
#define PCOMP_CD_THUNDER_JOLT 120
#define PCOMP_CD_THUNDER 180
#define PCOMP_CD_GMAX 60

// ── Companion struct ──
typedef struct PikachuCompanion {
    // SSBB character instance (skeleton, skin, animation)
    SSBBCharacterInstance charInst;
    u8 initialized;

    // AI
    u8 aiState;
    u8 attackType;
    s32 attackTimer;
    s32 stateTimer; // General purpose timer for current state
    Actor* targetEnemy;

    // Combat
    s16 hp;
    s32 faintTimer;
    u8 gigantamax;
    f32 giantScale;
    ColliderCylinder atCyl;
    ColliderCylinder bodyCyl;
    u8 colliderReady;

    // Movement
    Vec3f pos;
    s16 yaw;
    f32 moveSpeed;

    // Animation
    SSBBActionId currentAction;
    u16 actionFrame;

    // Cooldowns
    s32 jabCD;
    s32 quickAtkCD;
    s32 thunderCD;
    s32 thunderJoltCD;
    s32 gmaxCD;

    // Misc
    s32 stuckTimer; // Frames stuck on wall → teleport
    u8 active;      // Is spawned and active
} PikachuCompanion;

// ── API ──
void PikaCompanion_Init(PikachuCompanion* comp, PlayState* play);
void PikaCompanion_Update(PikachuCompanion* comp, PlayState* play, Player* player);
void PikaCompanion_Draw(PikachuCompanion* comp, PlayState* play);
void PikaCompanion_Destroy(PikachuCompanion* comp);

#endif // SSBB_COMPANION_H
