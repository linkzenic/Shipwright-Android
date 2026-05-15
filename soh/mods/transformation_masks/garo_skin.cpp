/**
 * garo_skin.cpp — Drive OOT's native Skin (z_skin.c) for the Garo body.
 *
 * Garo's .o2r contains a parallel Skin skeleton (gGaroSkinSkel) with all
 * geometry on the Torso limb's SkinAnimatedLimbData. Verts on cross-bone
 * triangles carry 50/50 weights between the two involved bones; everything
 * else is rigid (single bone, weight 100). z_skin.c CPU-blends them every
 * frame, so seam edges stay connected as the bones rotate.
 *
 * We bypass Skin_Init entirely: it requires an AnimationHeader and runs its
 * own SkelAnime, which would conflict with Player's. Instead we replicate
 * the buffer-allocation steps manually and feed Player's jointTable to
 * Skin_ApplyAnimTransformations through skin->skelAnime.jointTable each
 * frame.
 */

#include "garo_skin.h"
#include "soh/OTRGlobals.h"
#include "soh/ResourceManagerHelpers.h"
#include <spdlog/spdlog.h>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "z64.h"
#include "z64skin.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
}

#define GARO_SKIN_SKEL_OTR  "__OTR__objects/garo/gGaroSkinSkel"
#define GARO_MAT_DL_OTR     "__OTR__objects/garo/gGaroSkinMatDL"

static Skin sGaroSkin = {};
static Gfx* sGaroMatDL = nullptr; // material DL: combiner / render mode / texture load
static bool sInitialized = false;

extern "C" s32 GaroSkin_Setup(PlayState* play) {
    if (sInitialized) return 1;

    SkeletonHeader* skel = ResourceMgr_LoadSkeletonByName(GARO_SKIN_SKEL_OTR, NULL);
    if (skel == nullptr) {
        SPDLOG_WARN("[GaroSkin] LoadSkeletonByName('{}') returned NULL", GARO_SKIN_SKEL_OTR);
        return 0;
    }

    SkeletonHeader* virtSkelHdr = (SkeletonHeader*)SEGMENTED_TO_VIRTUAL(skel);
    sGaroSkin.skeletonHeader = virtSkelHdr;
    sGaroSkin.limbCount = virtSkelHdr->limbCount;

    s32 limbCount = virtSkelHdr->limbCount;
    sGaroSkin.vtxTable = (SkinLimbVtx*)malloc(limbCount * sizeof(SkinLimbVtx));
    if (!sGaroSkin.vtxTable) {
        SPDLOG_WARN("[GaroSkin] vtxTable alloc failed for {} limbs", limbCount);
        return 0;
    }

    // Per-limb buffer setup (mirrors Skin_Init body without SkelAnime_InitSkin)
    SkinLimb** skeleton = (SkinLimb**)SEGMENTED_TO_VIRTUAL(virtSkelHdr->segment);
    s32 animatedCount = 0;
    for (s32 i = 0; i < limbCount; i++) {
        SkinLimbVtx* vtxEntry = &sGaroSkin.vtxTable[i];
        SkinLimb* limb = (SkinLimb*)SEGMENTED_TO_VIRTUAL(skeleton[i]);

        if ((limb->segmentType != SKIN_LIMB_TYPE_ANIMATED) || (limb->segment == NULL)) {
            vtxEntry->index = 0;
            vtxEntry->buf[0] = NULL;
            vtxEntry->buf[1] = NULL;
        } else {
            SkinAnimatedLimbData* anim =
                (SkinAnimatedLimbData*)SEGMENTED_TO_VIRTUAL(limb->segment);
            vtxEntry->index = 0;
            vtxEntry->buf[0] = (Vtx*)malloc(anim->totalVtxCount * sizeof(Vtx));
            vtxEntry->buf[1] = (Vtx*)malloc(anim->totalVtxCount * sizeof(Vtx));
            if (!vtxEntry->buf[0] || !vtxEntry->buf[1]) {
                SPDLOG_WARN("[GaroSkin] vtxBuf alloc failed for limb {} ({} verts)",
                            i, anim->totalVtxCount);
                return 0;
            }
            // Inlined Skin_InitAnimatedLimb (internal to z_skin_awb.c, not in
            // functions.h): seed both Vtx buffers with each modif's static
            // per-vert UV/alpha. Positions get filled by Skin_ApplyLimbModifications.
            SkinLimbModif* mods = (SkinLimbModif*)SEGMENTED_TO_VIRTUAL(anim->limbModifications);
            for (s32 bufIdx = 0; bufIdx < 2; bufIdx++) {
                Vtx* dstBuf = vtxEntry->buf[bufIdx];
                for (u32 m = 0; m < anim->limbModifCount; m++) {
                    SkinVertex* verts = (SkinVertex*)SEGMENTED_TO_VIRTUAL(mods[m].skinVertices);
                    for (u32 v = 0; v < mods[m].vtxCount; v++) {
                        Vtx* vtx = &dstBuf[verts[v].index];
                        vtx->n.flag = 0;
                        vtx->n.tc[0] = verts[v].s;
                        vtx->n.tc[1] = verts[v].t;
                        vtx->n.a = verts[v].alpha;
                    }
                }
            }
            animatedCount++;
            SPDLOG_INFO("[GaroSkin] limb {} animated: {} verts, {} modifs",
                        i, anim->totalVtxCount, anim->limbModifCount);
        }
    }

    // SkelAnime: only the fields Skin_ApplyAnimTransformations reads need
    // to be valid. jointTable is replaced with Player's pointer each frame
    // in GaroSkin_Draw; skeleton + limbCount are read once.
    memset(&sGaroSkin.skelAnime, 0, sizeof(sGaroSkin.skelAnime));
    sGaroSkin.skelAnime.skeleton = (void**)SEGMENTED_TO_VIRTUAL(virtSkelHdr->segment);
    sGaroSkin.skelAnime.limbCount = limbCount + 1;
    // jointTable starts NULL; GaroSkin_Draw points it at Player's.

    // Load the material DL (XML resource emitted by glb_to_o2r.py). Runs
    // before the Skin draw to set combiner / render mode / texture state.
    sGaroMatDL = ResourceMgr_LoadGfxByName(GARO_MAT_DL_OTR);
    if (sGaroMatDL == nullptr) {
        SPDLOG_WARN("[GaroSkin] LoadGfxByName('{}') failed — verts will render "
                    "with whatever combiner state was last set", GARO_MAT_DL_OTR);
    } else {
        SPDLOG_INFO("[GaroSkin] material DL loaded at {}", (void*)sGaroMatDL);
    }

    SPDLOG_INFO("[GaroSkin] manual setup OK: {} limbs, {} animated",
                limbCount, animatedCount);
    sInitialized = true;
    return 1;
}

extern "C" void GaroSkin_Teardown(PlayState* play) {
    if (!sInitialized) return;
    if (sGaroSkin.vtxTable) {
        for (s32 i = 0; i < sGaroSkin.limbCount; i++) {
            if (sGaroSkin.vtxTable[i].buf[0]) {
                free(sGaroSkin.vtxTable[i].buf[0]);
            }
            if (sGaroSkin.vtxTable[i].buf[1]) {
                free(sGaroSkin.vtxTable[i].buf[1]);
            }
        }
        free(sGaroSkin.vtxTable);
    }
    memset(&sGaroSkin, 0, sizeof(sGaroSkin));
    sInitialized = false;
}

// Hybrid jointTable for 26-bone skeleton (Link bones 0..21 + cloak bones
// 22..26). Total entries = 27 (1 root translation + 26 limb rotations).
// Entries 0..21 are populated each frame from Player's anim (Link's body
// movement preserves), entries 22..26 are populated from cloak source —
// rest pose for now, later from Garo's idle/selected anim.
static Vec3s sGaroHybridJointTable[27] = {};
static const s32 GARO_HYBRID_JOINT_COUNT = 27;
static const s32 LINK_JOINT_COUNT_INCL_ROOT = 22;

extern "C" void GaroSkin_Draw(PlayState* play, Player* player) {
    // Defensive re-init when the loaded skeleton pointer changes — happens
    // when the ResourceMgr re-loads the .o2r after a scene transition or
    // similar cache invalidation. Catches the case where our cached pointer
    // is dangling.
    if (sInitialized) {
        SkeletonHeader* current = ResourceMgr_LoadSkeletonByName(GARO_SKIN_SKEL_OTR, NULL);
        if (current != nullptr &&
            (SkeletonHeader*)SEGMENTED_TO_VIRTUAL(current) != sGaroSkin.skeletonHeader) {
            SPDLOG_INFO("[GaroSkin] skeleton pointer changed; re-initialising");
            GaroSkin_Teardown(play);
        }
    }
    if (!sInitialized) {
        if (!GaroSkin_Setup(play)) return;
    }

    // === Hybrid skeleton translator ===
    // Build the 27-entry jointTable that Skin_ApplyAnimTransformations reads:
    //   [0]      = root translation (from Player's jointTable[0])
    //   [1..21]  = Link's limb rotations (copy from player's anim — body moves
    //              with Link's climb, walk, run, etc.)
    //   [22..26] = cloak bones (ROBE_TOP, ROBE_BACK, ROBE_LEFT, ROBE_RIGHT,
    //              ROBE_FRONT) — rest pose for now. Future: drive from Garo's
    //              idle/selected anim for cloak ondea.
    Vec3s* playerJT = player->skelAnime.jointTable;
    if (playerJT != nullptr) {
        // Copy root translation + Link's 21 limb rotations (entries 0..21).
        for (s32 i = 0; i < LINK_JOINT_COUNT_INCL_ROOT && i < GARO_HYBRID_JOINT_COUNT; i++) {
            sGaroHybridJointTable[i] = playerJT[i];
        }
    }
    // Cloak bones [22..26] → rest pose. Could be replaced per-frame with
    // values from a Garo idle anim or animation viewer selection.
    for (s32 i = LINK_JOINT_COUNT_INCL_ROOT; i < GARO_HYBRID_JOINT_COUNT; i++) {
        sGaroHybridJointTable[i].x = 0;
        sGaroHybridJointTable[i].y = 0;
        sGaroHybridJointTable[i].z = 0;
    }

    Vec3s* savedJointTable = sGaroSkin.skelAnime.jointTable;
    sGaroSkin.skelAnime.jointTable = sGaroHybridJointTable;

    // Run material DL first (combiner, render mode, texture load), then the
    // Skin draw which renders verts via segment 0x08 with that state in effect.
    if (sGaroMatDL != nullptr) {
        OPEN_DISPS(play->state.gfxCtx);
        gSPDisplayList(POLY_OPA_DISP++, sGaroMatDL);
        CLOSE_DISPS(play->state.gfxCtx);
    }

    // func_800A6330 is the public wrapper around Skin_DrawImpl.
    // setTranslation=1: include jointTable[0] (root translation) in the
    // root limb matrix. Player's normal Flex draw does this; without it,
    // Garo's body renders below its expected position because the anim's
    // root height offset gets dropped.
    func_800A6330(&player->actor, play, &sGaroSkin, /*postDraw*/ NULL,
                  /*setTranslation*/ 1);

    sGaroSkin.skelAnime.jointTable = savedJointTable;
}
