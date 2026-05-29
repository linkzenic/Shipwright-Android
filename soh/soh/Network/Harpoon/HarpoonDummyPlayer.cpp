#include "Harpoon.h"
#include "HarpoonSkinSync.h"
#include "Combat/CombatSync.h"
#include "PropHunt/PropHunt.h"
#include "soh/Enhancements/nametag.h"
#include "soh/frame_interpolation.h"

#include <map>
#include <string>
#include <spdlog/spdlog.h>

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
#include "mods/pak_loader/pak_loader.h"
extern PlayState* gPlayState;

void Player_UseItem(PlayState* play, Player* player, s32 item);
void Player_Draw(Actor* actor, PlayState* play);

// Defined in z_player_lib.c:423 with C linkage. Declared here so dummy
// hand-DL refresh can index into it.
extern Gfx** sPlayerDListGroups[];

// Gust Jar VFX particle spawners — exposed in item_gustjar.c, forward-declared
// here because item_gustjar.h uses C99 designated initializers and can't be
// included from C++. Mirrors of the local Handle_GustJar particle calls.
void GustJar_SpawnSuckVFX(PlayState* play, Vec3f* nozzle, s16 aimYaw);
void GustJar_SpawnBlowVFX(PlayState* play, Vec3f* nozzle, s16 aimYaw, u8 element);
#define HARPOON_GUST_MODE_ABSORB 2
#define HARPOON_GUST_MODE_BLOW   3
}

// =============================================================================
// Per-clientId diagnostic memos
// =============================================================================
// Both maps memoize the last logged state per cid so the SPDLOG_INFO calls
// inside Update/Draw don't spam every frame the dummy stays in the same
// state. They erase on state CHANGE but never on disconnect or room-left,
// so without an explicit clear they'd grow by one entry per cid the server
// ever sees. HarpoonDummyPlayer_ClearPerClientDiagnostics drops both.
static std::map<uint32_t, std::string> sLastExileReason;
static std::map<uint32_t, std::string> sLastDecision;

void HarpoonDummyPlayer_ClearPerClientDiagnostics() {
    sLastExileReason.clear();
    sLastDecision.clear();
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

// Damage values are quarter-hearts (multiplied by 8 in HandlePacket_Damage to
// produce the OOT eighth-heart damage unit). Slots line up with the OOT damage
// type bit positions used by AT colliders. Custom items (Fire Rod, Gust Jar,
// sw97 elemental arrows, etc.) typically reuse these standard slots — e.g.
// Fire Rod uses DMG_ARROW_FIRE (slot 11), so Path 1's AC_HIT detection picks
// up its damage automatically. Slots that previously had damage=0 are now
// non-zero so a hit with that bit set actually deals damage in PvP.
static DamageTable HarpoonDummyPlayerDamageTable = {
    /* Deku nut      */ DMG_ENTRY(0, HARPOON_HIT_RESPONSE_STUN),     // stun only
    /* Deku stick    */ DMG_ENTRY(1, HARPOON_HIT_RESPONSE_NORMAL),
    /* Slingshot     */ DMG_ENTRY(1, HARPOON_HIT_RESPONSE_NORMAL),
    /* Explosive     */ DMG_ENTRY(2, PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE), // vanilla bomb
    /* Boomerang     */ DMG_ENTRY(0, PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE), // 0 dmg + big kb
    /* Normal arrow  */ DMG_ENTRY(1, HARPOON_HIT_RESPONSE_NORMAL),
    /* Hammer swing  */ DMG_ENTRY(4, PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE),
    /* Hookshot      */ DMG_ENTRY(1, HARPOON_HIT_RESPONSE_STUN),
    /* Kokiri sword  */ DMG_ENTRY(1, HARPOON_HIT_RESPONSE_NORMAL),
    /* Master sword  */ DMG_ENTRY(2, HARPOON_HIT_RESPONSE_NORMAL),
    /* Giant's Knife */ DMG_ENTRY(4, HARPOON_HIT_RESPONSE_NORMAL),
    /* Fire arrow    */ DMG_ENTRY(2, HARPOON_HIT_RESPONSE_FIRE),     // + burn DOT via status
    /* Ice arrow     */ DMG_ENTRY(0, PLAYER_HIT_RESPONSE_ICE_TRAP),  // freeze only, no damage
    /* Light arrow   */ DMG_ENTRY(4, HARPOON_HIT_RESPONSE_LIGHT),    // dedicated LIGHT type
    /* Unk arrow 1   */ DMG_ENTRY(3, HARPOON_HIT_RESPONSE_DARK),     // sw97 dark arrow + blindness
    /* Unk arrow 2   */ DMG_ENTRY(1, HARPOON_HIT_RESPONSE_SOUL_DRAIN), // sw97 soul arrow
    /* Unk arrow 3   */ DMG_ENTRY(0, HARPOON_HIT_RESPONSE_WIND_PUSH),  // sw97 wind arrow
    /* Fire magic    */ DMG_ENTRY(3, HARPOON_HIT_RESPONSE_FIRE),     // SW97 magic fire
    /* Ice magic     */ DMG_ENTRY(0, PLAYER_HIT_RESPONSE_ICE_TRAP),  // freeze 5s
    /* Light magic   */ DMG_ENTRY(0, HARPOON_HIT_RESPONSE_LIGHT),    // heals friendlies via status
    /* Shield        */ DMG_ENTRY(0, PLAYER_HIT_RESPONSE_NONE),
    /* Mirror Ray    */ DMG_ENTRY(0, PLAYER_HIT_RESPONSE_NONE),
    /* Kokiri spin   */ DMG_ENTRY(1, HARPOON_HIT_RESPONSE_NORMAL),
    /* Giant spin    */ DMG_ENTRY(4, HARPOON_HIT_RESPONSE_NORMAL),
    /* Master spin   */ DMG_ENTRY(2, HARPOON_HIT_RESPONSE_NORMAL),
    /* Kokiri jump   */ DMG_ENTRY(2, HARPOON_HIT_RESPONSE_NORMAL),
    /* Giant jump    */ DMG_ENTRY(8, HARPOON_HIT_RESPONSE_NORMAL),
    /* Master jump   */ DMG_ENTRY(4, HARPOON_HIT_RESPONSE_NORMAL),
    /* Unknown 1     */ DMG_ENTRY(0, HARPOON_HIT_RESPONSE_WIND_BLOW), // Deku Leaf gust / Gust Jar — zero dmg, big horizontal launch
    /* Unblockable   */ DMG_ENTRY(4, HARPOON_HIT_RESPONSE_NORMAL), // FD beam, custom heavy
    /* Hammer jump   */ DMG_ENTRY(6, PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE),
    /* Unknown 2     */ DMG_ENTRY(2, HARPOON_HIT_RESPONSE_NORMAL),
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
    // ocFlags2 is the OC type tag the dummy presents to OTHER actors.
    // OC2_TYPE_1 (not OC2_TYPE_PLAYER) so pushable boxes / grass / etc.
    // that look for `ocFlags1 & OC1_TYPE_PLAYER` don't classify the
    // dummy as a real player. Otherwise every peer's dummy pushes the
    // same block on the local machine, producing the "all Links push it
    // together" bug the user reported.
    player->cylinder.base.ocFlags2 = OC2_TYPE_1;
    // ocFlags1 is the set of OC2 types the dummy can collide with. Strip
    // it down to OC1_ON only (no type bits) so the dummy doesn't actively
    // push ANY OC2 actor. The dummy is still bumpable by the local Player
    // (whose ocFlags1 = OC1_TYPE_ALL includes OC1_TYPE_1, matching the
    // dummy's OC2_TYPE_1), so peers can still walk into each other; they
    // just can't shove blocks / grass / wooden crates / Goron statues.
    player->cylinder.base.ocFlags1 = OC1_ON;
    player->cylinder.info.bumperFlags = BUMP_ON | BUMP_HOOKABLE | BUMP_NO_HITMARK;
    player->actor.flags |= ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER;
    player->cylinder.dim.radius = 30;
    player->actor.colChkInfo.damageTable = &HarpoonDummyPlayerDamageTable;

    // Boost render distance so distant teammates remain visible. Default
    // Player_Init values (z_actor.c:1255-1257: 1000 / 350 / 700) cull the
    // dummy as soon as the local camera moves a screen or two away, which
    // makes large open scenes (Hyrule Field, Lake Hylia, Gerudo Valley) feel
    // empty. Multiplying by ~5-8x keeps dummies on-screen across most overworld
    // distances without disturbing local-player behaviour (these fields are
    // per-actor; only this dummy's culling envelope grows).
    player->actor.uncullZoneForward  = 8000.0f;
    player->actor.uncullZoneScale    = 2000.0f;
    player->actor.uncullZoneDownward = 2000.0f;

    gSaveContext.linkAge = originalAge;

    // Prop Hunt: hiders' identities must stay anonymous so seekers can't tell
    // who is who by reading nametags. Triforce Thief (and everything else):
    // names are visible above remote dummies so players can identify each
    // other and the carrier.
    bool hideNames = (Harpoon::Instance != nullptr &&
                      Harpoon::Instance->currentRoomGameMode == "prop_hunt");
    if (!hideNames) {
        NameTag_RegisterForActorWithOptions(actor, client.name.c_str(), {});
    }
}

void HarpoonDummyPlayer_Update(Actor* actor, PlayState* play) {
    Player* player = (Player*)actor;

    uint32_t clientId = Harpoon::Instance->GetDummyPlayerClientId(actor);

    if (!Harpoon::Instance->clients.contains(clientId)) {
        Actor_Kill(actor);
        return;
    }

    HarpoonClient& client = Harpoon::Instance->clients[clientId];

    // Memo of last exile reason per cid (sLastExileReason is file-scope so
    // it can be cleared on disconnect) — keeps the log readable instead of
    // spamming every frame the dummy is hidden. When the dummy goes back to
    // visible we erase the entry so the NEXT exile re-logs.
    if (client.sceneNum != gPlayState->sceneNum || !client.online || !client.isSaveLoaded) {
        const char* reason =
            !client.isSaveLoaded ? "saveNotLoaded" :
            !client.online       ? "offline" :
                                   "sceneMismatch";
        char buf[128];
        snprintf(buf, sizeof(buf), "%s(their=%d mine=%d)",
                 reason, client.sceneNum, gPlayState->sceneNum);
        std::string r = buf;
        auto it = sLastExileReason.find(clientId);
        if (it == sLastExileReason.end() || it->second != r) {
            sLastExileReason[clientId] = r;
            SPDLOG_INFO("[Harpoon] dummy EXILE cid={} '{}' reason={}",
                        clientId, client.name, r);
        }
        actor->world.pos.x = -9999.0f;
        actor->world.pos.y = -9999.0f;
        actor->world.pos.z = -9999.0f;
        actor->shape.shadowAlpha = 0;
        HarpoonRemoteCubes_Kill(play, client);
        return;
    }
    if (sLastExileReason.erase(clientId) > 0) {
        SPDLOG_INFO("[Harpoon] dummy VISIBLE cid={} '{}'", clientId, client.name);
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
    // Hand types — direct sync so dummy's hand DL selection (open vs closed
    // vs holding-sword vs holding-bow etc.) matches what the remote is
    // actually doing. Player_OverrideLimbDrawGameplayCommon reads these at
    // root limb to drive sLeftHandType / sRightHandType in z_player_lib.c.
    player->leftHandType = client.leftHandType;
    player->rightHandType = client.rightHandType;
    player->sheathType = client.sheathType;
    // Refresh the actual DL pointer arrays the engine uses based on hand type.
    // Without this, leftHandDLists still points at the prior modelGroup's
    // arrays even though leftHandType was updated. Defined in z_player_lib.c:423
    // as `Gfx** sPlayerDListGroups[PLAYER_MODELTYPE_MAX]`.
    // sPlayerDListGroups is declared at file scope under extern "C" above.
    // PLAYER_MODELTYPE_MAX = 21 (z64player.h:348). Bound check before
    // indexing to avoid OOB if the remote sent a corrupt value.
    if (player->leftHandType < 21) {
        player->leftHandDLists = &sPlayerDListGroups[player->leftHandType][client.linkAge];
    }
    if (player->rightHandType < 21) {
        player->rightHandDLists = &sPlayerDListGroups[player->rightHandType][client.linkAge];
    }
    if (player->sheathType < 21) {
        player->sheathDLists = &sPlayerDListGroups[player->sheathType][client.linkAge];
    }
    // Per-frame animation/state sync — these all flow into the engine's
    // per-limb DL selection at draw time (Player_OverrideLimbDrawGameplayCommon).
    // Without these the dummy's hands stay in their default pose even when
    // the remote is running, drawing a bow, holding an item, or in FP.
    player->actor.speedXZ = client.speedXZ;
    player->meleeWeaponState = client.meleeWeaponState;
    player->unk_6AD = client.fpModeFlag;
    player->unk_858 = client.bowStringDraw;
    player->unk_860 = client.bowArrowState;
    player->unk_834 = client.bowDrawAnimFrame;
    Math_Vec3s_Copy_Harpoon(&player->headLimbRot, &client.headLimbRot);
    player->upperLimbYawSecondary = client.upperLimbYawSecondary;
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

    // Prop Hunt cylinder sizing: match the visible prop's scale so small
    // props (rupee, mushroom) have a small hitbox that doesn't bump the
    // seeker just by walking near, and big props (chest, boulder) have a
    // big enough hitbox that sword swings at the visible edge connect.
    // Base 30u radius / 60u height = vanilla Link. Variant.scale=1.0 is
    // Link-sized; smaller props shrink the cylinder proportionally,
    // bigger props expand it. yShift=0 keeps the cylinder anchored to
    // the floor where the prop visual sits.
    if (Harpoon::Instance != nullptr && Harpoon::Instance->isPropHuntMode &&
        client.propIndex >= 0) {
        s32 mapIdx = Harpoon::Instance->confirmedMapIndex;
        if (mapIdx < 0) mapIdx = 0;
        f32 propScale = HarpoonPropHunt::GetPropVisualScale(
            client.propCategory, client.propIndex, client.propState, mapIdx);
        // Clamp to a sane band: too small and seekers can't ever hit;
        // too big and a chest hider becomes a wall.
        if (propScale < 0.3f) propScale = 0.3f;
        if (propScale > 2.5f) propScale = 2.5f;
        player->cylinder.dim.radius = (s16)(30.0f * propScale);
        player->cylinder.dim.height = (s16)(60.0f * propScale);
        player->cylinder.dim.yShift = 0;
    }

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

    // Z-target gating per gamemode capability flag (loaded from gamemode.yaml
    // default_config / seeded per known mode at room-join). Triforce Thief
    // sets supports_z_target=true so thieves can lock onto each other; Prop
    // Hunt sets it false so seekers can't auto-lock a disguised hider. Other
    // gamemodes (randomizer/coop) opt in via their manifest.
    bool ztargetable = (Harpoon::Instance != nullptr && Harpoon::Instance->supportsZTarget);
    if (ztargetable) {
        actor->flags &= ~ACTOR_FLAG_LOCK_ON_DISABLED;
    } else {
        actor->flags |= ACTOR_FLAG_LOCK_ON_DISABLED;
    }

    if (player->cylinder.base.acFlags & AC_HIT && player->invincibilityTimer == 0) {
        // PvP routing: only the OWNER of the attacking actor should report
        // damage. If the AT actor that hit this dummy is a remote-mirrored
        // VFX (registered via SetVfxActorOwner with someone else's clientId),
        // suppress the send — the original owner's client will detect the
        // collision against THEIR local dummy and notify the victim. Without
        // this guard, every peer that mirrors the VFX forwards a duplicate
        // damage event, multiplying the hit by the room size.
        Actor* atActor = player->cylinder.base.ac;
        uint32_t atOwner = atActor ? Harpoon::Instance->GetVfxActorOwner(atActor) : 0;
        bool suppressForward = (atOwner != 0 && atOwner != Harpoon::Instance->ownClientId);

        // Broadcast on damage > 0 OR effect != 0. Weapons like Boomerang,
        // Deku Nut, Ice Arrow, Light Magic, Wind Arrow deal 0 damage but
        // carry an effect (stun, freeze, electric shock, heal, push); the
        // old `damage > 0` gate silently dropped these so peers never
        // received the status. Now we broadcast for either condition and
        // the receiver's HandlePacket_Damage / ApplyStatusFromAttacker
        // resolve the effect locally.
        u8 effect = player->actor.colChkInfo.damageEffect;
        u8 damage = player->actor.colChkInfo.damage;
        bool hasEffect = (effect != 0);
        bool hasDamage = (damage > 0);
        if (!suppressForward && Harpoon::Instance->pvpEnabled && (hasDamage || hasEffect)) {
            Harpoon::Instance->SendPacket_Damage(client.clientId, effect, damage);
            HarpoonCombat::ApplyStatusFromAttacker(client.clientId, atActor);
        }
        if (player->actor.colChkInfo.damageEffect == HARPOON_HIT_RESPONSE_STUN) {
            Actor_SetColorFilter(&player->actor, 0, 0xFF, 0, 24);
        } else {
            player->invincibilityTimer = 20;
        }
    }

    Collider_UpdateCylinder(&player->actor, &player->cylinder);

    // Gust Jar VFX replay — the local update path that spawns absorb/blow
    // particles never runs for a dummy. When the remote is in absorb or blow
    // mode, replay the cone particles from the dummy's nozzle so teammates
    // see the wind effect. Mirrors the nozzle offset used by the local update
    // (world.pos + 20Y, then 15 units forward along shape.rot.y).
    if (client.ciGustJarMode == HARPOON_GUST_MODE_ABSORB || client.ciGustJarMode == HARPOON_GUST_MODE_BLOW) {
        Vec3f nozzle = actor->world.pos;
        nozzle.y += 20.0f;
        s16 yaw = actor->shape.rot.y;
        nozzle.x += Math_SinS(yaw) * 15.0f;
        nozzle.z += Math_CosS(yaw) * 15.0f;
        if (client.ciGustJarMode == HARPOON_GUST_MODE_ABSORB) {
            GustJar_SpawnSuckVFX(play, &nozzle, yaw);
        } else {
            GustJar_SpawnBlowVFX(play, &nozzle, yaw, client.ciGustJarElement);
        }
    }

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

    // Prop Hunt: if this remote client is a hider with a selected prop,
    // render them as the prop's ghost actor at their world position and
    // skip the vanilla skeleton draw entirely. The peer broadcasts their
    // (propCat, propIndex, propState) via PROP_HUNT.SET_DISGUISE so we
    // know what to draw. Without this branch, seekers see remote hiders
    // as regular Link — defeats the disguise. If DrawHiderAsProp fails
    // (ghost not spawned, object not loaded), fall through to vanilla
    // skeleton draw — better to see Link than nothing.
    if (Harpoon::Instance && Harpoon::Instance->isPropHuntMode &&
        client.propIndex >= 0 &&
        HarpoonPropHunt::AreGhostsReady()) {
        // Gate on propIndex>=0 alone — the role string may not have
        // arrived yet (ROLE_ASSIGN packet timing) but if the remote
        // broadcast a SET_DISGUISE with a real prop, they're a hider.
        // Without dropping the role check, the dummy renders as vanilla
        // Link until ROLE_ASSIGN arrives (which only fires when the host
        // clicks Start Game).
        s32 mapIdx = Harpoon::Instance->confirmedMapIndex;
        if (mapIdx < 0) mapIdx = 0;
        bool drew = HarpoonPropHunt::DrawHiderAsProp(actor, play,
                                         client.propCategory,
                                         client.propIndex,
                                         client.propState,
                                         mapIdx);
        if (drew) return;  // prop rendered, skip vanilla skel
    }

    // MM transformations 1=Goron, 2=Zora, 3=Deku, 4=FierceDeity → custom MM
    // skeleton draw. Anything else (Pikachu=5 with SSBB skin, Mario via libsm64,
    // future forms) falls through to OOT Link draw as a Phase A placeholder so
    // the dummy is at least visible — the local renderers for those forms
    // (PikachuForm_Draw, Sm64Mario_Draw) read singleton state that can't be
    // safely shared with a remote dummy without per-instance forks.
    if (client.transformation >= 1 && client.transformation <= 4) {
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
        // ── Phase 1 sync apply ───────────────────────────────────────────
        remoteCustomItems.rocsFeatherJumpActive  = client.ciRocsFeatherJumpActive;
        remoteCustomItems.rocsJumpCount          = client.ciRocsJumpCount;
        remoteCustomItems.rocsMmAnimTimer        = client.ciRocsMmAnimTimer;
        remoteCustomItems.bombArrowState         = client.ciBombArrowState;
        remoteCustomItems.hyliasGraceState       = client.ciHyliasGraceState;
        remoteCustomItems.hyliasGraceSubPhase    = client.ciHyliasGraceSubPhase;
        remoteCustomItems.hyliasGraceTimer       = client.ciHyliasGraceTimer;
        remoteCustomItems.hyliasGraceForcedBySpell = client.ciHyliasGraceForcedBySpell;
        remoteCustomItems.zonaiPermafrostState   = client.ciZonaiPermafrostState;
        remoteCustomItems.zonaiPermafrostSubPhase= client.ciZonaiPermafrostSubPhase;
        remoteCustomItems.zonaiPermafrostTimer   = client.ciZonaiPermafrostTimer;
        remoteCustomItems.lanternFireType        = client.ciLanternFireType;
        remoteCustomItems.lanternSwinging        = client.ciLanternSwinging;
        remoteCustomItems.lanternEquipped        = client.ciLanternEquipped;
        remoteCustomItems.lanternSwingFrame      = client.ciLanternSwingFrame;
        remoteCustomItems.minishCapWarpMode      = client.ciMinishCapWarpMode;
        remoteCustomItems.minishCapShrinking     = client.ciMinishCapShrinking;
        remoteCustomItems.minishCapGrowing       = client.ciMinishCapGrowing;
        remoteCustomItems.postmanHatDashing      = client.ciPostmanHatDashing;
        remoteCustomItems.postmanHatArriving     = client.ciPostmanHatArriving;
        remoteCustomItems.postmanHatTransitionTimer = client.ciPostmanHatTransitionTimer;
        remoteCustomItems.desireSensorState      = client.ciDesireSensorState;
        remoteCustomItems.desireSensorTimer      = client.ciDesireSensorTimer;
        remoteCustomItems.desireSensorResult     = client.ciDesireSensorResult;

        CustomItems_ApplyVisualSync(&remoteCustomItems);
    }

    // Skin sync: resolve remote's broadcast skin names against our sync registry
    // (models loaded from harpoon/skins/, flagged isSyncOnly in sModels).
    // Missing names fire a one-shot UI notification and fall back to vanilla Link.
    //
    // For adult/child we pick by age (gSaveContext.linkAge was already overridden
    // above with client.linkAge, so LINK_AGE_IN_YEARS reads the remote's age).
    // Equipment slot is deferred — synced equipment paks would need per-actor
    // cached equip DLs, a larger Phase 2 refactor.
    s32 syncAdult = PakLoader_FindSyncIndexByName(client.adultSkinName.c_str());
    s32 syncChild = PakLoader_FindSyncIndexByName(client.childSkinName.c_str());
    if (!client.adultSkinName.empty() && syncAdult < 0)
        HarpoonSkinSync::NotifyMissingPak(clientId, client.name, client.adultSkinName);
    if (!client.childSkinName.empty() && syncChild < 0)
        HarpoonSkinSync::NotifyMissingPak(clientId, client.name, client.childSkinName);

    s32 syncBodyIdx = (LINK_AGE_IN_YEARS == YEARS_ADULT) ? syncAdult : syncChild;

    // Forced model override (Kafei, Champion's Tunic, etc.) takes priority over
    // the user-selected adult/child slots when active on the remote. Resolves
    // against the same harpoon/skins/ sync registry — if the remote has Kafei
    // mask transform on but the local user lacks N64_Kafei.pak, fall back to
    // their normal selection and surface a missing-pak notice.
    if (!client.forcedSkinName.empty()) {
        s32 syncForced = PakLoader_FindSyncIndexByName(client.forcedSkinName.c_str());
        if (syncForced >= 0) {
            syncBodyIdx = syncForced;
        } else {
            HarpoonSkinSync::NotifyMissingPak(clientId, client.name, client.forcedSkinName);
        }
    }

    // Vanilla skeleton swap: the dummy's skelAnime.skeleton was resolved at
    // HarpoonDummyPlayer_Init time through the GLOBAL ResourceManager and so
    // points to whatever skeleton the LOCAL user has mounted from their own
    // mods/. Walking that skeleton during Player_Draw queries limb DL paths
    // defined by the local user's mods — every gSPDisplayList that misses
    // both the override stack and the vanilla cache falls through to global
    // and renders the LOCAL user's skin on the REMOTE's dummy. To prevent
    // that, force the dummy to walk the vanilla limb table (pre-loaded
    // BEFORE InitMods()) when no .pak body skin is active for the remote.
    // .pak skin path already replaces the skeleton via PakLoader_BeginRemoteRender,
    // so we only swap when syncBodyIdx < 0.
    void** savedSkeleton = player->skelAnime.skeleton;
    u8 savedDListCount = player->skelAnime.dListCount;
    bool didSwapSkel = false;

    // .pak skin sync — pak_loader swaps the dummy's body skeleton + equipment
    // based on which sync .pak the remote selected.
    PakLoader_BeginRemoteRender(syncBodyIdx);
    // .o2r override sync — activate matching overrides BEFORE picking the
    // skeleton, since the override's own skel (if any) is what we want to
    // walk when an override is active.
    HarpoonSkinSync::BeginRemoteOverrides(client.enabledO2rMods);

    if (syncBodyIdx < 0) {
        bool isAdult = (LINK_AGE_IN_YEARS == YEARS_ADULT);
        // Prefer an active override's own Link skeleton (so override-specific
        // limb DL paths like `bone003_*_layer_Opaque` get queried by the
        // engine and resolved via the override's dlsByPath). Fall back to
        // vanilla skel when no override has one — this isolates the dummy
        // from any LOCAL globally-mounted skin mod that would otherwise
        // contaminate the limb-name namespace.
        void** overrideLimbs = HarpoonSkinSync::GetActiveOverrideLinkLimbTable(isAdult);
        int overrideDLs = HarpoonSkinSync::GetActiveOverrideLinkDListCount(isAdult);
        const char* skelSource = "(none)";
        if (overrideLimbs != nullptr && overrideDLs > 0) {
            player->skelAnime.skeleton = overrideLimbs;
            player->skelAnime.dListCount = (u8)overrideDLs;
            didSwapSkel = true;
            skelSource = "override";
        } else {
            void** vanillaLimbs = HarpoonSkinSync::GetVanillaLinkLimbTable(isAdult);
            int vanillaDLs = HarpoonSkinSync::GetVanillaLinkDListCount(isAdult);
            if (vanillaLimbs != nullptr && vanillaDLs > 0) {
                player->skelAnime.skeleton = vanillaLimbs;
                player->skelAnime.dListCount = (u8)vanillaDLs;
                didSwapSkel = true;
                skelSource = "vanilla fallback";
            }
        }
        // Diagnostic — emit only when the (clientId, source, age) tuple
        // changes so the log isn't spammed every frame. sLastDecision is
        // file-scope so it can be cleared on disconnect.
        std::string decision = std::string(skelSource) + (isAdult ? " adult" : " child");
        auto it = sLastDecision.find(clientId);
        if (it == sLastDecision.end() || it->second != decision) {
            sLastDecision[clientId] = decision;
            SPDLOG_INFO("[HarpoonSkinSync] dummy '{}' (clientId={}) skel = {}",
                        client.name, clientId, decision);
        }
    }

    // Suppress sword-trail effects on the dummy. The vanilla
    // Player_PostLimbDrawGameplay path reads `meleeWeaponEffectIndex` and
    // calls Effect_GetByIndex → EffectBlure_ChangeType. That effect slot
    // is owned by the LOCAL player; on a remote dummy it points at NULL or
    // garbage, so the moment a dummy enters a sword-swing anim the engine
    // crashes with a 0xc0000005 inside EffectBlure_ChangeType. Forcing
    // meleeWeaponState=0 skips the entire blur path in vanilla draw.
    u8 savedMeleeWeaponState = player->meleeWeaponState;
    player->meleeWeaponState = 0;

    Player_Draw((Actor*)player, play);

    player->meleeWeaponState = savedMeleeWeaponState;

    HarpoonSkinSync::EndRemoteOverrides();
    PakLoader_EndRemoteRender();

    if (didSwapSkel) {
        player->skelAnime.skeleton = savedSkeleton;
        player->skelAnime.dListCount = savedDListCount;
    }

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
