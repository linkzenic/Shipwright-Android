/**
 * deku_flower_assets.h - MM Gold Deku Flower DL paths from mm.o2r
 *
 * The "gold" Deku flower is the launching flower Deku Link dives into to be
 * shot upward — in MM these are placed in scenes; we spawn one dynamically
 * each time Deku uses the Deku Leaf on the ground (custom enhancement, not
 * present in MM original which relied on pre-placed scene flowers).
 *
 * DLs live in MM's gameplay_keep object (packed into mm.o2r).
 */

#ifndef DEKU_FLOWER_ASSETS_H
#define DEKU_FLOWER_ASSETS_H

#include "align_asset_macro.h"

// Composite "idle" DL — calls all the part DLs (base, center, petals, leaves)
// and renders the full flower in its resting pose. Used by MM's deku-flower
// scene actors and reused here for the dynamically-summoned launch flower.
#define dgGoldDekuFlowerIdleDL "__OTR__objects/gameplay_keep/gGoldDekuFlowerIdleDL"
static const ALIGN_ASSET(2) char gGoldDekuFlowerIdleDL[] = dgGoldDekuFlowerIdleDL;

#endif // DEKU_FLOWER_ASSETS_H
