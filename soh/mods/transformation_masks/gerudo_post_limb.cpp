/**
 * gerudo_post_limb.cpp — DEAD STUB.
 *
 * Was a null-body PostLimbDraw for the native GeldB 23-bone skel (so the
 * per-limb side effects — bodyPartsPos, leftHandPos, focus.pos, feetPos,
 * shieldMf — still fired while the gerudo body drew separately). The
 * current pipeline uses a Link-rigged gerudo skin (see
 * tools/repack_gerudo_player.py + o2r_loader.cpp::EnsureInit), so
 * Player_DrawImpl runs naturally and produces those side effects through
 * Player_PostLimbDrawGameplay — no separate null-body pass needed.
 *
 * The stub remains so the vcxproj entry compiles cleanly. No callers in
 * z_player.c reference GerudoForm_DrawNullBody anymore.
 */

#include "gerudo_post_limb.h"

extern "C" void GerudoForm_DrawNullBody(PlayState* play, Player* player, s32 lod) {
    (void)play;
    (void)player;
    (void)lod;
}
