#ifndef SSBB_ANIM_H
#define SSBB_ANIM_H

#include "z64.h"

// SSBB Animation format: per-bone translate + rotate + scale per frame.
// Unlike OOT's AnimationHeader (rotation only), this stores all 3 transform types.

#define SSBB_ANIM_MAX_BONES 64

// Per-bone per-frame transform (9 floats: T3 + R3 + S3)
typedef struct {
    f32 tx, ty, tz; // Translation (DAE space, local to parent)
    f32 rx, ry, rz; // Rotation in degrees (ZYX Euler order, matching Maya/Brawl)
    f32 sx, sy, sz; // Scale (1.0 = no scale)
} SSBBBoneFrame;

// Full animation: array of [numFrames x numBones] SSBBBoneFrame
// NOTE: Use "struct SSBBAnim" (not typedef) to match forward declaration in ssbb_character.h
struct SSBBAnim {
    const char* name;
    u16 numFrames;
    u16 numBones;
    f32 frameRate;               // frames per second (usually 30 or 60)
    const SSBBBoneFrame* frames; // [numFrames * numBones] -- frame-major order
                                 // frames[frame * numBones + boneIdx]
};

// Get the transform for a specific bone at a specific frame
static inline const SSBBBoneFrame* SSBBAnim_GetBoneFrame(const struct SSBBAnim* anim, u16 frame, u16 boneIdx) {
    if (!anim || !anim->frames || frame >= anim->numFrames || boneIdx >= anim->numBones)
        return NULL;
    return &anim->frames[frame * anim->numBones + boneIdx];
}

#endif // SSBB_ANIM_H
