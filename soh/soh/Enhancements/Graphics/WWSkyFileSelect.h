#ifndef WW_SKY_FILE_SELECT_H
#define WW_SKY_FILE_SELECT_H

// C-callable entry so the decompiled file-select gamestate (z_file_choose.c) can draw the Wind Waker night
// sky. Kept in its own header (with engine types) so the C decomp can include it without the C++-only
// WWSkyEnv.h; mirrors soh/Enhancements/FileSelectEnhancements.h.

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Draw the Wind Waker night sky (gradient dome + starfield) over the file-select screen, replacing the
// vanilla night skybox. Call right after SkyboxDraw_Draw with the FileChooseContext's gfxCtx and view. A
// no-op (vanilla skybox stays) unless the "Use Sky" master toggle is on.
void WWSky_DrawFileSelect(GraphicsContext* gfxCtx, View* view);

#ifdef __cplusplus
}
#endif

#endif
