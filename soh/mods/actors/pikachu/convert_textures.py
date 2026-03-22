"""
Converts Pikachu face textures to RGBA16 u64 arrays for pikachu.c.

Priority:
  1. Blender CI4 + palette (from pikachuDL_forSmashTest.c) — used for the
     'happy' base variant of each texture.  Gives exact Blender colours.
  2. SSB64 PNGs (fallback) — used for additional expression variants that
     don't exist in the Blender export.
"""
import re, math
from PIL import Image

PIKACHU_C     = "c:/Users/LENOVO/Documents/GitHub/Shipwright/soh/mods/actors/pikachu/pikachuDL.c"
PIKACHU_H     = "c:/Users/LENOVO/Documents/GitHub/Shipwright/soh/mods/actors/pikachu/pikachuDL.h"
FAST64_C      = "C:/Users/LENOVO/Downloads/pikachu_fast64/pikachuDL.c"
TEX_FOLDER    = r"C:/Users/LENOVO/Downloads/Nintendo 64 - Super Smash Bros. - Fighters - Pikachu"

# ── core pixel → RGBA16 u64 helpers ────────────────────────────────────────────

def rgba_to_px16(r, g, b, a):
    """Convert 8-bit RGBA to N64 RGBA16 (R5G5B5A1), byte-swapped for x86."""
    r5 = (r >> 3) & 0x1F
    g5 = (g >> 3) & 0x1F
    b5 = (b >> 3) & 0x1F
    a1 = 1 if a >= 128 else 0
    px16 = (r5 << 11) | (g5 << 6) | (b5 << 1) | a1
    # SOH runs on x86 (little-endian). The renderer reads raw bytes expecting
    # big-endian 16-bit pixels. Byte-swap so the bytes in memory are correct.
    return ((px16 & 0xFF) << 8) | ((px16 >> 8) & 0xFF)

def pixels_to_u64s(pixel_list):
    """
    Convert list of (R,G,B,A) tuples to byte-swapped RGBA16 u64 array.
    4 pixels per u64, pixel 0 in the least-significant 16 bits so that
    in x86 LE memory pixel 0 occupies the lowest address bytes, which
    the Fast3D renderer reads first (= leftmost pixel on screen).
    """
    u64s = []
    i = 0
    while i < len(pixel_list):
        val = 0
        for j in range(4):
            if i + j < len(pixel_list):
                px16 = rgba_to_px16(*pixel_list[i + j])
            else:
                px16 = 0
            val |= (px16 & 0xFFFF) << (j * 16)
        u64s.append(val)
        i += 4
    return u64s

def format_u64_array(name, u64s, per_line=4):
    rows = []
    for i in range(0, len(u64s), per_line):
        rows.append("\t" + ", ".join(f"0x{v:016x}" for v in u64s[i:i + per_line]) + ",")
    return "u64 " + name + "[] = {\n" + "\n".join(rows) + "\n};\n"

# ── PNG → RGBA16 ────────────────────────────────────────────────────────────────

def png_to_rgba16_u64s(path, target_w=None, target_h=None, transparent_bg=None, bg_thr=40):
    """
    transparent_bg: (R,G,B) colour to key out as transparent (alpha=0).
    Pixels within bg_thr Euclidean distance of that colour become alpha=0.
    """
    img = Image.open(path).convert("RGBA")
    if target_w or target_h:
        tw = target_w or img.width
        th = target_h or img.height
        img = img.resize((tw, th), Image.Resampling.LANCZOS
                         if hasattr(Image, 'Resampling') else Image.LANCZOS)
    if transparent_bg is not None:
        br, bg_c, bb = transparent_bg
        thr2 = bg_thr * bg_thr
        img.putdata([
            (r, g, b, 0) if (r-br)**2 + (g-bg_c)**2 + (b-bb)**2 < thr2
            else (r, g, b, a)
            for r, g, b, a in img.getdata()
        ])
    return pixels_to_u64s(list(img.getdata())), img.width, img.height

# ── Blender CI4 extractor ───────────────────────────────────────────────────────

def parse_c_u64_array(c_source, array_name):
    """Extract list of integer u64 values from a C u64 array declaration."""
    pattern = r'u64\s+' + re.escape(array_name) + r'\s*\[\s*\]\s*=\s*\{([^}]+)\}'
    m = re.search(pattern, c_source, re.DOTALL)
    if not m:
        raise ValueError(f"Array '{array_name}' not found in C source")
    return [int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]+)', m.group(1))]

def ci4_u64s_to_pixels(ci4_u64s, pal_u64s):
    """
    Decode CI4 (4-bit indexed) + RGBA16 palette u64 arrays into (R,G,B,A) list.
    Both arrays are in N64 big-endian format (as they appear in the C source).
    """
    # Build palette: each u64 holds 4 RGBA16 entries (big-endian N64)
    palette_rgba16 = []
    for u in pal_u64s:
        for j in range(4):
            palette_rgba16.append((u >> (48 - j * 16)) & 0xFFFF)
    # Pad palette to 16 entries
    while len(palette_rgba16) < 16:
        palette_rgba16.append(0)

    def rgba16_to_rgba8(px16):
        r5 = (px16 >> 11) & 0x1F
        g5 = (px16 >> 6)  & 0x1F
        b5 = (px16 >> 1)  & 0x1F
        a1 = px16 & 1
        return (r5 << 3 | r5 >> 2,
                g5 << 3 | g5 >> 2,
                b5 << 3 | b5 >> 2,
                255 if a1 else 0)

    # Extract CI4 nibbles (big-endian: high nibble of each byte = first pixel)
    pixels = []
    for u in ci4_u64s:
        for j in range(16):          # 16 nibbles per u64
            nibble = (u >> (60 - j * 4)) & 0xF
            pixels.append(rgba16_to_rgba8(palette_rgba16[nibble]))
    return pixels

def blender_ci4_to_u64s(ci4_name, pal_name, c_source,
                         target_w=None, target_h=None, orig_w=None, orig_h=None):
    """
    Load a CI4 texture from the Blender fast64 C source, optionally resize,
    and return (u64s, width, height).
    """
    ci4_u64s = parse_c_u64_array(c_source, ci4_name)
    pal_u64s = parse_c_u64_array(c_source, pal_name)
    pixels   = ci4_u64s_to_pixels(ci4_u64s, pal_u64s)

    # If dimensions given, wrap in PIL Image for resize
    if target_w or target_h:
        w = orig_w or int(len(pixels) / (orig_h or 1))
        h = orig_h or int(len(pixels) / w)
        img = Image.new("RGBA", (w, h))
        img.putdata(pixels)
        tw = target_w or w
        th = target_h or h
        img = img.resize((tw, th), Image.Resampling.LANCZOS
                         if hasattr(Image, 'Resampling') else Image.LANCZOS)
        pixels = list(img.getdata())
        return pixels_to_u64s(pixels), tw, th

    return pixels_to_u64s(pixels), None, None  # caller supplies w,h

# ── material DL generator ───────────────────────────────────────────────────────

def rgba16_mat_dl(name, tex_array_name, width, height,
                  wrap_s="G_TX_WRAP | G_TX_NOMIRROR",
                  wrap_t="G_TX_WRAP | G_TX_NOMIRROR",
                  shift_s=0,
                  face_mode=False):
    """
    face_mode=True: use TEX_EDGE render mode + texture alpha in combiner so that
    pixels with A1=0 (background) are discarded, letting the body show through.
    """
    stride        = (width * 2 + 7) // 8
    lrs           = width * height - 1
    bytes_per_row = width * 2
    dxt           = (2048 + bytes_per_row - 1) // bytes_per_row   # ceil(2048/bpr)
    maskW = int(math.log2(width))  if (width  & (width  - 1)) == 0 else 0
    maskH = int(math.log2(height)) if (height & (height - 1)) == 0 else 0
    tW    = (width  - 1) * 4
    tH    = (height - 1) * 4
    return (
        f"Gfx {name}[] = {{\n"
        f"\tgsSPLoadGeometryMode(G_SHADING_SMOOTH | G_SHADE | G_CULL_BACK | G_FOG | G_ZBUFFER),\n"
        f"\tgsDPPipeSync(),\n"
        f"\tgsDPSetCombineLERP(0, 0, 0, TEXEL0, 0, 0, 0, 1, COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED),\n"
        f"\tgsSPSetOtherMode(G_SETOTHERMODE_H, 4, 20, G_TF_BILERP | G_TC_FILT | G_TP_PERSP | G_TT_NONE | G_AD_NOISE | G_PM_NPRIMITIVE | G_CK_NONE | G_TD_CLAMP | G_CYC_2CYCLE | G_CD_MAGICSQ | G_TL_TILE),\n"
        f"\tgsSPSetOtherMode(G_SETOTHERMODE_L, 0, 32, G_RM_FOG_SHADE_A | G_AC_NONE | G_RM_AA_ZB_OPA_SURF2 | G_ZS_PIXEL),\n"
        f"\tgsSPTexture(65535, 65535, 0, 0, 1),\n"
        f"\tgsDPSetPrimColor(0, 0, 255, 255, 255, 255),\n"
        f"\tgsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, {tex_array_name}),\n"
        f"\tgsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, 7, 0, {wrap_t}, 0, 0, {wrap_s}, 0, 0),\n"
        f"\tgsDPLoadSync(),\n"
        f"\tgsDPLoadBlock(7, 0, 0, {lrs}, {dxt}),\n"
        f"\tgsDPPipeSync(),\n"
        f"\tgsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, {stride}, 0, 0, 0, {wrap_t}, {maskH}, 0, {wrap_s}, {maskW}, {shift_s}),\n"
        f"\tgsDPSetTileSize(0, 0, 0, {tW}, {tH}),\n"
        f"\tgsSPEndDisplayList(),\n"
        f"}};\n"
    )

def to_iron(u64s):
    """Desaturate a byte-swapped RGBA16 u64 array to grayscale."""
    out = []
    for v in u64s:
        new_v = 0
        for j in range(4):
            raw  = (v >> (j * 16)) & 0xFFFF
            px   = ((raw & 0xFF) << 8) | ((raw >> 8) & 0xFF)   # undo swap
            r5, g5, b5, a1 = (px >> 11) & 0x1F, (px >> 6) & 0x1F, (px >> 1) & 0x1F, px & 1
            lum  = (r5 * 7 + g5 * 14 + b5 * 5 + 13) // 26
            px16 = (lum << 11) | (lum << 6) | (lum << 1) | a1
            px16 = ((px16 & 0xFF) << 8) | ((px16 >> 8) & 0xFF)  # re-swap
            new_v |= (px16 & 0xFFFF) << (j * 16)
        out.append(new_v)
    return out

# ── generate textures ───────────────────────────────────────────────────────────

MOUTH_W, MOUTH_H = 64, 32   # scale 128x32 mouth → 64x32 (TMEM limit)

generated_arrays = []
generated_mats   = []

# ── Eyes ─────────────────────────────────────────────────────────────────────
EYES_VARIANTS = [
    ("eyes_happy",   "eyes_happy.png"),
    ("eyes_angry",   "eyes_angry.png"),
    ("eyes_closed",  "eyes_closed.png"),
    ("eyes_neutral", "eyes_happy.png"),  # no neutral in SSB64, use happy,
]

# SSB64 Pikachu face background yellow — pixels within 40 colour-units of this
# are made transparent so the correctly-shaded body shows through underneath.
FACE_BG = (240, 208, 48)

eyes_mat_names = []
for vname, fname in EYES_VARIANTS:
    arr_name = f"pika_tex_{vname}"
    mat_name = f"pika_mat_{vname}"
    u64s, w, h = png_to_rgba16_u64s(f"{TEX_FOLDER}/{fname}")
    generated_arrays.append(format_u64_array(arr_name, u64s))
    generated_mats.append(rgba16_mat_dl(mat_name, arr_name, w, h))
    eyes_mat_names.append(mat_name)
    print(f"  eyes {vname}: {w}x{h}")

# ── Mouth ─────────────────────────────────────────────────────────────────────
MOUTH_VARIANTS = [
    ("mouth_happy",            "mouth_happy.png"),
    ("mouth_smile",            "mouth_happy.png"),  # no smile in SSB64, use happy,
    ("mouth_attack",           "mouth_attack.png"),
    ("mouth_attack_charge",    "mouth_attack_charge.png"),
    ("mouth_attack_discharge", "mouth_attack_discharge.png"),
    ("mouth_surprised",        "mouth_surprised.png"),
]

mouth_mat_names = []
for vname, fname in MOUTH_VARIANTS:
    arr_name = f"pika_tex_{vname}"
    mat_name = f"pika_mat_{vname}"
    u64s, w, h = png_to_rgba16_u64s(f"{TEX_FOLDER}/{fname}",
                                    target_w=MOUTH_W, target_h=MOUTH_H)
    generated_arrays.append(format_u64_array(arr_name, u64s))
    generated_mats.append(rgba16_mat_dl(mat_name, arr_name, w, h, shift_s=1))
    mouth_mat_names.append(mat_name)
    print(f"  mouth {vname}: {w}x{h}")

# ── Back ──────────────────────────────────────────────────────────────────────
back_u64s, back_w, back_h = png_to_rgba16_u64s(f"{TEX_FOLDER}/back.png", target_w=64, target_h=32)
generated_arrays.append(format_u64_array("pika_tex_back", back_u64s))
generated_mats.append(rgba16_mat_dl("pika_mat_back", "pika_tex_back", 64, 32))
print(f"  back: {back_w}x{back_h}")

# ── Tail ──────────────────────────────────────────────────────────────────────
tail_u64s, tail_w, tail_h = png_to_rgba16_u64s(f"{TEX_FOLDER}/tail.png", target_w=32, target_h=1)
generated_arrays.append(format_u64_array("pika_tex_tail", tail_u64s))
generated_mats.append(rgba16_mat_dl("pika_mat_tail", "pika_tex_tail", 32, 1))
iron_u64s = to_iron(tail_u64s)
generated_arrays.append(format_u64_array("pika_tex_tail_iron", iron_u64s))
generated_mats.append(rgba16_mat_dl("pika_mat_tail_iron", "pika_tex_tail_iron", 32, 1))
print(f"  tail: {tail_w}x{tail_h} + iron variant")

# ── Yellow body (from Blender CI4 export) ─────────────────────────────────────
# The CI4 data is all-zeros (all pixels → palette[0] = yellow).
# CI4/TLUT loading is unreliable in SOH; convert to a flat RGBA16 texture instead.
with open(FAST64_C, "r", encoding="utf-8") as _f64:
    _fast64_src = _f64.read()
yellow_u64s, _, _ = blender_ci4_to_u64s(
    "pika_001_yellow_ci4", "pika_001_yellow_pal_rgba16", _fast64_src,
    orig_w=8, orig_h=8)
generated_arrays.append(format_u64_array("pika_tex_yellow", yellow_u64s))
# Lit body DL: TEXEL0*SHADE combiner + G_LIGHTING so vertex normals shade correctly.
_stride = (8 * 2 + 7) // 8   # 2
_lrs    = 8 * 8 - 1           # 63
_dxt    = (2048 + 8 * 2 - 1) // (8 * 2)  # 129
generated_mats.append(
    "Gfx mat_pika_001_f3dlite_material_004_layerOpaque[] = {\n"
    "\tgsSPLoadGeometryMode(G_SHADING_SMOOTH | G_LIGHTING | G_SHADE | G_CULL_BACK | G_FOG | G_ZBUFFER),\n"
    "\tgsDPPipeSync(),\n"
    "\tgsDPSetCombineLERP(TEXEL0, 0, SHADE, 0, 0, 0, 0, 1, COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED),\n"
    "\tgsSPSetOtherMode(G_SETOTHERMODE_H, 4, 20, G_TF_BILERP | G_TC_FILT | G_TP_PERSP | G_TT_NONE | G_AD_NOISE | G_PM_NPRIMITIVE | G_CK_NONE | G_TD_CLAMP | G_CYC_2CYCLE | G_CD_MAGICSQ | G_TL_TILE),\n"
    "\tgsSPSetOtherMode(G_SETOTHERMODE_L, 0, 32, G_RM_FOG_SHADE_A | G_AC_NONE | G_RM_AA_ZB_OPA_SURF2 | G_ZS_PIXEL),\n"
    "\tgsSPTexture(65535, 65535, 0, 0, 1),\n"
    "\tgsDPSetPrimColor(0, 0, 255, 255, 255, 255),\n"
    "\tgsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, pika_tex_yellow),\n"
    "\tgsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, 7, 0, G_TX_WRAP | G_TX_NOMIRROR, 0, 0, G_TX_WRAP | G_TX_NOMIRROR, 0, 0),\n"
    "\tgsDPLoadSync(),\n"
    f"\tgsDPLoadBlock(7, 0, 0, {_lrs}, {_dxt}),\n"
    "\tgsDPPipeSync(),\n"
    f"\tgsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, {_stride}, 0, 0, 0, G_TX_WRAP | G_TX_NOMIRROR, 3, 0, G_TX_WRAP | G_TX_NOMIRROR, 3, 0),\n"
    "\tgsDPSetTileSize(0, 0, 0, 28, 28),\n"
    "\tgsSPEndDisplayList(),\n"
    "};\n"
)
print("  yellow: 8x8 RGBA16 (from Blender CI4)")

# ── pointer tables ─────────────────────────────────────────────────────────────

eyes_table = (
    "Gfx* pika_eyes_mats[] = {\n" +
    "".join(f"\t{n},\n" for n in eyes_mat_names) +
    "};\n"
)
mouth_table = (
    "Gfx* pika_mouth_mats[] = {\n" +
    "".join(f"\t{n},\n" for n in mouth_mat_names) +
    "};\n"
)
tail_table = (
    "Gfx* pika_tail_mats[] = {\n"
    "\tpika_mat_tail,\n"
    "\tpika_mat_tail_iron,\n"
    "};\n"
)

# ── helper: patch vertex normals in a named vtx array ──────────────────────────

def patch_vtx_normals(src, vtx_name, nx, ny, nz, alpha=255):
    """
    Within the named Vtx array, replace the per-vertex {r,g,b,a} colour bytes
    with the supplied normal values.  The colour field is the last element of
    each vertex line and ends the vertex entry: ... {r, g, b, a} }},
    """
    start_marker = f"Vtx {vtx_name}["
    start = src.find(start_marker)
    if start == -1:
        print(f"  WARNING: {vtx_name} not found, skipping normal patch")
        return src
    # find the matching closing '};' by tracking brace depth
    pos   = src.find('{', start)
    depth = 0
    end   = pos
    while end < len(src):
        if src[end] == '{':
            depth += 1
        elif src[end] == '}':
            depth -= 1
            if depth == 0:
                end += 2   # include the ';'
                break
        end += 1
    section  = src[start:end]
    patched  = re.sub(
        r'\{\s*(\d+),\s*(\d+),\s*(\d+),\s*255\s*\}\s*\}\}',
        f'{{{nx}, {ny}, {nz}, {alpha}}} }}}}',
        section
    )
    return src[:start] + patched + src[end:]

# ── read & patch pikachu.c ─────────────────────────────────────────────────────

with open(PIKACHU_C, "r", encoding="utf-8", errors="replace") as f:
    content = f.read()

# Match body PRIM colour to the SSB64 PNG yellow (240, 208, 48)
content = content.replace(
    "gsDPSetPrimColor(0, 0, 237, 205, 64, 255)",
    "gsDPSetPrimColor(0, 0, 240, 208, 48, 255)"
)
print("  Patched body PRIM colour -> (240, 208, 48)")

# Remove old CI4/palette arrays
for old_name in [
    "pika_001_eyes_happy_ci4",   "pika_001_eyes_happy_pal_rgba16",
    "pika_001_mouth_happy_ci4",  "pika_001_mouth_happy_pal_rgba16",
    "pika_001_back_ci4",         "pika_001_back_pal_rgba16",
    "pika_001_tail_ci4",         "pika_001_tail_pal_rgba16",
    "pika_001_yellow_ci4",       "pika_001_yellow_pal_rgba16",
]:
    content = re.sub(
        r'u64\s+' + re.escape(old_name) + r'\s*\[\s*\]\s*=\s*\{[^}]*\};\s*\n?',
        '', content, flags=re.DOTALL)

# Remove old mat DLs
for old_mat in [
    "mat_pika_001_eyes_layerOpaque",
    "mat_pika_001_mouth_layerOpaque",
    "mat_pika_001_body_layerOpaque",
    "mat_pika_001_tail_layerOpaque",
    "mat_pika_001_f3dlite_material_004_layerOpaque",
]:
    content = re.sub(
        r'Gfx\s+' + re.escape(old_mat) + r'\s*\[\s*\]\s*=\s*\{[^}]*\};\s*\n?',
        '', content, flags=re.DOTALL)

# Remove any previously generated block to avoid duplicates on re-run
content = re.sub(
    r'\n// ── Pikachu textures \(RGBA16.*?(?=\nVtx\s)',
    '\n',
    content,
    flags=re.DOTALL)

# Insert new block before first Vtx
new_block = (
    "\n// ── Pikachu textures (RGBA16, from Blender CI4 or SSB64 PNG) ─────────\n" +
    "\n".join(generated_arrays) +
    "\n// ── material display lists ────────────────────────────────────────────\n" +
    "\n".join(generated_mats) +
    "\n// ── variant pointer tables ────────────────────────────────────────────\n" +
    eyes_table + "\n" +
    mouth_table + "\n" +
    tail_table + "\n"
)
content = re.sub(r'(Vtx\s+pika_001_pika_001)', new_block + r'\1', content, count=1)

# Patch pika_001_opaque_dl to use segmented face/tail DLs
old_face_dl = (
    "\tgsSPDisplayList(mat_pika_001_eyes_layerOpaque),\n"
    "\tgsSPDisplayList(pika_001_pika_001_mesh_layer_Opaque_tri_1),\n"
    "\tgsSPDisplayList(mat_pika_001_mouth_layerOpaque),\n"
    "\tgsSPDisplayList(pika_001_pika_001_mesh_layer_Opaque_tri_2),\n"
    "\tgsSPDisplayList(mat_pika_001_body_layerOpaque),\n"
    "\tgsSPDisplayList(pika_001_pika_001_mesh_layer_Opaque_tri_3),\n"
    "\tgsSPDisplayList(mat_pika_001_black_layerOpaque),\n"
    "\tgsSPDisplayList(pika_001_pika_001_mesh_layer_Opaque_tri_4),\n"
    "\tgsSPDisplayList(mat_pika_001_tail_layerOpaque),\n"
    "\tgsSPDisplayList(pika_001_pika_001_mesh_layer_Opaque_tri_5),"
)
new_face_dl = (
    "\tgsSPDisplayList(0x08000001),\n"
    "\tgsSPDisplayList(pika_001_pika_001_mesh_layer_Opaque_tri_1),\n"
    "\tgsSPDisplayList(0x09000001),\n"
    "\tgsSPDisplayList(pika_001_pika_001_mesh_layer_Opaque_tri_2),\n"
    "\tgsSPDisplayList(pika_mat_back),\n"
    "\tgsSPDisplayList(pika_001_pika_001_mesh_layer_Opaque_tri_3),\n"
    "\tgsSPDisplayList(mat_pika_001_black_layerOpaque),\n"
    "\tgsSPDisplayList(pika_001_pika_001_mesh_layer_Opaque_tri_4),\n"
    "\tgsSPDisplayList(0x0a000001),\n"
    "\tgsSPDisplayList(pika_001_pika_001_mesh_layer_Opaque_tri_5),"
)
if old_face_dl in content:
    content = content.replace(old_face_dl, new_face_dl)
    print("  Patched pika_001_opaque_dl")
else:
    print("  WARNING: could not find face DL block to patch")

with open(PIKACHU_C, "w", encoding="utf-8") as f:
    f.write(content)

print("\npikachu.c updated.")
print(f"  Eyes variants:  {len(eyes_mat_names)}")
print(f"  Mouth variants: {len(mouth_mat_names)}")
print("  Tail variants:  2 (normal, iron)")
