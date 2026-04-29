/**
 * mailbox_actor.h - Postman's Hat mailbox prop actor
 *
 * Hijacks ACTOR_EN_LIGHTBOX (same pattern as somaria_cubes.c) to render a
 * static mailbox visual with a cylinder collider. When the player stands
 * close and facing and presses A (with the Postman's Hat worn), the mailbox
 * opens the Postman Kaleido for warp selection.
 */

#ifndef MAILBOX_ACTOR_H
#define MAILBOX_ACTOR_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Spawns a mailbox actor at the given position / yaw. Returns the spawned
// actor pointer (Actor*) or NULL on failure. `mailboxIdx` is the index into
// sMailboxTable in item_postman_hat.c; it is stashed in actor->home.rot.z
// so the Update handler can look up the associated mailbox entry.
Actor* Mailbox_Spawn(PlayState* play, const Vec3f* pos, s16 yaw, s32 mailboxIdx);

// True if the given actor is a hijacked mailbox actor (identifies by custom
// update function pointer).
u8 Mailbox_IsMailboxActor(Actor* actor);

#ifdef __cplusplus
}
#endif

#endif // MAILBOX_ACTOR_H
