/**
 * sw97_hud.c - SW97 HUD/UI modifications
 *
 * Original: z64proto/sw97 team (Spaceworld '97 Experience)
 * Adapted for Ship of Harkinian (Shipwright)
 *
 * Provides CVar-gated hooks for:
 * - Minimap color scheme (SW97 beta colors)
 * - Action button text/icons
 * - Rupee counter font style
 *
 * These hooks are called from z_parameter.c when SW97 beta UI is enabled.
 * For the initial port, this provides the basic color modifications.
 * Full texture replacement requires SW97 texture assets.
 */

/**
 * Get SW97 minimap primary color (green-tinted).
 * In SW97, the minimap uses a different color scheme than final OOT.
 */
static void Sw97_GetMinimapColor(s32* r, s32* g, s32* b, s32* a) {
    *r = 0;
    *g = 200;
    *b = 80;
    *a = 140;
}

/**
 * Get SW97 A-button color (blue, matching SW97 demo).
 */
static void Sw97_GetAButtonColor(s32* r, s32* g, s32* b) {
    *r = 90;
    *g = 90;
    *b = 255;
}

/**
 * Get SW97 B-button color (green, matching SW97 demo).
 */
static void Sw97_GetBButtonColor(s32* r, s32* g, s32* b) {
    *r = 0;
    *g = 200;
    *b = 80;
}

/**
 * Get SW97 C-button color (yellow, matching SW97 demo).
 */
static void Sw97_GetCButtonColor(s32* r, s32* g, s32* b) {
    *r = 255;
    *g = 240;
    *b = 60;
}
