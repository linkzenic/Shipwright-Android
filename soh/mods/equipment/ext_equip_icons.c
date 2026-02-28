/**
 * ext_equip_icons.c - Placeholder icon textures for extended equipment
 *
 * Each icon is 32x32 RGBA32 (4096 bytes), generated at runtime.
 * Swords = red, Shields = blue, Tunics = green, Boots = yellow.
 * Intensity varies by index (1=bright, 2=medium, 3=dark).
 *
 * Included by extended_equipment.c (unity build).
 */

// 12 icon buffers: [type][index] = [4][3]
static u8 sExtEquipIconBufs[4][3][32 * 32 * 4];
static u8 sExtEquipIconsGenerated = 0;

// Base colors per equipment type (R, G, B)
static const u8 sExtEquipIconColors[4][3] = {
    { 220,  50,  50 },  // EQUIP_TYPE_SWORD  - red
    {  50,  80, 220 },  // EQUIP_TYPE_SHIELD - blue
    {  50, 200,  80 },  // EQUIP_TYPE_TUNIC  - green
    { 220, 200,  50 },  // EQUIP_TYPE_BOOTS  - yellow
};

// Simple 5x7 digit patterns (columns of bits, top-to-bottom)
// Used to stamp "1", "2", "3" onto the icon
static const u8 sDigitPatterns[3][5 * 7] = {
    // "1"
    {
        0,0,1,0,0,
        0,1,1,0,0,
        0,0,1,0,0,
        0,0,1,0,0,
        0,0,1,0,0,
        0,0,1,0,0,
        0,1,1,1,0,
    },
    // "2"
    {
        0,1,1,1,0,
        1,0,0,0,1,
        0,0,0,0,1,
        0,0,0,1,0,
        0,0,1,0,0,
        0,1,0,0,0,
        1,1,1,1,1,
    },
    // "3"
    {
        0,1,1,1,0,
        1,0,0,0,1,
        0,0,0,0,1,
        0,0,1,1,0,
        0,0,0,0,1,
        1,0,0,0,1,
        0,1,1,1,0,
    },
};

static void ExtEquip_GenerateIcons(void) {
    if (sExtEquipIconsGenerated) return;

    for (int type = 0; type < 4; type++) {
        for (int idx = 0; idx < 3; idx++) {
            u8* buf = sExtEquipIconBufs[type][idx];
            // Brightness multiplier: 1.0, 0.7, 0.5
            float bright = 1.0f - (idx * 0.25f);
            u8 r = (u8)(sExtEquipIconColors[type][0] * bright);
            u8 g = (u8)(sExtEquipIconColors[type][1] * bright);
            u8 b = (u8)(sExtEquipIconColors[type][2] * bright);

            // Fill with solid color + alpha border
            for (int y = 0; y < 32; y++) {
                for (int x = 0; x < 32; x++) {
                    int px = (y * 32 + x) * 4;
                    u8 isBorder = (x == 0 || x == 31 || y == 0 || y == 31) ? 1 : 0;
                    u8 isDarkBorder = (x <= 1 || x >= 30 || y <= 1 || y >= 30) ? 1 : 0;

                    if (isBorder) {
                        buf[px + 0] = 255; // white border
                        buf[px + 1] = 255;
                        buf[px + 2] = 255;
                        buf[px + 3] = 200;
                    } else if (isDarkBorder) {
                        buf[px + 0] = r / 2;
                        buf[px + 1] = g / 2;
                        buf[px + 2] = b / 2;
                        buf[px + 3] = 255;
                    } else {
                        buf[px + 0] = r;
                        buf[px + 1] = g;
                        buf[px + 2] = b;
                        buf[px + 3] = 255;
                    }
                }
            }

            // Stamp digit (centered: start at x=13, y=12 for 5x7 pattern)
            const u8* pattern = sDigitPatterns[idx];
            for (int dy = 0; dy < 7; dy++) {
                for (int dx = 0; dx < 5; dx++) {
                    if (pattern[dy * 5 + dx]) {
                        // Draw 2x2 pixel for each pattern pixel (scale up)
                        for (int sy = 0; sy < 2; sy++) {
                            for (int sx = 0; sx < 2; sx++) {
                                int px_x = 11 + dx * 2 + sx;
                                int px_y = 9 + dy * 2 + sy;
                                if (px_x >= 2 && px_x < 30 && px_y >= 2 && px_y < 30) {
                                    int px = (px_y * 32 + px_x) * 4;
                                    buf[px + 0] = 255;
                                    buf[px + 1] = 255;
                                    buf[px + 2] = 255;
                                    buf[px + 3] = 255;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    sExtEquipIconsGenerated = 1;
}
