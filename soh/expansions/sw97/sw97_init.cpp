/**
 * sw97_init.cpp - SW97 spell/arrow actor registration
 *
 * Original actors: z64proto/sw97 team (Spaceworld '97 Experience)
 * Adapted for Ship of Harkinian (Shipwright)
 *
 * Registers 12 SW97 custom actors (6 spells + 6 arrows) with ActorDB at runtime.
 * Called from ActorDB::AddBuiltInCustomActors().
 *
 * NOTE: You must add this file to the VS Solution Explorer manually.
 */

#include "soh/ActorDB.h"

// Include headers outside extern "C" — they transitively pull in C++ headers
#include "global.h"

extern "C" {

// Forward declarations for actor lifecycle functions (defined in ported .c files)
// These are compiled in z_player.c's TU via sw97_router.c
// All names are Sw97_ prefixed via #define in sw97_router.c

// Magic spell actors
extern void Sw97_MagicFire_Init(Actor* thisx, PlayState* play);
extern void Sw97_MagicFire_Destroy(Actor* thisx, PlayState* play);
extern void Sw97_MagicFire_Update(Actor* thisx, PlayState* play);
extern void Sw97_MagicFire_Draw(Actor* thisx, PlayState* play);

extern void Sw97_MagicIce_Init(Actor* thisx, PlayState* play);
extern void Sw97_MagicIce_Destroy(Actor* thisx, PlayState* play);
extern void Sw97_MagicIce_Update(Actor* thisx, PlayState* play);
extern void Sw97_MagicIce_Draw(Actor* thisx, PlayState* play);

extern void Sw97_MagicLight_Init(Actor* thisx, PlayState* play);
extern void Sw97_MagicLight_Destroy(Actor* thisx, PlayState* play);
extern void Sw97_MagicLight_Update(Actor* thisx, PlayState* play);
extern void Sw97_MagicLight_Draw(Actor* thisx, PlayState* play);

extern void Sw97_MagicDark_Init(Actor* thisx, PlayState* play);
extern void Sw97_MagicDark_Destroy(Actor* thisx, PlayState* play);
extern void Sw97_MagicDark_Update(Actor* thisx, PlayState* play);
extern void Sw97_MagicDark_Draw(Actor* thisx, PlayState* play);

extern void Sw97_MagicSoul_Init(Actor* thisx, PlayState* play);
extern void Sw97_MagicSoul_Destroy(Actor* thisx, PlayState* play);
extern void Sw97_MagicSoul_Update(Actor* thisx, PlayState* play);
extern void Sw97_MagicSoul_Draw(Actor* thisx, PlayState* play);

extern void Sw97_MagicWind_Init(Actor* thisx, PlayState* play);
extern void Sw97_MagicWind_Destroy(Actor* thisx, PlayState* play);
extern void Sw97_MagicWind_Update(Actor* thisx, PlayState* play);
extern void Sw97_MagicWind_Draw(Actor* thisx, PlayState* play);

// Arrow variant actors
extern void Sw97_ArrowFire_Init(Actor* thisx, PlayState* play);
extern void Sw97_ArrowFire_Destroy(Actor* thisx, PlayState* play);
extern void Sw97_ArrowFire_Update(Actor* thisx, PlayState* play);
extern void Sw97_ArrowFire_Draw(Actor* thisx, PlayState* play);

extern void Sw97_ArrowIce_Init(Actor* thisx, PlayState* play);
extern void Sw97_ArrowIce_Destroy(Actor* thisx, PlayState* play);
extern void Sw97_ArrowIce_Update(Actor* thisx, PlayState* play);
extern void Sw97_ArrowIce_Draw(Actor* thisx, PlayState* play);

extern void Sw97_ArrowLight_Init(Actor* thisx, PlayState* play);
extern void Sw97_ArrowLight_Destroy(Actor* thisx, PlayState* play);
extern void Sw97_ArrowLight_Update(Actor* thisx, PlayState* play);
extern void Sw97_ArrowLight_Draw(Actor* thisx, PlayState* play);

extern void Sw97_ArrowDark_Init(Actor* thisx, PlayState* play);
extern void Sw97_ArrowDark_Destroy(Actor* thisx, PlayState* play);
extern void Sw97_ArrowDark_Update(Actor* thisx, PlayState* play);
extern void Sw97_ArrowDark_Draw(Actor* thisx, PlayState* play);

extern void Sw97_ArrowSoul_Init(Actor* thisx, PlayState* play);
extern void Sw97_ArrowSoul_Destroy(Actor* thisx, PlayState* play);
extern void Sw97_ArrowSoul_Update(Actor* thisx, PlayState* play);
extern void Sw97_ArrowSoul_Draw(Actor* thisx, PlayState* play);

extern void Sw97_ArrowWind_Init(Actor* thisx, PlayState* play);
extern void Sw97_ArrowWind_Destroy(Actor* thisx, PlayState* play);
extern void Sw97_ArrowWind_Update(Actor* thisx, PlayState* play);
extern void Sw97_ArrowWind_Draw(Actor* thisx, PlayState* play);

// Runtime actor IDs (globals accessed from C code)
s16 gSw97ActorId_MagicFire = -1;
s16 gSw97ActorId_MagicIce = -1;
s16 gSw97ActorId_MagicLight = -1;
s16 gSw97ActorId_MagicDark = -1;
s16 gSw97ActorId_MagicSoul = -1;
s16 gSw97ActorId_MagicWind = -1;
s16 gSw97ActorId_ArrowFire = -1;
s16 gSw97ActorId_ArrowIce = -1;
s16 gSw97ActorId_ArrowLight = -1;
s16 gSw97ActorId_ArrowDark = -1;
s16 gSw97ActorId_ArrowSoul = -1;
s16 gSw97ActorId_ArrowWind = -1;

} // extern "C"

// Actor struct sizes
#define SW97_MAGIC_FIRE_SIZE 0x250
#define SW97_MAGIC_ICE_SIZE 0x280
#define SW97_MAGIC_LIGHT_SIZE 0x200
#define SW97_MAGIC_DARK_SIZE 0x200
#define SW97_MAGIC_SOUL_SIZE 0x200
#define SW97_MAGIC_WIND_SIZE 0x250
#define SW97_ARROW_SIZE 0x200

void Sw97_RegisterActors() {
    // Magic Spells
    {
        ActorDBInit init;
        init.name = "Sw97_Magic_Fire";
        init.desc = "SW97 Fire Spell";
        init.category = ACTORCAT_ITEMACTION;
        init.flags = 0x10000030;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = SW97_MAGIC_FIRE_SIZE;
        init.init = Sw97_MagicFire_Init;
        init.destroy = Sw97_MagicFire_Destroy;
        init.update = Sw97_MagicFire_Update;
        init.draw = Sw97_MagicFire_Draw;
        gSw97ActorId_MagicFire = ActorDB::Instance->AddEntry(init).entry.id;
    }
    {
        ActorDBInit init;
        init.name = "Sw97_Magic_Ice";
        init.desc = "SW97 Ice Spell";
        init.category = ACTORCAT_ITEMACTION;
        init.flags = 0x10000030;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = SW97_MAGIC_ICE_SIZE;
        init.init = Sw97_MagicIce_Init;
        init.destroy = Sw97_MagicIce_Destroy;
        init.update = Sw97_MagicIce_Update;
        init.draw = Sw97_MagicIce_Draw;
        gSw97ActorId_MagicIce = ActorDB::Instance->AddEntry(init).entry.id;
    }
    {
        ActorDBInit init;
        init.name = "Sw97_Magic_Light";
        init.desc = "SW97 Light Spell";
        init.category = ACTORCAT_ITEMACTION;
        init.flags = 0x10000030;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = SW97_MAGIC_LIGHT_SIZE;
        init.init = Sw97_MagicLight_Init;
        init.destroy = Sw97_MagicLight_Destroy;
        init.update = Sw97_MagicLight_Update;
        init.draw = Sw97_MagicLight_Draw;
        gSw97ActorId_MagicLight = ActorDB::Instance->AddEntry(init).entry.id;
    }
    {
        ActorDBInit init;
        init.name = "Sw97_Magic_Dark";
        init.desc = "SW97 Shadow Spell";
        init.category = ACTORCAT_ITEMACTION;
        init.flags = 0x10000030;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = SW97_MAGIC_DARK_SIZE;
        init.init = Sw97_MagicDark_Init;
        init.destroy = Sw97_MagicDark_Destroy;
        init.update = Sw97_MagicDark_Update;
        init.draw = Sw97_MagicDark_Draw;
        gSw97ActorId_MagicDark = ActorDB::Instance->AddEntry(init).entry.id;
    }
    {
        ActorDBInit init;
        init.name = "Sw97_Magic_Soul";
        init.desc = "SW97 Spirit Spell";
        init.category = ACTORCAT_ITEMACTION;
        init.flags = 0x10000030;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = SW97_MAGIC_SOUL_SIZE;
        init.init = Sw97_MagicSoul_Init;
        init.destroy = Sw97_MagicSoul_Destroy;
        init.update = Sw97_MagicSoul_Update;
        init.draw = Sw97_MagicSoul_Draw;
        gSw97ActorId_MagicSoul = ActorDB::Instance->AddEntry(init).entry.id;
    }
    {
        ActorDBInit init;
        init.name = "Sw97_Magic_Wind";
        init.desc = "SW97 Wind Spell";
        init.category = ACTORCAT_ITEMACTION;
        init.flags = 0x10000030;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = SW97_MAGIC_WIND_SIZE;
        init.init = Sw97_MagicWind_Init;
        init.destroy = Sw97_MagicWind_Destroy;
        init.update = Sw97_MagicWind_Update;
        init.draw = Sw97_MagicWind_Draw;
        gSw97ActorId_MagicWind = ActorDB::Instance->AddEntry(init).entry.id;
    }

    // Arrow Variants
    {
        ActorDBInit init;
        init.name = "Sw97_Arrow_Fire";
        init.desc = "SW97 Fire Arrow";
        init.category = ACTORCAT_ITEMACTION;
        init.flags = 0x10000030;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = SW97_ARROW_SIZE;
        init.init = Sw97_ArrowFire_Init;
        init.destroy = Sw97_ArrowFire_Destroy;
        init.update = Sw97_ArrowFire_Update;
        init.draw = Sw97_ArrowFire_Draw;
        gSw97ActorId_ArrowFire = ActorDB::Instance->AddEntry(init).entry.id;
    }
    {
        ActorDBInit init;
        init.name = "Sw97_Arrow_Ice";
        init.desc = "SW97 Ice Arrow";
        init.category = ACTORCAT_ITEMACTION;
        init.flags = 0x10000030;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = SW97_ARROW_SIZE;
        init.init = Sw97_ArrowIce_Init;
        init.destroy = Sw97_ArrowIce_Destroy;
        init.update = Sw97_ArrowIce_Update;
        init.draw = Sw97_ArrowIce_Draw;
        gSw97ActorId_ArrowIce = ActorDB::Instance->AddEntry(init).entry.id;
    }
    {
        ActorDBInit init;
        init.name = "Sw97_Arrow_Light";
        init.desc = "SW97 Light Arrow";
        init.category = ACTORCAT_ITEMACTION;
        init.flags = 0x10000030;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = SW97_ARROW_SIZE;
        init.init = Sw97_ArrowLight_Init;
        init.destroy = Sw97_ArrowLight_Destroy;
        init.update = Sw97_ArrowLight_Update;
        init.draw = Sw97_ArrowLight_Draw;
        gSw97ActorId_ArrowLight = ActorDB::Instance->AddEntry(init).entry.id;
    }
    {
        ActorDBInit init;
        init.name = "Sw97_Arrow_Dark";
        init.desc = "SW97 Dark Arrow";
        init.category = ACTORCAT_ITEMACTION;
        init.flags = 0x10000030;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = SW97_ARROW_SIZE;
        init.init = Sw97_ArrowDark_Init;
        init.destroy = Sw97_ArrowDark_Destroy;
        init.update = Sw97_ArrowDark_Update;
        init.draw = Sw97_ArrowDark_Draw;
        gSw97ActorId_ArrowDark = ActorDB::Instance->AddEntry(init).entry.id;
    }
    {
        ActorDBInit init;
        init.name = "Sw97_Arrow_Soul";
        init.desc = "SW97 Soul Arrow";
        init.category = ACTORCAT_ITEMACTION;
        init.flags = 0x10000030;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = SW97_ARROW_SIZE;
        init.init = Sw97_ArrowSoul_Init;
        init.destroy = Sw97_ArrowSoul_Destroy;
        init.update = Sw97_ArrowSoul_Update;
        init.draw = Sw97_ArrowSoul_Draw;
        gSw97ActorId_ArrowSoul = ActorDB::Instance->AddEntry(init).entry.id;
    }
    {
        ActorDBInit init;
        init.name = "Sw97_Arrow_Wind";
        init.desc = "SW97 Wind Arrow";
        init.category = ACTORCAT_ITEMACTION;
        init.flags = 0x10000030;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = SW97_ARROW_SIZE;
        init.init = Sw97_ArrowWind_Init;
        init.destroy = Sw97_ArrowWind_Destroy;
        init.update = Sw97_ArrowWind_Update;
        init.draw = Sw97_ArrowWind_Draw;
        gSw97ActorId_ArrowWind = ActorDB::Instance->AddEntry(init).entry.id;
    }
}

void Sw97_RegisterHooks() {
    // No hooks needed — medallion equipping is handled by KaleidoScope and z_player.c
}
