/**
 * equip_ikana.c - Shield of Ikana (Extended Shield Slot 3)
 *
 * Behavior: MM Mirror Shield model + Soul Drain + Death Save.
 * - Uses OOT's Mirror Shield model (EQUIP_VALUE_SHIELD_MIRROR) — unchanged
 * - Soul Drain: if you raise shield and get hit within 12 frames, drain enemy HP
 * - Death Save: when you die with this shield, revive with darkness aura (like fairy)
 *
 * Included by ext_equip_behavior.c (unity build).
 */

// No extra includes — unity-built from ext_equip_behavior.c

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define IKANA_PERFECT_GUARD_WINDOW 12 // Frames after raising shield for "perfect guard"
#define IKANA_SOUL_DRAIN_DAMAGE 4     // Quarter hearts drained from enemy per perfect guard
#define IKANA_REVIVE_HEARTS 3         // Hearts restored on death save (3 full hearts = 48 HP)

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static s16 sIkanaGuardTimer = 0;        // Counts frames since shield was raised
static u8 sIkanaGuardActive = 0;        // Whether shield is currently raised
static u8 sIkanaDeathSaveUsed = 0;      // Prevent double-revive per life
static u8 sIkanaDeathSaveAvailable = 1; // Reset on scene change or respawn

// ---------------------------------------------------------------------------
// Soul Drain: perfect guard detection
// ---------------------------------------------------------------------------
static void Ikana_UpdateSoulDrain(Player* player, PlayState* play) {
    // Detect shield raise: player is in guard state (R button held)
    u8 isGuarding = (player->stateFlags1 & PLAYER_STATE1_SHIELDING) != 0;

    if (isGuarding && !sIkanaGuardActive) {
        // Just started guarding — start the perfect guard window
        sIkanaGuardActive = 1;
        sIkanaGuardTimer = 0;
    } else if (isGuarding) {
        sIkanaGuardTimer++;
    } else {
        sIkanaGuardActive = 0;
        sIkanaGuardTimer = 0;
    }

    // Check for shield bounce within the perfect guard window
    if (sIkanaGuardActive && sIkanaGuardTimer <= IKANA_PERFECT_GUARD_WINDOW) {
        if (player->shieldQuad.base.acFlags & AC_BOUNCED) {
            // Perfect guard! Get the attacking actor directly
            Actor* attacker = player->shieldQuad.base.ac;
            if (attacker != NULL && attacker != &player->actor) {
                // Drain HP from the attacker
                if (attacker->colChkInfo.health > IKANA_SOUL_DRAIN_DAMAGE) {
                    attacker->colChkInfo.health -= IKANA_SOUL_DRAIN_DAMAGE;
                } else {
                    attacker->colChkInfo.health = 0;
                }

                // Visual/audio feedback: dark drain sound
                Audio_PlaySoundGeneral(NA_SE_EN_GANON_AT_RETURN, &player->actor.world.pos, 4,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);

                // Heal player slightly (soul absorbed)
                Health_ChangeBy(play, 8); // Half heart
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Death Save: check if we should revive instead of dying
// Called from z_player.c death handler via hook
// ---------------------------------------------------------------------------
u8 Ikana_ShouldRevive(void) {
    if (!ExtEquip_IsEnabled())
        return 0;
    if (gExtEquipState.currentExtShield != 3)
        return 0;
    if (sIkanaDeathSaveUsed)
        return 0;

    return 1;
}

void Ikana_ConsumeDeathSave(PlayState* play) {
    sIkanaDeathSaveUsed = 1;

    // Restore hearts
    gSaveContext.health = IKANA_REVIVE_HEARTS * 16;

    // Dark flash effect (purple/black)
    play->envCtx.screenFillColor[0] = 80;  // R (dark purple)
    play->envCtx.screenFillColor[1] = 0;   // G
    play->envCtx.screenFillColor[2] = 120; // B
    play->envCtx.screenFillColor[3] = 200; // A

    Audio_PlaySoundGeneral(NA_SE_EN_FANTOM_LAUGH, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

// Reset death save (called on scene change / re-equip)
static void Ikana_ResetDeathSave(void) {
    sIkanaDeathSaveUsed = 0;
    sIkanaDeathSaveAvailable = 1;
}

// Track scene to detect transitions (reset death save on new scene)
static s16 sIkanaLastScene = -1;

// ---------------------------------------------------------------------------
// Main Behavior
// ---------------------------------------------------------------------------
static void Ikana_Behavior(Player* player, PlayState* play) {
    // Reset death save on scene change (respawn, warp, new area)
    if (play->sceneNum != sIkanaLastScene) {
        sIkanaLastScene = play->sceneNum;
        Ikana_ResetDeathSave();
    }

    // Skip during cutscenes, dying, etc.
    if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_LOADING |
                               PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_GETTING_ITEM)) {
        return;
    }

    // Soul Drain on perfect guard
    Ikana_UpdateSoulDrain(player, play);
}
