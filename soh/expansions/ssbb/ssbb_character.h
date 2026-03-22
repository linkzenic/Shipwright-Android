#ifndef SSBB_CHARACTER_H
#define SSBB_CHARACTER_H

#include "z64.h"

// ── SSBB Character System ───────────────────────────────────────────────────
// General-purpose rendering system for Super Smash Bros Brawl characters
// imported into OOT via brawl_to_oot.py converter.
//
// NOTE: This system bypasses SoH's OTR resource manager entirely.
// SoH's SkelAnime functions (SkelAnime_InitFlex, Animation_Change, etc.)
// check (pointer & 1) to detect OTR paths. C struct pointers are aligned
// (LSB=0), so they get treated as OTR paths and crash. We manage animation
// state ourselves and only use SkelAnime_DrawFlex for rendering (no OTR check).

#define SSBB_MAX_CHARACTERS 16
#define SSBB_MAX_ANIMS 64
#define SSBB_LIMB_NONE 0xFF

#define SSBB_ROT_ORDER_ZYX 1

// Forward declarations
typedef struct SSBBSkinMesh SSBBSkinMesh;
// SSBBAnim is defined in ssbb_anim.h (must be included before this header)

typedef struct {
    const char* name;
    FlexSkeletonHeader* skeleton;
    AnimationHeader** anims;        // OOT format (rotation only, for rigid path)
    const struct SSBBAnim** ssbbAnims;     // New format (translate+rotate+scale, for skin path)
    u16 numAnims;
    u16 numSSBBAnims;
    f32 scale;
    u8 numLimbs;
    u8 rotOrder;
    SSBBSkinMesh* skinMesh;  // NULL = rigid limb rendering, non-NULL = weighted skinning
} SSBBCharacterDef;

typedef struct {
    SSBBCharacterDef* def;
    // Raw pointers (no SkelAnime, no OTR)
    void** skeleton;
    Vec3s* jointTable;
    s32 limbCount;
    s32 dListCount;
    // Animation state (OOT format)
    AnimationHeader* currentAnim;
    f32 curFrame;
    f32 animLength;
    f32 playSpeed;
    u16 currentAnimIndex;
    u8 initialized;
    // Animation state (SSBB format — translate+rotate+scale)
    const struct SSBBAnim* ssbbAnim;
} SSBBCharacterInstance;

s32 SSBBChar_Register(SSBBCharacterDef* def);
void SSBBChar_Init(SSBBCharacterInstance* inst, s32 defIndex, PlayState* play);
void SSBBChar_SetAnim(SSBBCharacterInstance* inst, u16 animIndex, f32 playSpeed);
void SSBBChar_Update(SSBBCharacterInstance* inst);
void SSBBChar_Draw(SSBBCharacterInstance* inst, PlayState* play, Vec3f* pos, Vec3s* rot);

#endif // SSBB_CHARACTER_H
