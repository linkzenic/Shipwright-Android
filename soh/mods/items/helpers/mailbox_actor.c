/**
 * mailbox_actor.c - Postman's Hat mailbox prop actor (unity-included).
 *
 * Based on somaria_cubes.c pattern:
 *   - Spawns ACTOR_EN_LIGHTBOX then hijacks update/draw/destroy.
 *   - Can't extend the actor struct (Actor_Spawn only allocates
 *     sizeof(EnLightbox)), so the collider lives in a static pool.
 *   - Stashes the mailbox index in actor->home.rot.z.
 *
 * Trigger pattern (mirrors z_en_box.c:447 "player in front zone + facing"):
 *   - Player must be close and facing the mailbox (cone in front).
 *   - Player must own AND wear the Postman's Hat.
 *   - On A press the hijacked update calls PostmanHat_TryTriggerWarpMode(),
 *     which opens the Postman Kaleido overlay (pauseCtx freeze + warp menu).
 */

#include "mailbox_actor.h"
#include "../custom_items.h"
#include "../logic/item_postman_hat.h"
#include "overlays/actors/ovl_En_Lightbox/z_en_lightbox.h"
#include "mods/transformation_masks/assets/mm_asset_loader.h"

extern Gfx* ResourceMgr_LoadGfxByName(const char* path);

// Player helper — the "return to idle after talk" used by Player_Action_Talk
// when a message closes. We need it to release Link from the TALKING state we
// enter via the offer-talk pattern (see Mailbox_Update).
extern void func_80853080(Player* this, PlayState* play);

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

static void Mailbox_Update(Actor* thisx, PlayState* play);
static void Mailbox_Draw(Actor* thisx, PlayState* play);
static void Mailbox_DestroyFunc(Actor* thisx, PlayState* play);

static ActorFunc sMailboxOriginalDestroy = NULL;
static ActorFunc sMailboxUpdateFunc = Mailbox_Update;

// ============================================================================
// STATIC COLLIDER POOL (cannot extend the actor struct)
// ============================================================================

#define MAILBOX_MAX_COLLIDERS 12

typedef struct {
    ColliderCylinder collider;
    Actor* owner;
    u8 initialized;
} MailboxColliderSlot;

static MailboxColliderSlot sMailboxColliderPool[MAILBOX_MAX_COLLIDERS] = { 0 };

static ColliderCylinderInit sMailboxColliderInit = {
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
    { 28, 80, 0, { 0, 0, 0 } },
};

static s8 Mailbox_GetColliderSlot(Actor* actor) {
    for (s8 i = 0; i < MAILBOX_MAX_COLLIDERS; i++) {
        if (sMailboxColliderPool[i].owner == actor)
            return i;
    }
    return -1;
}

static s8 Mailbox_AllocCollider(PlayState* play, Actor* actor) {
    for (s8 i = 0; i < MAILBOX_MAX_COLLIDERS; i++) {
        if (sMailboxColliderPool[i].owner == NULL) {
            if (!sMailboxColliderPool[i].initialized) {
                Collider_InitCylinder(play, &sMailboxColliderPool[i].collider);
                sMailboxColliderPool[i].initialized = 1;
            }
            Collider_SetCylinder(play, &sMailboxColliderPool[i].collider, actor, &sMailboxColliderInit);
            sMailboxColliderPool[i].owner = actor;
            return i;
        }
    }
    return -1;
}

static void Mailbox_FreeCollider(Actor* actor) {
    s8 slot = Mailbox_GetColliderSlot(actor);
    if (slot >= 0) {
        sMailboxColliderPool[slot].owner = NULL;
    }
}

// ============================================================================
// ASSET: postbox DL from mm.o2r (cached on first draw)
// ============================================================================

static Gfx* sMailboxFrameDL = NULL;
static s32 sMailboxAssetsChecked = 0;
static s32 sMailboxAssetsAvailable = 0;

static void Mailbox_EnsureAssets(void) {
    if (sMailboxAssetsChecked)
        return;
    sMailboxAssetsChecked = 1;
    // object_pst lives in mm.o2r — without it, ResourceMgr_LoadGfxByName crashes
    // dereferencing an empty Instructions vector.
    if (!MmAssets_IsLoaded()) {
        sMailboxFrameDL = NULL;
        sMailboxAssetsAvailable = 0;
        return;
    }
    sMailboxFrameDL = ResourceMgr_LoadGfxByName("__OTR__objects/object_pst/gPostboxFrameDL");
    if (sMailboxFrameDL == NULL || ((const char*)sMailboxFrameDL)[0] == '_') {
        sMailboxFrameDL = NULL;
        sMailboxAssetsAvailable = 0;
    } else {
        sMailboxAssetsAvailable = 1;
    }
}

// ============================================================================
// UPDATE — offer-talk pattern (mirror MM's En_Pst SubS_Offer flow)
// ============================================================================
// MM's En_Pst calls SubS_Offer to register its "SPEAK" A-prompt. OOT's
// equivalent is `func_8002F2CC(actor, play, radius)` which sets the player's
// talkActor when in range + facing, making the engine paint the A label.
//
// When the player accepts (A press), `Actor_ProcessTalkRequest` returns true.
// Normally the engine would have opened a textbox via Player_SetupTalk, but
// because we leave `actor->textId = 0` the textbox never starts — only the
// TALKING state is set. We then manually clear that state and open the
// Postman Kaleido overlay instead (pauseCtx freeze).
// ============================================================================

#define MAILBOX_TALK_RADIUS 80.0f
#define MAILBOX_TALK_RANGE_Y 40.0f

static void Mailbox_Update(Actor* thisx, PlayState* play) {
    Player* player = GET_PLAYER(play);
    if (player == NULL)
        return;

    // Collider update
    s8 slot = Mailbox_GetColliderSlot(thisx);
    if (slot >= 0) {
        Collider_UpdateCylinder(thisx, &sMailboxColliderPool[slot].collider);
        CollisionCheck_SetOC(play, &play->colChkCtx, &sMailboxColliderPool[slot].collider.base);
    }

    // --- Player accepted the SPEAK offer (pressed A in range + facing) ---
    if (Actor_ProcessTalkRequest(thisx, play)) {
        s32 mailboxIdx = thisx->home.rot.z;

        // We never set textId so Player_SetupTalk skipped Message_StartTextbox,
        // but it did flip stateFlags1 into TALKING | IN_CUTSCENE. Clear those
        // and return the player to a normal idle action — otherwise Link is
        // stuck waiting for a textbox that will never close.
        player->stateFlags1 &= ~(PLAYER_STATE1_TALKING | PLAYER_STATE1_IN_CUTSCENE);
        player->talkActor = NULL;
        player->actor.flags &= ~ACTOR_FLAG_TALK;
        func_80853080(player, play);

        if (mailboxIdx >= 0 && mailboxIdx < POSTMAN_MAILBOX_COUNT &&
            PostmanHat_IsMailboxUnlocked(mailboxIdx)) {
            // TryTriggerWarpMode re-verifies owned+worn+scene-safe guards.
            PostmanHat_TryTriggerWarpMode(play);
        }
        return;
    }

    // --- Offer SPEAK prompt when the player is near and facing ---
    // func_8002F2CC handles the "in range + facing + no lock-on" logic itself;
    // we just have to nuke textId so Message_StartTextbox never runs on A.
    thisx->textId = 0;
    func_8002F2CC(thisx, play, MAILBOX_TALK_RADIUS);
}

// ============================================================================
// DRAW — MM postbox DL with segment bindings
// ============================================================================

static void Mailbox_Draw(Actor* thisx, PlayState* play) {
    Mailbox_EnsureAssets();
    if (!sMailboxAssetsAvailable)
        return;

    OPEN_DISPS(play->state.gfxCtx);

    gDPPipeSync(POLY_OPA_DISP++);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    // MM DLs from mm.o2r reference segment 0x0C (cull list) and 0x08
    // (animated material) which OOT leaves unset. Bind them before the DL
    // runs to prevent "Unhandled OP code" crashes in the Fast3D interpreter.
    // Pattern: mm_player_form.cpp:12548-12559.
    gSPSegment(POLY_OPA_DISP++, 0x0C, (uintptr_t)gCullBackDList);
    gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)gEmptyDL);

    Matrix_Translate(thisx->world.pos.x, thisx->world.pos.y, thisx->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(BINANG_TO_RAD(thisx->shape.rot.y), MTXMODE_APPLY);
    Matrix_Scale(0.02f, 0.02f, 0.02f, MTXMODE_APPLY);

    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPDisplayList(POLY_OPA_DISP++, sMailboxFrameDL);

    gDPPipeSync(POLY_OPA_DISP++);

    CLOSE_DISPS(play->state.gfxCtx);
}

// ============================================================================
// DESTROY
// ============================================================================

static void Mailbox_DestroyFunc(Actor* thisx, PlayState* play) {
    Mailbox_FreeCollider(thisx);
    if (sMailboxOriginalDestroy != NULL) {
        sMailboxOriginalDestroy(thisx, play);
    }
}

// ============================================================================
// SPAWN + IDENTIFICATION
// ============================================================================

u8 Mailbox_IsMailboxActor(Actor* actor) {
    if (actor == NULL || actor->update == NULL)
        return 0;
    return (actor->update == sMailboxUpdateFunc);
}

Actor* Mailbox_Spawn(PlayState* play, const Vec3f* pos, s16 yaw, s32 mailboxIdx) {
    Actor* mailbox =
        Actor_Spawn(&play->actorCtx, play, ACTOR_EN_LIGHTBOX, pos->x, pos->y, pos->z, 0, yaw, 0, 0);

    if (mailbox == NULL)
        return NULL;

    EnLightbox* lightbox = (EnLightbox*)mailbox;

    if (sMailboxOriginalDestroy == NULL) {
        sMailboxOriginalDestroy = mailbox->destroy;
    }

    // Hijack function pointers
    mailbox->update = Mailbox_Update;
    mailbox->draw = Mailbox_Draw;
    mailbox->destroy = Mailbox_DestroyFunc;

    // Ditch EnLightbox's DynaPoly — we use our own cylinder collider
    if (lightbox->dyna.bgId != BGACTOR_NEG_ONE) {
        DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, lightbox->dyna.bgId);
        lightbox->dyna.bgId = BGACTOR_NEG_ONE;
    }

    // Allocate collider from the static pool
    Mailbox_AllocCollider(play, mailbox);

    // Static prop — no gravity, no shadow (for now)
    mailbox->gravity = 0.0f;
    mailbox->shape.shadowDraw = NULL;
    mailbox->shape.shadowScale = 0.0f;

    // Make sure the actor is visible and updated regardless of culling dist
    mailbox->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
    mailbox->flags |= ACTOR_FLAG_DRAW_CULLING_DISABLED;

    // Stash the mailbox index in home.rot.z for lookup in Update
    mailbox->home.rot.z = (s16)mailboxIdx;

    return mailbox;
}
