// SSBB Global — includes all SSBB system .c files in one place
// This keeps z_player.c clean. Only include this file from z_player.c.

#include "expansions/ssbb/ssbb_character.c"
#include "expansions/ssbb/ssbb_skin.c"
#include "expansions/ssbb/characters/pikachu_ssbb_skel.c"
#include "expansions/ssbb/characters/pikachu_ssbb_Wait1.c"
#include "expansions/ssbb/characters/pikachu_ssbb_Wait3.c"
#include "expansions/ssbb/characters/pikachu_ssbb_all_anims.c"
#include "expansions/ssbb/characters/pikachu_ssbb_dl.c"
#include "expansions/ssbb/characters/pikachu_ssbb_skin.c"
#include "expansions/ssbb/characters/pikachu_ssbb_shadow.c"

// Register helper (defined as static inline in header — expose as non-static for C++ linkage)
#include "expansions/ssbb/characters/pikachu_ssbb_register.h"
s32 pikachu_ssbb_Register_Extern(void) {
    return pikachu_ssbb_Register();
}
