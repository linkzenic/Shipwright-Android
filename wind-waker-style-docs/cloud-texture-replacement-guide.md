# Making your own cloud textures

The Wind Waker-style sky draws its clouds from **five images**, and they're easy to replace with
your own art.

## How to replace them

1. In your game folder (next to the game itself) is **`soh.o2r`** — the game's data archive.
   **Make a copy of it** (leave the original alone) and open the copy in
   **[Retro](https://github.com/HarbourMasters/retro)** (the community tool for unpacking and
   packing o2r files).
2. Inside, find the folder **`textures/wind-waker/clouds/`** — the five cloud textures live there.
3. Make your own versions of the five images (rules below).
4. Use Retro to pack a **new** o2r containing **only** your five textures, at that exact folder path
   (`textures/wind-waker/clouds/`) with the exact same names. Call the file anything you like, e.g.
   `my_clouds.o2r`.
5. Drop your new `.o2r` into the game's **`mods`** folder.

That's it — the game uses your clouds instead of the built-in ones. Deleting the mod file brings the
originals back. (Don't edit `soh.o2r` itself — game updates overwrite it, so changes there would be
lost. The mods folder is the safe place.)

## The five textures

> Previews below are shown over a blue sky — the real images are white clouds on a **transparent**
> background.

### The drifting clouds — `cloudtx_01` / `cloudtx_02` / `cloudtx_03`

| `cloudtx_01` | `cloudtx_02` | `cloudtx_03` |
|:---:|:---:|:---:|
| ![cloudtx_01](images/cloudtx_01-preview.png) | ![cloudtx_02](images/cloudtx_02-preview.png) | ![cloudtx_03](images/cloudtx_03-preview.png) |

64×64. The puffy clouds that drift across the sky. **Every cloud is drawn from all three layered on
top of each other** (slightly offset) — so make them three different-shaped siblings, not identical
copies.

### The horizon cloud ring — `cloud_mae` / `cloud_naka`

`cloud_mae` — the **front** layer: separate cloud clusters with gaps between them.

![cloud_mae](images/cloud_mae-preview.png)

`cloud_naka` — the **back** layer: one continuous bank of clouds.

![cloud_naka](images/cloud_naka-preview.png)

256×64. These two strips wrap all the way around the horizon, scrolling and slowly evolving with the
wind. Each strip is drawn twice with the copies sliding over each other (transparencies multiplied),
so clean silhouettes matter more than fine internal detail.

## Rules

1. **Transparency is the cloud shape.** Keep the visible part near-white — the game colours the
   clouds for time of day and weather (orange sunsets, dark nights, grey storms). If you paint them
   pink, they'll be pink at midnight too.
2. Sizes must be **powers of two** (64, 128, 256, 512), max 512 in either direction — except the
   two horizon strips (`cloud_mae`/`cloud_naka`), which are capped at **256 wide** (the band wraps
   the strip several times around the horizon, and wider strips overflow the renderer's texture
   coordinate range). Bigger = sharper in game.
3. The two horizon strips must **tile seamlessly left-to-right**; the **bottom edge sits on the
   horizon line**.
4. Soft, fuzzy edges look best — hard edges read as cutouts once stretched across the sky.
