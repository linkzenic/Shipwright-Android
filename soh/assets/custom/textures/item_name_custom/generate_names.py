"""
Item Name Texture Generator
Generates 128x16 IA4-style PNGs matching GIMP settings:
- Font: Century Gothic Bold, 12px
- Letter spacing: -1.0
- Text color: white
- Shadow: "Sombra alargada" color #16202b, length 2.0, angles 45, -45, 135
- Centered on canvas
"""

from PIL import Image, ImageDraw, ImageFont
import os
import sys

# Config
WIDTH = 128
HEIGHT = 16
TEXT_COLOR = (255, 255, 255, 255)
SHADOW_COLOR = (0x16, 0x20, 0x2B, 255)
SHADOW_LENGTH = 3.0
SHADOW_ANGLES = [45, -45, 135, -135]
FONT_SIZE = 12
LETTER_SPACING = -1.0
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
    ("gPending2NameTex", "Pending 2"),
    ("gPending3NameTex", "Pending 3"),
]


def find_font():
    """Find Century Gothic Bold font."""
    paths = [
        "C:/Windows/Fonts/GOTHICB.TTF",
        "C:\\Windows\\Fonts\\GOTHICB.TTF",
        os.path.join(OUTPUT_DIR, "..", "..", "fonts", "CenturyGothicBold.ttf"),
    ]
    for p in paths:
        if os.path.exists(p):
            return p
    return None


def measure_text_with_spacing(font, text, spacing):
    """Measure text width accounting for custom letter spacing."""
    total = 0
    for i, ch in enumerate(text):
        bbox = font.getbbox(ch)
        char_advance = bbox[2] - bbox[0]
        # Use font.getlength for proper advance width
        char_advance = font.getlength(ch)
        total += char_advance
        if i < len(text) - 1:
            total += spacing
    return total


def draw_text_with_spacing(draw, pos, text, font, fill, spacing):
    """Draw text character by character with custom letter spacing."""
    x, y = pos
    for i, ch in enumerate(text):
        draw.text((x, y), ch, font=font, fill=fill)
        x += font.getlength(ch) + spacing


def generate_name_texture(name, text, font, output_path):
    """Generate a single item name texture matching GIMP's Sombra Alargada."""
    import math

    img = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    try:
        pil_font = ImageFont.truetype(font, FONT_SIZE)
    except Exception:
        print(f"  ERROR: Could not load font at size {FONT_SIZE}")
        return False

    # Measure and center
    text_width = measure_text_with_spacing(pil_font, text, LETTER_SPACING)
    x = (WIDTH - text_width) / 2

    bbox = pil_font.getbbox(text)
    text_height = bbox[3] - bbox[1]
    y = (HEIGHT - text_height) / 2 - bbox[1]

    # GIMP "Sombra alargada" (Long Shadow), Estilo: Finito
    # Algorithm: duplicate text 1 pixel at a time along angle direction for `length` copies.
    # Each copy offset = (cos(angle), -sin(angle)) * i, for i = 1..length
    shadow_len = SHADOW_LENGTH
    # Use enough sub-pixel steps to fill every pixel along the diagonal
    num_copies = int(shadow_len * 2) + 2
    for angle_deg in SHADOW_ANGLES:
        angle_rad = math.radians(angle_deg)
        step_x = math.cos(angle_rad)
        step_y = -math.sin(angle_rad)  # screen Y is inverted

        # Draw from farthest to nearest
        for i in range(num_copies, 0, -1):
            t = i / num_copies * shadow_len
            dx = step_x * t
            dy = step_y * t
            draw_text_with_spacing(draw, (x + dx, y + dy), text, pil_font, SHADOW_COLOR, LETTER_SPACING)

    # Main text on top
    draw_text_with_spacing(draw, (x, y), text, pil_font, TEXT_COLOR, LETTER_SPACING)

    img.save(output_path)
    return True


def main():
    font_path = find_font()
    if not font_path:
        print("ERROR: Century Gothic Bold not found!")
        print("Install it or place CenturyGothicBold.ttf in the fonts folder")
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
