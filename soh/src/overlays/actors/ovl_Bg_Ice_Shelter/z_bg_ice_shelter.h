#ifndef Z_BG_ICE_SHELTER_H
#define Z_BG_ICE_SHELTER_H

#include <libultraship/libultra.h>
#include "global.h"

struct BgIceShelter;

typedef void (*BgIceShelterActionFunc)(struct BgIceShelter*, PlayState*);

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

#endif
