/**
 * custom_items.h - Custom items system for OOT
 *
 * Item IDs: 0x9C - 0xB5
 * Global state: CustomItemState tracks all item behavior
 */

#ifndef CUSTOM_ITEMS_H
#define CUSTOM_ITEMS_H

#include "z64.h"
#include "z64player.h"
#include "helpers/cutscene_helper.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CUSTOM_BLOCKING_STATE1_FLAGS \
    (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_LOADING | PLAYER_STATE1_IN_ITEM_CS)

#define GUSTJAR_MAX_TRACKED 16

// Item IDs (custom items start at 0x9D, develop's ITEM_ROCS_FEATHER is at 0x9C)
#define ITEM_ROCS_FEATHER_SKIJER 0x9D
#define ITEM_ROCS_CAPE 0x9E
#define ITEM_DESIRE_SENSOR 0x9F
#define ITEM_HYLIAS_GRACE 0xA0
#define ITEM_ZONAI_PERMAFROST 0xA1
#define ITEM_DEMISE_DESTRUCTION 0xA2
#define ITEM_DEKU_LEAF 0xA3
#define ITEM_SWITCH_HOOK 0xA4
#define ITEM_MOGMA_MITTS 0xA5
#define ITEM_GUST_JAR 0xA6
#define ITEM_BALL_AND_CHAIN 0xA7
#define ITEM_WHIP 0xA8
#define ITEM_SPINNER 0xA9
#define ITEM_CANE_OF_SOMARIA 0xAA
#define ITEM_DOMINION_ROD 0xAB
#define ITEM_TIME_GATE 0xAC
#define ITEM_BOMB_ARROWS 0xAD
#define ITEM_ROD_FIRE 0xAE
#define ITEM_ROD_ICE 0xAF
#define ITEM_ROD_LIGHT 0xB0
#define ITEM_BEETLE 0xB1
#define ITEM_SHOVEL 0xB2
#define ITEM_MINISH_CAP 0xB3
#define ITEM_PENDING_2 0xB4
#define ITEM_PENDING_3 0xB6

/**
 * Global state for all custom items.
 */
typedef struct {
    // General
    s16 timer1;
    s16 timer2;
    s32 globalCooldownTimer;

    // Roc's Feather / Cape
    u8 rocsFeatherJumpActive;
    u8 rocsJumpCount;
    s16 rocsMmAnimTimer; // Timer to force MM animation for ground jump

    // Deku Leaf
    u8 dekuLeafActive;
    u8 dekuLeafMode;
    u8 dekuLeafGliding;
    u8 dekuLeafBlowing;
    s16 dekuLeafAnimTimer;
    s16 dekuLeafBlowTimer;

    // Ball and Chain
    u8 ballAndChainThrown;
    u8 ballAndChainFirstPersonActive;
    ColliderCylinder ballAndChainCollider;

    // Spinner
    u8 spinnerActive;
    s16 spinnerSpinAttackTimer;
    s16 spinnerWallBumpTimer;

    // Gust Jar
    s32 gustJarState;
    Actor* gustJarTarget;
    u8 gustJarMode;
    u8 gustJarAmmoType;
    u8 gustJarProjectileActive;
    Vec3f gustJarProjPos;
    s16 gustJarProjYaw;
    s16 gustJarProjPitch;
    s16 gustJarTimer;
    ColliderCylinder gustJarCollider;
    u8 gustJarFirstPersonActive;
    u8 gustJarAimMode;
    s16 gustJarPrevCameraMode;
    u8 gustJarEquipped;
    u16 gustJarButtonMask;
    s8 gustJarPrevInvincibility;
    Actor* gustJarPotActor;
    struct {
        Actor* actor;
        Vec3f originalScale;
    } gustJarScaleCache[GUSTJAR_MAX_TRACKED];
    u8 gustJarScaleCacheCount;

    // Shovel
    u8 shovelActive;
    u8 shovelAnimating;
    s16 shovelAnimTimer;
    Actor* shovelHoleActor;

    // Demise Destruction
    u8 demiseDestructionActive;
    ColliderCylinder demiseDestructionCollider;

    // Beetle
    u8 beetleActive;
    u8 beetleState;
    u8 beetleFirstPersonActive;
    Vec3f beetlePos;
    Vec3s beetleRot;
    Actor* beetleGrabbed;
    f32 beetleWingScale;
    s8 beetleWingDir;
    s16 beetleTimer;
    Vec3f beetleStartPos;
    ColliderCylinder beetleCollider;
    s16 beetleSubCamId;

    // Bomb Arrows
    u8 bombArrowActive;
    u8 bombArrowState;
    Actor* bombArrowBombActor;
    Actor* bombArrowArrowActor;
    u8 bombArrowFirstPersonActive;
    u16 bombArrowButtonMask;

    // Fire Rod
    u8 fireRodActive;
    u8 fireRodState;
    u8 fireRodPrevSword;
    MtxF fireRodMatrix;
    u8 fireRodMatrixValid;
    u8 fireRodProjActive;
    u8 fireRodProjType;
    u8 fireRodProjCount;
    Vec3f fireRodProjPos;
    Vec3f fireRodProjPos2;
    Vec3f fireRodProjPos3;
    Vec3f fireRodProjVel[3];
    s16 fireRodProjYaw;
    s16 fireRodProjPitch;
    s16 fireRodProjTimer;
    ColliderCylinder fireRodCollider;
    ColliderCylinder fireRodCollider2;
    ColliderCylinder fireRodCollider3;
    s32 fireRodBlureIdx;
    Vec3f fireRodProjTrail[6];
    f32 fireRodProjScale;
    s16 fireRodProjRotZ;
    u8 fireRodProjTrailIdx;
    u8 fireRodFlameActive;
    u8 fireRodFlameTimer;
    Vec3f fireRodFlamePos[6];
    ColliderCylinder fireRodFlameColliders[6];
    u8 fireRodCharging;
    f32 fireRodChargeLevel;
    u8 fireRodChargeReady;
    s16 fireRodChargeTimer;
    u8 fireRodSpinActive;
    u8 fireRodSpinIsBig;
    f32 fireRodSpinRadius;
    f32 fireRodSpinMaxRadius;
    ColliderCylinder fireRodSpinCollider;
    u8 fireRodFirstPerson;
    u16 fireRodButtonMask;

    // Ice Rod
    u8 iceRodActive;
    u8 iceRodState;
    u8 iceRodPrevSword;
    MtxF iceRodMatrix;
    u8 iceRodMatrixValid;
    u8 iceRodProjActive;
    u8 iceRodProjType;
    u8 iceRodProjCount;
    Vec3f iceRodProjPos;
    Vec3f iceRodProjPos2;
    Vec3f iceRodProjPos3;
    Vec3f iceRodProjVel[3];
    s16 iceRodProjYaw;
    s16 iceRodProjPitch;
    s16 iceRodProjTimer;
    ColliderCylinder iceRodCollider;
    ColliderCylinder iceRodCollider2;
    ColliderCylinder iceRodCollider3;
    s32 iceRodBlureIdx;
    Vec3f iceRodProjTrail[6];
    f32 iceRodProjScale;
    s16 iceRodProjRotZ;
    u8 iceRodProjTrailIdx;
    u8 iceRodWaveActive;
    u8 iceRodWaveTimer;
    Vec3f iceRodWavePos[6];
    ColliderCylinder iceRodWaveColliders[6];
    u8 iceRodCharging;
    f32 iceRodChargeLevel;
    u8 iceRodChargeReady;
    s16 iceRodChargeTimer;
    u8 iceRodSpinActive;
    u8 iceRodSpinIsBig;
    f32 iceRodSpinRadius;
    f32 iceRodSpinMaxRadius;
    ColliderCylinder iceRodSpinCollider;
    u8 iceRodFirstPerson;
    u16 iceRodButtonMask;

    // Light Rod
    u8 lightRodActive;
    u8 lightRodState;
    u8 lightRodPrevSword;
    MtxF lightRodMatrix;
    u8 lightRodMatrixValid;
    u8 lightRodProjActive;
    u8 lightRodProjType;
    u8 lightRodProjCount;
    Vec3f lightRodProjPos;
    Vec3f lightRodProjPos2;
    Vec3f lightRodProjPos3;
    Vec3f lightRodProjVel[3];
    s16 lightRodProjYaw;
    s16 lightRodProjPitch;
    s16 lightRodProjTimer;
    ColliderCylinder lightRodCollider;
    ColliderCylinder lightRodCollider2;
    ColliderCylinder lightRodCollider3;
    s32 lightRodBlureIdx;
    Vec3f lightRodProjTrail[6];
    f32 lightRodProjScale;
    s16 lightRodProjRotZ;
    u8 lightRodProjTrailIdx;
    u8 lightRodBeamActive;
    u8 lightRodBeamTimer;
    Vec3f lightRodBeamPos[6];
    ColliderCylinder lightRodBeamColliders[6];
    u8 lightRodCharging;
    f32 lightRodChargeLevel;
    u8 lightRodChargeReady;
    s16 lightRodChargeTimer;
    u8 lightRodSpinActive;
    u8 lightRodSpinIsBig;
    f32 lightRodSpinRadius;
    f32 lightRodSpinMaxRadius;
    ColliderCylinder lightRodSpinCollider;
    u8 lightRodFirstPerson;
    u16 lightRodButtonMask;

    // Dominion Rod
    u8 dominionRodActive;
    u8 dominionRodState;
    u8 dominionRodFirstPersonActive;
    Vec3f dominionRodOrbPos;
    Vec3s dominionRodOrbRot;
    Actor* dominionRodControlledActor;
    s16 dominionRodTimer;
    Vec3f dominionRodStartPos;
    ColliderCylinder dominionRodCollider;
    LightNode* dominionRodLightNode;
    LightInfo dominionRodLightInfo;
    u16 dominionRodButtonMask;
    u8 dominionRodControlType;
    Vec3f dominionRodControlVel;
    u8 dominionRodDamagePaused;
    s8 dominionRodPrevInvincibility;
    Vec3f dominionRodActorHomePos;
    s16 dominionRodFlameTimer;
    s16 dominionRodAttackCooldown;
    u8 dominionRodSpikeInvulnerable;
    Actor* dominionRodFlameActor;
    s16 dominionRodCButtonHoldTimer;

    // Cane of Somaria
    u8 somariaActive;
    Actor* somariaBlocks[3];
    u8 somariaBlockCount;
    u8 somariaOldestSlot;
    u16 somariaButtonMask;
    s16 somariaCooldown;
    u8 somariaSelectedType;
    u8 somariaAnimating;
    s16 somariaAnimTimer;
    u8 somariaActionType;

    // Hylia's Grace
    u8 hyliasGraceActive;
    u8 hyliasGraceState;
    u8 hyliasGraceSubPhase;
    s16 hyliasGraceTimer;
    s16 hyliasGraceCooldown;
    Actor* hyliasGraceFairy;
    u8 hyliasGraceForcedBySpell;

    // Zonai Permafrost
    u8 zonaiPermafrostActive;
    u8 zonaiPermafrostState;
    u8 zonaiPermafrostSubPhase;
    s16 zonaiPermafrostTimer;
    u16 zonaiPermafrostSavedTimeIncr;

    // Time Gate
    u8 timeGateActive;
    u8 timeGateState;
    u8 timeGateSubPhase;
    s16 timeGateTimer;
    u8 timeGatePromptShown;
    u8 timeGateItemVisible;  // Show item in Link's hand during cast
    u8 timeGatePortalActive; // Show blue warp portal on ground
    f32 timeGatePortalAlpha; // Portal fade alpha (0-255)
    f32 timeGatePortalScale; // Portal scale for grow/shrink effect

    // Mogma Mitts
    u8 mogmaMittsActive;
    u8 mogmaMittsDrainTick;

    // Whip
    u8 whipActive;
    u8 whipState;
    Vec3f whipTipPos;
    Vec3f whipAttachPos;
    Vec3f whipAttachNormal;
    s16 whipTimer;
    ColliderCylinder whipCollider;
    f32 whipSwingAngle;
    f32 whipSwingVel;
    s16 whipSwingYaw;
    f32 whipRopeLength;
    s32 whipAttachedBgId;
    Actor* whipPullTarget;
    Actor* whipRageTarget;
    s16 whipRageTimer;
    f32 whipRageOrigSpeed;
    s8 whipPrevInvinc;
    s16 whipExtendYaw;
    s16 whipExtendPitch;
    u8 whipFirstPersonActive;

    // Desire Sensor
    u8 desireSensorActive;
    u8 desireSensorState;
    s16 desireSensorTimer;
    u8 desireSensorResult;

    // Switch Hook
    u8 switchHookActive;
    u8 switchHookState;
    u8 switchHookFirstPerson;
    Vec3f switchHookProjPos;
    s16 switchHookProjYaw;
    s16 switchHookProjPitch;
    s16 switchHookTimer;
    Actor* switchHookTarget;
    Vec3f switchHookLinkStartPos;
    Vec3f switchHookTargetStartPos;
    s16 switchHookSwapTimer;
    ColliderQuad switchHookCollider;
    u16 switchHookButtonMask;
    s16 switchHookVortexTimer;

    // Minish Cap (Fast Travel)
    u8 minishCapWarpMode;  // Warp map is active (pause menu override)
    s8 minishCapCursorIdx; // Selected pod soil index in table
    s8 minishCapConfirmed; // 1 = warp pending after unpause
    s8 minishCapDestIdx;   // Destination pod soil index
    u8 minishCapShrinking; // 1 = shrinking player during departure fade
    u8 minishCapGrowing;   // 1 = snap to start scale, 2 = growing to normal

    // Shared
    Vec3f sharedProjectilePos;
} CustomItemState;

extern CustomItemState gCustomItemState;

// Bitfield flags for CustomItemVisualSync.activeFlags
#define CI_FLAG_SPINNER (1 << 0)
#define CI_FLAG_GUSTJAR (1 << 1)
#define CI_FLAG_BALLCHAIN (1 << 2)
#define CI_FLAG_SHOVEL (1 << 3)
#define CI_FLAG_BEETLE (1 << 4)
#define CI_FLAG_DOMINION_ROD (1 << 5)
#define CI_FLAG_SOMARIA (1 << 6)
#define CI_FLAG_MOGMA_MITTS (1 << 7)
#define CI_FLAG_WHIP (1 << 8)
#define CI_FLAG_TIME_GATE (1 << 9)
#define CI_FLAG_SWITCH_HOOK (1 << 10)
#define CI_FLAG_DEKU_LEAF (1 << 11)
#define CI_FLAG_FIRE_ROD (1 << 12)
#define CI_FLAG_ICE_ROD (1 << 13)
#define CI_FLAG_LIGHT_ROD (1 << 14)

/**
 * Compact visual state for network sync.
 * Only contains fields read by draw functions (not logic/colliders/actors).
 */
typedef struct {
    u32 activeFlags;

    // Deku Leaf
    u8 dekuLeafGliding;
    u8 dekuLeafBlowing;
    s16 dekuLeafAnimTimer;

    // Spinner (only needs active flag + player pos)

    // Gust Jar
    u8 gustJarMode;
    u8 gustJarAmmoType;
    u8 gustJarProjectileActive;
    Vec3f gustJarProjPos;
    s16 gustJarProjYaw;

    // Ball and Chain
    u8 ballAndChainThrown;
    s16 timer2;
    Vec3f sharedProjectilePos;

    // Shovel
    u8 shovelAnimating;

    // Beetle
    u8 beetleState;
    Vec3f beetlePos;
    Vec3s beetleRot;
    f32 beetleWingScale;

    // Fire Rod
    u8 fireRodProjActive;
    u8 fireRodProjCount;
    u8 fireRodProjType;
    Vec3f fireRodProjPos;
    Vec3f fireRodProjPos2;
    Vec3f fireRodProjPos3;
    Vec3f fireRodProjTrail[6];
    f32 fireRodProjScale;
    MtxF fireRodMatrix;
    u8 fireRodMatrixValid;

    // Ice Rod
    u8 iceRodProjActive;
    u8 iceRodProjCount;
    Vec3f iceRodProjPos;
    Vec3f iceRodProjPos2;
    Vec3f iceRodProjPos3;
    Vec3f iceRodProjTrail[6];
    f32 iceRodProjScale;
    MtxF iceRodMatrix;
    u8 iceRodMatrixValid;

    // Light Rod
    u8 lightRodProjActive;
    u8 lightRodProjCount;
    Vec3f lightRodProjPos;
    Vec3f lightRodProjPos2;
    Vec3f lightRodProjPos3;
    MtxF lightRodMatrix;
    u8 lightRodMatrixValid;

    // Dominion Rod
    u8 dominionRodState;
    Vec3f dominionRodOrbPos;

    // Cane of Somaria (only needs active flag)

    // Mogma Mitts (only needs active flag)

    // Whip
    u8 whipState;
    Vec3f whipTipPos;
    Vec3f whipAttachPos;
    Vec3f whipAttachNormal;

    // Time Gate
    u8 timeGateItemVisible;
    u8 timeGatePortalActive;
    f32 timeGatePortalAlpha;
    f32 timeGatePortalScale;

    // Switch Hook
    u8 switchHookState;
    Vec3f switchHookProjPos;
} CustomItemVisualSync;

/**
 * Build visual sync struct from current gCustomItemState (sender side).
 */
void CustomItems_BuildVisualSync(CustomItemVisualSync* out);

/**
 * Apply visual sync struct to gCustomItemState (receiver side, temporary override).
 */
void CustomItems_ApplyVisualSync(const CustomItemVisualSync* sync);

/**
 * Initialize custom items system.
 * @param play PlayState instance
 * @param player Player instance
 */
void CustomItems_Init(PlayState* play, Player* player);

/**
 * Update all custom items. Called every frame.
 * @param player Player instance
 * @param play PlayState instance
 */
void CustomItems_Update(Player* player, PlayState* play);

/**
 * Override player draw for custom item models.
 * @param player Player instance
 * @param play PlayState instance
 * @return 1 if draw was overridden
 */
s32 CustomItems_OverrideDraw(Player* player, PlayState* play);

/**
 * Check if custom item activation is blocked.
 * @param player Player instance
 * @param play PlayState instance
 * @return 1 if blocked
 */
s32 CustomItems_IsBlocked(Player* player, PlayState* play);

// Item handlers
void Handle_RocsFeather(Player* player, PlayState* play);
void Handle_RocsCape(Player* player, PlayState* play);
void Handle_DekuLeaf(Player* player, PlayState* play);
void Handle_Spinner(Player* player, PlayState* play);
void Handle_GustJar(Player* player, PlayState* play);
void Handle_BallAndChain(Player* player, PlayState* play);
void Handle_Shovel(Player* player, PlayState* play);
void Handle_DemiseDestruction(Player* player, PlayState* play);
void Handle_Beetle(Player* player, PlayState* play);
void Handle_BombArrows(Player* player, PlayState* play);
void Handle_FireRod(Player* player, PlayState* play);
void Handle_IceRod(Player* player, PlayState* play);
void Handle_LightRod(Player* player, PlayState* play);
void Handle_DominionRod(Player* player, PlayState* play);
void Handle_CaneOfSomaria(Player* player, PlayState* play);
void Handle_MogmaMitts(Player* player, PlayState* play);
void Handle_HyliasGrace(Player* player, PlayState* play);
void Handle_ZonaiPermafrost(Player* player, PlayState* play);
void Handle_TimeGate(Player* player, PlayState* play);
void Handle_Whip(Player* player, PlayState* play);
void Handle_DesireSensor(Player* player, PlayState* play);
void Handle_SwitchHook(Player* player, PlayState* play);
void Handle_MinishCap(Player* player, PlayState* play);

// Draw functions
void CustomItems_DrawDekuLeaf(Player* player, PlayState* play);
void CustomItems_DrawSpinner(Player* player, PlayState* play);
void CustomItems_DrawGustJar(Player* player, PlayState* play);
void CustomItems_DrawBallChain(Player* player, PlayState* play);
void CustomItems_DrawShovel(Player* player, PlayState* play);
void CustomItems_DrawDemiseDestruction(Player* player, PlayState* play);
void CustomItems_DrawBeetle(Player* player, PlayState* play);
void CustomItems_DrawBombArrowsReticle(Player* player, PlayState* play);
void CustomItems_DrawFireRod(Player* player, PlayState* play);
void CustomItems_DrawFireRodReticle(Player* player, PlayState* play);
void CustomItems_DrawIceRod(Player* player, PlayState* play);
void CustomItems_DrawIceRodReticle(Player* player, PlayState* play);
void CustomItems_DrawLightRod(Player* player, PlayState* play);
void CustomItems_DrawLightRodReticle(Player* player, PlayState* play);
void CustomItems_DrawDominionRod(Player* player, PlayState* play);
void CustomItems_DrawDominionRodReticle(Player* player, PlayState* play);
void CustomItems_DrawCaneOfSomaria(Player* player, PlayState* play);
void CustomItems_DrawMogmaMitts(Player* player, PlayState* play);
void CustomItems_DrawWhip(Player* player, PlayState* play);
void CustomItems_DrawTimeGate(Player* player, PlayState* play);
void CustomItems_DrawTimeGatePortal(Player* player, PlayState* play);
void CustomItems_DrawSwitchHook(Player* player, PlayState* play);
void CustomItems_DrawSwitchHookInHand(Player* player, PlayState* play);
void CustomItems_DrawSwitchHookReticle(Player* player, PlayState* play);

// External display lists
extern Gfx* gFireRodBodyDL;
extern Gfx* gFireRodGlowDL;
extern Gfx* gIceRodBodyDL;
extern Gfx* gIceRodGlowDL;
extern Gfx* gLightRodBodyDL;
extern Gfx* gLightRodGlowDL;

// Upper action functions
s32 Player_UpperAction_Shovel(Player* player, PlayState* play);
s32 Player_UpperAction_DemiseDestruction(Player* player, PlayState* play);
s32 Player_UpperAction_SwitchHook(Player* player, PlayState* play);

// Init functions
void Player_InitSpinnerIA(PlayState* play, Player* player);
void Player_InitBallAndChainIA(PlayState* play, Player* player);
void Player_InitGustJarIA(PlayState* play, Player* player);
void Player_InitDemiseDestructionIA(PlayState* play, Player* player);
void Player_InitBeetleIA(PlayState* play, Player* player);
void Player_InitBombArrowsIA(PlayState* play, Player* player);
void Player_InitFireRodIA(PlayState* play, Player* player);
void Player_InitIceRodIA(PlayState* play, Player* player);
void Player_InitLightRodIA(PlayState* play, Player* player);
void Player_InitDominionRodIA(PlayState* play, Player* player);
void Player_InitCaneOfSomariaIA(PlayState* play, Player* player);
void Player_InitMogmaMittsIA(PlayState* play, Player* player);
void Player_InitHyliasGraceIA(PlayState* play, Player* player);
void Player_InitZonaiPermafrostIA(PlayState* play, Player* player);
void Player_InitTimeGateIA(PlayState* play, Player* player);
void Player_InitWhipIA(PlayState* play, Player* player);
void Player_InitDesireSensorIA(PlayState* play, Player* player);
void Player_InitSwitchHookIA(PlayState* play, Player* player);
void Player_InitMinishCapIA(PlayState* play, Player* player);

#ifdef __cplusplus
}
#endif

#endif // CUSTOM_ITEMS_H
