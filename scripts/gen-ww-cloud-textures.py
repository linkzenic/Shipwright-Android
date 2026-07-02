#!/usr/bin/env python3
"""Generates the built-in Wind Waker-STYLE cloud textures (original art, no Nintendo content).

Outputs the five RGBA32 PNGs under soh/assets/custom/textures/wind-waker/clouds/, which the build
packs into soh.o2r (resource paths textures/wind-waker/clouds/*). The drifting-cloud sprites are a
max-union of soft ellipses (wide flat core + a crown of bumps) with a slate underside; the horizon
band strips are periodic-in-X rows of clustered puffs (gappy front "mae" layer, continuous back
"naka" layer). Deterministic: fixed LCG seeds, same output every run.

Requires Pillow: python3 -m pip install pillow
"""
import math
import os
from PIL import Image

OUT = os.path.join(os.path.dirname(__file__), "..", "soh", "assets", "custom", "textures", "wind-waker", "clouds")
SPRITE = 64
BAND_W, BAND_H = 256, 64


class Rng:
    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF

    def next01(self):
        self.s = (self.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return (self.s >> 8) / 16777216.0

    def range(self, a, b):
        return a + (b - a) * self.next01()


def smoothstep(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3.0 - 2.0 * t)


def shade_px(alpha, t):
    """White-to-slate shading; t = shadow amount 0..1."""
    return (round(246 - t * (246 - 148)), round(248 - t * (248 - 176)), round(250 - t * (250 - 205)),
            round(alpha * 255))


def gen_sprite(seed):
    rng = Rng(seed)
    blobs = [(32.0, 38.0, 17.0, 9.0)]  # wide flat core
    for k in range(4):  # crown bumps along the top, overlapping the core
        u = (k + 0.5) / 4.0 + rng.range(-0.05, 0.05)
        r = rng.range(7.0, 11.0)
        blobs.append((14.0 + 36.0 * u, 36.0 - math.sin(u * math.pi) * rng.range(3.0, 7.0), r, r * 0.85))
    img = Image.new("RGBA", (SPRITE, SPRITE))
    px = img.load()
    for y in range(SPRITE):
        for x in range(SPRITE):
            alpha = 0.0
            interior = 0.0
            for cx, cy, rx, ry in blobs:
                dn = math.hypot((x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry)
                alpha = max(alpha, smoothstep(1.0, 0.80, dn))
                if dn < 1.0:
                    interior += 1.0 - dn
            t = smoothstep(0.48, 0.80, y / SPRITE) * 0.62
            t *= 1.0 - 0.35 * smoothstep(1.0, 2.2, interior)
            px[x, y] = shade_px(alpha, t)
    return img


def band_kernel(d2):
    if d2 >= 1.0:
        return 0.0
    u = 1.0 - d2
    return u * u


def gen_band(seed, clusters, per_cluster, tallness, floor_fill):
    rng = Rng(seed)
    blobs = []
    for c in range(clusters):
        base = (c + rng.range(0.1, 0.9)) * BAND_W / clusters
        for _ in range(per_cluster):
            cx = base + rng.range(-26.0, 26.0)
            rx = rng.range(13.0, 26.0)
            ry = rng.range(8.0, 13.0) * tallness
            cy = BAND_H - rng.range(0.0, 8.0) - ry * 0.30
            blobs.append((cx % BAND_W, cy, rx, ry))
    img = Image.new("RGBA", (BAND_W, BAND_H))
    px = img.load()
    for y in range(BAND_H):
        for x in range(BAND_W):
            f = 0.0
            for cx, cy, rx, ry in blobs:
                dx = x + 0.5 - cx
                dx -= BAND_W * round(dx / BAND_W)  # periodic wrap in X
                nx, ny = dx / rx, (y + 0.5 - cy) / ry
                f += band_kernel(nx * nx + ny * ny)
            if floor_fill:
                f += 1.5 * smoothstep(BAND_H - 12.0, BAND_H + 3.0, y + 0.5)
            alpha = smoothstep(0.40, 0.75, f)
            t = smoothstep(0.50, 0.96, y / BAND_H) * 0.55
            t *= 1.0 - 0.40 * smoothstep(1.5, 3.0, f)
            px[x, y] = shade_px(alpha, t)
    return img


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for i, seed in enumerate([0x1A2B3C4D, 0x2B3C4D5E, 0x3C4D5E6F]):
        path = os.path.join(OUT, f"cloudtx_{i+1:02d}.rgba32.png")
        gen_sprite(seed).save(path)
        print("wrote", path)
    p = os.path.join(OUT, "cloud_mae.rgba32.png")
    gen_band(0x5E6F7081, 4, 4, 1.0, False).save(p)  # front strip: scattered clusters
    print("wrote", p)
    p = os.path.join(OUT, "cloud_naka.rgba32.png")
    gen_band(0x4D5E6F70, 5, 5, 1.5, True).save(p)  # back strip: continuous mass
    print("wrote", p)
