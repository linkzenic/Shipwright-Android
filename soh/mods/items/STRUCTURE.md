# Not Enough Items - Technical Structure
### Developer reference for the custom items system

---

## Architecture Overview

The mod implements custom items through a unified state machine system, extending OoT's item handling via hooks into Ship of Harkinian's modding framework. All items share a centralized state structure and follow consistent patterns for input handling, animations, and collision.

---

## Directory Structure

```
soh/mods/
├── items/
│   ├── custom_items.h              # Core header - item IDs, state struct
│   ├── custom_items_common.c       # Global state, update dispatcher
│   │
│   ├── logic/                      # Item behavior implementations
│   │   ├── custom_items.c          # Main update dispatcher
│   │   ├── item_rocsfeather.c/.h   # Roc's Feather
│   │   ├── item_rocscape.c/.h      # Roc's Cape (upgrade)
│   │   ├── item_dekuleaf.c/.h      # Deku Leaf
│   │   ├── item_ballchain.c/.h     # Ball and Chain
│   │   ├── item_whip.c/.h          # Whip
│   │   ├── item_switchhook.c/.h    # Switch Hook
│   │   ├── item_gustjar.c/.h       # Gust Jar
│   │   ├── item_beetle.c/.h        # Beetle
│   │   ├── item_spinner.c/.h       # Spinner
│   │   ├── item_shovel.c/.h        # Shovel
│   │   ├── item_rod_fire.c/.h      # Fire Rod
│   │   ├── item_rod_ice.c/.h       # Ice Rod
│   │   ├── item_rod_light.c/.h     # Light Rod
│   │   └── ... (other items)
│   │
│   ├── helpers/                    # Shared utility modules
│   │   ├── camera_helper.c/.h      # First-person mode, reticles
│   │   ├── combat_helper.c/.h      # Damage, colliders
│   │   ├── cutscene_helper.c/.h    # Animation control
│   │   ├── equip_helper.c/.h       # Input handling, equip state
│   │   ├── fx_helper.c/.h          # Visual effects
│   │   ├── movement_helper.c/.h    # Ground checks, physics
│   │   └── grappling_helper.c/.h   # Surface analysis for whip
│   │
│   ├── icons/                      # Item icon textures (32x32)
│   ├── names/                      # Item name textures
│   ├── objects/                    # 3D models and display lists
│   │   ├── object_firerod.c
│   │   ├── object_icerod.c
│   │   ├── object_lightrod.c
│   │   ├── object_dekuleaf.c
│   │   ├── object_shovel.c
│   │   ├── object_cane_of_somaria.c
│   │   └── ...
│   │
│   └── anim/                       # Custom skeletal animations
│
├── actors/                         # Custom actors
│   └── somaria_cubes.c/.h          # Cane of Somaria block actor
│
├── extended_inventory.c/.h         # 2-page inventory system
├── extended_player.c/.h            # Player extensions
│
└── transformation_masks/           # (Beta) MM masks system
    ├── transformation_masks.h
    ├── assets/
    └── masks/
```

---

## Core System: CustomItemState

All item state is centralized in a single global struct defined in `custom_items.h`:

```c
typedef struct {
    // General timers
    s16 timer1;
    s16 timer2;
    s32 globalCooldownTimer;

    // Per-item state (example: Ball and Chain)
    u8 ballAndChainThrown;
    u8 ballAndChainFirstPersonActive;
    ColliderCylinder ballAndChainCollider;
    Vec3f ballAndChainPos;
    f32 ballAndChainChargeLevel;

    // Whip state
    u8 whipState;
    Vec3f whipTipPos;
    Actor* whipAttachedActor;
    f32 whipSwingAngle;
    f32 whipSwingVelocity;

    // Switch Hook state
    u8 switchHookState;
    Vec3f switchHookTipPos;
    Actor* switchHookTargetActor;

    // ... similar fields for each item

} CustomItemState;

extern CustomItemState gCustomItemState;
```

---

## Item ID Allocation

Custom items use IDs starting at `0x9D`, defined in `custom_items.h`:

| ID | Constant | Item |
|----|----------|------|
| 0x9D | `ITEM_ROCS_FEATHER_SKIJER` | Roc's Feather |
| 0x9E | `ITEM_ROCS_CAPE` | Roc's Cape |
| 0x9F | `ITEM_DESIRE_SENSOR` | Desire Sensor |
| 0xA0 | `ITEM_HYLIAS_GRACE` | Hylia's Grace |
| 0xA1 | `ITEM_ZONAI_PERMAFROST` | Zonai Permafrost |
| 0xA2 | `ITEM_DEMISE_DESTRUCTION` | Demise Destruction |
| 0xA3 | `ITEM_DEKU_LEAF` | Deku Leaf |
| 0xA4 | `ITEM_SWITCH_HOOK` | Switch Hook |
| 0xA5 | `ITEM_MOGMA_MITTS` | Mogma Mitts |
| 0xA6 | `ITEM_GUST_JAR` | Gust Jar |
| 0xA7 | `ITEM_BALL_AND_CHAIN` | Ball and Chain |
| 0xA8 | `ITEM_WHIP` | Whip |
| 0xA9 | `ITEM_SPINNER` | Spinner |
| 0xAA | `ITEM_CANE_OF_SOMARIA` | Cane of Somaria |
| 0xAB | `ITEM_DOMINION_ROD` | Dominion Rod |
| 0xAC | `ITEM_TIME_GATE` | Time Gate |
| 0xAD | `ITEM_BOMB_ARROWS` | Bomb Arrows |
| 0xAE | `ITEM_ROD_FIRE` | Fire Rod |
| 0xAF | `ITEM_ROD_ICE` | Ice Rod |
| 0xB0 | `ITEM_ROD_LIGHT` | Light Rod |
| 0xB1 | `ITEM_BEETLE` | Beetle |
| 0xB2 | `ITEM_SHOVEL` | Shovel |
| 0xB3-0xB6 | `ITEM_PENDING_*` | Reserved for future items |

---

## Extended Inventory Slots

The 2-page inventory system maps custom items to slots 24-47:

| Slot | Item |
|------|------|
| 24 | Roc's Feather/Cape (shared, progressive) |
| 25 | Whip |
| 26 | Spinner |
| 27 | Bomb Arrows |
| 28 | Fire Rod |
| 29 | Demise Destruction |
| 30 | Ice Rod |
| 31 | Light Rod |
| 32 | Ball and Chain |
| 33 | Deku Leaf |
| 34 | Switch Hook |
| 35 | Gust Jar |
| 36 | Cane of Somaria |
| 37 | Beetle |
| 38 | Time Gate |
| 39 | Desire Sensor |
| 40 | Hylia's Grace |
| 41 | Zonai Permafrost |
| 42 | Mogma Mitts |
| 43 | Shovel |
| 44-47 | Reserved (Dominion Rod, etc.) |

---

## Handler Pattern

Each item follows a consistent implementation pattern:

```c
/**
 * item_example.c - Example Item Handler
 *
 * Controls:
 *   C Button: Action description
 *
 * Features:
 *   - Feature 1
 *   - Feature 2
 */

#include "custom_items.h"
#include "helpers/equip_helper.h"

// State aliases for readability
#define exampleState      gCustomItemState.exampleState
#define exampleTimer      gCustomItemState.exampleTimer
#define exampleActive     gCustomItemState.exampleActive

// State constants
typedef enum {
    EXAMPLE_STATE_IDLE,
    EXAMPLE_STATE_EQUIP,
    EXAMPLE_STATE_ACTIVE,
    EXAMPLE_STATE_COOLDOWN
} ExampleState;

// Previous invincibility for damage detection
static s8 sPrevInvinc = 0;

void Handle_Example(Player* p, PlayState* play) {
    ItemInputState in;
    ItemInput_Update(&in, ITEM_EXAMPLE, p, play);

    // Check if equipped
    if (!in.wasEquipped) {
        if (exampleActive) {
            Example_Stop(p, play);
        }
        return;
    }

    // Blocking checks (swimming, climbing, cutscenes, etc.)
    if (ItemInput_IsBlocked(p, play)) {
        if (exampleActive) {
            Example_Stop(p, play);
        }
        return;
    }

    // Damage interrupt
    if (ItemInput_CheckDamage(p, &sPrevInvinc)) {
        Example_Stop(p, play);
        return;
    }

    // State machine
    switch (exampleState) {
        case EXAMPLE_STATE_IDLE:
            if (in.isPressed) {
                exampleState = EXAMPLE_STATE_EQUIP;
                ItemEquip_PlayEquipSFX(play, p);
            }
            break;

        case EXAMPLE_STATE_EQUIP:
            // Play equip animation
            exampleTimer++;
            if (exampleTimer > 20) {
                exampleState = EXAMPLE_STATE_ACTIVE;
                exampleActive = true;
            }
            break;

        case EXAMPLE_STATE_ACTIVE:
            // Main item logic here
            Example_Update(p, play);

            if (!in.isHeld) {
                Example_Stop(p, play);
            }
            break;

        case EXAMPLE_STATE_COOLDOWN:
            exampleTimer--;
            if (exampleTimer <= 0) {
                exampleState = EXAMPLE_STATE_IDLE;
            }
            break;
    }
}

static void Example_Stop(Player* p, PlayState* play) {
    exampleActive = false;
    exampleState = EXAMPLE_STATE_COOLDOWN;
    exampleTimer = 30;  // Cooldown frames
    ItemEquip_PlayUnequipSFX(play, p);
}
```

---

## Helper Modules

### equip_helper.h
Input handling and equipment state:

```c
typedef struct {
    u8 wasEquipped;       // Is item currently equipped to a C button?
    u8 isPressed;         // Button pressed this frame?
    u8 isHeld;            // Button held?
    u8 otherButtonPressed; // Another C button pressed?
    u16 equippedButton;   // Which button (BTN_CLEFT, etc.)
} ItemInputState;

void ItemInput_Update(ItemInputState* state, u8 itemId, Player* p, PlayState* play);
s32  ItemInput_IsBlocked(Player* p, PlayState* play);
s32  ItemInput_CheckDamage(Player* p, s8* prevInvinc);
void ItemEquip_PlayEquipSFX(PlayState* play, Player* p);
void ItemEquip_PlayUnequipSFX(PlayState* play, Player* p);
```

### camera_helper.h
First-person mode and reticle drawing:

```c
void FirstPerson_Init(Player* p, PlayState* play);
void FirstPerson_Exit(Player* p, PlayState* play);
void FirstPerson_Update(Player* p, PlayState* play);
s16  FirstPerson_GetAimYaw(Player* p);
s16  FirstPerson_GetAimPitch(Player* p);
void FirstPerson_DrawReticle(Player* p, PlayState* play, f32 depth, u8 r, u8 g, u8 b);
```

### combat_helper.h
Collision and damage:

```c
void Combat_InitCollider(PlayState* play, Player* p, ColliderCylinder* col);
void Combat_UpdateCollider(PlayState* play, Vec3f* pos, ColliderCylinder* col);
void Combat_CheckHit(ColliderCylinder* col, Vec3f* pos);
void Combat_DealDamage(Actor* target, s32 damage, PlayState* play);
```

### fx_helper.h
Visual effects:

```c
void FX_SpawnSparkles(PlayState* play, Vec3f* pos, s32 count, u8 r, u8 g, u8 b);
void FX_SpawnShockwave(PlayState* play, Vec3f* pos, f32 radius);
void FX_SpawnFireEffect(PlayState* play, Vec3f* pos);
void FX_SpawnIceEffect(PlayState* play, Vec3f* pos);
```

---

## Update Flow

1. `CustomItems_Update()` called every frame from player update
2. Blocking items checked first (Demise Destruction, Hylia's Grace, etc.)
3. Global blocking check via `CustomItems_IsBlocked()`
4. Each equipped custom item's handler called
5. `CustomItems_OverrideDraw()` renders active item visuals

```c
void CustomItems_Update(Player* p, PlayState* play) {
    // Global cooldown
    if (gCustomItemState.globalCooldownTimer > 0) {
        gCustomItemState.globalCooldownTimer--;
    }

    // Blocking items have priority
    if (Handle_DemiseDestruction_IsActive()) {
        Handle_DemiseDestruction(p, play);
        return;  // Block all other items
    }

    if (Handle_HyliasGrace_IsActive()) {
        Handle_HyliasGrace(p, play);
        return;
    }

    // Normal items
    Handle_RocsFeather(p, play);
    Handle_RocsCape(p, play);
    Handle_DekuLeaf(p, play);
    Handle_Whip(p, play);
    Handle_Spinner(p, play);
    Handle_BallAndChain(p, play);
    Handle_SwitchHook(p, play);
    Handle_GustJar(p, play);
    Handle_Beetle(p, play);
    Handle_Shovel(p, play);
    Handle_FireRod(p, play);
    Handle_IceRod(p, play);
    Handle_LightRod(p, play);
    Handle_CaneOfSomaria(p, play);
    Handle_DominionRod(p, play);
    Handle_TimeGate(p, play);
    Handle_BombArrows(p, play);
    Handle_DesireSensor(p, play);
    Handle_ZonaiPermafrost(p, play);
    Handle_MogmaMitts(p, play);
}
```

---

## Blocking States

Standard blocking flags checked by `ItemInput_IsBlocked()`:

```c
#define CUSTOM_BLOCKING_STATE1_FLAGS ( \
    PLAYER_STATE1_DEAD | \
    PLAYER_STATE1_IN_CUTSCENE | \
    PLAYER_STATE1_LOADING | \
    PLAYER_STATE1_IN_ITEM_CS | \
    PLAYER_STATE1_GETTING_ITEM | \
    PLAYER_STATE1_TALKING \
)

#define CUSTOM_BLOCKING_STATE2_FLAGS ( \
    PLAYER_STATE2_CRAWLING | \
    PLAYER_STATE2_DIVING | \
    PLAYER_STATE2_GRABBING_DYNAPOLY \
)

s32 ItemInput_IsBlocked(Player* p, PlayState* play) {
    if (p->stateFlags1 & CUSTOM_BLOCKING_STATE1_FLAGS) return 1;
    if (p->stateFlags2 & CUSTOM_BLOCKING_STATE2_FLAGS) return 1;
    if (Player_InCsMode(play)) return 1;
    return 0;
}
```

---

## Animation Integration

Items can use three animation approaches:

### 1. Upper Body Animations
Overlay animations that only affect arms/torso:

```c
s32 Player_UpperAction_Example(Player* player, PlayState* play) {
    if (!exampleActive) return 0;

    if (LinkAnimation_Update(play, &player->upperSkelAnime)) {
        // Animation complete
    }

    return 1;  // Upper body controlled by this item
}
```

### 2. Full Body Overrides
Complete animation replacement:

```c
void Example_PlayFullBodyAnim(Player* p, PlayState* play) {
    LinkAnimation_PlayOnce(play, &p->skelAnime, &gPlayerAnim_Example);
}
```

### 3. Joint Table Manipulation
Direct bone manipulation for poses:

```c
void Example_ModifyJoints(Player* p, Vec3s* jointTable) {
    // Rotate right hand
    jointTable[PLAYER_LIMB_R_HAND].x += 0x1000;
}
```

---

## Collider Types

| Type | Usage |
|------|-------|
| `ColliderCylinder` | Projectiles, area effects, item hitboxes |
| `ColliderQuad` | Line attacks (Switch Hook beam) |
| `ColliderTris` | Triangle-based collision |

Example cylinder setup:

```c
static ColliderCylinderInit sBallColliderInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_PLAYER,
        AC_NONE,
        OC1_ON | OC1_TYPE_ALL,
        OC2_TYPE_1,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK0,
        { 0x00000000, 0x00, 0x00 },
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_ON | TOUCH_SFX_NORMAL,
        BUMP_NONE,
        OCELEM_ON,
    },
    { 30, 30, 0, { 0, 0, 0 } },
};
```

---

## Adding New Items

Step-by-step guide to add a new custom item:

### 1. Allocate ID in `custom_items.h`
```c
#define ITEM_NEW_ITEM 0xB3  // Use next available ID
```

### 2. Add State Fields to `CustomItemState`
```c
typedef struct {
    // ... existing fields ...

    // New Item state
    u8 newItemState;
    u8 newItemActive;
    s16 newItemTimer;
    Vec3f newItemPos;
    ColliderCylinder newItemCollider;
} CustomItemState;
```

### 3. Create Handler Files
Create `logic/item_newitem.c` and `logic/item_newitem.h`:

```c
// item_newitem.h
#ifndef ITEM_NEWITEM_H
#define ITEM_NEWITEM_H

#include "z64.h"

void Handle_NewItem(Player* p, PlayState* play);
s32 Handle_NewItem_IsActive(void);

#endif

// item_newitem.c
#include "item_newitem.h"
#include "../custom_items.h"
#include "../helpers/equip_helper.h"

void Handle_NewItem(Player* p, PlayState* play) {
    // Implementation
}
```

### 4. Register in Update Dispatcher
Add call in `custom_items_common.c`:

```c
#include "logic/item_newitem.h"

void CustomItems_Update(Player* p, PlayState* play) {
    // ... existing handlers ...
    Handle_NewItem(p, play);
}
```

### 5. Add Icon Texture
Place 32x32 RGBA texture in `icons/`:
- `gNewItemIconTex`

### 6. Add Name Texture
Place name texture in `names/`:
- `gNewItemNameTex`

### 7. Add Inventory Slot
In `extended_inventory.h`:

```c
#define INV_SLOT_NEW_ITEM 44  // Next available slot
```

### 8. Add to Randomizer (if applicable)
Register item in randomizer tables in `soh/Enhancements/randomizer/`:
- Add to item pool
- Set location if needed
- Configure hint text

---

## Beta Systems

### Transformation Masks
Located in `transformation_masks/`:

State machine for MM-style transformations:

```c
typedef enum {
    TRANSFORM_STATE_IDLE,
    TRANSFORM_STATE_TRANSFORMING_IN,
    TRANSFORM_STATE_TRANSFORMED,
    TRANSFORM_STATE_TRANSFORMING_OUT
} TransformState;

typedef enum {
    TRANSFORM_FORM_LINK,
    TRANSFORM_FORM_DEKU,
    TRANSFORM_FORM_GORON,
    TRANSFORM_FORM_ZORA,
    TRANSFORM_FORM_FIERCE_DEITY
} TransformForm;
```

### Extended Equipment
9 additional equipment slots planned:
- Extended C-button mapping
- Quick-swap wheel system

---

## Debugging Tips

### Enable Debug Logging
```c
#define CUSTOM_ITEMS_DEBUG 1

#if CUSTOM_ITEMS_DEBUG
    #define CI_LOG(fmt, ...) osSyncPrintf("[CI] " fmt "\n", ##__VA_ARGS__)
#else
    #define CI_LOG(fmt, ...)
#endif
```

### Visualize Colliders
```c
void Debug_DrawCollider(PlayState* play, ColliderCylinder* col) {
    // Draw cylinder wireframe at collider position
}
```

### State Inspection
Use SoH's built-in debug menu to inspect `gCustomItemState` values.

---

## Performance Notes

1. **Collider Pooling:** Reuse colliders instead of creating new ones each frame
2. **Effect Limits:** Cap particle effects to prevent slowdown
3. **State Caching:** Cache frequently accessed player states
4. **Lazy Updates:** Only update items that are currently equipped

---

## Contributing

1. Follow existing code style and patterns
2. Add header comments with control descriptions
3. Use state aliases for readability
4. Test with both Child and Adult Link
5. Verify randomizer compatibility
6. Update documentation (this file, CONTROLS.md)
