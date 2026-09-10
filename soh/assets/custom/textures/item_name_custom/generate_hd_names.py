"""Render the NEI 2.0 name list using the previous Android HD label style.

Keep generate_names.py's current names rather than restoring an outdated list.
The drawing code samples these as 256x32 IA4 in a vanilla 128x16 panel.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from generate_names import ALL_ITEMS

MASK_NAMES = [
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


def main():
    fonts = [
        Path('/System/Library/Fonts/Supplemental/Arial Bold Italic.ttf'),
        Path('C:/Windows/Fonts/arialbi.ttf'),
        Path('/usr/share/fonts/truetype/liberation2/LiberationSans-BoldItalic.ttf'),
    ]
    font_path = next((p for p in fonts if p.is_file()), None)
    if font_path is None:
        raise SystemExit('Arial or Liberation Sans Bold Italic is required')
    for name, text in ALL_ITEMS + MASK_NAMES:
        master = Image.new('RGBA', (512, 64), (0, 0, 0, 0))
        draw = ImageDraw.Draw(master)
        for size in range(36, 7, -1):
            font = ImageFont.truetype(str(font_path), size)
            box = draw.textbbox((0, 0), text, font=font, stroke_width=3)
            if box[2] - box[0] <= 488:
                break
        x = (512 - box[2] + box[0]) / 2 - box[0]
        y = (64 - box[3] + box[1]) / 2 - box[1] - 2
        draw.text((x, y), text, font=font, fill='white', stroke_width=3, stroke_fill='black')
        label = master.resize((256, 32), Image.Resampling.LANCZOS)
        # ZAPD encodes ANY nonzero alpha as opaque for IA4. Remove faint
        # resampling rings explicitly; otherwise they become solid outlines.
        alpha = label.getchannel('A').point(lambda value: 255 if value >= 128 else 0)
        intensity = label.getchannel('R').point(lambda value: round(value / 255 * 7) * 255 // 7)
        Image.merge('RGBA', (intensity, intensity, intensity, alpha)).save(
            Path(__file__).parent / f'{name}.ia4.png')
    print(f'Generated {len(ALL_ITEMS) + len(MASK_NAMES)} HD labels at 256x32')


if __name__ == '__main__':
    main()
