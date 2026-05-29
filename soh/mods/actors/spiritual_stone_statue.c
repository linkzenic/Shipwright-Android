/**
 * spiritual_stone_statue.c - MM Owl Statue actor for spiritual-stone warp points.
 *
 * Same actor-hijack pattern as somaria_cubes.c: spawn ACTOR_EN_LIGHTBOX and
 * override its update/draw to render the recolored MM owl statue DL from
 * object_sek.
 *
 * Stone index (0=Kokiri, 1=Goron, 2=Zora) is stashed in actor->home.rot.x —
 * the draw uses it to pick the per-stone env color tint. The lifetime is
 * scene-bound (dies on scene unload, the orchestrator re-spawns it on
 * scene init for any warp that lives in the new scene).
 *
 * Consumed via #include from spiritual_stones.cpp (the .c is NOT in vcxproj).
 */

#include "spiritual_stone_statue.h"

#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "align_asset_macro.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "objects/object_gi_jewel/object_gi_jewel.h"
#include "overlays/actors/ovl_En_Lightbox/z_en_lightbox.h"
#include <math.h>

// Not exposed in functions.h. Forward-declared in z_scene.c and a handful of
// actor overlays (e.g. z_en_girla.c:76). Same pattern here.
s32 Object_Spawn(ObjectContext* objectCtx, s16 objectId);

// OTR path for the MM owl statue DL. Same convention as elegy_shell_assets.h —
// pass the string pointer to Gfx_DrawDListOpa and the resource manager
// resolves it at render time. We use the OPENED variant so the wings show
// extended — the closed one hides them against the body.
#define dgOwlStatueOpenedDL "__OTR__objects/object_sek/gOwlStatueOpenedDL"
static const ALIGN_ASSET(2) char gOwlStatueOpenedDL_path[] = dgOwlStatueOpenedDL;

// Per-stone get-item jewel render data. Mirrors GetItem_DrawJewelKokiri/Goron/
// Zora in z_draw.c — the same DL pair + prim/env color pair the vanilla
// get-item draws use, so each spiritual stone floating above its owl statue
// matches the look of receiving it.
typedef struct {
    const char* gemDL;       // XLU pass (the gem itself, scintillating)
    const char* settingDL;   // OPA pass (the gold setting around the gem)
    u8 primXlu[3];
    u8 envXlu[3];
    u8 primOpa[3];
    u8 envOpa[3];
} StoneJewel;

// Index matches SPIRITUAL_STONE_* enum in spiritual_stones.h.
static const StoneJewel sStoneJewels[3] = {
    // Kokiri Emerald — green
    {
        gGiKokiriEmeraldGemDL, gGiKokiriEmeraldSettingDL,
        { 255, 255, 160 }, { 0, 255, 0 }, { 255, 255, 170 }, { 150, 120, 0 },
    },
    // Goron Ruby — red
    {
        gGiGoronRubyGemDL, gGiGoronRubySettingDL,
        { 255, 170, 255 }, { 255, 0, 100 }, { 255, 255, 170 }, { 150, 120, 0 },
    },
    // Zora Sapphire — blue
    {
        gGiZoraSapphireGemDL, gGiZoraSapphireSettingDL,
        { 50, 255, 255 }, { 50, 0, 150 }, { 255, 255, 170 }, { 150, 120, 0 },
    },
};

// MM DLs branch into segment 0x0C for the scene cull list, which OOT leaves
// unset. Bind it to no-op gsSPEndDisplayList so those branches just return.
// Same trick as sSegment0xC_Noop in somaria_cubes.c.
static Gfx sStatueSegment0xC_Noop[] = {
    gsSPEndDisplayList(),
    gsSPEndDisplayList(),
    gsSPEndDisplayList(),
    gsSPEndDisplayList(),
};

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

static void Statue_Update(Actor* thisx, PlayState* play);
static void Statue_Draw(Actor* thisx, PlayState* play);

static ActorFunc sStatueUpdateFunc = Statue_Update;

#define STATUE_GET_STONE(actor) ((actor)->home.rot.x)
#define STATUE_SET_STONE(actor, s) ((actor)->home.rot.x = (s))

// Slot in play->objectCtx for OBJECT_GI_JEWEL. The jewel DLs branch into
// segment 6 expecting their object bank to be there, but En_Lightbox (the
// host actor we hijack) doesn't load OBJECT_GI_JEWEL — so we have to load it
// ourselves and rebind segment 6 around the jewel draw.
#define STATUE_GET_JEWEL_SLOT(actor) ((actor)->home.rot.y)
#define STATUE_SET_JEWEL_SLOT(actor, s) ((actor)->home.rot.y = (s))

// Visual scale — 1/20 of the previous 0.1f. The MM owl statue is authored
// quite large; this brings it down to a marker-sized prop instead of an
// over-scale landmark.
#define STATUE_VISUAL_SCALE 0.005f

// ============================================================================
// UPDATE — nothing dynamic, the statue is purely decorative.
// ============================================================================

static void Statue_Update(Actor* thisx, PlayState* play) {
    // Lazily request OBJECT_GI_JEWEL the first time we run. We can't do it
    // during spawn because the EnLightbox we hijack already chose its own
    // object slot — so we keep our own slot index on the actor. -1 means
    // "no request issued yet".
    s16 slot = STATUE_GET_JEWEL_SLOT(thisx);
    if (slot < 0) {
        s32 existing = Object_GetIndex(&play->objectCtx, OBJECT_GI_JEWEL);
        if (existing >= 0) {
            STATUE_SET_JEWEL_SLOT(thisx, (s16)existing);
        } else {
            s32 spawned = Object_Spawn(&play->objectCtx, OBJECT_GI_JEWEL);
            if (spawned >= 0) {
                STATUE_SET_JEWEL_SLOT(thisx, (s16)spawned);
            }
        }
    }
}

// ============================================================================
// DRAW — recolored MM owl statue DL.
// ============================================================================

// Float / spin parameters for the jewel hovering above each statue. The
// statue itself is tiny (STATUE_VISUAL_SCALE 0.005f) so we keep the jewel
// just slightly above ground and lean on the larger jewel scale to read
// against the small statue.
#define JEWEL_HOVER_Y_BASE    35.0f // height above the statue's anchor
#define JEWEL_HOVER_AMPLITUDE  2.0f // how much it bobs up/down
#define JEWEL_HOVER_PERIOD_F  80.0f // ~80 frames for a full bob
#define JEWEL_SPIN_DEG_PER_F   5.0f // ~72 frames for a full spin
#define JEWEL_SCALE          0.12f  // markedly larger than the statue so it reads

static void Statue_Draw(Actor* thisx, PlayState* play) {
    s16 stone = STATUE_GET_STONE(thisx);
    if (stone < 0 || stone >= 3) {
        return;
    }

    // ----- Pass 1: opaque owl statue with vanilla colors. ----------------
    {
        OPEN_DISPS(play->state.gfxCtx);
        // Cast needed because this file gets pulled into the C++ translation
        // unit of spiritual_stones.cpp — C-style pointer→uintptr_t conversion
        // isn't implicit in C++.
        gSPSegment(POLY_OPA_DISP++, 0x0C, (uintptr_t)sStatueSegment0xC_Noop);
        Gfx_DrawDListOpa(play, (Gfx*)gOwlStatueOpenedDL_path);
        CLOSE_DISPS(play->state.gfxCtx);
    }

    // ----- Pass 2: floating + rotating spiritual stone gem above the statue.
    // Re-uses the same DL pair (Gem XLU, Setting OPA) and color setup that
    // z_draw.c uses for GetItem_DrawJewel{Kokiri,Goron,Zora}, so the floating
    // model matches what the player saw when they received the stone. Only
    // runs once OBJECT_GI_JEWEL has finished loading.
    s16 jewelSlot = STATUE_GET_JEWEL_SLOT(thisx);
    if (jewelSlot >= 0 && Object_IsLoaded(&play->objectCtx, jewelSlot)) {
        const StoneJewel* j = &sStoneJewels[stone];
        f32 t = (f32)play->gameplayFrames;
        f32 bob = sinf(t * (2.0f * M_PI / JEWEL_HOVER_PERIOD_F)) * JEWEL_HOVER_AMPLITUDE;
        // BINANG units = 0x10000 / 360. Per-frame degree increment → BINANG.
        s16 spin = (s16)(t * (JEWEL_SPIN_DEG_PER_F * (0x10000 / 360.0f)));

        OPEN_DISPS(play->state.gfxCtx);

        // Rebind segment 6 (object data) to OBJECT_GI_JEWEL — En_Lightbox,
        // which we hijack, had bound its own object here. Without this the
        // jewel DLs dereference garbage and render nothing.
        void* jewelSeg = play->objectCtx.status[jewelSlot].segment;
        gSPSegment(POLY_OPA_DISP++, 0x06, (uintptr_t)jewelSeg);
        gSPSegment(POLY_XLU_DISP++, 0x06, (uintptr_t)jewelSeg);

        // Texture-scroll segments the gem DLs reference. Without these, the
        // scintillation texture renders garbage. Same calls vanilla uses for
        // the get-item draw of the stones (see GetItem_DrawJewel in z_draw.c).
        // Casts: Gfx_*TexScrollEx returns Gfx*, gSPSegment wants uintptr_t —
        // C lets that decay implicitly, C++ (this TU is compiled as C++) does
        // not.
        gSPSegment(POLY_XLU_DISP++, 9,
                   (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0, 255, 64, 64, 1, 0, 255, 16, 16, 0, 0, 0, 0));
        gSPSegment(POLY_OPA_DISP++, 8,
                   (uintptr_t)Gfx_TexScrollEx(play->state.gfxCtx, 0, 0, 16, 16, 0, 0));

        Matrix_Translate(thisx->world.pos.x,
                         thisx->world.pos.y + JEWEL_HOVER_Y_BASE + bob,
                         thisx->world.pos.z, MTXMODE_NEW);
        Matrix_RotateY(BINANG_TO_RAD((f32)spin), MTXMODE_APPLY);
        Matrix_Scale(JEWEL_SCALE, JEWEL_SCALE, JEWEL_SCALE, MTXMODE_APPLY);

        // Gem (XLU): cast const char[] → Gfx* for the C++ translation unit.
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
                  G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 128, j->primXlu[0], j->primXlu[1], j->primXlu[2], 255);
        gDPSetEnvColor(POLY_XLU_DISP++, j->envXlu[0], j->envXlu[1], j->envXlu[2], 255);
        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)j->gemDL);

        // Setting (OPA): same matrix, different color pair.
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
                  G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 128, j->primOpa[0], j->primOpa[1], j->primOpa[2], 255);
        gDPSetEnvColor(POLY_OPA_DISP++, j->envOpa[0], j->envOpa[1], j->envOpa[2], 255);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)j->settingDL);

        CLOSE_DISPS(play->state.gfxCtx);
    }
}

// ============================================================================
// SPAWN
// ============================================================================

Actor* SpiritualStoneStatue_Spawn(PlayState* play, Vec3f* pos, s16 rotY, int stone) {
    if (stone < 0 || stone >= 3) {
        return NULL;
    }

    Actor* a = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_LIGHTBOX,
                           pos->x, pos->y, pos->z, 0, rotY, 0, 0);
    if (a == NULL) {
        return NULL;
    }

    EnLightbox* lightbox = (EnLightbox*)a;

    // Strip En_Lightbox's DynaPoly — the statue is purely visual and shouldn't
    // shove the player around. Same as somaria_cubes.
    if (lightbox->dyna.bgId != BGACTOR_NEG_ONE) {
        DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, lightbox->dyna.bgId);
        lightbox->dyna.bgId = BGACTOR_NEG_ONE;
    }

    a->update = Statue_Update;
    a->draw = Statue_Draw;
    a->gravity = 0.0f;
    a->minVelocityY = 0.0f;
    a->shape.shadowDraw = NULL;
    a->shape.shadowScale = 0.0f;

    Actor_SetScale(a, STATUE_VISUAL_SCALE);
    STATUE_SET_STONE(a, stone);

    // Kick off the OBJECT_GI_JEWEL load right away rather than waiting for the
    // first Update — Update still acts as a safety net if the load fails.
    s32 jewelSlot = Object_GetIndex(&play->objectCtx, OBJECT_GI_JEWEL);
    if (jewelSlot < 0) {
        jewelSlot = Object_Spawn(&play->objectCtx, OBJECT_GI_JEWEL);
    }
    STATUE_SET_JEWEL_SLOT(a, (s16)jewelSlot);

    return a;
}

u8 SpiritualStoneStatue_IsStatue(Actor* actor) {
    if (actor == NULL || actor->update == NULL) {
        return 0;
    }
    return (actor->update == sStatueUpdateFunc);
}
