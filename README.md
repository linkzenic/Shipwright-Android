# Not Enough Items
### A Custom Items Mod for Ship of Harkinian

---

## About

**Not Enough Items** is a fan-made mod for [Ship of Harkinian](https://www.shipofharkinian.com/) that brings **Custom items** from various Zelda titles into Ocarina of Time.

| | |
|---|---|
| **Version** | Copper Charlie |
| **Status** | Alpha 3 |
| **Author** | Skijer |
| **Platforms** | Windows, Linux, macOS |

> **Disclaimer:** This is an unofficial fan project. Not affiliated with Nintendo or the Ship of Harkinian team.

---

## Features

### Custom Items
Items from across the Zelda franchise, fully integrated into OoT's gameplay:

- **Randomizer Integration** - All items work with SoH's randomizer
- **Anchor Compatible** - Multiplayer support with Anchor
- **Copper Charlie Features** - Full compatibility with the latest SoH version

### Extended Inventory
- 2-page inventory system (48 total slots)
- **L or A Button** to switch pages in pause menu
- Page 1: Vanilla OoT items
- Page 2: Custom items

---

## Item List

### Traversal Items
| Item | Origin | Description | Supported Logic |
|------|--------|-------------|-----------------|
| **Roc's Feather** | Oracle Games | High jump with sparkle effects | No |
| **Roc's Cape** | Four Swords Adventures | Double jump upgrade for Roc's Feather | No |
| **Deku Leaf** | The Wind Waker | Glider and wind gust attack | No |
| **Whip** | Spirit Tracks | Grappling hook with pendulum swinging | No |
| **Spinner** | Twilight Princess | Rideable vehicle with homing dash attack | No |

### Combat Items
| Item | Origin | Description | Supported Logic |
|------|--------|-------------|-----------------|
| **Ball and Chain** | Twilight Princess | Chargeable heavy projectile, breaks walls | No |
| **Fire Rod** | A Link Between Worlds | Magic rod with 4 fire attack types | No |
| **Ice Rod** | A Link Between Worlds | Magic rod with 4 ice attack types, freezes enemies | No |
| **Light Rod** | A Link Between Worlds | Magic rod with 4 light attack types, stuns enemies | No |
| **Bomb Arrows** | Twilight Princess | Bow + bomb combo projectile | No |
| **Demise Destruction** | A Link to the Past | AoE spell with lightning, heavy damage | Yes |

### Utility Items
| Item | Origin | Description | Supported Logic |
|------|--------|-------------|-----------------|
| **Switch Hook** | Oracle of Ages | Swap positions with objects and enemies | No |
| **Gust Jar** | The Minish Cap | Suction and projectile device | No |
| **Cane of Somaria** | A Link to the Past | Create hookable and swappable blocks | No |
| **Dominion Rod** | Twilight Princess | Remote control enemies and statues | No |
| **Beetle** | Skyward Sword | Remote-controlled scout, carries items | No |
| **Shovel** | Link's Awakening | Dig for buried items and hidden grottos | Yes |
| **Mogma Mitts** | Skyward Sword | Climb any wall using magic | No |

### Special Items
| Item | Origin | Description | Supported Logic |
|------|--------|-------------|-----------------|
| **Time Gate** | Hyrule Warriors | Swap between Child/Adult Link (48 MP) | No |
| **Desire Sensor** | Monster Hunter | Detect major items in current scene | No |
| **Hylia's Grace** | Zelda II | Fairy transformation with free flight | No |
| **Zonai Permafrost** | Tears of the Kingdom | Time freeze - stops all actors for 30s | No |

---

## Quick Start

### Requirements
- Git and build tools (CMake, Visual Studio 2022 / GCC / Clang)
- Any OoT ROM compatible with Ship of Harkinian
- Windows, Linux, or macOS

### Installation

**1. Clone the repository**
```bash
git clone https://github.com/YOUR_USERNAME/Shipwright.git
cd Shipwright
```

**2. Build the project**

Follow the standard [SoH building instructions](docs/BUILDING.md) for your platform.

**3. Launch the game**
- Windows: Run `soh.exe`
- Linux: Run `soh.appimage`
- macOS: Run `soh.app`

### Verify Your ROM
Use the [compatibility checker](https://ship.equipment/) to verify your ROM is supported.

---

## Roadmap

### Beta (Planned)
- **Transformation Masks** from Majora's Mask
  - Deku Mask
  - Goron Mask
  - Zora Mask
  - Fierce Deity Mask
- **9 Additional Equipment Slots**

---

## Documentation

- [Controls Guide](soh/mods/items/CONTROLS.md) - Detailed controls for all 21 items
- [Technical Structure](soh/mods/items/STRUCTURE.md) - Developer documentation

---

## Community & Support

- **Discord:** [Ship of Harkinian Discord](https://discord.com/invite/shipofharkinian)
- **Issues:** Report bugs via GitHub Issues

---

## Credits

- **Skijer** - Mod Author
- **TheLynk** - Logic Author
- **Ship of Harkinian Team** - Base project
- **OoT Decompilation Project** - Source code foundation
- **libultraship Team** - Engine framework

---

## License

This project is built upon Ship of Harkinian. See the original SoH repository for license details.

The Ship does not include any copyrighted assets. You are required to provide a supported copy of the game.

---

<a href="https://github.com/Kenix3/libultraship/">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="./docs/poweredbylus.darkmode.png">
    <img alt="Powered by libultraship" src="./docs/poweredbylus.lightmode.png">
  </picture>
</a>
