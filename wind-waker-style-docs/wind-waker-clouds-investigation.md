# Wind Waker Clouds — status & investigation log

Status: **ROOT CAUSE FOUND & FIXED (pending in-game confirmation): the draw never set the cycle type.**

The skybox draws immediately before the clouds hook via `SETUPDL_40` (`z_rcp.c`), which leaves the RDP in
`G_CYC_2CYCLE`. The cloud DL set every texture-pipeline state *except* `gDPSetCycleType`. In 2-cycle mode,
Fast3D's combiner parser (`interpreter.cpp`, `CreateColorCombiner` — the `i == 1` normalization block) sees
that our duplicated `G_CC_MODULATERGBA` cycle 2 never references `COMBINED`, so it **clears cycle 1 away
before scanning for used textures**, and cycle-2's `TEXEL0` is remapped to texture slot **1**. Net effect:
`usedTextures[0] = false` (so `ImportTexture` for our texture genuinely never fired — log #2 below was
accurate, just misdiagnosed) and the shader samples the *skybox's* stale tile-1 texture × shade — solid
light blue, regardless of what texture we load. The magenta debug texture couldn't change anything because
the shader never read slot 0.

Fix in `EmitClouds` (WWClouds.cpp): `gDPSetCycleType(G_CYC_1CYCLE)` + `gDPSetAlphaCompare(G_AC_NONE)`
(alpha-compare bits are also outside `gDPSetRenderMode`; a stale `G_AC_THRESHOLD` would clip the soft
cloud alpha). Lesson for any future hand-built DL: **cycle type and alpha compare are inherited state —
always set them**, or run a `Gfx_SetupDL_*` first like every vanilla draw does.

Second gotcha found while confirming the fix in-game: the staged `cloudtx_01.rgba32.png` (and the packed
`ww_clouds.o2r`) had been **overwritten in place with the magenta/yellow debug texture** during the earlier
investigation — so the first post-fix run showed the debug art, proving the pipeline but not the clouds.
Re-extracted the real sprite from `sea/Stage.arc` (standalone `.bti` files, not inside a BDL — see
`extract_cloudtx.py` pattern: `RARC` → `BTI(file_entry).render()`), repacked, reinstalled. The "asset
verified correct" claim below predates that overwrite.

## Shipping without Nintendo assets — the texture override contract

The mod ships **no Wind Waker art**. The built-in textures are original WW-*style* look-alikes generated
by `scripts/gen-ww-cloud-textures.py` (procedural metaball puffs + periodic band strips) into
`soh/assets/custom/textures/wind-waker/clouds/`, which the build packs into **soh.o2r** — so a modder can
unzip soh.o2r and see the exact names, sizes and locations. Any mods-folder o2r that provides the same
resource paths overrides the built-ins at runtime — that is the delivery mechanism for a WW-themed
texture pack (and for users who extract WW's own art privately):

| Resource path | Used for | Built-in size |
|---|---|---|
| `textures/wind-waker/clouds/cloudtx_01` | drifting cloud sprite, layer 1 | 64×64 |
| `textures/wind-waker/clouds/cloudtx_02` | drifting cloud sprite, layer 2 | 64×64 |
| `textures/wind-waker/clouds/cloudtx_03` | drifting cloud sprite, layer 3 | 64×64 |
| `textures/wind-waker/clouds/cloud_mae`  | horizon band, front strip (gappy clusters) | 256×64 |
| `textures/wind-waker/clouds/cloud_naka` | horizon band, back strip (continuous mass) | 256×64 |

Replacement rules: RGBA32, power-of-two dimensions, max 512×512 (vertex texel coords are S10.5). The
cloud *shape* lives in the alpha channel; RGB should stay near-white (it gets tinted by time-of-day
vertex colour). Band strips must tile horizontally; their bottom edge is the horizon line. Pack them
with ZAPD's custom-otr path: name PNGs `<name>.rgba32.png` under `textures/wind-waker/clouds/` and build
an o2r (see `[[shipwright-ww-asset-extraction]]` for the exact command), drop it in `mods/`.

The rest of this file is the original investigation log, kept for the record.

## What this feature is

A port of Wind Waker's drifting puffy-cloud system, `dKankyo_vrkumo_Packet`
(noclip `src/ZeldaWindWaker/d_kankyo_wether.ts`): up to 100 textured billboards placed on a
camera-following dome, drifting on the wind, fading in/out, sized/tinted by a distance falloff. It is the
*dynamic* clouds — distinct from the horizon cloud band (`vr_back_cloud.bdl`), which was prototyped first and
dropped.

Lives in `soh/soh/Enhancements/Graphics/WWClouds.cpp`, drawn via the `OnPlayDrawSkyClouds` hook (fired in
`z_play.c` after the skybox/stars, before sun-moon/world). GUI toggle + Opacity/Coverage/Drift sliders under
**Wind Waker Style → Clouds**.

## What WORKS

- **The vrkumo simulation** — spawn (disk placement), per-frame drift along `envCtx.windDirection`, edge
  respawn, distance falloff, `position.y` dome bulge, alpha ease, overhead fade, size bounce. Ported faithfully
  and confirmed good in-game ("the animations seem pretty decent"). See `UpdateClouds` / `BuildClouds`.
- **The dome-projected billboard geometry** — quads follow the camera with no parallax (eye-relative verts +
  `Matrix_Translate(eye)`), sit around the horizon, thin out overhead. This is the same technique as the
  working star field (`WWNightSky.cpp`).
- **The asset pipeline** (see `[[shipwright-ww-asset-extraction]]`): extract `cloudtx_01/02/03.bti` (32×32
  RGBA32) from the user's WW ROM via gclib, pack into `ww_clouds.o2r`, load from the `mods/` folder.

## What does NOT work

The cloud **texture never renders**. Every variant has drawn the quads as **solid rectangles** the colour of
the vertex tint / a mangled texel — never the actual soft cloud sprite. A magenta debug texture (opaque, with
a yellow corner marker) rendered as solid `#0000FF`.

## Ground truth established by logging (in `libultraship/src/fast/interpreter.cpp`)

All logging has since been reverted; these were the findings:

1. `gfx_set_timg_handler_rdp` (the `gsDPSetTextureImage` handler) **fires ~once per frame** for our texture and
   **resolves it correctly**: `__OTR__textures/wind-waker/clouds/cloudtx_01 -> loaded flags=0x2 type=1 (RGBA32)
   32x32`. So the resource loads fine and the `__OTR__` path resolves.
2. **`Interpreter::ImportTexture` is NEVER called for our cloud texture.** A log over *all* 32×32 RGBA32
   imports showed many other textures importing every frame (`gItemIconSwordKokiriTex`, Toon Link HD-pack
   textures — all fine via the normal TMEM path) but **no `cloudtx` line ever**. The texture is *set* but never
   *uploaded*, so the quads sample whatever texture was uploaded last → solid colour.

Conclusion: **the texture load is fine; the bug is that this hand-built 3D textured draw in `POLY_OPA` never
triggers the texture upload.** The identical format (32×32 RGBA32) renders fine when drawn the game's normal
way and in 2D overlays (item icons, `DrawCustomItemIcon`, `HookshotReticle`, `VisualAgony`).

## Everything tried (all still produced solid rectangles)

Texture source / load path:
- `__OTR__`-path string via `gDPLoadTextureBlock` (like `HookshotReticle`). Resolves (log #1) but no import.
- Raw decoded pixel pointer (`Fast::Texture::ImageData`) via `gDPLoadTextureBlock` — exactly how
  `DrawCustomItemIcon` draws a 32×32 RGBA32 that works. Still no render.
- Setting `TEX_FLAG_LOAD_AS_IMG` on the cached resource at runtime (flag *did* reach the resource — log showed
  `flags=0x2`).
- A libultraship change to make `ImportTexture` honour `metadata->resource->Flags` (reverted — it was chasing
  a *different* texture; our import never fires so it was moot).

Render state / combiner / mode:
- Render modes: `G_RM_AA_XLU_SURF` and `G_RM_XLU_SURF`.
- Combiner: `G_CC_MODULATERGBA` (= `G_CC_MODULATEIA`, TEXEL0×SHADE).
- Added the full texture-pipeline reset that the setup-DL macros do and `gDPLoadTextureBlock` doesn't:
  `gDPSetTextureLUT(G_TT_NONE)`, `TexturePersp`, `TextureDetail`, `TextureLOD`, `TextureFilter`,
  `TextureConvert` — to rule out a leaked CI/LUT palette state. No change.

Asset/format checks (all verified correct):
- Extracted sprites ARE real WW clouds (verified visually; composited over blue they match noclip exactly).
- Packed resource is valid: 32×32, `type=RGBA32bpp`, `dataSize=4096` (=32·32·4), alpha 0–255 present.
- The cloud shape lives in the **alpha channel**; RGB is a near-uniform light blue. So a texture with no alpha
  applied reads as a flat light-blue rectangle — which is consistent with "solid blue".

Loading logistics (resolved, not the cause):
- Mods load from `~/Library/Application Support/com.shipofharkinian.soh/mods/`, not `build-cmake/soh/mods`.
- Put the `.o2r` in a subfolder (`WindWakerClouds/`) matching other working mods.
- Confirmed via the guard (`ResourceMgr_GetResourceByNameHandlingMQ`) and log #1 that the resource loads.

## Leading hypotheses for next time (untested, need an interactive build)

- **The texture upload isn't triggered by this particular draw.** All confirmed-working comparisons draw in
  2D overlay (`OVERLAY_DISP`) or via the game's baked display lists (OTR-hash texture path,
  `gfx_set_timg_otr_hash_handler_custom`). Our draw is a hand-built `POLY_OPA` list using the `__OTR__`-string
  / raw-pointer `gsDPSetTextureImage` path. Something in the flush/`ImportTexture` trigger differs. Add logging
  in the triangle-flush path (where `ImportTexture` is actually invoked) to see why our tile is skipped.
- **Possible fix directions:** (a) draw the clouds as **2D screen-projected sprites** (`gSPTextureRectangle`
  in `OVERLAY_DISP`) — the proven path — projecting each dome position to screen; (b) reference the texture via
  the **OTR-hash** mechanism the game's own draws use instead of the raw string; (c) mirror a *working 3D
  textured dynamic draw* if one exists (the sun in `z_kankyo.c` is textured in `POLY_OPA` but uses
  `Gfx_SetupDL_54Opa` + `gDPLoadTextureBlock_4b` and static `VTX`).

## Recommendation

Bank the two features that work — **twinkling stars** (`WWNightSky.cpp`, committed) and **gradient sky**
(`WWSkyGradient.cpp`) — and treat clouds as a follow-up to be finished where the build can be run interactively
to iterate on the draw. The vrkumo simulation and geometry are done and good; only the texture upload remains.
