/**
 * broken_items.c - "Broken Modes" pause subscreen. See broken_items.h.
 *
 * Renders INSIDE the Map pause page: the Map page's own stone/parchment frame
 * (KaleidoScope_DrawPageSections + sMapTexs) is kept, the dungeon/world map
 * image is replaced by two item-icon selectors drawn as quads on the map face
 * (Ocarina of Time = Link Mode, Mario Mask = Mario Mode). The selected mode's
 * name + control map are printed below with the game font (GfxPrint).
 */

#include "global.h"
#include "mods/extended_inventory.h" // ExtInv_GetItemIcon
#include "assets/soh_assets.h"       // gPikaIconPikachuTex (Pikachu mode selector icon)
#include "broken_items.h"

#define CVAR_BROKEN_ITEMS_ENABLED "gBrokenItems.Enabled"
#define CVAR_SM64_MARIO           "gSm64Mario"

// Pikachu MODE — persistent CVar like Mario's gSm64Mario. The per-frame watcher
// in mm_player_form.cpp (MmForm_Update) sees the CVar and holds the Pikachu form
// via the instant 5-frame flash (no mm.o2r transformation-cutscene anims).
// This MODE coexists with the Pokeball ITEM (extended inventory page 2), which
// keeps its classic transform flow + cutscene untouched.
#define CVAR_PIKACHU_MODE "gPikachuMode"

// ---------------------------------------------------------------------------
// Mode + control-map data (English on purpose). Keep action strings short.
// ---------------------------------------------------------------------------
typedef struct {
    const char* btn;
    const char* action;
} BrokenCtrl;

typedef struct {
    const char* name;
    const BrokenCtrl* controls;
    s32 controlCount;
} BrokenMode;

static const BrokenCtrl sLinkControls[] = {
    { "Stick", "Move / run" },
    { "A",     "Action/roll" },
    { "B",     "Sword" },
    { "C",     "Items" },
    { "Z",     "Z-target" },
    { "R",     "Shield" },
};

static const BrokenCtrl sMarioControls[] = {
    { "Stick", "Move (SM64)" },
    { "A",     "Jump x2/x3" },
    { "B",     "Fire/punch" },
    { "Z",     "Crouch/GP" },
    { "D-Dn",  "Wing Cap" },
    { "D-Lf",  "Metal Cap" },
    { "D-Rt",  "Vanish Cap" },
    { "D-Up",  "Fire (soon)" },
};

// Physical X / Y / RB are expected mapped to C-Left / C-Right / C-Down in the
// input editor (right stick stays free for the camera). Rebindable: gPikaBind.*.
static const BrokenCtrl sPikachuControls[] = {
    { "A",    "Fight/talk" },
    { "B",    "Electric" },
    { "R",    "Shield" },
    { "C-Lf", "Jump" },
    { "C-Rt", "Quick Atk" },
    { "C-Dn", "Grass dash" },
    { "D-Up", "GMax/Charge" },
    { "D-Dn", "Iron Tail" },
    { "D-Rt", "Dark bomb" },
    { "D-Lf", "Sleep" },
};

typedef enum {
    BROKEN_MODE_LINK,
    BROKEN_MODE_MARIO,
    BROKEN_MODE_PIKACHU,
    BROKEN_MODE_COUNT
} BrokenModeId;

static const BrokenMode sModes[BROKEN_MODE_COUNT] = {
    { "LINK MODE",    sLinkControls,    ARRAY_COUNT(sLinkControls) },
    { "MARIO MODE",   sMarioControls,   ARRAY_COUNT(sMarioControls) },
    { "PIKACHU MODE", sPikachuControls, ARRAY_COUNT(sPikachuControls) },
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
// (State for the old Map-page overlay was removed; the equipment-page selector
// owns its own cursor state in z_kaleido_equipment.c.)

// ---------------------------------------------------------------------------
static void BrokenItems_PlaySfx(u16 sfxId) {
    Audio_PlaySoundGeneral(sfxId, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultReverb);
}

// Which mode is currently equipped, derived from the persistent mode CVars.
// (A pokeball-item transformation is intentionally NOT reflected here — that is
// the other, transient system and doesn't change the equipped MODE.)
static s32 BrokenItems_CurrentEquipped(void) {
    if (CVarGetInteger(CVAR_PIKACHU_MODE, 0) != 0) {
        return BROKEN_MODE_PIKACHU;
    }
    return (CVarGetInteger(CVAR_SM64_MARIO, 0) != 0) ? BROKEN_MODE_MARIO : BROKEN_MODE_LINK;
}

s32 BrokenItems_Enabled(void) {
    return CVarGetInteger(CVAR_BROKEN_ITEMS_ENABLED, 0);
}

// Equips a mode (3-way, mutually exclusive CVars — like Mario, NOT a transform
// call). The Pikachu form itself is engaged by the gPikachuMode watcher in
// mm_player_form.cpp via the instant flash on the next gameplay frame. We
// deliberately do NOT touch gSm64MarioMaskForce so Mario is no longer bound to
// C-Down, and we never touch the pokeball-item transform flow.
static void BrokenItems_Equip(PlayState* play, s32 mode) {
    (void)play;
    CVarSetInteger(CVAR_SM64_MARIO, (mode == BROKEN_MODE_MARIO) ? 1 : 0);
    CVarSetInteger(CVAR_PIKACHU_MODE, (mode == BROKEN_MODE_PIKACHU) ? 1 : 0);
    CVarSave();
    BrokenItems_PlaySfx(NA_SE_SY_DECIDE);
}

// Forward decl — the icon resolver is defined in the Drawing section below, but
// BrokenItems_FormIconTex (an accessor) calls it here first.
static void* BrokenItems_ModeIconTex(s32 mode);

// ---------------------------------------------------------------------------
// Public accessors for the EQUIPMENT-page transform selector (z_kaleido_equipment.c
// draws the forms in the equipment grid + uses cursorItem for the name; it reuses
// this form data + the same toggle so there's one source of truth).
// ---------------------------------------------------------------------------
s32 BrokenItems_FormCount(void) {
    return BROKEN_MODE_COUNT;
}
const char* BrokenItems_FormName(s32 i) {
    if (i < 0 || i >= BROKEN_MODE_COUNT) {
        return "";
    }
    return sModes[i].name;
}
void* BrokenItems_FormIconTex(s32 i) {
    return BrokenItems_ModeIconTex(i);
}
// The OOT item whose NAME texture represents this form (shown where the equipment
// item name normally appears). Link→Ocarina, Mario→Mario's Mask, Pikachu→Pokeball.
u16 BrokenItems_FormItem(s32 i) {
    if (i == BROKEN_MODE_MARIO) {
        return ITEM_MARIO_MASK;
    }
    if (i == BROKEN_MODE_PIKACHU) {
        return ITEM_POKEBALL;
    }
    return ITEM_OCARINA_TIME;
}
s32 BrokenItems_CurrentForm(void) {
    return BrokenItems_CurrentEquipped();
}
void BrokenItems_EquipForm(PlayState* play, s32 i) {
    BrokenItems_Equip(play, i);
}

// ===========================================================================
// Drawing
// ===========================================================================

// Item icon for each mode (the selectors). Pikachu uses its own custom texture
// (gPikaIconPikachuTex), resolved in the draw loop instead of via item id.
static u16 BrokenItems_ModeIcon(s32 mode) {
    return (mode == BROKEN_MODE_MARIO) ? ITEM_MARIO_MASK : ITEM_OCARINA_TIME;
}

static void* BrokenItems_ModeIconTex(s32 mode) {
    if (mode == BROKEN_MODE_PIKACHU) {
        // Custom icon lives in soh.o2r (textures/pikachu/). If the archive
        // hasn't been repacked with it yet, fall back to the pokeball item
        // icon (always shipped) instead of feeding Fast3D a missing path.
        extern uint8_t ResourceMgr_FileExists(const char* resName);
        if (ResourceMgr_FileExists(dgPikaIconPikachuTex)) {
            return (void*)gPikaIconPikachuTex;
        }
        return ExtInv_GetItemIcon(ITEM_POKEBALL);
    }
    return ExtInv_GetItemIcon(BrokenItems_ModeIcon(mode));
}

// (The old Map-page overlay drawing + input lived here; the transform selector now
// renders inside the Equipment page — see KaleidoScope_DrawEquipment. The shared
// form data + toggle above is what that page reuses.)
