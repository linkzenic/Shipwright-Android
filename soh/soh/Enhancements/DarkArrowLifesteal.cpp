#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/ShipInit.hpp"
#include "soh/ObjectExtension/ObjectExtension.h"
#include "expansions/sw97/sw97_config.h"

extern "C" {
#include "z64.h"
#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h"
extern PlayState* gPlayState;
}

// Dark arrow lifesteal DoT: when an SW97 Dark arrow (bow OR slingshot seed)
// hits an enemy, attach this state. A per-actor Update hook then ticks the
// drain over 3 seconds (90 frames), dealing damage every 20 frames and
// refunding the same amount to Link. Can kill the target.
struct DarkLifestealData {
    s16 remainingFrames; // counts down from 90 to 0
    s16 tickPhase;       // counts down to next tick (every 20 frames)
};

static ObjectExtension::Register<DarkLifestealData> DarkLifestealDataRegister;

static constexpr s16 LIFESTEAL_TOTAL_FRAMES = 90;
static constexpr s16 LIFESTEAL_TICK_FRAMES = 20;
static constexpr s16 LIFESTEAL_DAMAGE_PER_TICK = 4; // ¼ heart (1 heart = 16 HP)

static bool IsValidLifestealTarget(Actor* actor) {
    if (actor == NULL || actor->update == NULL) return false;
    return actor->category == ACTORCAT_ENEMY || actor->category == ACTORCAT_BOSS;
}

void RegisterDarkArrowLifestealHooks() {
    // Only active when SW97 medallions are enabled — Dark arrows are SW97-only.
    bool shouldRegister = SW97_MEDALLIONS_ENABLED();

    // On every EnArrow Update, check if it's an SW97 Dark arrow (bow or slingshot
    // seed) that just hit (hitFlags bit 1) and tag the hit actor with a lifesteal entry.
    // The hit actor lives on `collider.base.at` (set by the engine on AT hits);
    // arrow->hitActor only gets populated for actors with ACTOR_FLAG_CAN_ATTACH_TO_ARROW,
    // which excludes most enemies — using `at` covers everything.
    COND_ID_HOOK(OnActorUpdate, ACTOR_EN_ARROW, shouldRegister, [](void* actorPtr) {
        auto* arrow = (EnArrow*)actorPtr;
        s16 p = (s16)arrow->actor.params;
        if (p != ARROW_SW97_0C && p != ARROW_SEED_0C) return; // bow + slingshot Dark
        if (!(arrow->hitFlags & 1)) return;                   // not the impact frame

        Actor* hit = arrow->collider.base.at;
        if (!IsValidLifestealTarget(hit)) return;

        // Re-tag on every hit so repeated hits refresh the duration.
        DarkLifestealData data{};
        data.remainingFrames = LIFESTEAL_TOTAL_FRAMES;
        data.tickPhase = LIFESTEAL_TICK_FRAMES;
        ObjectExtension::GetInstance().Set(hit, data);
    });

    // Tick the lifesteal on every actor's Update. Cheap unless extension exists.
    COND_HOOK(OnActorUpdate, shouldRegister, [](void* actorPtr) {
        Actor* actor = (Actor*)actorPtr;
        auto* data = ObjectExtension::GetInstance().Get<DarkLifestealData>(actor);
        if (data == nullptr) return;

        if (actor->update == NULL) {
            ObjectExtension::GetInstance().Remove<DarkLifestealData>(actor);
            return;
        }

        if (--data->tickPhase <= 0) {
            data->tickPhase = LIFESTEAL_TICK_FRAMES;

            // Drain enemy HP directly (avoids invuln/iframe gating that
            // collider-based damage would trip on a freshly hit actor).
            s16 hp = actor->colChkInfo.health;
            s16 drain = (hp > LIFESTEAL_DAMAGE_PER_TICK) ? LIFESTEAL_DAMAGE_PER_TICK : hp;
            actor->colChkInfo.health -= drain;

            // If we drained the last HP, make sure the actor actually dies — set
            // the AC_HIT flag plus a death-grade damage effect so the actor's own
            // update routine processes the kill on its next tick.
            if (actor->colChkInfo.health <= 0) {
                actor->colChkInfo.health = 0;
                actor->colChkInfo.damage = 8;
                actor->colChkInfo.damageEffect = 0; // generic
                actor->colorFilterTimer = 0;
            }

            // Refund Link the drained amount, capped at max.
            gSaveContext.health += drain;
            if (gSaveContext.health > gSaveContext.healthCapacity) {
                gSaveContext.health = gSaveContext.healthCapacity;
            }

            // Dark tint pulse for visual feedback (lasts until next tick).
            Actor_SetColorFilter(actor, 0x8000, 200, 0x2000, LIFESTEAL_TICK_FRAMES);
        }

        if (--data->remainingFrames <= 0) {
            ObjectExtension::GetInstance().Remove<DarkLifestealData>(actor);
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterDarkArrowLifestealHooks, { SW97_MEDALLIONS_CVAR });
