"""Generate NEI name plates matching Djipi's regular SOH HD item names.

Each label is authored at 512x64, then downsampled to a 256x32 IA4 source
texture.  This is twice the native OOT resolution while remaining within the
original 4 KiB TMEM budget.  A 512x64 ``alt`` replacement cannot be used here:
the custom 256x32 IA4 draw path causes Fast3D to repeat that replacement.

The regular labels use Arial Bold Italic, white lettering, and a compact black
outline.  Long names reduce their point size only as much as needed to fit.
"""

from PIL import Image, ImageDraw, ImageFont
import os
import sys

# Config
WIDTH = 256
HEIGHT = 32
HD_WIDTH = 512
HD_HEIGHT = 64
TEXT_COLOR = (255, 255, 255, 255)
OUTLINE_COLOR = (0, 0, 0, 255)
HD_FONT_SIZE = 36
HD_OUTLINE_WIDTH = 3
HD_HORIZONTAL_MARGIN = 12
OUTPUT_DIR = os.path.dirname(os.path.abspath(__file__))

# All custom items: (filename_base, display_name)
ALL_ITEMS = [
    ("gRocsFeatherNameTex", "Roc's Feather"),
    ("gRocsCapeNameTex", "Roc's Cape"),
    ("gDesireSensorNameTex", "Desire Sensor HP3"),
    ("gHyliaGraceNameTex", "Hylia's Grace MP24"),
    ("gZonaiPermafrostNameTex", "Zonai Permafrost"),
    ("gDemiseDestructionNameTex", "Demise's Destruct. MP12"),
    ("gDekuLeafNameTex", "Deku Leaf MP1"),
    ("gSwitchHookNameTex", "Switch Hook"),
    ("gMogmaMittsNameTex", "Mogma Mitts MP1"),
    ("gGustJarNameTex", "Gust Jar"),
    ("gBallAndChainNameTex", "Ball and Chain"),
    ("gWhipNameTex", "Whip"),
    ("gSpinnerNameTex", "Spinner"),
    ("gCaneOfSomariaNameTex", "Cane of Somaria"),
    ("gDominionRodNameTex", "Dominion Rod"),
    ("gTimeGateNameTex", "Time Gate"),
    ("gBombArrowsNameTex", "Bomb Arrows"),
    ("gFireRodNameTex", "Fire Rod MP3"),
    ("gIceRodNameTex", "Ice Rod MP3"),
    ("gLightRodNameTex", "Light Rod MP3"),
    ("gBeetleNameTex", "Beetle"),
    ("gShovelNameTex", "Shovel"),
    ("gMinishCapNameTex", "Minish Cap"),
    ("gLanternNameTex", "Lantern"),
    ("gPokeballNameTex", "Pokeball"),
    # Extended equipment
    ("gCaneOfByrnaNameTex", "Cane of Byrna"),
    ("gFourSwordNameTex", "Four Sword"),
    ("gDrillshaftNameTex", "Drillshaft"),
    ("gDivineShieldNameTex", "Divine Shield"),
    ("gGerudoScimitarNameTex", "Gerudo Scimitar"),
    ("gIronKnuckleAxeNameTex", "Iron Knuckle Axe"),
    ("gSheikahShieldNameTex", "Sheikah Shield"),
    ("gSpiritBreastplateNameTex", "Spirit Breastplate"),
    ("gKiteShieldNameTex", "Kite Shield"),
    ("gShieldOfIkanaNameTex", "Shield of Ikana"),
    ("gMagicCapeNameTex", "Magic Cape"),
    ("gMagicArmorNameTex", "Magic Armor"),
    ("gChampionsTunicNameTex", "Champion's Tunic"),
    ("gPegasusAnkletNameTex", "Pegasus Anklet"),
    ("gPendantOfMemoriesNameTex", "Pendant of Memories"),
    ("gWaterDragonScaleNameTex", "Water Dragon Scale"),
    # Reserved slots retain readable labels in the save editor.
    ("gPending2NameTex", "Pending 2"),
    ("gPending3NameTex", "Pending 3"),
    ("gPending4NameTex", "Pending 4"),
    # Twilight Upgrade mode-toggle names (shown when Clawshot/Gale modes are active
    # via the A-button toggle on hookshot/longshot or boomerang).
    ("gClawshotNameTex", "Clawshot"),
    ("gGaleBoomerangNameTex", "Gale Boomerang"),
    # Majora's Mask inventory
    ("gPostmansHatNameTex", "Postman's Hat"),
    ("gAllNightMaskNameTex", "All-Night Mask"),
    ("gBlastMaskNameTex", "Blast Mask"),
    ("gStoneMaskNameTex", "Stone Mask"),
    ("gGreatFairysMaskNameTex", "Great Fairy's Mask"),
    ("gDekuMaskNameTex", "Deku Mask"),
    ("gKeatonMaskNameTex", "Keaton Mask"),
    ("gBremenMaskNameTex", "Bremen Mask"),
    ("gBunnyHoodNameTex", "Bunny Hood"),
    ("gDonGerosMaskNameTex", "Don Gero's Mask"),
    ("gMaskOfScentsNameTex", "Mask of Scents"),
    ("gGoronMaskNameTex", "Goron Mask"),
    ("gRomanisMaskNameTex", "Romani's Mask"),
    ("gCircusLeadersMaskNameTex", "Circus Leader's Mask"),
    ("gKafeisMaskNameTex", "Kafei's Mask"),
    ("gCouplesMaskNameTex", "Couple's Mask"),
    ("gMaskOfTruthNameTex", "Mask of Truth"),
    ("gZoraMaskNameTex", "Zora Mask"),
    ("gKamarosMaskNameTex", "Kamaro's Mask"),
    ("gGibdoMaskNameTex", "Gibdo Mask"),
    ("gGarosMaskNameTex", "Garo's Mask"),
    ("gCaptainsHatNameTex", "Captain's Hat"),
    ("gGiantsMaskNameTex", "Giant's Mask"),
    ("gFierceDeitysMaskNameTex", "Fierce Deity's Mask"),
]


def find_font():
    """Find the bold-italic face used by the regular HD name textures."""
    paths = [
        "C:/Windows/Fonts/arialbi.ttf",
        "C:\\Windows\\Fonts\\arialbi.ttf",
        "/System/Library/Fonts/Supplemental/Arial Bold Italic.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-BoldItalic.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-BoldItalic.ttf",
    ]
    for p in paths:
        if os.path.exists(p):
            return p
    return None


def generate_name_texture(name, text, font_path, output_path):
    """Generate one 2x name texture from a 4x-quality master."""
    size = HD_FONT_SIZE
    while size >= 24:
        pil_font = ImageFont.truetype(font_path, size)
        probe = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
        bbox = probe.textbbox((0, 0), text, font=pil_font, stroke_width=HD_OUTLINE_WIDTH)
        if bbox[2] - bbox[0] <= HD_WIDTH - (HD_HORIZONTAL_MARGIN * 2):
            break
        size -= 1

    hd = Image.new("RGBA", (HD_WIDTH, HD_HEIGHT), (0, 0, 0, 0))
    draw = ImageDraw.Draw(hd)
    bbox = draw.textbbox((0, 0), text, font=pil_font, stroke_width=HD_OUTLINE_WIDTH)
    x = (HD_WIDTH - (bbox[2] - bbox[0])) / 2 - bbox[0]
    y = (HD_HEIGHT - (bbox[3] - bbox[1])) / 2 - bbox[1] - 2
    draw.text(
        (x, y),
        text,
        font=pil_font,
        fill=TEXT_COLOR,
        stroke_width=HD_OUTLINE_WIDTH,
        stroke_fill=OUTLINE_COLOR,
    )

    hd.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS).save(output_path)
    return True


def main():
    font_path = find_font()
    if not font_path:
        print("ERROR: Arial/Liberation Sans Bold Italic not found!")
        sys.exit(1)

    print(f"Font: {font_path}")
    print(f"Output: {OUTPUT_DIR}")
    print()

    # Check which already exist
    existing = set()
    for f in os.listdir(OUTPUT_DIR):
        if f.endswith(".ia4.png"):
            existing.add(f.replace(".ia4.png", ""))

    # Parse args
    generate_all = "--all" in sys.argv
    only_missing = "--missing" in sys.argv
    custom_name = None
    custom_text = None

    # Custom single item: generate_names.py "FileName" "Display Text"
    if len(sys.argv) >= 3 and not sys.argv[1].startswith("-"):
        custom_name = sys.argv[1]
        custom_text = sys.argv[2]

    if custom_name and custom_text:
        # Generate single custom item
        out = os.path.join(OUTPUT_DIR, f"{custom_name}.ia4.png")
        print(f"  Generating: {custom_name} -> \"{custom_text}\"")
        if generate_name_texture(custom_name, custom_text, font_path, out):
            print(f"  OK: {out}")
        return

    items_to_generate = []
    for fname, display in ALL_ITEMS:
        if generate_all:
            items_to_generate.append((fname, display))
        elif only_missing and fname not in existing:
            items_to_generate.append((fname, display))
        elif not generate_all and not only_missing:
            # Default: show status
            status = "EXISTS" if fname in existing else "MISSING"
            print(f"  [{status}] {fname}.ia4.png -> \"{display}\"")

    if not generate_all and not only_missing and not custom_name:
        print()
        print("Usage:")
        print("  python generate_names.py                    # Show status")
        print("  python generate_names.py --missing          # Generate only missing")
        print("  python generate_names.py --all              # Regenerate all")
        print('  python generate_names.py "gMyItemTex" "My Item"  # Single custom')
        return

    if not items_to_generate:
        print("Nothing to generate!")
        return

    print(f"Generating {len(items_to_generate)} textures...")
    for fname, display in items_to_generate:
        out = os.path.join(OUTPUT_DIR, f"{fname}.ia4.png")
        print(f"  {fname} -> \"{display}\"...", end=" ")
        if generate_name_texture(fname, display, font_path, out):
            print("OK")
        else:
            print("FAILED")

    print("Done!")


if __name__ == "__main__":
    main()
