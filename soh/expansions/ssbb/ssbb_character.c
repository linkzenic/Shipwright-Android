#include "expansions/ssbb/ssbb_character.h"
#include "expansions/ssbb/ssbb_skin.h"
#include "z64.h"

// DaeToOot: converts DAE/Brawl bone-chain output to OOT/F64 coordinate space.
// oot.x = +dae.y * 1.4899, oot.y = -dae.x * 1.4899, oot.z = -dae.z * 1.4899
// Built at runtime to avoid MSVC static union initialization issues.
static void SSBBChar_ApplyDaeToOot(void) {
    MtxF m;
    s32 i;
    for (i = 0; i < 16; i++)
        ((f32*)&m)[i] = 0.0f;
    // Column-major: mf[col][row]
    // col 0: input X → output Y = -1.4899*x
    m.mf[0][1] = -1.4899f;
    // col 1: input Y → output X = +1.4899*y
    m.mf[1][0] = 1.4899f;
    // col 2: input Z → output Z = -1.4899*z
    m.mf[2][2] = -1.4899f;
    // col 3: homogeneous
    m.mf[3][3] = 1.0f;
    Matrix_Mult(&m, MTXMODE_APPLY);
}

static SSBBCharacterDef* sRegisteredChars[SSBB_MAX_CHARACTERS] = { 0 };
static s32 sNumRegistered = 0;

s32 SSBBChar_Register(SSBBCharacterDef* def) {
    if (!def || sNumRegistered >= SSBB_MAX_CHARACTERS)
        return -1;
    sRegisteredChars[sNumRegistered] = def;
    return sNumRegistered++;
}

// Read frame data directly from AnimationHeader C structs (no OTR check).
static void SSBBChar_GetFrameData(AnimationHeader* animHeader, s32 frame, s32 limbCount, Vec3s* frameTable) {
    JointIndex* jointIndices = animHeader->jointIndices;
    s16* frameData = animHeader->frameData;
    s16* staticData = &frameData[0];
    s16* dynamicData = &frameData[frame];
    u16 staticIndexMax = animHeader->staticIndexMax;
    s32 i;

    for (i = 0; i < limbCount; i++, frameTable++, jointIndices++) {
        frameTable->x =
            (jointIndices->x >= staticIndexMax) ? dynamicData[jointIndices->x] : staticData[jointIndices->x];
        frameTable->y =
            (jointIndices->y >= staticIndexMax) ? dynamicData[jointIndices->y] : staticData[jointIndices->y];
        frameTable->z =
            (jointIndices->z >= staticIndexMax) ? dynamicData[jointIndices->z] : staticData[jointIndices->z];
    }
}

void SSBBChar_Init(SSBBCharacterInstance* inst, s32 defIndex, PlayState* play) {
    if (!inst || defIndex < 0 || defIndex >= sNumRegistered)
        return;

    inst->def = sRegisteredChars[defIndex];
    inst->initialized = 0;

    FlexSkeletonHeader* skelHeader = inst->def->skeleton;

    inst->skeleton = (void**)skelHeader->sh.segment;
    inst->limbCount = skelHeader->sh.limbCount + 1;
    inst->dListCount = skelHeader->dListCount;

    inst->jointTable = ZELDA_ARENA_MALLOC_DEBUG(inst->limbCount * sizeof(Vec3s));
    if (inst->jointTable) {
        memset(inst->jointTable, 0, inst->limbCount * sizeof(Vec3s));
    }

    inst->currentAnim = NULL;
    inst->ssbbAnim = NULL;
    inst->curFrame = 0.0f;
    inst->animLength = 0.0f;
    inst->playSpeed = 1.0f;
    inst->currentAnimIndex = 0;

    // Prefer SSBB anim format (translate+rotate+scale) if available
    if (inst->def->numSSBBAnims > 0 && inst->def->ssbbAnims && inst->def->ssbbAnims[0]) {
        inst->ssbbAnim = inst->def->ssbbAnims[0];
        inst->animLength = (f32)inst->ssbbAnim->numFrames;
        inst->curFrame = 0.0f;
    } else if (inst->def->numAnims > 0 && inst->def->anims[0] != NULL) {
        inst->currentAnim = inst->def->anims[0];
        inst->animLength = (f32)inst->currentAnim->common.frameCount;
        inst->curFrame = 0.0f;
    }

    // Initialize weighted skin buffers if present (destroy first for soft reset safety)
    if (inst->def->skinMesh) {
        SSBBSkin_Destroy(inst->def->skinMesh);
        SSBBSkin_Init(inst->def->skinMesh);
    }

    inst->initialized = 1;
}

void SSBBChar_SetAnim(SSBBCharacterInstance* inst, u16 animIndex, f32 playSpeed) {
    if (!inst || !inst->initialized || !inst->def)
        return;

    // Prefer SSBB format (translate+rotate+scale) if available
    if (inst->def->ssbbAnims && animIndex < inst->def->numSSBBAnims && inst->def->ssbbAnims[animIndex]) {
        inst->ssbbAnim = inst->def->ssbbAnims[animIndex];
        inst->animLength = (f32)inst->ssbbAnim->numFrames;
        inst->curFrame = 0.0f;
        inst->playSpeed = playSpeed;
        inst->currentAnimIndex = animIndex;
        return;
    }

    // Fallback to OOT format (rotation only)
    if (animIndex >= inst->def->numAnims)
        return;
    AnimationHeader* anim = inst->def->anims[animIndex];
    if (!anim)
        return;

    inst->currentAnim = anim;
    inst->ssbbAnim = NULL;
    inst->animLength = (f32)anim->common.frameCount;
    inst->curFrame = 0.0f;
    inst->playSpeed = playSpeed;
    inst->currentAnimIndex = animIndex;
}

void SSBBChar_Update(SSBBCharacterInstance* inst) {
    if (!inst || !inst->initialized)
        return;

    // SSBB anim: just advance frame (bone computation happens in SSBBSkin_Draw)
    if (inst->ssbbAnim) {
        f32 updateRate = R_UPDATE_RATE * (1.0f / 3.0f);
        inst->curFrame += inst->playSpeed * updateRate;
        if (inst->curFrame >= inst->animLength) {
            inst->curFrame -= inst->animLength;
        } else if (inst->curFrame < 0.0f) {
            inst->curFrame += inst->animLength;
        }
        return;
    }

    // OOT anim: decode frame data into jointTable
    if (!inst->currentAnim || !inst->jointTable)
        return;
    SSBBChar_GetFrameData(inst->currentAnim, (s32)inst->curFrame, inst->limbCount, inst->jointTable);

    f32 updateRate = R_UPDATE_RATE * (1.0f / 3.0f);
    inst->curFrame += inst->playSpeed * updateRate;
    if (inst->curFrame >= inst->animLength) {
        inst->curFrame -= inst->animLength;
    } else if (inst->curFrame < 0.0f) {
        inst->curFrame += inst->animLength;
    }
}

// ── Fill matrix buffer (same pattern as working Pikachu's Pika_FillMatBuf) ──
// Uses OOT's global matrix stack directly. Max child depth is ~13 for Brawl
// skeletons, well within the 20-entry global stack limit.

static void SSBBChar_FillMatBuf(void** skeleton, Vec3s* jointTable, Mtx* buf, s32* slot, u8 limbIdx, s32 numLimbs) {
    if (limbIdx == LIMB_DONE || limbIdx >= numLimbs)
        return;

    StandardLimb* limb = (StandardLimb*)skeleton[limbIdx];
    Vec3f pos;
    Vec3s rot;

    if (limbIdx == 0) {
        pos.x = (f32)jointTable[0].x;
        pos.y = (f32)jointTable[0].y;
        pos.z = (f32)jointTable[0].z;
    } else {
        pos.x = (f32)limb->jointPos.x;
        pos.y = (f32)limb->jointPos.y;
        pos.z = (f32)limb->jointPos.z;
    }
    rot = jointTable[limbIdx + 1];

    Matrix_Push();
    Matrix_TranslateRotateZYX(&pos, &rot);

    if (limb->dList != NULL) {
        MATRIX_TOMTX(&buf[(*slot)++]);
    }

    SSBBChar_FillMatBuf(skeleton, jointTable, buf, slot, limb->child, numLimbs);
    Matrix_Pop();
    SSBBChar_FillMatBuf(skeleton, jointTable, buf, slot, limb->sibling, numLimbs);
}

static void SSBBChar_DrawLimbR(PlayState* play, void** skeleton, Mtx* buf, s32* slot, u8 limbIdx, s32 numLimbs) {
    if (limbIdx == LIMB_DONE || limbIdx >= numLimbs)
        return;

    StandardLimb* limb = (StandardLimb*)skeleton[limbIdx];

    if (limb->dList != NULL) {
        OPEN_DISPS(play->state.gfxCtx);
        gSPMatrix(POLY_OPA_DISP++, &buf[(*slot)++], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, limb->dList);
        CLOSE_DISPS(play->state.gfxCtx);
    }

    SSBBChar_DrawLimbR(play, skeleton, buf, slot, limb->child, numLimbs);
    SSBBChar_DrawLimbR(play, skeleton, buf, slot, limb->sibling, numLimbs);
}

// Debug: draw all limbs with a single world matrix (no skeleton transforms)
// This tests if the vertices are in world-space vs bone-local-space
static void SSBBChar_DrawAllFlat(SSBBCharacterInstance* inst, PlayState* play) {
    s32 i;
    // Must use Graph_Alloc — stack matrices become dangling pointers before GPU reads them
    Mtx* worldMtx = Graph_Alloc(play->state.gfxCtx, sizeof(Mtx));
    OPEN_DISPS(play->state.gfxCtx);
    MATRIX_TOMTX(worldMtx);
    for (i = 0; i < inst->def->numLimbs; i++) {
        StandardLimb* limb = (StandardLimb*)inst->skeleton[i];
        if (limb->dList != NULL) {
            gSPMatrix(POLY_OPA_DISP++, worldMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, limb->dList);
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

void SSBBChar_Draw(SSBBCharacterInstance* inst, PlayState* play, Vec3f* pos, Vec3s* rot) {
    s32 debugMode;
    f32 s;
    Mtx* matBuf;
    s32 fillSlot;
    s32 drawSlot;
    Vec3s* drawTable;
    Vec3s zeroTable[50];

    if (!inst || !inst->initialized || !inst->def || !inst->jointTable)
        return;

    debugMode = CVarGetInteger("gExpansions.SSBB.Pikachu", 0);

    // Weighted skinning path (mode 2 only)
    if (inst->def->skinMesh && debugMode == 2) {
        SSBBSkin_Draw(inst, play, pos, rot);
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    gDPPipeSync(POLY_OPA_DISP++);
    // Yellow base color × lighting shade (normals stored in vertex RGBA)
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 220, 50, 255);
    gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, 0, SHADE, 0, 0, 0, 0, PRIMITIVE, PRIMITIVE, 0, SHADE, 0, 0, 0, 0,
                      PRIMITIVE);
    gSPLoadGeometryMode(POLY_OPA_DISP++, G_ZBUFFER | G_SHADING_SMOOTH | G_LIGHTING | G_SHADE);
    gSPSegment(POLY_OPA_DISP++, 0x0C, gCullBackDList);

    // Set up world transform
    Matrix_SetTranslateRotateYXZ(pos->x, pos->y, pos->z, rot);
    s = inst->def->scale;
    Matrix_Scale(s, s, s, MTXMODE_APPLY);

    // Convert from DAE/Brawl coordinate space to OOT/F64 coordinate space
    SSBBChar_ApplyDaeToOot();

    if (debugMode == 3) {
        // Flat draw: all DLs with the same world matrix (tests if verts are in world space)
        SSBBChar_DrawAllFlat(inst, play);
    } else {
        // For debug mode 4: use zero rotations (rest pose test)
        drawTable = inst->jointTable;
        if (debugMode == 4) {
            memset(zeroTable, 0, sizeof(zeroTable));
            drawTable = zeroTable;
        }

        matBuf = Graph_Alloc(play->state.gfxCtx, inst->dListCount * sizeof(Mtx));

        fillSlot = 0;
        SSBBChar_FillMatBuf(inst->skeleton, drawTable, matBuf, &fillSlot, 0, inst->def->numLimbs);

        drawSlot = 0;
        SSBBChar_DrawLimbR(play, inst->skeleton, matBuf, &drawSlot, 0, inst->def->numLimbs);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}
