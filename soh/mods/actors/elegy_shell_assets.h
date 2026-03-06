/**
 * elegy_shell_assets.h - MM Elegy of Emptiness shell DL paths from mm.o2r
 *
 * These display lists are from MM's gameplay_keep object.
 * They render as static statues of Link's different forms.
 */

#ifndef ELEGY_SHELL_ASSETS_H
#define ELEGY_SHELL_ASSETS_H

#include "align_asset_macro.h"

// OTR paths for Elegy Shell display lists (from mm.o2r gameplay_keep)
#define dgElegyShellHumanDL "__OTR__objects/gameplay_keep/gElegyShellHumanDL"
static const ALIGN_ASSET(2) char gElegyShellHumanDL[] = dgElegyShellHumanDL;

#define dgElegyShellGoronDL "__OTR__objects/gameplay_keep/gElegyShellGoronDL"
static const ALIGN_ASSET(2) char gElegyShellGoronDL[] = dgElegyShellGoronDL;

#define dgElegyShellZoraDL "__OTR__objects/gameplay_keep/gElegyShellZoraDL"
static const ALIGN_ASSET(2) char gElegyShellZoraDL[] = dgElegyShellZoraDL;

#define dgElegyShellDekuDL "__OTR__objects/gameplay_keep/gElegyShellDekuDL"
static const ALIGN_ASSET(2) char gElegyShellDekuDL[] = dgElegyShellDekuDL;

// Form indices (matches MM's PlayerForm enum for shell selection)
#define ELEGY_FORM_HUMAN 0
#define ELEGY_FORM_GORON 1
#define ELEGY_FORM_ZORA  2
#define ELEGY_FORM_DEKU  3
#define ELEGY_FORM_FD    4 // Fierce Deity uses human shell
#define ELEGY_FORM_MAX   5

#endif // ELEGY_SHELL_ASSETS_H
