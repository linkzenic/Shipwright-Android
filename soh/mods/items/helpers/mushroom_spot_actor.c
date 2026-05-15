/**
 * mushroom_spot_actor.c - Mask of Scents mushroom spot prop actor.
 *
 * Mirrors mailbox_actor.c: hijack ACTOR_EN_LIGHTBOX, static collider pool,
 * spotIdx stashed in actor->home.rot.z.
 *
 * Update behavior:
 *   - If Mask of Scents not worn OR spot collected → skip draw, no talk offer.
 *   - Else: offer A-press (radius 60). On accept with empty bottle, call
 *     Actor_OfferGetItem for the Magic Mushroom bottle and mark collected.
 *     Without empty bottle, play error SFX.
 *
 * Draw: prefer MM mask_bu_san mushroom DL from mm.o2r; fall back to OOT
 * gGiMushroomDL.
 */

#include "mushroom_spot_actor.h"
#include "../custom_items.h"
#include "overlays/actors/ovl_En_Lightbox/z_en_lightbox.h"
#include "mods/transformation_masks/assets/mm_asset_loader.h"
#include "mods/transformation_masks/mm_mask_wear.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"

extern Gfx* ResourceMgr_LoadGfxByName(const char* path);
extern void func_80853080(Player* this, PlayState* play);

// Direct GetItemEntry delivery (CLM pattern). Looks up the registered
// RG_BOTTLE_WITH_MAGIC_MUSHROOM entry from the rando item table and starts
// the get-item cutscene immediately.
extern GetItemEntry ItemTable_RetrieveEntry(s16 modIndex, s16 getItemID);
extern s32 GiveItemEntryFromActor(Actor* actor, PlayState* play, GetItemEntry getItemEntry,
                                  f32 xzRange, f32 yRange);

#define MUSHROOM_SPOT_RADIUS 60.0f

// ────────── 5 fixed Lost Woods spot positions ──────────
// Yaw set to 0 (mushrooms are radial). Heights tuned to ground level — refine
// after first build using the dev pos-printer.
const MushroomSpotPoint sMushroomSpots[MUSHROOM_SPOT_COUNT] = {
    { SCENE_LOST_WOODS, 5, { -1180.0f,  0.0f,   980.0f } }, // Bridge area
    { SCENE_LOST_WOODS, 1, {   300.0f,  0.0f,  -200.0f } }, // Central crossroads
    { SCENE_LOST_WOODS, 6, {   650.0f,  0.0f, -1700.0f } }, // Forest Stage room
    { SCENE_LOST_WOODS, 3, {  -800.0f, 20.0f, -1100.0f } }, // Saria's grotto branch
    { SCENE_LOST_WOODS, 8, {  1450.0f,  0.0f,   250.0f } }, // Goron Shop branch
};

// ────────── Forward decls ──────────
static void MushroomSpot_Update(Actor* thisx, PlayState* play);
static void MushroomSpot_Draw(Actor* thisx, PlayState* play);
static void MushroomSpot_DestroyFunc(Actor* thisx, PlayState* play);
static ActorFunc sMushroomSpotOriginalDestroy = NULL;
static ActorFunc sMushroomSpotUpdateFunc = MushroomSpot_Update;

// ────────── Static collider pool ──────────
typedef struct {
    ColliderCylinder collider;
    Actor* owner;
    u8 initialized;
} MushroomColliderSlot;

#define MUSHROOM_MAX_COLLIDERS 8

static MushroomColliderSlot sMushroomColliderPool[MUSHROOM_MAX_COLLIDERS] = { 0 };

static ColliderCylinderInit sMushroomColliderInit = {
    {
        COLTYPE_NONE,
        AT_NONE,
        AC_NONE,
        OC1_ON | OC1_TYPE_ALL,
        OC2_TYPE_1,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK0,
        { 0x00000000, 0x00, 0x00 },
        { 0x00000000, 0x00, 0x00 },
        TOUCH_NONE,
        BUMP_NONE,
        OCELEM_ON,
    },
    { 20, 40, 0, { 0, 0, 0 } },
};

static s8 MushroomSpot_GetColliderSlot(Actor* actor) {
    for (s8 i = 0; i < MUSHROOM_MAX_COLLIDERS; i++) {
        if (sMushroomColliderPool[i].owner == actor)
            return i;
    }
    return -1;
}

static s8 MushroomSpot_AllocCollider(PlayState* play, Actor* actor) {
    for (s8 i = 0; i < MUSHROOM_MAX_COLLIDERS; i++) {
        if (sMushroomColliderPool[i].owner == NULL) {
            if (!sMushroomColliderPool[i].initialized) {
                Collider_InitCylinder(play, &sMushroomColliderPool[i].collider);
                sMushroomColliderPool[i].initialized = 1;
            }
            Collider_SetCylinder(play, &sMushroomColliderPool[i].collider, actor, &sMushroomColliderInit);
            sMushroomColliderPool[i].owner = actor;
            return i;
        }
    }
    return -1;
}

static void MushroomSpot_FreeCollider(Actor* actor) {
    s8 slot = MushroomSpot_GetColliderSlot(actor);
    if (slot >= 0) {
        sMushroomColliderPool[slot].owner = NULL;
    }
}

// ────────── Asset cache ──────────
static Gfx* sMushroomDL = NULL;
static s32 sMushroomAssetsChecked = 0;

static void MushroomSpot_EnsureAssets(void) {
    if (sMushroomAssetsChecked)
        return;
    sMushroomAssetsChecked = 1;
    // Prefer MM's mask_bu_san asset (the mushroom prop visual used on the
    // Mask of Scents itself). Fall back to OOT object_gi_mushroom DL.
    if (MmAssets_IsLoaded()) {
        sMushroomDL = ResourceMgr_LoadGfxByName("__OTR__objects/object_mask_bu_san/object_mask_bu_san_DL_000710");
        if (sMushroomDL != NULL && ((const char*)sMushroomDL)[0] != '_') {
            return;
        }
    }
    sMushroomDL = ResourceMgr_LoadGfxByName("__OTR__objects/object_gi_mushroom/gGiOddMushroomDL");
    if (sMushroomDL == NULL || ((const char*)sMushroomDL)[0] == '_') {
        sMushroomDL = NULL;
    }
}

// ────────── Collection state helpers ──────────
u8 MushroomSpot_IsCollected(s32 spotIdx) {
    if (spotIdx < 0 || spotIdx >= MUSHROOM_SPOT_COUNT)
        return 1;
    return (gCustomItemState.mushroomSpotsCollected & (1 << spotIdx)) != 0;
}

void MushroomSpot_MarkCollected(s32 spotIdx) {
    if (spotIdx < 0 || spotIdx >= MUSHROOM_SPOT_COUNT)
        return;
    gCustomItemState.mushroomSpotsCollected |= (1 << spotIdx);
}

void MushroomSpot_ResetAll(void) {
    gCustomItemState.mushroomSpotsCollected = 0;
}

// ────────── Update ──────────
static void MushroomSpot_Update(Actor* thisx, PlayState* play) {
    Player* player = GET_PLAYER(play);
    if (player == NULL)
        return;

    s32 spotIdx = thisx->home.rot.z;
    s32 maskWorn = (MmMaskWear_GetCurrent() == ITEM_MM_MASK_SCENTS);
    s32 collected = MushroomSpot_IsCollected(spotIdx);

    // Collider update (always tracked so player physics still touch a hidden
    // mushroom — invisible spots block movement subtly, which is fine).
    s8 slot = MushroomSpot_GetColliderSlot(thisx);
    if (slot >= 0) {
        Collider_UpdateCylinder(thisx, &sMushroomColliderPool[slot].collider);
        CollisionCheck_SetOC(play, &play->colChkCtx, &sMushroomColliderPool[slot].collider.base);
    }

    // Hidden when mask isn't on OR already collected — no draw, no talk.
    if (!maskWorn || collected) {
        thisx->draw = NULL;
        return;
    }
    thisx->draw = MushroomSpot_Draw;

    // Player accepted the SPEAK prompt (A press in range + facing).
    if (Actor_ProcessTalkRequest(thisx, play)) {
        // Clear vanilla TALKING/IN_CUTSCENE we never wanted (mailbox pattern).
        player->stateFlags1 &= ~(PLAYER_STATE1_TALKING | PLAYER_STATE1_IN_CUTSCENE);
        player->talkActor = NULL;
        player->actor.flags &= ~ACTOR_FLAG_TALK;
        func_80853080(player, play);

        if (Inventory_HasEmptyBottle()) {
            // Direct rando delivery: look up the registered RG entry and start
            // the get-item cutscene. Same pattern as clm_behavior.cpp:185.
            // Randomizer_Item_Give dispatches via the RG_BOTTLE_WITH_MAGIC_MUSHROOM
            // switch (bottle-fill block in randomizer.cpp).
            GetItemEntry entry = ItemTable_RetrieveEntry(MOD_RANDOMIZER,
                                                         (s16)RG_BOTTLE_WITH_MAGIC_MUSHROOM);
            GiveItemEntryFromActor(thisx, play, entry, 80.0f, 60.0f);
            MushroomSpot_MarkCollected(spotIdx);
        } else {
            Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
        }
        return;
    }

    // Offer the SPEAK prompt when in range + facing.
    thisx->textId = 0;
    func_8002F2CC(thisx, play, MUSHROOM_SPOT_RADIUS);
}

// ────────── Draw ──────────
static void MushroomSpot_Draw(Actor* thisx, PlayState* play) {
    MushroomSpot_EnsureAssets();
    if (sMushroomDL == NULL)
        return;

    OPEN_DISPS(play->state.gfxCtx);

    gDPPipeSync(POLY_OPA_DISP++);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    // MM DLs reference segments 0x08 + 0x0C — bind them safely.
    gSPSegment(POLY_OPA_DISP++, 0x0C, (uintptr_t)gCullBackDList);
    gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)gEmptyDL);

    Matrix_Translate(thisx->world.pos.x, thisx->world.pos.y, thisx->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(BINANG_TO_RAD(thisx->shape.rot.y), MTXMODE_APPLY);
    Matrix_Scale(0.5f, 0.5f, 0.5f, MTXMODE_APPLY);

    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPDisplayList(POLY_OPA_DISP++, sMushroomDL);

    gDPPipeSync(POLY_OPA_DISP++);

    CLOSE_DISPS(play->state.gfxCtx);
}

// ────────── Destroy ──────────
static void MushroomSpot_DestroyFunc(Actor* thisx, PlayState* play) {
    MushroomSpot_FreeCollider(thisx);
    if (sMushroomSpotOriginalDestroy != NULL) {
        sMushroomSpotOriginalDestroy(thisx, play);
    }
}

// ────────── Spawn + identification ──────────
u8 MushroomSpot_IsActor(Actor* actor) {
    if (actor == NULL || actor->update == NULL)
        return 0;
    return (actor->update == sMushroomSpotUpdateFunc);
}

Actor* MushroomSpot_Spawn(PlayState* play, const Vec3f* pos, s16 yaw, s32 spotIdx) {
    Actor* spot = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_LIGHTBOX, pos->x, pos->y, pos->z, 0, yaw, 0, 0);
    if (spot == NULL)
        return NULL;

    EnLightbox* lightbox = (EnLightbox*)spot;

    if (sMushroomSpotOriginalDestroy == NULL) {
        sMushroomSpotOriginalDestroy = spot->destroy;
    }

    spot->update = MushroomSpot_Update;
    spot->draw = MushroomSpot_Draw;
    spot->destroy = MushroomSpot_DestroyFunc;

    // Drop EnLightbox's DynaPoly (we use our own cylinder collider).
    if (lightbox->dyna.bgId != BGACTOR_NEG_ONE) {
        DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, lightbox->dyna.bgId);
        lightbox->dyna.bgId = BGACTOR_NEG_ONE;
    }

    MushroomSpot_AllocCollider(play, spot);

    // Static prop — no gravity, no shadow.
    spot->gravity = 0.0f;
    spot->shape.shadowDraw = NULL;
    spot->shape.shadowScale = 0.0f;

    spot->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
    spot->flags |= ACTOR_FLAG_DRAW_CULLING_DISABLED;

    spot->home.rot.z = (s16)spotIdx;
    return spot;
}

// ────────── Scene-load tick (per-frame from case 10 of MmMaskWear_Update) ──────────
void MushroomSpots_Tick(PlayState* play) {
    static s16 sLastScene = -1;
    static u32 sLastFrames = 0;

    if (!MmAssets_IsLoaded()) {
        sLastScene = -1; // force re-check next time mm.o2r becomes available
        return;
    }

    s32 sceneLoaded = (play->sceneNum != sLastScene) || (play->state.frames < sLastFrames);
    if (sceneLoaded) {
        sLastScene = play->sceneNum;
        for (s32 i = 0; i < MUSHROOM_SPOT_COUNT; i++) {
            if (sMushroomSpots[i].sceneId == play->sceneNum) {
                MushroomSpot_Spawn(play, &sMushroomSpots[i].pos, 0, i);
            }
        }
    }
    sLastFrames = play->state.frames;
}
