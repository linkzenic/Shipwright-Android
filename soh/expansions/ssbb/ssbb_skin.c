#include "expansions/ssbb/ssbb_skin.h"
#include "expansions/ssbb/ssbb_anim.h"
#include "z64.h"

// ── Bone matrix storage ─────────────────────────────────────────────────────
static MtxF sBoneWorldMatrices[SSBB_MAX_SKIN_BONES];
static MtxF sCombinedMatrices[SSBB_MAX_SKIN_BONES];

// ── Init / Destroy ──────────────────────────────────────────────────────────

void SSBBSkin_Init(SSBBSkinMesh* skin) {
    s32 size;

    if (!skin)
        return;

    size = skin->vertexCount * sizeof(Vtx);
    skin->vtxBuf[0] = ZELDA_ARENA_MALLOC_DEBUG(size);
    skin->vtxBuf[1] = ZELDA_ARENA_MALLOC_DEBUG(size);
    skin->bufIndex = 0;

    if (skin->vtxBuf[0] && skin->vtxBuf[1]) {
        s32 v;
        for (v = 0; v < skin->vertexCount; v++) {
            SSBBSkinVertex* sv = &skin->vertices[v];
            Vtx* vtx0 = &skin->vtxBuf[0][v];
            Vtx* vtx1 = &skin->vtxBuf[1][v];

            vtx0->n.ob[0] = vtx1->n.ob[0] = 0;
            vtx0->n.ob[1] = vtx1->n.ob[1] = 0;
            vtx0->n.ob[2] = vtx1->n.ob[2] = 0;
            vtx0->n.flag = vtx1->n.flag = 0;
            vtx0->n.tc[0] = vtx1->n.tc[0] = sv->texS;
            vtx0->n.tc[1] = vtx1->n.tc[1] = sv->texT;
            vtx0->n.n[0] = vtx1->n.n[0] = 0;
            vtx0->n.n[1] = vtx1->n.n[1] = 0;
            vtx0->n.n[2] = vtx1->n.n[2] = 0;
            vtx0->n.a = vtx1->n.a = sv->alpha;
        }
    }
}

void SSBBSkin_Destroy(SSBBSkinMesh* skin) {
    if (!skin)
        return;
    if (skin->vtxBuf[0]) {
        ZELDA_ARENA_FREE_DEBUG(skin->vtxBuf[0]);
        skin->vtxBuf[0] = NULL;
    }
    if (skin->vtxBuf[1]) {
        ZELDA_ARENA_FREE_DEBUG(skin->vtxBuf[1]);
        skin->vtxBuf[1] = NULL;
    }
}

// ── Build local bone matrix: T(pos) × R(euler_ZYX) × S(scale) ──
// Pure math, NO OOT Matrix stack, NO FrameInterpolation hooks.
// Matches Three.js exactly: bone.matrix = T × R × S

static void SSBBSkin_BuildLocalMatrix(const SSBBBoneFrame* bf, MtxF* out) {
    f32 deg2rad = 3.14159265358979f / 180.0f;
    f32 rx = bf->rx * deg2rad;
    f32 ry = bf->ry * deg2rad;
    f32 rz = bf->rz * deg2rad;
    f32 cx = cosf(rx), sx = sinf(rx);
    f32 cy = cosf(ry), sy = sinf(ry);
    f32 cz = cosf(rz), sz = sinf(rz);
    s32 i;

    for (i = 0; i < 16; i++)
        ((f32*)out)[i] = 0.0f;

    // R(ZYX) × S — combined rotation+scale in column-major mf[col][row]
    out->mf[0][0] = cy * cz * bf->sx;
    out->mf[0][1] = cy * sz * bf->sx;
    out->mf[0][2] = -sy * bf->sx;

    out->mf[1][0] = (sx * sy * cz - cx * sz) * bf->sy;
    out->mf[1][1] = (sx * sy * sz + cx * cz) * bf->sy;
    out->mf[1][2] = sx * cy * bf->sy;

    out->mf[2][0] = (cx * sy * cz + sx * sz) * bf->sz;
    out->mf[2][1] = (cx * sy * sz - sx * cz) * bf->sz;
    out->mf[2][2] = cx * cy * bf->sz;

    // Translation in column 3
    out->mf[3][0] = bf->tx;
    out->mf[3][1] = bf->ty;
    out->mf[3][2] = bf->tz;
    out->mf[3][3] = 1.0f;
}

// ── Compute Bone World Matrices (NO OOT Matrix stack) ──
// Uses SkinMatrix_MtxFMtxFMult directly to avoid FrameInterpolation interference.
// Matches Three.js: bone.matrixWorld = parent.matrixWorld × bone.localMatrix

static void SSBBSkin_ComputeBoneMatricesFromAnim(void** skeleton, const struct SSBBAnim* anim, u16 frame,
                                                 MtxF* parentWorld, u8 limbIdx, s32 numLimbs) {
    StandardLimb* limb;
    const SSBBBoneFrame* bf;
    MtxF localMat;

    if (limbIdx == LIMB_DONE || limbIdx >= numLimbs)
        return;

    limb = (StandardLimb*)skeleton[limbIdx];
    bf = SSBBAnim_GetBoneFrame(anim, frame, limbIdx);

    if (bf) {
        SSBBSkin_BuildLocalMatrix(bf, &localMat);

        // Neutralize root motion for movement bones (TopN=0, EyeYellowM=1, TransN=2)
        // These bones have animated translation that moves the character forward.
        // In OOT, movement is handled by Player.actor.world.pos — we only want rotation.
        // Keep bind-pose translation (from the animation frame's tx/ty/tz at frame 0).
        // For bone 2 (TransN): zero out X and Z translation, keep Y (height bobbing ok).
        if (limbIdx == 0 || limbIdx == 1) {
            localMat.mf[3][0] = 0.0f;
            localMat.mf[3][1] = 0.0f;
            localMat.mf[3][2] = 0.0f;
        }
        if (limbIdx == 2) {
            localMat.mf[3][0] = 0.0f; // No forward/back root motion
            localMat.mf[3][2] = 0.0f; // No left/right root motion
            // Keep Y (ty) for height — walk bobbing is ok
        }
    } else {
        // Identity if no animation data
        s32 i;
        for (i = 0; i < 16; i++)
            ((f32*)&localMat)[i] = 0.0f;
        localMat.mf[0][0] = localMat.mf[1][1] = localMat.mf[2][2] = localMat.mf[3][3] = 1.0f;
    }

    // boneWorld = parentWorld × localMatrix
    SkinMatrix_MtxFMtxFMult(parentWorld, &localMat, &sBoneWorldMatrices[limbIdx]);

    // Children inherit this bone's world matrix
    SSBBSkin_ComputeBoneMatricesFromAnim(skeleton, anim, frame, &sBoneWorldMatrices[limbIdx], limb->child, numLimbs);

    // Siblings inherit PARENT's world matrix
    SSBBSkin_ComputeBoneMatricesFromAnim(skeleton, anim, frame, parentWorld, limb->sibling, numLimbs);
}

// ── Blend Vertices ──────────────────────────────────────────────────────────

static void SSBBSkin_BlendVertices(SSBBSkinMesh* skin) {
    Vtx* vtxBuf;
    s32 v;
    s32 j;

    if (!skin->vtxBuf[0] || !skin->vtxBuf[1])
        return;

    vtxBuf = skin->vtxBuf[skin->bufIndex];

    for (v = 0; v < skin->vertexCount; v++) {
        SSBBSkinVertex* sv = &skin->vertices[v];
        SSBBSkinWeight* sw = &skin->weights[v];
        Vec3f restPos;
        Vec3f restNorm;
        Vec3f blendedPos;
        Vec3f blendedNorm;
        Vec3f transformedPos;
        Vec3f transformedNorm;
        f32 w;
        f32 len;
        f32 savedXW, savedYW, savedZW;

        restPos.x = sv->posX;
        restPos.y = sv->posY;
        restPos.z = sv->posZ;

        restNorm.x = sv->normX;
        restNorm.y = sv->normY;
        restNorm.z = sv->normZ;

        blendedPos.x = blendedPos.y = blendedPos.z = 0.0f;
        blendedNorm.x = blendedNorm.y = blendedNorm.z = 0.0f;

        for (j = 0; j < SSBB_MAX_INFLUENCES; j++) {
            if (sw->weight[j] == 0)
                break;

            w = sw->weight[j] * (1.0f / 255.0f);

            SkinMatrix_Vec3fMtxFMultXYZ(&sCombinedMatrices[sw->boneIndex[j]], &restPos, &transformedPos);
            blendedPos.x += transformedPos.x * w;
            blendedPos.y += transformedPos.y * w;
            blendedPos.z += transformedPos.z * w;

            savedXW = sCombinedMatrices[sw->boneIndex[j]].xw;
            savedYW = sCombinedMatrices[sw->boneIndex[j]].yw;
            savedZW = sCombinedMatrices[sw->boneIndex[j]].zw;
            sCombinedMatrices[sw->boneIndex[j]].xw = 0.0f;
            sCombinedMatrices[sw->boneIndex[j]].yw = 0.0f;
            sCombinedMatrices[sw->boneIndex[j]].zw = 0.0f;

            SkinMatrix_Vec3fMtxFMultXYZ(&sCombinedMatrices[sw->boneIndex[j]], &restNorm, &transformedNorm);

            sCombinedMatrices[sw->boneIndex[j]].xw = savedXW;
            sCombinedMatrices[sw->boneIndex[j]].yw = savedYW;
            sCombinedMatrices[sw->boneIndex[j]].zw = savedZW;

            blendedNorm.x += transformedNorm.x * w;
            blendedNorm.y += transformedNorm.y * w;
            blendedNorm.z += transformedNorm.z * w;
        }

        vtxBuf[v].n.ob[0] = (s16)blendedPos.x;
        vtxBuf[v].n.ob[1] = (s16)blendedPos.y;
        vtxBuf[v].n.ob[2] = (s16)blendedPos.z;

        len = sqrtf(blendedNorm.x * blendedNorm.x + blendedNorm.y * blendedNorm.y + blendedNorm.z * blendedNorm.z);
        if (len > 0.001f) {
            vtxBuf[v].n.n[0] = (s8)(blendedNorm.x / len * 127.0f);
            vtxBuf[v].n.n[1] = (s8)(blendedNorm.y / len * 127.0f);
            vtxBuf[v].n.n[2] = (s8)(blendedNorm.z / len * 127.0f);
        }
    }

    skin->bufIndex ^= 1;
}

// ── Draw ────────────────────────────────────────────────────────────────────

void SSBBSkin_Draw(SSBBCharacterInstance* inst, PlayState* play, Vec3f* pos, Vec3s* rot) {
    SSBBSkinMesh* skin;
    f32 s;
    s32 b;
    Mtx* worldMtx;

    if (!inst || !inst->initialized || !inst->def || !inst->def->skinMesh)
        return;

    skin = inst->def->skinMesh;
    if (!skin->vtxBuf[0] || !skin->vtxBuf[1])
        return;
    if (!inst->ssbbAnim)
        return;

    // ── 1. Compute bone matrices from SSBBAnim (translate + rotate + scale) ──
    {
        s32 skinDebug = CVarGetInteger("gExpansions.SSBB.SkinDebug", 0);

        if (skinDebug == 1) {
            // DEBUG: Identity combined matrices — renders bind pose (rest position).
            // If this looks correct, the vertex/weight/DL data is good.
            // If this looks wrong, the issue is in vertex data or DL generation.
            MtxF identity;
            s32 i;
            for (i = 0; i < 16; i++)
                ((f32*)&identity)[i] = 0.0f;
            identity.mf[0][0] = identity.mf[1][1] = identity.mf[2][2] = identity.mf[3][3] = 1.0f;
            for (b = 0; b < skin->boneCount; b++) {
                sCombinedMatrices[b] = identity;
            }
        } else {
            // Normal: compute bone world matrices from SSBBAnim
            u16 frame = (u16)inst->curFrame;
            if (frame >= inst->ssbbAnim->numFrames)
                frame = inst->ssbbAnim->numFrames - 1;

            SSBBSkin_ComputeBoneMatricesFromAnim(inst->skeleton, inst->ssbbAnim, frame, &skin->daeToF64, 0,
                                                 inst->def->numLimbs);

            for (b = 0; b < skin->boneCount; b++) {
                SkinMatrix_MtxFMtxFMult(&sBoneWorldMatrices[b], &skin->invBindMatrices[b], &sCombinedMatrices[b]);
            }
        }
    }

    // ── 3. Blend all vertices (CPU skinning) ──
    SSBBSkin_BlendVertices(skin);

    // ── 4. Draw ──
    OPEN_DISPS(play->state.gfxCtx);

    // Base RDP state (ensures consistent state regardless of what drew before)
    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    gSPSegment(POLY_OPA_DISP++, 0x0C, gCullBackDList);

    // Material DL: loads texture + sets combiner/geometry/render mode.
    // Must come AFTER Gfx_SetupDL_25Opa to override the default combiner.
    if (skin->materialDL) {
        gSPDisplayList(POLY_OPA_DISP++, skin->materialDL);
    }

    // World matrix: pos/rot × renderScale (vertices already in polygon0 space)
    Matrix_SetTranslateRotateYXZ(pos->x, pos->y, pos->z, rot);
    {
        f32 skinScale = CVarGetFloat("gExpansions.SSBB.SkinScale", 0.0f);
        s = (skinScale > 0.001f) ? skinScale : inst->def->scale;
    }
    Matrix_Scale(s, s, s, MTXMODE_APPLY);

    worldMtx = Graph_Alloc(play->state.gfxCtx, sizeof(Mtx));
    MATRIX_TOMTX(worldMtx);
    gSPMatrix(POLY_OPA_DISP++, worldMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    gSPSegment(POLY_OPA_DISP++, 0x08, skin->vtxBuf[skin->bufIndex ^ 1]);
    gSPDisplayList(POLY_OPA_DISP++, skin->displayList);

    CLOSE_DISPS(play->state.gfxCtx);
}
