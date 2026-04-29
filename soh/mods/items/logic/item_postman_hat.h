/**
 * item_postman_hat.h - Postman's Hat (Fast Travel via Mailboxes)
 *
 * Mailbox warp-point table and utility functions.
 * Each mailbox unlocks the first time the player walks within range.
 */

#ifndef ITEM_POSTMAN_HAT_H
#define ITEM_POSTMAN_HAT_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    s16 sceneId;       // Primary scene for warp destination
    s16 altSceneId;    // Secondary scene for draw + proximity-unlock (-1 = none)
    s16 entranceIndex; // ENTR_* used by the warp
    Vec3f pos;         // World coords of the mailbox (and player respawn)
    s16 yaw;           // Player facing (rotY binang)
    u8 roomIndex;
    s16 mapCX, mapCY;  // Kaleido map coords for the icon
    const char* name;
} PostmanMailboxPoint;

#define POSTMAN_MAILBOX_COUNT 7

extern const PostmanMailboxPoint sMailboxTable[POSTMAN_MAILBOX_COUNT];

s32 PostmanHat_IsMailboxUnlocked(s32 idx);
s32 PostmanHat_GetUnlockedCount(void);

// Called when the B button is pressed while wearing the Postman's Hat.
void PostmanHat_TryTriggerWarpMode(PlayState* play);

// Per-frame: proximity-unlock + Mail Dash transition state machine.
void Handle_PostmanHat(Player* p, PlayState* play);

// Draws the mailbox DLs on POLY_OPA for every mailbox matching the current scene.
void MailboxDrawer_DrawAllForScene(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // ITEM_POSTMAN_HAT_H
