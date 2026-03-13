/**
 * sw97_config.h - CVar definitions for SW97 Medallion Spells
 *
 * Original actors: z64proto/sw97 team (Spaceworld '97 Experience)
 * Adapted for Ship of Harkinian (Shipwright)
 */
#ifndef SW97_CONFIG_H
#define SW97_CONFIG_H

// Single CVar toggle — enables medallion spell/arrow equipping
#define SW97_MEDALLIONS_CVAR "gEnhancements.SkijerNEI.SW97Medallions"
#define SW97_MEDALLIONS_ENABLED() CVarGetInteger(SW97_MEDALLIONS_CVAR, 0)

#endif // SW97_CONFIG_H
