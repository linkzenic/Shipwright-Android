/**
 * sw97_player_behavior.inc.c - Player behavior hooks for SW97 Medallion Spells
 *
 * Original actors: z64proto/sw97 team (Spaceworld '97 Experience)
 * Adapted for Ship of Harkinian (Shipwright)
 *
 * Provides CVar-gated hooks for:
 * - Magic spell actor spawning (6 spells mapped to spell indices 0-5)
 * - Helper functions for medallion/arrow item identification
 * - Medallion-to-arrow item conversion
 */

// Runtime actor IDs (set by sw97_init.cpp via ActorDB)
extern s16 gSw97ActorId_MagicFire;
extern s16 gSw97ActorId_MagicIce;
extern s16 gSw97ActorId_MagicLight;
extern s16 gSw97ActorId_MagicDark;
extern s16 gSw97ActorId_MagicSoul;
extern s16 gSw97ActorId_MagicWind;
extern s16 gSw97ActorId_ArrowFire;
extern s16 gSw97ActorId_ArrowIce;
extern s16 gSw97ActorId_ArrowLight;
extern s16 gSw97ActorId_ArrowDark;
extern s16 gSw97ActorId_ArrowSoul;
extern s16 gSw97ActorId_ArrowWind;

// SW97 magic spell costs: indices 0-5 match Player_ActionToMagicSpell output
// 0=Wind(12), 1=Soul(24), 2=Dark(12), 3=Ice(24), 4=Light(24), 5=Fire(12)
static u8 sSw97MagicSpellCosts[] = { 12, 24, 12, 24, 24, 12 };

// SW97 magic arrow costs: all 4 except light which is 8
static u8 sSw97MagicArrowCosts[] = { 4, 4, 8, 4, 4, 4 };

/**
 * Spawn the correct SW97 magic spell actor based on spell index.
 * Called from Player_SpawnMagicSpell in z_player.c.
 *
 * Spell index mapping (from Player_ActionToMagicSpell):
 *   0 = IA_MAGIC_SPELL_15 = Forest Medallion → MagicWind
 *   1 = IA_MAGIC_SPELL_16 = Spirit Medallion → MagicSoul
 *   2 = IA_MAGIC_SPELL_17 = Shadow Medallion → MagicDark
 *   3 = IA_FARORES_WIND   = Water Medallion  → MagicIce
 *   4 = IA_NAYRUS_LOVE    = Light Medallion  → MagicLight
 *   5 = IA_DINS_FIRE       = Fire Medallion   → MagicFire
 *
 * Returns the spawned actor, or NULL if SW97 spells are disabled.
 */
static Actor* Sw97_TrySpawnMagicSpell(PlayState* play, Player* player, s32 spell) {
    if (!SW97_MEDALLIONS_ENABLED()) {
        return NULL;
    }

    if (spell < 0 || spell >= 6) {
        return NULL;
    }

    s16* spellActorIds[] = {
        &gSw97ActorId_MagicWind,  // 0 = Forest
        &gSw97ActorId_MagicSoul,  // 1 = Spirit
        &gSw97ActorId_MagicDark,  // 2 = Shadow
        &gSw97ActorId_MagicIce,   // 3 = Water
        &gSw97ActorId_MagicLight, // 4 = Light
        &gSw97ActorId_MagicFire,  // 5 = Fire
    };

    s16 actorId = *spellActorIds[spell];
    if (actorId < 0) {
        return NULL;
    }

    Actor* spawned = Actor_Spawn(&play->actorCtx, play, actorId, player->actor.world.pos.x,
                                 player->actor.world.pos.y, player->actor.world.pos.z, 0, 0, 0, 0);

    // Tell teammates to spawn the same spell-effect actor on their side.
    // Spells follow the caster (attached_to_owner=1) — their visual stays
    // around the caster's dummy as long as the spell is active. Map the
    // spell index back to the corresponding HARPOON_VFX_KIND_SW97_MAGIC_*.
    if (spawned != NULL) {
        s32 vfxKindByIndex[] = {
            HARPOON_VFX_KIND_SW97_MAGIC_WIND,   // 0
            HARPOON_VFX_KIND_SW97_MAGIC_SOUL,   // 1
            HARPOON_VFX_KIND_SW97_MAGIC_DARK,   // 2
            HARPOON_VFX_KIND_SW97_MAGIC_ICE,    // 3
            HARPOON_VFX_KIND_SW97_MAGIC_LIGHT,  // 4
            HARPOON_VFX_KIND_SW97_MAGIC_FIRE,   // 5
        };
        Harpoon_NotifyVfxSpawn(spawned, vfxKindByIndex[spell], /*attachedToOwner=*/1);
    }
    return spawned;
}

/**
 * Check if an item ID is a quest medallion (spell mode).
 */
static s32 Sw97_IsMedallionItem(s32 item) {
    return (item >= ITEM_MEDALLION_FOREST && item <= ITEM_MEDALLION_LIGHT);
}

/**
 * Check if an item ID is an SW97 arrow variant (arrow mode).
 */
static s32 Sw97_IsArrowItem(s32 item) {
    return (item >= ITEM_SW97_ARROW_FIRE && item <= ITEM_SW97_ARROW_WIND);
}

/**
 * Convert a medallion item to its corresponding SW97 arrow item.
 * Used by L+C swap in z_player.c.
 */
s32 Sw97_MedallionToArrowItem(s32 medallionItem) {
    switch (medallionItem) {
        case ITEM_MEDALLION_FIRE:
            return ITEM_SW97_ARROW_FIRE;
        case ITEM_MEDALLION_WATER:
            return ITEM_SW97_ARROW_ICE;
        case ITEM_MEDALLION_LIGHT:
            return ITEM_SW97_ARROW_LIGHT;
        case ITEM_MEDALLION_SHADOW:
            return ITEM_SW97_ARROW_DARK;
        case ITEM_MEDALLION_SPIRIT:
            return ITEM_SW97_ARROW_SOUL;
        case ITEM_MEDALLION_FOREST:
            return ITEM_SW97_ARROW_WIND;
        default:
            return ITEM_NONE;
    }
}
