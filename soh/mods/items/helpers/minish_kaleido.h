/**
 * minish_kaleido.h - Standalone overlay for The Minish Cap warp map
 *
 * Completely independent from z_kaleido_scope. Uses OVERLAY_DISP
 * with 2D screen coordinates. Called from z_play.c when
 * minishCapWarpMode is active.
 */

#ifndef MINISH_KALEIDO_H
#define MINISH_KALEIDO_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Called from z_play.c instead of KaleidoScopeCall_Update when minishCapWarpMode is active
void MinishKaleido_Update(PlayState* play);

// Called from z_play.c Play_DrawOverlayElements when minishCapWarpMode is active
void MinishKaleido_Draw(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // MINISH_KALEIDO_H
