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
// Soul Drain: guard-window tracker (per-frame)
//
// We CANNOT poll `shieldQuad.base.acFlags & AC_BOUNCED` here — by the time
// the ext-equip dispatch runs (z_player.c:13276), Player_UpdateCommon has
// already called Collider_ResetQuadAC (z_player.c:13100) and cleared the
// flag. The actual bounce-detection runs in Ikana_OnShieldBlock below,
// hooked from z_player.c:5228 (next to DivineShield_OnShieldBlock) where
// the flag is still live. This function only maintains the guard timer
// state that the hook consults to know if we're in the perfect-guard window.
// Age-agnostic: works for both child and adult equipping the Ikana shield.
// ---------------------------------------------------------------------------
static void Ikana_UpdateSoulDrain(Player* player, PlayState* play) {
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
}

// ---------------------------------------------------------------------------
// Soul Drain hook: called DIRECTLY from z_player.c the EXACT moment a shield
// bounce is detected (AC_BOUNCED on shieldQuad). This runs BEFORE
// Collider_ResetQuadAC clears the flags, so `shieldQuad.base.ac` is still
// the live attacker pointer. Mirrors DivineShield_OnShieldBlock.
// ---------------------------------------------------------------------------
void Ikana_OnShieldBlock(Player* player, PlayState* play) {
    if (!ExtEquip_IsEnabled())
        return;
    if (gExtEquipState.currentExtShield != 3)
        return;

    // Must be within the perfect-guard window from when shield was raised
    if (!sIkanaGuardActive || sIkanaGuardTimer > IKANA_PERFECT_GUARD_WINDOW)
        return;

    Actor* attacker = player->shieldQuad.base.ac;
    if (attacker == NULL || attacker == &player->actor)
        return;

    // Drain HP from the attacker
    if (attacker->colChkInfo.health > IKANA_SOUL_DRAIN_DAMAGE) {
        attacker->colChkInfo.health -= IKANA_SOUL_DRAIN_DAMAGE;
    } else {
        attacker->colChkInfo.health = 0;
    }

    // Visual/audio feedback: dark drain sound
    Audio_PlaySoundGeneral(NA_SE_EN_GANON_AT_RETURN, &player->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);

    // Heal player slightly (soul absorbed)
    Health_ChangeBy(play, 8); // Half heart

    // Cross-gamemode PvP: broadcast the soul drain so the
    // ACTUAL attacker peer (if remote) loses HP + we visibly
    // heal. shieldType=5 (SHIELD_IKANA), effect=2 (PARRY_SOUL_DRAIN).
    extern void HarpoonCombat_BroadcastShieldParry_C(int shieldType, int effect);
    HarpoonCombat_BroadcastShieldParry_C(5, 2);
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

    // Cross-gamemode PvP: broadcast so peers see the revive (dark-purple
    // flash + HP restoration are mirrored client-side).
    extern void HarpoonCombat_BroadcastShieldRevive_C(int restoredHealth);
    HarpoonCombat_BroadcastShieldRevive_C(IKANA_REVIVE_HEARTS * 16);

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
