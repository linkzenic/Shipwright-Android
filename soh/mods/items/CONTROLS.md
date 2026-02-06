# Not Enough Items - Controls Guide
### Complete control reference for all 21 custom items

---

## Control Conventions

| Term | Meaning |
|------|---------|
| **C Button** | The C-button slot where the item is equipped (C-Left, C-Down, C-Right), including the DPads when enhancement active |
| **Hold** | Press and hold the button |
| **Tap** | Quick press and release |
| **Z-Target** | Lock onto enemies/objects with Z button |
| **First-Person Mode** | Some items automatically enter aiming mode by pressing C-Up |

---

## Traversal Items

### Roc's Feather
> *Origin: Oracle Games*

**Type:** Passive Jump
**Age:** Child & Adult

| Input | Action |
|-------|--------|
| C Button (ground) | High jump with sparkle effects |
| C Button (water) | Reduced-height water jump |

**Notes:**
- If mm.o2r file present it uses MM animations

---

### Roc's Cape
> *Origin: Four Swords Adventures*

**Type:** Progressive upgrade of Roc's Feather
**Age:** Child & Adult

| Input | Action |
|-------|--------|
| C Button (ground) | High jump |
| C Button (air) | Double jump (once per airtime) |

**Notes:**
- Double jump resets when you land
- Shockwave effect on double jump
- Requires Roc's Feather first
- If mm.o2r file present it uses MM animations
---

### Deku Leaf
> *Origin: The Wind Waker*

**Type:** Glider and Combat
**Age:** Child & Adult
**Cost:** Magic

| Input | Context | Action |
|-------|---------|--------|
| C Button | Ground | Swing leaf - creates wind gust |
| Hold C Button | Air | Glide - reduces fall speed |
| Release C | Gliding | Stop gliding |

**Notes:**
- Wind gust pushes enemies and objects forward
- Gliding consumes magic over time (1 MP per 30 frames)

---

### Whip
> *Origin: Spirit Tracks*

**Type:** Grappling Hook + Boomerang Damage
**Age:** Child & Adult

| Input | Context | Action |
|-------|---------|--------|
| C Button | Equipped | Enter first-person aiming |
| C Button | Aiming | Lash whip forward |
| Analog Stick | Swinging | Control pendulum direction |
| Any Button | Swinging | Release with momentum |

**Swinging Mechanics:**
- **Stick Y:** Lean forward/backward for momentum
- **Stick X:** Turn swing plane
- Release at peak momentum for maximum launch distance

**Combat Uses:**
- Paralyzes Keese and Bubbles
- Disarms Stalfos and Lizalfos (Not tested yet)
- Boomerang Damage to other enemies

**Notes:**
- It can glide to any geometry with beam shape (theoretically)

---

### Spinner
> *Origin: Twilight Princess*

**Type:** Vehicle
**Age:** Child & Adult

| Input | Action |
|-------|--------|
| C Button | Activate/deactivate spinner ride |
| Analog Stick | Steer direction |
| Z Target + C Button | Homing dash attack toward nearest enemy |
| C Button (Ridding) | Attack |

**Notes:**
- Constant spinning animation while riding
- Homing attack: 8 hearts damage
- Ridding attack: 2 hearts damage on contact
- Can break all boulders with both attacks

---

## Combat Items

### Ball and Chain
> *Origin: Twilight Princess*

**Type:** Heavy Projectile Weapon
**Age:** Child & Adult

| Input | Action |
|-------|--------|
| Hold C Button | Spin ball overhead (charging) |
| Release C Button | Throw ball in aimed direction |
| C-Up | Toggle first-person aiming (while charging) |
| Analog (while spinning) | Lean/tilt spin direction |

**Notes:**
- Charge time affects throw distance (150-300 units)
- 40% movement speed while equipped
- Heavy damage to enemies (Giant's Knife level)
- Breaks ice walls, Iron Knuckle Pillars, Goron City Big Jar, Shadow Temple Jars

---

### Fire Rod
> *Origin: A Link Between Worlds*

**Type:** Magic Combat Rod
**Age:** Child & Adult
**Cost:** Magic per attack

| Input | Attack Type | Effect |
|-------|-------------|--------|
| B (slash) | Triple Spread | 3 fireballs at +30/0/-30 degrees |
| B (stab) | Single Shot | Long-range single fireball |
| B (jump slash) | Flamethrower | Cone of 6 flame colliders |
| Spin Attack | Fire Ring | Expanding fire cylinder |
| C-Up | Toggle | First-person aiming mode |

**Magic Costs:**
- Slash/Stab: 3 MP
- Jump Slash: 6 MP
- Spin (small): 6 MP
- Spin (big): 12 MP

**Warning:** Using without magic has a chance to backfire and burn Link!

---

### Ice Rod
> *Origin: A Link Between Worlds*

**Type:** Magic Combat Rod
**Age:** Child & Adult
**Cost:** Magic per attack

| Input | Attack Type | Effect |
|-------|-------------|--------|
| B (slash) | Triple Spread | 3 ice projectiles at +30/0/-30 degrees |
| B (stab) | Single Shot | Long-range ice projectile |
| B (jump slash) | Ice Wave | Cone of 6 ice wave colliders |
| Spin Attack | Ice Ring | Expanding ice cylinder |
| C-Up | Toggle | First-person aiming mode |

**Notes:**
- Freezes enemies for 60 frames
- Same magic costs as Fire Rod
- Backfire freezes Link if used without magic

---

### Light Rod
> *Origin: A Link Between Worlds (?)*

**Type:** Magic Rod
**Age:** Child & Adult
**Cost:** Magic per attack

| Input | Attack Type | Effect |
|-------|-------------|--------|
| B (slash) | Triple Spread | 3 light projectiles |
| B (stab) | Single Shot | Long-range light beam |
| B (jump slash) | Light Beam | Cone of 6 beam colliders |
| Spin Attack | Light Ring | Expanding light cylinder |
| C-Up | Toggle | First-person aiming mode |

**Notes:**
- Stuns/paralyzes enemies
- Same magic costs as Fire/Ice Rods
- Backfire electrocutes Link if used without magic

---

### Bomb Arrows
> *Origin: Twilight Princess*

**Type:** Combination Weapon
**Age:** Child & Adult
**Cost:** 1 Arrow + 1 Bomb per shot

| Input | Action |
|-------|--------|
| Hold C Button | First-person aiming mode |
| Release C Button | Fire bomb arrow |

**Notes:**
- Combines bow and bomb into single projectile
- Explosive damage on impact
- Requires both arrows and bombs in inventory

---

### Switch Hook
> *Origin: Oracle of Ages*

**Type:** Position Swap Hookshot
**Age:** Child & Adult
**Range:** Longshot distance

| Input | Action |
|-------|--------|
| Hold C Button | First-person aiming mode |
| Release C Button | Fire hook projectile |
| Z-Target + C | Instant swap with targeted actor |

**Swappable Targets:**
- Crates
- Torches
- Signs
- Chests
- Scarecrow
- Enemies: Poes/Ghosts, Like Likes, Armos, Tektites Lizalfos/Dinolfos, ReDeads/Gibdos, Floormasters, Wallmasters, Freezards, Anubis

**Notes:**
- Non-swappable actors take hookshot damage
- Blue reticle during aiming
- No First Person Model

---

### Gust Jar
> *Origin: The Minish Cap*

**Type:** Suction & Projectile Device
**Age:** Child & Adult

| Input | Action |
|-------|--------|
| Hold C Button | Suction mode - pulls items/enemies toward Link |
| Release C Button | Fire sucked object as projectile |
| Z-Target | Toggle between first-person and Z-target modes |

**Reticle Colors:**
| Color | Mode |
|-------|------|
| Blue | Suction (pulling) |
| Red | Shoot/idle (expelling) |

---
**Notes:**
- It has different damage accordingly to the enemy sucked

### Cane of Somaria
> *Origin: A Link to the Past*

**Type:** Block Creation
**Age:** Child & Adult

| Input | Action |
|-------|--------|
| C Button | Swing cane - spawn a hookshot / switchhook block |

**Notes:**
- Maximum 3 blocks active at once
- Oldest block destroyed when limit reached
- Blocks can press all floor switches (including the ones that require Link be lifting Ruto), except rust
- Blocks are liftable
- Blocks can be swapped and hookshotted

---

### Dominion Rod
> *Origin: Twilight Princess*

**Type:** Remote Control
**Age:** Child & Adult

| Input | Action |
|-------|--------|
| C Button | Fire control orb at target |
| Analog Stick | Move controlled actor |
| A Button | Make controlled actor jump |
| C-Up | Toggle first-person aiming |
| C Button (controlled) | Use actor's special ability |

**Controllable Actors:**
- Beamos (laser attack)
- Armos (explode)
- Anubis (fire attack)
- Statue-type actors

**Notes:**
- Green reticle in control mode
- Controlled actors mimic Link's movement

---

### Beetle
> *Origin: Skyward Sword*

**Type:** Remote Boomerang
**Age:** Child & Adult

| Input | State | Action |
|-------|-------|--------|
| C Button | Equipped | Launch beetle |
| Analog Stick | Flying | Steer flight path |
| C Button | Flying | Recall beetle early |
| B Button | Flying | Boost speed temporarily |

**Notes:**
- Camera follows beetle during flight
- Can grab and carry items back to Link
- Damages enemies on impact
- Can carry silver rupees
- Limited flight time (600 frames) before auto-return

---

### Shovel
> *Origin: Link's Awakening*

**Type:** Digging Tool
**Age:** Child & Adult

| Input | Action |
|-------|--------|
| C Button | Dig at current position |


**Notes:**
- Uses Dampe dig animation
- Uncovers buried items: rupees, hearts, secret items
- Instant graveyard reward
- Summon pod soild GS
- Summon hidden grottos

---

### Mogma Mitts
> *Origin: Skyward Sword*

**Type:** Wall Climbing
**Age:** Child & Adult
**Cost:** Magic drain while active

| Input | Action |
|-------|--------|
| C Button | Toggle climb mode on/off |

**Notes:**
- All walls become climbable while active
- Consumes 1 MP per interval
- Auto-deactivates when magic depleted
- Forces white gauntlets visible on Link

---

### Demise Destruction
> *Origin: A Link to the Past (Quake)*

**Type:** AoE Spell
**Age:** Child & Adult
**Cost:** High magic consumption

| Input | Action |
|-------|--------|
| C Button | Activate destruction attack |

**Notes:**
- Large area explosion with lightning effects
- Heavy damage to all enemies in radius
- Ground only - cannot use while airborne
- Custom "superhero landing" animation
- Blocks all other items while active

---

### Time Gate
> *Origin: Custom (Hyrule Warriors)*

**Type:** Age Swap Spell
**Age:** Child & Adult
**Cost:** 48 MP

| Input | Action |
|-------|--------|
| C Button | Activate time travel |
| A (on prompt) | Confirm - "Travel through time?" |
| B (on prompt) | Cancel |

**Notes:**
- Swaps between Child and Adult Link
- Scene reloads after age change

---

### Desire Sensor
> *Origin: Custom (Monster Hunter)*

**Type:** Item Sensor Spell
**Age:** Child & Adult
**Cost:** 3 hearts

| Input | Action |
|-------|--------|
| C Button | Activate sensing |

**Effects:**
- **Major item found:** Golden sparkles + chime sound
- **No major item:** Ganondorf laugh + dark flash

**Notes:**
- Detects major items in current scene

---

### Hylia's Grace
> *Origin: Zelda II: Adventure of Link (Fairy Spell)*

**Type:** Fairy Transformation Spell
**Age:** Child & Adult

| Input | Action |
|-------|--------|
| C Button | Activate fairy mode |
| A Button | Ascend while flying |
| B Button | Descend while flying |
| L Button | Sprint while flying |
| Analog Stick | Control flight direction |

**Notes:**
- 10-second flight duration
- Ignores collision while active
- Requires Fairy in a Bottle
- Blocks all other items while active

---

### Zonai Permafrost
> *Origin: Custom*

**Type:** Time Stop
**Age:** Child & Adult
**Cost:** 12 MP

| Input | Action |
|-------|--------|
| C Button | Activate time freeze |

**Notes:**
- Freezes all actors for 30 seconds
- Day/night cycle also frozen
- Link moves freely during freeze
- For your own safety do NOT use it with beetle

---

## Reticle Color Reference

| Color | Meaning | Items Using This |
|-------|---------|------------------|
| **Red** | Attack/Expel | Fire/Ice/Light Rods, Ball and Chain, Bomb Arrows, Whip |
| **Blue** | Pull/Swap | Switch Hook, Gust Jar (suck mode) |
| **Green** | Control | Beetle, Dominion Rod |

---

## First-Person Mode Items

These items can enter first-person aiming mode:

| Item | How to Enter | How to Exit |
|------|--------------|-------------|
| Ball and Chain | C-Up | C-Up or fire |
| Fire/Ice/Light Rod | C-Up | C-Up |
| Switch Hook | Hold C | Release C |
| Gust Jar | Hold C | Release C |
| Beetle | C (launch) | Auto-exits |
| Bomb Arrows | Hold C | Release C |
| Dominion Rod | C-Up | C-Up |

---

## Tips

1. **Combining Items:** Some items work well together:
   - You can use mogma mitts mid air, use it after roc's item
   - Cane of Somaria Blocks can be swapped by Switch Hook

2. **Magic Rods:**
- Light Rod attacks can hit Ganon (excluding charge attack)

3. **KNOWN ISSUES**
- Using Beetle while in Permafrost State can stop beetle and give a potential crash
