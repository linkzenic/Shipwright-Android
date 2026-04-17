/**
 * item_pending_3.c - Pokeball (ITEM_POKEBALL, slot 0xB7)
 *
 * Transforms Link into Pikachu (SSBB skinned form).
 * Always active — press C-button to toggle transform/detransform.
 */

#include "z64.h"
#include "../custom_items.h"
#include "../helpers/equip_helper.h"
#include "../helpers/fx_helper.h"
#include "macros.h"
#include "functions.h"

extern void TransformMasks_HandleMaskUse(PlayState* play, Player* player, s32 maskType);

void Player_InitPending3IA(PlayState* play, Player* this) {
}

void Handle_Pending3(Player* this, PlayState* play) {
    ItemInputState input;
    ItemInput_Update(&input, ITEM_POKEBALL, this, play);
    if (!input.wasEquipped)
        return;
    if (ItemInput_IsBlocked(this, play))
        return;

    if (input.isPressed) {
        TransformMasks_HandleMaskUse(play, this, ITEM_POKEBALL);
    }
}

s32 Player_UpperAction_Pending3(Player* this, PlayState* play) {
    return 0;
}
