#ifndef Z_BG_ICE_SHELTER_H
#define Z_BG_ICE_SHELTER_H

#include <libultraship/libultra.h>
#include "global.h"

struct BgIceShelter;

typedef void (*BgIceShelterActionFunc)(struct BgIceShelter*, PlayState*);

typedef enum RedIceType {
    /* 0 */ RED_ICE_LARGE,    // Large red ice block
    /* 1 */ RED_ICE_SMALL,    // Small red ice block
    /* 2 */ RED_ICE_PLATFORM, // Complex structure that can be climbed and walked on. Unused in vanilla OoT, used in MQ to cover the Ice Cavern Map chest
    /* 3 */ RED_ICE_WALL,     // Vertical ice sheets blocking corridors
    /* 4 */ RED_ICE_KING_ZORA // Giant red ice block covering King Zora
} RedIceType;

typedef struct BgIceShelter {
    /* 0x0000 */ DynaPolyActor dyna;
    /* 0x0164 */ BgIceShelterActionFunc actionFunc;
    /* 0x0168 */ ColliderCylinder cylinder1;
    /* 0x01B4 */ ColliderCylinder cylinder2;
    /* 0x0200 */ s16 alpha;
} BgIceShelter; // size = 0x0204

// Public: Ball and Chain shatters red ice instantly (ice fragments + Actor_Kill)
void BgIceShelter_BreakInstantly(Actor* thisx, PlayState* play);

// Public: Ice Rod melts red ice (fast melt animation, like Blue Fire)
void BgIceShelter_MeltInstantly(Actor* thisx, PlayState* play);

// Public: Ball and Chain shatter + melt — spawns ice fragments and triggers
// the melt path so VB_RED_ICE_DROP_ITEM fires (entrega rando reward).
void BgIceShelter_ShatterMelt(Actor* thisx, PlayState* play);

#endif
