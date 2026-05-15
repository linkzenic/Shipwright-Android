/**
 * custom_items.c - Unity build aggregator for custom items
 *
 * This file includes all custom item implementation files for unity build.
 * Unity builds compile multiple .c files as one translation unit for faster
 * compile times and potential optimizations.
 *
 * Add new item logic files here to include them in the build.
 */

#include "../custom_items.h"
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "objects/gameplay_keep/gameplay_keep.h"

// Helper modules
#include "../helpers/movement_helper.c"
#include "../helpers/equip_helper.c"
#include "../helpers/camera_helper.c"
#include "../helpers/cutscene_helper.c"
#include "../helpers/fx_helper.c"
#include "../helpers/combat_helper.c"
#include "../helpers/grappling_helper.c"
#include "../custom_items_common.c"
#include "../objects/object_custom_items.c"

// MM Animation Loader (must be before item_rocscape.c which uses it)
#include "mods/anim_translator/mm_anim_loader.c"

// Item implementations
#include "item_rocsfeather.c"
#include "item_rocscape.c"
#include "item_dekuleaf.c"
#include "item_spinner.c"
#include "item_rod_fire.c"
#include "item_rod_ice.c"
#include "item_rod_light.c"
#include "item_lantern.c"
#include "item_pending_3.c"
#include "item_hylias_grace.c"
#include "item_demise_destruction.c"
#include "item_zonai_permafrost.c"
#include "item_mitts.c"
#include "item_shovel.c"
#include "item_switchhook.c"
#include "item_desire_sensor.c"
#include "item_whip.c"
#include "item_ballchain.c"
#include "item_bombarrows.c"
#include "item_gustjar.c"
#include "item_beetle.c"
#include "item_dominionrod.c"
#include "item_cane_of_somaria.c"
#include "item_time_gate.c"
#include "item_minish_cap.c"
#include "../helpers/minish_kaleido.c"
#include "item_postman_hat.c"
// item_postman_hat.c appends `#include "../helpers/postman_kaleido.c"` at
// its tail so the kaleido body ends up in the same TU.

// Bremen Mask: chick + adult cucco follower actor (unity-included).
#include "../helpers/bremen_follower_actor.c"

// Mask of Scents: hidden Lost Woods mushroom spot prop actor (unity-included).
#include "../helpers/mushroom_spot_actor.c"

// Transformation Masks: REMOVED - now included directly in z_player.c
// (transformation_masks.c includes mask_goron.c internally)