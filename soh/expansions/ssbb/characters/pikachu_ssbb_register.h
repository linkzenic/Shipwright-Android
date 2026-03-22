#ifndef PIKACHU_SSBB_REGISTER_H
#define PIKACHU_SSBB_REGISTER_H

// Auto-generated SSBB character registration
// Include this file and call the register function to use this character

#include "expansions/ssbb/ssbb_character.h"
#include "expansions/ssbb/characters/pikachu_ssbb_skel.h"
#include "expansions/ssbb/characters/pikachu_ssbb_skin.h"
#include "expansions/ssbb/characters/pikachu_ssbb_Wait1.h"
#include "expansions/ssbb/characters/pikachu_ssbb_Wait3.h"
#include "expansions/ssbb/characters/pikachu_ssbb_Wait1_ssbb.h"
#include "expansions/ssbb/characters/pikachu_ssbb_Wait3_ssbb.h"

static AnimationHeader* pikachu_ssbb_anims[] = {
    &pikachu_ssbb_Wait1_anim,
    &pikachu_ssbb_Wait3_anim,
};

static const struct SSBBAnim* pikachu_ssbb_ssbb_anims[] = {
    &pikachu_ssbb_Wait1_ssbb_anim,
    &pikachu_ssbb_Wait3_ssbb_anim,
};

static SSBBCharacterDef pikachu_ssbb_def = {
    .name = "pikachu_ssbb",
    .skeleton = &pikachu_ssbb_skeleton,
    .anims = pikachu_ssbb_anims,
    .ssbbAnims = pikachu_ssbb_ssbb_anims,
    .numAnims = 2,
    .numSSBBAnims = 2,
    .scale = 0.05f,
    .numLimbs = 48,
    .rotOrder = SSBB_ROT_ORDER_ZYX,
    .skinMesh = &pikachu_ssbb_skin_mesh,
};

static inline s32 pikachu_ssbb_Register(void) {
    return SSBBChar_Register(&pikachu_ssbb_def);
}

#endif // PIKACHU_SSBB_REGISTER_H
