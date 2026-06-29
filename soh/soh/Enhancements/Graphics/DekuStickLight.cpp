// Wind Waker-style held Deku stick light — game-side policy.
//
// When Link holds a *lit* Deku stick, vanilla OoT draws a flame sprite at the burning tip but emits no
// actual light. This module registers a dynamic point light at that tip so the flame becomes a real light
// source — which means it feeds all three Wind Waker features for free, since they all read the engine's
// point-light list (play->lightCtx):
//   - Cel Shading  — the stick can become an actor's key light (closest in-range point light),
//   - Light Casting — it casts a pool on the world (governed by the same Cast Size / Intensity sliders),
//   - Actor Shadows — which reuse the cel key direction.
// It behaves like a torch: torch-matched radius, the same per-frame white-noise brightness so the
// "Improve Flame Flicker" hook smooths it into the Wind Waker random-walk identically to every torch, and
// it obeys the existing range/size/intensity sliders with no extra code.
//
// Light *color* is deliberately not hardcoded as the source of truth: we look up an optional, modder-
// supplied color texture in the loaded archives and average it into the light's RGB. Vanilla archives do
// not contain that path, so we fall back to OoT's canonical fire color. A texture pack can add (or swap)
// that one asset to recolor the stick light without touching code.

#include <libultraship/bridge.h>
#include <ship/Context.h>
#include <fast/resource/type/Texture.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
#include "soh/ResourceManagerHelpers.h"
#include "DekuStickLight.h"

#include <memory>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace {

// Optional, modder-supplied color asset. Vanilla archives don't contain this path, so by default the
// fallback color below is used. A texture pack can add (or alt-swap) a texture here to recolor the stick
// light — its texels are averaged into the light's RGB. Resolved once per game session (a swap applies on
// the next launch), which keeps this off every hot path.
constexpr const char* kColorAssetPath = "textures/wind-waker/deku_stick_light_color";

// OoT's canonical fire color — En_Light's standard-fire primColor (a warm orange-yellow). Used when the
// optional color asset is absent (i.e. always, on vanilla archives). See the wind-waker docs: no stock
// flame texture carries color, so there is nothing in the o2r to sample by default.
constexpr u8 kFallbackR = 255;
constexpr u8 kFallbackG = 200;
constexpr u8 kFallbackB = 0;

// Matches the wall torch (obj_syokudai) so the stick's reach and cast pool feel torch-equivalent. The
// cel-shading PointLightRange and light-casting Cast Size sliders both scale off this, automatically.
constexpr s16 kStickLightRadius = 200;

// A single, frame-stable LightInfo. Both feature modules key their per-light animation/flicker state by
// the LightInfo* address, so it must persist (never stack/realloc). One Link → one static is correct.
LightInfo sStickLight;
LightNode* sStickNode = nullptr;
bool sInserted = false;

// Cached base color (asset-or-fallback), resolved once.
bool sColorResolved = false;
u8 sBaseR = kFallbackR;
u8 sBaseG = kFallbackG;
u8 sBaseB = kFallbackB;

// Average the optional color asset into the base RGB, if present. Leaves the fallback in place when the
// asset is absent, empty, or in a format we don't decode. Handles the two formats a modder is likely to
// ship a color swatch in: decoded RGBA32 image data (LOAD_AS_IMG) and N64 RGBA16 (5/5/5/1).
void ResolveBaseColor() {
    if (sColorResolved) {
        return;
    }
    sColorResolved = true; // resolve once; a modder asset swap is picked up on the next launch

    if (!ResourceMgr_FileExists(kColorAssetPath)) {
        return; // vanilla: keep the fallback fire color
    }
    auto tex = std::static_pointer_cast<Fast::Texture>(ResourceMgr_GetResourceByNameHandlingMQ(kColorAssetPath));
    if (tex == nullptr || tex->ImageData == nullptr || tex->ImageDataSize == 0) {
        return;
    }

    const uint8_t* px = tex->ImageData;
    const uint32_t n = tex->ImageDataSize;
    uint64_t r = 0, g = 0, b = 0;
    uint32_t count = 0;

    if ((tex->Flags & TEX_FLAG_LOAD_AS_IMG) || tex->Type == Fast::TextureType::RGBA32bpp) {
        for (uint32_t i = 0; i + 3 < n; i += 4) {
            if (px[i + 3] == 0) {
                continue; // skip fully transparent texels
            }
            r += px[i + 0];
            g += px[i + 1];
            b += px[i + 2];
            count++;
        }
    } else if (tex->Type == Fast::TextureType::RGBA16bpp) {
        for (uint32_t i = 0; i + 1 < n; i += 2) {
            uint16_t texel = (uint16_t)((px[i] << 8) | px[i + 1]); // big-endian RGBA5551
            if ((texel & 0x1) == 0) {
                continue; // alpha bit clear → transparent
            }
            r += ((texel >> 11) & 0x1F) << 3;
            g += ((texel >> 6) & 0x1F) << 3;
            b += ((texel >> 1) & 0x1F) << 3;
            count++;
        }
    } else {
        return; // unsupported format → keep fallback
    }

    if (count == 0) {
        return;
    }
    sBaseR = (u8)(r / count);
    sBaseG = (u8)(g / count);
    sBaseB = (u8)(b / count);
}

void RemoveStickLight(PlayState* play) {
    if (sInserted && play != nullptr) {
        LightContext_RemoveLight(play, &play->lightCtx, sStickNode);
    }
    sInserted = false;
    sStickNode = nullptr;
}

// Per-frame: keep a point light registered at the burning tip while the Deku stick is lit.
void UpdateDekuStickLight() {
    PlayState* play = gPlayState;
    if (play == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(play);

    // unk_860 is the stick burn timer (nonzero == on fire), but it is overloaded for the fishing pole, so
    // the held-item-action check must come first. unk_860 / meleeWeaponInfo live on the Player struct.
    bool lit = (player != nullptr) && (player->heldItemAction == PLAYER_IA_DEKU_STICK) && (player->unk_860 != 0);

    if (!lit) {
        RemoveStickLight(play);
        return;
    }

    ResolveBaseColor();

    // meleeWeaponInfo[0].tip is the world-space burning end of the stick (where vanilla spawns the flame
    // sprite). As the stick burns down it shortens, and the tip — read live — follows it inward for free.
    Vec3f* tip = &player->meleeWeaponInfo[0].tip;
    s16 x = (s16)tip->x;
    s16 y = (s16)tip->y;
    s16 z = (s16)tip->z;

    if (!sInserted) {
        Lights_PointNoGlowSetInfo(&sStickLight, x, y, z, sBaseR, sBaseG, sBaseB, kStickLightRadius);
        sStickNode = LightContext_InsertLight(play, &play->lightCtx, &sStickLight);
        sInserted = (sStickNode != nullptr);
    } else {
        // Lights_PointSetColorAndRadius (below) doesn't touch position, so move the light ourselves.
        sStickLight.params.point.x = x;
        sStickLight.params.point.y = y;
        sStickLight.params.point.z = z;
    }

    if (!sInserted) {
        return;
    }

    // Torch-matched flicker: feed a per-frame white-noise brightness exactly like obj_syokudai does. When
    // "Improve Flame Flicker" is on, the Lights_PointSetColorAndRadius interception detects the jumpy
    // brightness and replaces it with the smooth Wind Waker random-walk — so the stick flickers identically
    // to every torch. Hue stays the (asset-or-fallback) base color; only brightness varies.
    f32 brightness = (Rand_ZeroOne() * 127.0f + 128.0f) / 255.0f;
    Lights_PointSetColorAndRadius(&sStickLight, (u8)(sBaseR * brightness), (u8)(sBaseG * brightness),
                                  (u8)(sBaseB * brightness), kStickLightRadius);
}

// A scene change rebuilds lightCtx from a fresh node pool, so our node pointer is stale. Drop our
// bookkeeping; if the stick is still lit, the next update re-inserts into the new context.
void OnSceneInitResetLight(int16_t sceneNum) {
    sInserted = false;
    sStickNode = nullptr;
}

} // namespace

void* DekuStickLight_GetActiveLightInfo() {
    return sInserted ? &sStickLight : nullptr;
}

void RegisterDekuStickLight() {
    bool enabled = CVarGetInteger(CVAR_ENHANCEMENT("Graphics.WorldLighting.DekuStickLight"), 1);
    COND_HOOK(OnPlayerUpdate, enabled, UpdateDekuStickLight);
    COND_HOOK(OnSceneInit, enabled, OnSceneInitResetLight);
    // Turned off mid-game: drop our light so it doesn't linger in the current scene.
    if (!enabled) {
        RemoveStickLight(gPlayState);
    }
}

static RegisterShipInitFunc initFunc(RegisterDekuStickLight,
                                     { CVAR_ENHANCEMENT("Graphics.WorldLighting.DekuStickLight") });
