/**
 * gerudo_skin.cpp — fallback Skin-based gerudo render (currently a stub).
 *
 * The hybrid SkelAnime path (GerudoHybrid_Draw) handles the default case and
 * is enough for v1. This file exists so the file layout mirrors Garo's
 * (form / skin / post_limb / hybrid_render) and the `gGerudoHybrid.Disable`
 * CVar has somewhere to route. Expand later if we want CPU-blended seam
 * smoothing across the gerudo bones (mirror garo_skin.cpp).
 */

#include "gerudo_skin.h"
#include <spdlog/spdlog.h>

extern "C" {
#include "z64.h"
}

extern "C" s32 GerudoSkin_Setup(PlayState* play) {
    (void)play;
    return 1; // no-op
}

extern "C" void GerudoSkin_Teardown(PlayState* play) {
    (void)play;
}

extern "C" void GerudoSkin_Draw(PlayState* play, Player* player) {
    (void)play;
    (void)player;
    // Stub: the hybrid render path is the production target. If the user sets
    // CVar gGerudoHybrid.Disable=1 we end up here and currently render nothing.
    // SPDLOG_WARN once if needed.
}
