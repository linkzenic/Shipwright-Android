/**
 * gerudo_hybrid_render.cpp — path-swap override for the gerudo form.
 *
 * The .o2r ships ALL gerudo resources (skel, limbs, body mesh, item DLs,
 * materials, vtx/tri buffers) under a custom namespace
 *   `objects/gerudoPlayer/object_link_boy/X`
 * exactly mirroring the vanilla
 *   `objects/object_link_boy/X`
 * layout. This means EVERY resource that Link's Player_DrawImpl references
 * by symbol — body parts, items, sword/shield holds, etc. — has a parallel
 * gerudo version at the same leaf name.
 *
 * Player_DrawImpl walks the player skeleton and calls our limb-draw
 * override for each limb. The override picks the right DL (e.g. master
 * sword, hookshot, closed hand) and writes its OTR-path string into
 * `*dList`. SoH's gfx interpreter resolves the string via the resource
 * manager at render time.
 *
 * To make the gerudo form purely visual, this wrapper intercepts that
 * `*dList`, detects vanilla-Link paths, and rewrites the prefix from
 *   __OTR__objects/object_link_boy/...
 * to
 *   __OTR__objects/gerudoPlayer/object_link_boy/...
 * The vanilla-prefix → custom-prefix swap is done ONLY when the gerudo
 * form is active (mask equipped). When not transformed, the wrapper still
 * runs but the swap is a no-op, so Link looks vanilla.
 *
 * Buffer lifetime: the rewritten path lives in the gfx frame arena
 * (Graph_Alloc), valid for the rest of the current frame — long enough
 * for the gfx interpreter to consume it.
 */

#include "gerudo_form.h"
#include "gerudo_hybrid_render.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
}

#include <cstring>

// Vanilla / custom path prefixes — matched verbatim against the OTR strings
// the engine emits via Player_DrawImpl's override callback.
static const char VANILLA_PREFIX_BOY[]  = "__OTR__objects/object_link_boy/";
static const char GERUDO_PREFIX_BOY[]   = "__OTR__objects/gerudoPlayer/object_link_boy/";
static const char VANILLA_PREFIX_KID[]  = "__OTR__objects/object_link_child/";
static const char GERUDO_PREFIX_KID[]   = "__OTR__objects/gerudoPlayer/object_link_child/";

// The original Player_DrawImpl override is passed in by Player_DrawGameplay
// (defaults to Player_OverrideLimbDrawGameplayDefault). We capture it in a
// static slot right before swapping in our wrapper at the Player_DrawImpl
// call site. Single-threaded so this is safe.
typedef s32 (*OverrideLimbDrawOpaFn)(PlayState*, s32, Gfx**, Vec3f*, Vec3s*, void*);
static OverrideLimbDrawOpaFn sChainedOverride = nullptr;

static const char* swap_prefix_if_match(PlayState* play, const char* in,
                                        const char* vprefix, size_t vlen,
                                        const char* gprefix, size_t glen) {
    if (std::strncmp(in, vprefix, vlen) != 0) {
        return nullptr;
    }
    const char* leaf = in + vlen;
    size_t leafLen = std::strlen(leaf);
    char* buf = (char*)Graph_Alloc(play->state.gfxCtx, glen + leafLen + 1);
    std::memcpy(buf, gprefix, glen);
    std::memcpy(buf + glen, leaf, leafLen + 1); // copies NUL too
    return buf;
}

// Returns true if the string starts with the given prefix.
static bool starts_with(const char* s, const char* prefix) {
    return std::strncmp(s, prefix, std::strlen(prefix)) == 0;
}

// Returns true if the string is a gerudo-namespace path, with or without
// the `__OTR__` prefix that the runtime may or may not embed.
static bool is_gerudo_path(const char* s) {
    return s != nullptr &&
           (starts_with(s, "objects/gerudoPlayer/") ||
            starts_with(s, "__OTR__objects/gerudoPlayer/"));
}

// Wrapper around Player_OverrideLimbDrawGameplayDefault. Strategy:
//
//   1. Snapshot the LIMB'S OWN DL (the SkelAnime walk passes us *dList
//      pointing at it). For the gerudo .o2r, most limbs have their own
//      `objects/gerudoPlayer/object_link_boy/(bone001_…|gLinkAdultSkel_layer_…)`
//      DL referenced from their SkeletonLimb XML.
//
//   2. Call the chained override. Vanilla Player_OverrideLimbDrawGameplay
//      AGGRESSIVELY overwrites *dList for certain limbs (WAIST→waist belt,
//      hands when closed, sheath, etc.) with hardcoded vanilla paths like
//      `gLinkAdultWaistNearDL`. For our gerudo, that erases the gerudo
//      limb's own DL — the brown belt re-appears on top of the gerudo
//      torso, ugly.
//
//   3. If the chain replaced *dList with a vanilla `__OTR__objects/object_link_boy/…`
//      path AND we had a gerudo `objects/gerudoPlayer/…` DL before the
//      chain ran, RESTORE the gerudo DL. The vanilla override would have
//      hidden gerudo's torso/waist/etc.; we keep what the skel said.
//
//   4. If the chain replaced with a vanilla path AND we did NOT have a
//      gerudo DL (limb's own DL was gEmptyDL — happens on ROOT/LOWER/
//      UPPER control bones), try a prefix swap: rewrite
//      `__OTR__objects/object_link_boy/X`  →
//      `__OTR__objects/gerudoPlayer/object_link_boy/X`
//      so resources outside the skel chain (item DLs, etc.) resolve to
//      our gerudo versions.
extern "C" s32 GerudoForm_OverrideLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList,
                                           Vec3f* pos, Vec3s* rot, void* arg) {
    // Step 1: snapshot the limb's own DL before anyone touches it.
    Gfx* gerudoLimbDL = *dList;
    bool gerudoLimbHasOwnDL = is_gerudo_path((const char*)gerudoLimbDL);

    // Step 2: let the vanilla override do its thing.
    s32 ret = 0;
    if (sChainedOverride != nullptr) {
        ret = sChainedOverride(play, limbIndex, dList, pos, rot, arg);
    }

    // Step 3+4: post-process. The chain may have overwritten *dList.
    if (*dList == NULL) {
        return ret;
    }
    const char* path = (const char*)*dList;

    if (starts_with(path, VANILLA_PREFIX_BOY) || starts_with(path, VANILLA_PREFIX_KID)) {
        // Chain set a vanilla path. If we had our own gerudo DL on this limb,
        // restore it — vanilla's body-part override would clobber the gerudo
        // mesh (waist belt, head, hands, etc.) otherwise.
        if (gerudoLimbHasOwnDL) {
            *dList = gerudoLimbDL;
            return ret;
        }
        // No gerudo-native DL for this limb — try a prefix swap. Works for
        // items + body parts that are referenced from the override callback
        // by hardcoded vanilla path (e.g. gLinkAdultRightHandHoldingHookshotFarDL).
        const char* swapped = swap_prefix_if_match(
            play, path,
            VANILLA_PREFIX_BOY, sizeof(VANILLA_PREFIX_BOY) - 1,
            GERUDO_PREFIX_BOY,  sizeof(GERUDO_PREFIX_BOY) - 1);
        if (swapped == nullptr) {
            swapped = swap_prefix_if_match(
                play, path,
                VANILLA_PREFIX_KID, sizeof(VANILLA_PREFIX_KID) - 1,
                GERUDO_PREFIX_KID,  sizeof(GERUDO_PREFIX_KID) - 1);
        }
        if (swapped != nullptr) {
            *dList = (Gfx*)swapped;
        }
    }
    return ret;
}

// Set the chained override that GerudoForm_OverrideLimbDraw forwards to.
// Called from z_player.c immediately before Player_DrawImpl, then the
// wrapper takes the wheel. Reset to NULL after the draw is fine but not
// strictly required — we re-set it each frame.
extern "C" void GerudoForm_SetChainedOverride(void* fn) {
    sChainedOverride = (OverrideLimbDrawOpaFn)fn;
}

// ---------------------------------------------------------------------------
// Legacy hybrid-render stubs — kept so the vcxproj entries link cleanly and
// any leftover references compile. The hybrid SkelAnime approach was retired
// when we switched to the path-swap override path above.
// ---------------------------------------------------------------------------
extern "C" s32  GerudoHybrid_Setup(PlayState* play) { (void)play; return 0; }
extern "C" void GerudoHybrid_Teardown(void) {}
extern "C" void GerudoHybrid_Update(PlayState* play, Player* player) { (void)play; (void)player; }
extern "C" void GerudoHybrid_Draw  (PlayState* play, Player* player) { (void)play; (void)player; }
extern "C" void GerudoHybrid_SetAnim(PlayState* play, const char* otrPath) { (void)play; (void)otrPath; }
