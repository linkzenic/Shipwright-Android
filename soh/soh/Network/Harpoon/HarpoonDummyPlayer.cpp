#include "Harpoon.h"
#include "soh/Enhancements/nametag.h"
#include "soh/frame_interpolation.h"

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "mods/transformation_masks/assets/mm_asset_loader.h"
#include "mods/transformation_masks/mm_mask_wear.h"
#include "mods/anim_translator/mm_anim_loader.h"
#include "mods/mm_sources/objects/object_link_goron.h"
#include "mods/mm_sources/objects/object_link_zora.h"
#include "mods/mm_sources/objects/object_link_nuts.h"
#include "mods/mm_sources/objects/object_link_boy.h"
#include "mods/items/custom_items.h"
#include "mods/actors/somaria_cubes.h"
extern PlayState* gPlayState;

void Player_UseItem(PlayState* play, Player* player, s32 item);
void Player_Draw(Actor* actor, PlayState* play);
}

// =============================================================================
// MM Form Skeleton Cache for Remote Players
// =============================================================================

// Per-form skeleton cache (shared across all remote players)
// Index: 0=FD, 1=Goron, 2=Zora, 3=Deku
static struct {
    SkelAnime skelAnime;
    s32 dListCount;
    bool loaded;
    bool attempted;
} sRemoteMmSkel[4];

static const char* sRemoteMmSkelPaths[4] = {
    gLinkFierceDeitySkel, // [0] = FD
    gLinkGoronSkel,       // [1] = Goron
    gLinkZoraSkel,        // [2] = Zora
    gLinkDekuSkel,        // [3] = Deku
};

static const s32 sRemoteMmLimbCount[4] = { 22, 22, 22, 22 };

// Idle animation paths per form (from mm_player_form.cpp sFormProps lines 186-214)
// Required by SkelAnime_InitLink which calls LinkAnimation_Change internally
static const char* sRemoteMmIdleAnimPaths[4] = {
    "misc/link_animetion/gPlayerAnim_link_fighter_wait_long_Data", // [0] FD - 32 frames
    "misc/link_animetion/gPlayerAnim_pg_wait_Data",                // [1] Goron - 79 frames
    "misc/link_animetion/gPlayerAnim_pz_wait_Data",                // [2] Zora - 80 frames
    "misc/link_animetion/gPlayerAnim_link_normal_wait_free_Data",  // [3] Deku - 72 frames
};
static const s16 sRemoteMmIdleAnimFrames[4] = { 32, 79, 80, 72 };

// Per-form rootAnimScale (from mm_player_form.cpp sFormProps).
// Scales root bone position so the skeleton sits at the correct height on the ground.
// Index: 0=FD (excluded), 1=Goron, 2=Zora, 3=Deku
static const f32 sRemoteMmRootAnimScale[4] = { 1.5f, 0.74f, 1.0f, 0.3f };

// OverrideLimbDraw callback for remote MM skeletons: applies rootAnimScale on root limb
// (from MmForm_OverrideLimbDraw in mm_player_form.cpp line 9662)
static s32 sRemoteMmCurrentCacheIdx = 0; // Set before SkelAnime_DrawFlexOpa call

static s32 RemoteMmForm_OverrideLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot,
                                         void* thisx) {
    if (limbIndex == 1) { // Root limb (1-based index)
        s32 idx = sRemoteMmCurrentCacheIdx;
        // FD (cacheIdx 0) is excluded from rootAnimScale (scale=1.5 but handled via actor.scale)
        if (idx > 0 && idx <= 3) {
            f32 scale = sRemoteMmRootAnimScale[idx];
            pos->x *= scale;
            pos->y *= scale;
            pos->z *= scale;
        }
    }
    return 0;
}

static void EnsureRemoteMmSkelLoaded(PlayState* play, u8 modelType) {
    if (modelType == 0 || modelType > 4)
        return;

    // Remap: modelType 1=Goron→[1], 2=Zora→[2], 3=Deku→[3], 4=FD→[0]
    s32 cacheIdx = (modelType == 4) ? 0 : modelType;
    if (cacheIdx < 0 || cacheIdx > 3)
        return;

    if (sRemoteMmSkel[cacheIdx].loaded || sRemoteMmSkel[cacheIdx].attempted)
        return;
    sRemoteMmSkel[cacheIdx].attempted = true;

    if (!MmAssets_IsAvailable())
        return;

    const char* skelPath = sRemoteMmSkelPaths[cacheIdx];
    if (skelPath == NULL)
        return;

    FlexSkeletonHeader* skelHeader = (FlexSkeletonHeader*)MmAssets_LoadResource(skelPath);
    if (skelHeader == NULL)
        return;

    // Load idle animation from mm.o2r (SkelAnime_InitLink REQUIRES a valid animation;
    // passing NULL crashes in LinkAnimation_Change → AnimationContext_SetLoadFrame)
    LinkAnimationHeader* idleAnim =
        MmAnim_LoadByPath(sRemoteMmIdleAnimPaths[cacheIdx], sRemoteMmIdleAnimFrames[cacheIdx], 22);
    if (idleAnim == NULL)
        return;

    SkelAnime_InitLink(play, &sRemoteMmSkel[cacheIdx].skelAnime, skelHeader, idleAnim, 9, NULL, NULL,
                       sRemoteMmLimbCount[cacheIdx]);
    sRemoteMmSkel[cacheIdx].dListCount = sRemoteMmSkel[cacheIdx].skelAnime.dListCount;
    sRemoteMmSkel[cacheIdx].loaded = true;
}

// Eye texture paths per form: [cacheIdx][eyeIdx] (0=open, 1=half, 2=closed)
static const char* sRemoteFormEyeTextures[4][3] = {
    // [0] FD: no dynamic eyes
    { NULL, NULL, NULL },
    // [1] Goron
    { gLinkGoronEyesOpenTex, gLinkGoronEyesHalfTex, gLinkGoronEyesClosedTex },
    // [2] Zora
    { gLinkZoraEyesOpenTex, gLinkZoraEyesHalfTex, gLinkZoraEyesClosedTex },
    // [3] Deku: no dynamic eyes
    { NULL, NULL, NULL },
};

// MM stateFlags3 bit for Goron roll active
#define MM_PLAYER_STATE3_80000 (1 << 19)

static void HarpoonDummyPlayer_DrawMmForm(Actor* actor, PlayState* play, HarpoonClient& client) {
    Player* player = (Player*)actor;

    // Remap modelType to cache index
    s32 cacheIdx = (client.transformation == 4) ? 0 : client.transformation;
    if (cacheIdx < 0 || cacheIdx > 3)
        return;

    // Ensure skeleton loaded from mm.o2r
    EnsureRemoteMmSkelLoaded(play, client.transformation);
    if (!sRemoteMmSkel[cacheIdx].loaded)
        return;

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    // Segment 0x0C = gCullBackDList (required by ALL MM DLs)
    gSPSegment(POLY_OPA_DISP++, 0x0C, (uintptr_t)gCullBackDList);
    gSPSegment(POLY_XLU_DISP++, 0x0C, (uintptr_t)gCullBackDList);
    // Safety: init segment 0x08 to empty on XLU
    gSPSegment(POLY_XLU_DISP++, 0x08, (uintptr_t)gEmptyDL);

    // Damage flicker (red fog oscillation)
    if (player->invincibilityTimer > 0) {
        s32 flickerValue = CLAMP(50 - player->invincibilityTimer, 8, 40);
        player->damageFlickerAnimCounter += flickerValue;
        s32 fogDist = 4000 - (s32)(Math_CosS(player->damageFlickerAnimCounter * 256) * 2000.0f);
        POLY_OPA_DISP = Gfx_SetFog2(POLY_OPA_DISP, 255, 0, 0, 0, 0, fogDist);
    }

    // Is Goron rolling? Check mmStateFlags3 roll bit
    u8 isGoronRolling = (client.transformation == 1) && (client.mmStateFlags3 & MM_PLAYER_STATE3_80000);

    if (isGoronRolling) {
        // === GORON BALL FORM ===
        // Build matrix from scratch (from MmForm_Draw ball draw path)
        f32 yOffset = 1200.0f * actor->scale.y;

        Matrix_Translate(actor->world.pos.x, actor->world.pos.y + yOffset, actor->world.pos.z, MTXMODE_NEW);
        Matrix_RotateY(actor->shape.rot.y * ((f32)(M_PI / 0x8000)), MTXMODE_APPLY);
        Matrix_RotateZ(actor->shape.rot.z * ((f32)(M_PI / 0x8000)), MTXMODE_APPLY);

        f32 sq = client.rollSquash;
        Matrix_Scale(actor->scale.x * 1.15f * (1.0f + sq), actor->scale.y * 1.15f * (1.0f - sq),
                     actor->scale.z * 1.15f * (1.0f + sq), MTXMODE_APPLY);

        Matrix_RotateX(actor->shape.rot.x * ((f32)(M_PI / 0x8000)), MTXMODE_APPLY);

        gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                  G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gLinkGoronCurledDL);

        // V1: skip spikes/energy effects (requires segment 0x08 TwoTexScroll setup)
    } else {
        // === SKELETON FORM ===
        // Set eye texture on segment 0x08
        u8 eyeIdx = client.eyeIndex;
        if (eyeIdx > 2)
            eyeIdx = 0;

        const char* eyeTex = sRemoteFormEyeTextures[cacheIdx][eyeIdx];
        if (eyeTex != NULL) {
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)eyeTex);
        }

        // Zora mouth texture
        if (client.transformation == 2) {
            gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)gLinkZoraMouthClosedTex);
        }

        // Copy client jointTable into the cached skeleton's jointTable
        SkelAnime* skel = &sRemoteMmSkel[cacheIdx].skelAnime;
        s32 copyCount = skel->limbCount + 2; // LIMB_BUF_COUNT
        if (copyCount > 24)
            copyCount = 24;
        memcpy(skel->jointTable, client.jointTable, sizeof(Vec3s) * copyCount);

        // Draw MM skeleton with rootAnimScale callback (positions form on the ground)
        sRemoteMmCurrentCacheIdx = cacheIdx;
        SkelAnime_DrawFlexOpa(play, skel->skeleton, skel->jointTable, sRemoteMmSkel[cacheIdx].dListCount,
                              RemoteMmForm_OverrideLimbDraw, NULL, actor);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

static DamageTable HarpoonDummyPlayerDamageTable = {
    /* Deku nut      */ DMG_ENTRY(0, HARPOON_HIT_RESPONSE_STUN),
    /* Deku stick    */ DMG_ENTRY(2, HARPOON_HIT_RESPONSE_NORMAL),
    /* Slingshot     */ DMG_ENTRY(1, HARPOON_HIT_RESPONSE_NORMAL),
    /* Explosive     */ DMG_ENTRY(2, HARPOON_HIT_RESPONSE_NORMAL),
    /* Boomerang     */ DMG_ENTRY(0, HARPOON_HIT_RESPONSE_STUN),
    /* Normal arrow  */ DMG_ENTRY(2, HARPOON_HIT_RESPONSE_NORMAL),
    /* Hammer swing  */ DMG_ENTRY(2, PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE),
    /* Hookshot      */ DMG_ENTRY(0, HARPOON_HIT_RESPONSE_STUN),
    /* Kokiri sword  */ DMG_ENTRY(1, HARPOON_HIT_RESPONSE_NORMAL),
    /* Master sword  */ DMG_ENTRY(2, HARPOON_HIT_RESPONSE_NORMAL),
    /* Giant's Knife */ DMG_ENTRY(4, HARPOON_HIT_RESPONSE_NORMAL),
    /* Fire arrow    */ DMG_ENTRY(2, HARPOON_HIT_RESPONSE_FIRE),
    /* Ice arrow     */ DMG_ENTRY(4, PLAYER_HIT_RESPONSE_ICE_TRAP),
    /* Light arrow   */ DMG_ENTRY(2, PLAYER_HIT_RESPONSE_ELECTRIC_SHOCK),
    /* Unk arrow 1   */ DMG_ENTRY(2, PLAYER_HIT_RESPONSE_NONE),
    /* Unk arrow 2   */ DMG_ENTRY(2, PLAYER_HIT_RESPONSE_NONE),
    /* Unk arrow 3   */ DMG_ENTRY(2, PLAYER_HIT_RESPONSE_NONE),
    /* Fire magic    */ DMG_ENTRY(0, HARPOON_HIT_RESPONSE_FIRE),
    /* Ice magic     */ DMG_ENTRY(3, PLAYER_HIT_RESPONSE_ICE_TRAP),
    /* Light magic   */ DMG_ENTRY(0, PLAYER_HIT_RESPONSE_ELECTRIC_SHOCK),
    /* Shield        */ DMG_ENTRY(0, PLAYER_HIT_RESPONSE_NONE),
    /* Mirror Ray    */ DMG_ENTRY(0, PLAYER_HIT_RESPONSE_NONE),
    /* Kokiri spin   */ DMG_ENTRY(1, HARPOON_HIT_RESPONSE_NORMAL),
    /* Giant spin    */ DMG_ENTRY(4, HARPOON_HIT_RESPONSE_NORMAL),
    /* Master spin   */ DMG_ENTRY(2, HARPOON_HIT_RESPONSE_NORMAL),
    /* Kokiri jump   */ DMG_ENTRY(2, HARPOON_HIT_RESPONSE_NORMAL),
    /* Giant jump    */ DMG_ENTRY(8, HARPOON_HIT_RESPONSE_NORMAL),
    /* Master jump   */ DMG_ENTRY(4, HARPOON_HIT_RESPONSE_NORMAL),
    /* Unknown 1     */ DMG_ENTRY(0, PLAYER_HIT_RESPONSE_NONE),
    /* Unblockable   */ DMG_ENTRY(0, PLAYER_HIT_RESPONSE_NONE),
    /* Hammer jump   */ DMG_ENTRY(4, PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE),
    /* Unknown 2     */ DMG_ENTRY(0, PLAYER_HIT_RESPONSE_NONE),
};

static void Math_Vec3s_Copy_Harpoon(Vec3s* dest, Vec3s* src) {
    dest->x = src->x;
    dest->y = src->y;
    dest->z = src->z;
}

// =============================================================================
// Remote Somaria Cube Management
// =============================================================================

static void HarpoonRemoteCubes_Kill(PlayState* play, HarpoonClient& client) {
    for (int i = 0; i < 3; i++) {
        if (client.remoteCubeActors[i] != NULL) {
            Actor_Kill(client.remoteCubeActors[i]);
            client.remoteCubeActors[i] = NULL;
        }
    }
}

static void HarpoonRemoteCubes_Sync(PlayState* play, HarpoonClient& client) {
    // Despawn cubes that no longer exist on the remote player
    for (int i = 0; i < 3; i++) {
        if (i >= client.remoteCubeCount || client.remoteCubes[i].state == 0) {
            if (client.remoteCubeActors[i] != NULL) {
                Actor_Kill(client.remoteCubeActors[i]);
                client.remoteCubeActors[i] = NULL;
            }
        }
    }

    // Spawn or update cubes
    for (int i = 0; i < (int)client.remoteCubeCount && i < 3; i++) {
        auto& cube = client.remoteCubes[i];
        if (cube.state == 0)
            continue;

        // Held cubes: hide (they're attached to the remote player's hands visually)
        if (cube.state == 3) { // SOMARIA_STATE_HELD
            if (client.remoteCubeActors[i] != NULL) {
                client.remoteCubeActors[i]->world.pos.y = -9999.0f;
            }
            continue;
        }

        if (client.remoteCubeActors[i] == NULL) {
            // Spawn new remote cube
            client.remoteCubeActors[i] = SomariaCube_SpawnRemote(play, &cube.pos, cube.rotY, cube.form);
        }

        if (client.remoteCubeActors[i] != NULL) {
            SomariaCube_UpdateRemotePos(client.remoteCubeActors[i], &cube.pos, cube.scale, cube.rotY);
        }
    }
}

void HarpoonDummyPlayer_Init(Actor* actor, PlayState* play) {
    Player* player = (Player*)actor;

    uint32_t clientId = Harpoon::Instance->GetDummyPlayerClientId(actor);

    if (!Harpoon::Instance->clients.contains(clientId)) {
        Actor_Kill(actor);
        return;
    }

    HarpoonClient& client = Harpoon::Instance->clients[clientId];

    s32 originalAge = gSaveContext.linkAge;
    gSaveContext.linkAge = client.linkAge;

    actor->room = -1;
    player->itemAction = player->heldItemAction = -1;
    player->heldItemId = ITEM_NONE;
    Player_UseItem(play, player, ITEM_NONE);
    Player_SetModelGroup(player, Player_ActionToModelGroup(player, player->heldItemAction));
    play->playerInit(player, play, gPlayerSkelHeaders[client.linkAge]);
    play->func_11D54(player, play);

    player->cylinder.base.acFlags = AC_ON | AC_TYPE_PLAYER;
    player->cylinder.base.ocFlags2 = OC2_TYPE_1;
    player->cylinder.info.bumperFlags = BUMP_ON | BUMP_HOOKABLE | BUMP_NO_HITMARK;
    player->actor.flags |= ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER;
    player->cylinder.dim.radius = 30;
    player->actor.colChkInfo.damageTable = &HarpoonDummyPlayerDamageTable;

    gSaveContext.linkAge = originalAge;

    NameTag_RegisterForActorWithOptions(actor, client.name.c_str(), {});
}

void HarpoonDummyPlayer_Update(Actor* actor, PlayState* play) {
    Player* player = (Player*)actor;

    uint32_t clientId = Harpoon::Instance->GetDummyPlayerClientId(actor);

    if (!Harpoon::Instance->clients.contains(clientId)) {
        Actor_Kill(actor);
        return;
    }

    HarpoonClient& client = Harpoon::Instance->clients[clientId];

    if (client.sceneNum != gPlayState->sceneNum || !client.online || !client.isSaveLoaded) {
        actor->world.pos.x = -9999.0f;
        actor->world.pos.y = -9999.0f;
        actor->world.pos.z = -9999.0f;
        actor->shape.shadowAlpha = 0;
        HarpoonRemoteCubes_Kill(play, client);
        return;
    }

    actor->shape.shadowAlpha = 255;

    Math_Vec3s_Copy_Harpoon(&player->upperLimbRot, &client.upperLimbRot);
    Math_Vec3s_Copy_Harpoon(&actor->shape.rot, &client.posRot.rot);
    Math_Vec3f_Copy(&actor->world.pos, &client.posRot.pos);
    player->skelAnime.jointTable = client.jointTable;
    player->skelAnime.movementFlags = client.movementFlags;
    Math_Vec3s_Copy_Harpoon(&player->skelAnime.prevTransl, &client.prevTransl);
    player->currentBoots = client.currentBoots;
    player->currentShield = client.currentShield;
    player->currentTunic = client.currentTunic;
    player->stateFlags1 = client.stateFlags1;
    player->stateFlags2 = client.stateFlags2;
    player->itemAction = client.itemAction;
    player->heldItemAction = client.heldItemAction;
    player->invincibilityTimer = client.invincibilityTimer;
    player->unk_862 = client.unk_862;
    player->unk_85C = client.unk_85C;
    player->av1.actionVar1 = client.actionVar1;

    // OOT visual state
    player->currentMask = client.currentMask;
    player->actor.shape.face = client.face;
    player->actor.scale.x = client.scaleX;
    player->actor.scale.y = client.scaleY;
    player->actor.scale.z = client.scaleZ;

    // MM forms use yOffset=0 (set during local form init in mm_player_form.cpp:2048).
    // Without this, Actor_Draw adds the default OOT yOffset to the matrix Y position,
    // causing the MM skeleton to float above the ground.
    if (client.transformation != 0) {
        player->actor.shape.yOffset = 0.0f;
    }

    // Update collider dimensions from transformation data
    if (client.cylRadius > 0) {
        player->cylinder.dim.radius = client.cylRadius;
    }
    if (client.cylHeight > 0) {
        player->cylinder.dim.height = client.cylHeight;
    }
    player->cylinder.dim.yShift = client.cylYShift;

    // Apply animation movement
    Vec3f diff;
    SkelAnime_UpdateTranslation(&player->skelAnime, &diff, player->actor.shape.rot.y);

    if (player->skelAnime.movementFlags & 1) {
        if (!LINK_IS_ADULT) {
            diff.x *= 0.64f;
            diff.z *= 0.64f;
        }
        player->actor.world.pos.x += diff.x * player->actor.scale.x;
        player->actor.world.pos.z += diff.z * player->actor.scale.z;
    }

    if (player->skelAnime.movementFlags & 2) {
        if (!(player->skelAnime.movementFlags & 4)) {
            diff.y *= player->ageProperties->unk_08;
        }
        player->actor.world.pos.y += diff.y * player->actor.scale.y;
    }

    if (player->modelGroup != client.modelGroup) {
        s32 originalAge = gSaveContext.linkAge;
        gSaveContext.linkAge = client.linkAge;
        u8 originalButtonItem0 = gSaveContext.equips.buttonItems[0];
        gSaveContext.equips.buttonItems[0] = client.buttonItem0;
        Player_SetModelGroup(player, client.modelGroup);
        gSaveContext.linkAge = originalAge;
        gSaveContext.equips.buttonItems[0] = originalButtonItem0;
    }

    actor->flags &= ~ACTOR_FLAG_LOCK_ON_DISABLED;

    if (player->cylinder.base.acFlags & AC_HIT && player->invincibilityTimer == 0) {
        Harpoon::Instance->SendPacket_Damage(client.clientId, player->actor.colChkInfo.damageEffect,
                                             player->actor.colChkInfo.damage);
        if (player->actor.colChkInfo.damageEffect == HARPOON_HIT_RESPONSE_STUN) {
            Actor_SetColorFilter(&player->actor, 0, 0xFF, 0, 24);
        } else {
            player->invincibilityTimer = 20;
        }
    }

    Collider_UpdateCylinder(&player->actor, &player->cylinder);

    if (!(player->stateFlags2 & PLAYER_STATE2_FROZEN)) {
        if (!(player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_HANGING_OFF_LEDGE |
                                     PLAYER_STATE1_CLIMBING_LEDGE | PLAYER_STATE1_ON_HORSE))) {
            CollisionCheck_SetOC(play, &play->colChkCtx, &player->cylinder.base);
        }

        if (!(player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_DAMAGED)) &&
            (player->invincibilityTimer <= 0)) {
            CollisionCheck_SetAC(play, &play->colChkCtx, &player->cylinder.base);

            if (player->invincibilityTimer < 0) {
                CollisionCheck_SetAT(play, &play->colChkCtx, &player->cylinder.base);
            }
        }
    }

    if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_IN_CUTSCENE)) {
        player->actor.colChkInfo.mass = MASS_IMMOVABLE;
    } else {
        player->actor.colChkInfo.mass = 50;
    }

    Collider_ResetCylinderAC(play, &player->cylinder.base);

    // Sync remote somaria cubes for this client
    HarpoonRemoteCubes_Sync(play, client);
}

void HarpoonDummyPlayer_Draw(Actor* actor, PlayState* play) {
    Player* player = (Player*)actor;

    uint32_t clientId = Harpoon::Instance->GetDummyPlayerClientId(actor);

    if (!Harpoon::Instance->clients.contains(clientId)) {
        Actor_Kill(actor);
        return;
    }

    HarpoonClient& client = Harpoon::Instance->clients[clientId];

    if (client.sceneNum != gPlayState->sceneNum || !client.online || !client.isSaveLoaded) {
        return;
    }

    // If transformation != 0, draw MM form instead of OOT Link
    if (client.transformation != 0) {
        HarpoonDummyPlayer_DrawMmForm(actor, play, client);
        return;
    }

    s32 originalAge = gSaveContext.linkAge;
    gSaveContext.linkAge = client.linkAge;
    u8 originalButtonItem0 = gSaveContext.equips.buttonItems[0];
    gSaveContext.equips.buttonItems[0] = client.buttonItem0;

    // Override MM worn mask so TransformMasks_WearDraw (called from Player_PostLimbDrawGameplay
    // at HEAD limb) draws the REMOTE player's mask, not the local player's.
    s32 savedWornMask = MmMaskWear_GetCurrent();
    MmMaskWear_SetCurrent(client.wornMask);

    // Save/set/restore custom item state so remote player's items draw correctly
    CustomItemVisualSync savedCustomItems;
    CustomItems_BuildVisualSync(&savedCustomItems);
    {
        CustomItemVisualSync remoteCustomItems;
        memset(&remoteCustomItems, 0, sizeof(remoteCustomItems));
        remoteCustomItems.activeFlags = client.customItemFlags;
        // Beetle
        remoteCustomItems.beetlePos = client.ciBeetlePos;
        remoteCustomItems.beetleRot = client.ciBeetleRot;
        remoteCustomItems.beetleWingScale = client.ciBeetleWingScale;
        remoteCustomItems.beetleState = client.ciBeetleState;
        // Gust Jar
        remoteCustomItems.gustJarMode = client.ciGustJarMode;
        remoteCustomItems.gustJarElement = client.ciGustJarElement;
        remoteCustomItems.gustJarBlowActive = client.ciGustJarBlowActive;
        remoteCustomItems.gustJarHeatTimer = client.ciGustJarHeatTimer;
        // Fire Rod
        remoteCustomItems.fireRodProjActive = client.ciFireRodProjActive;
        remoteCustomItems.fireRodProjCount = client.ciFireRodProjCount;
        remoteCustomItems.fireRodProjType = client.ciFireRodProjType;
        remoteCustomItems.fireRodProjScale = client.ciFireRodProjScale;
        remoteCustomItems.fireRodProjPos = client.ciFireRodProjPos;
        remoteCustomItems.fireRodProjPos2 = client.ciFireRodProjPos2;
        remoteCustomItems.fireRodProjPos3 = client.ciFireRodProjPos3;
        // Ice Rod
        remoteCustomItems.iceRodProjActive = client.ciIceRodProjActive;
        remoteCustomItems.iceRodProjCount = client.ciIceRodProjCount;
        remoteCustomItems.iceRodProjScale = client.ciIceRodProjScale;
        remoteCustomItems.iceRodProjPos = client.ciIceRodProjPos;
        remoteCustomItems.iceRodProjPos2 = client.ciIceRodProjPos2;
        remoteCustomItems.iceRodProjPos3 = client.ciIceRodProjPos3;
        // Light Rod
        remoteCustomItems.lightRodProjActive = client.ciLightRodProjActive;
        remoteCustomItems.lightRodProjCount = client.ciLightRodProjCount;
        remoteCustomItems.lightRodProjPos = client.ciLightRodProjPos;
        remoteCustomItems.lightRodProjPos2 = client.ciLightRodProjPos2;
        remoteCustomItems.lightRodProjPos3 = client.ciLightRodProjPos3;
        // Ball and Chain
        remoteCustomItems.ballAndChainThrown = client.ciBallChainThrown;
        remoteCustomItems.timer2 = client.ciTimer2;
        remoteCustomItems.sharedProjectilePos = client.ciSharedProjPos;
        // Whip
        remoteCustomItems.whipState = client.ciWhipState;
        remoteCustomItems.whipTipPos = client.ciWhipTipPos;
        remoteCustomItems.whipAttachPos = client.ciWhipAttachPos;
        remoteCustomItems.whipAttachNormal = client.ciWhipAttachNormal;
        // Deku Leaf
        remoteCustomItems.dekuLeafGliding = client.ciDekuLeafGliding;
        remoteCustomItems.dekuLeafBlowing = client.ciDekuLeafBlowing;
        remoteCustomItems.dekuLeafAnimTimer = client.ciDekuLeafAnimTimer;
        // Shovel
        remoteCustomItems.shovelAnimating = client.ciShovelAnimating;
        // Dominion Rod
        remoteCustomItems.dominionRodState = client.ciDominionRodState;
        remoteCustomItems.dominionRodOrbPos = client.ciDominionRodOrbPos;
        // Switch Hook
        remoteCustomItems.switchHookState = client.ciSwitchHookState;
        remoteCustomItems.switchHookProjPos = client.ciSwitchHookProjPos;
        // Time Gate
        remoteCustomItems.timeGateItemVisible = client.ciTimeGateItemVisible;
        remoteCustomItems.timeGatePortalActive = client.ciTimeGatePortalActive;
        remoteCustomItems.timeGatePortalAlpha = client.ciTimeGatePortalAlpha;
        remoteCustomItems.timeGatePortalScale = client.ciTimeGatePortalScale;

        CustomItems_ApplyVisualSync(&remoteCustomItems);
    }

    Player_Draw((Actor*)player, play);

    // Restore all overridden state
    CustomItems_ApplyVisualSync(&savedCustomItems);
    MmMaskWear_SetCurrent(savedWornMask);
    gSaveContext.linkAge = originalAge;
    gSaveContext.equips.buttonItems[0] = originalButtonItem0;
}

void HarpoonDummyPlayer_Destroy(Actor* actor, PlayState* play) {
    uint32_t clientId = Harpoon::Instance->GetDummyPlayerClientId(actor);
    if (Harpoon::Instance->clients.contains(clientId)) {
        HarpoonRemoteCubes_Kill(play, Harpoon::Instance->clients[clientId]);
    }
}
