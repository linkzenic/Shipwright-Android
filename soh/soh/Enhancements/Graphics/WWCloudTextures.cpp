// Procedurally generated Wind Waker-STYLE cloud textures (original art, no Nintendo content).
//
// The drifting-cloud sprites are a max-union of soft ellipses — a wide flat core with a crown of bumps
// along the top — with a slate underside shade, mimicking the lumpy hand-drawn puffs WW uses. The horizon
// band strips are rows of wide puffs hugging the bottom edge, periodic in X so they tile seamlessly around
// the horizon ring. Everything is deterministic (fixed LCG seeds), generated once on first use.
//
// A texture pack can replace these at runtime by shipping the textures/wind-waker/clouds/* resources in a
// mods o2r (that is also how users who extract WW's own art plug it in) — see WWClouds.cpp.

#include "WWCloudTextures.h"

#include <math.h>

static constexpr int kSpriteSize = 64;
static constexpr int kBandW = 256;
static constexpr int kBandH = 64;

// Local deterministic RNG (same recurrence as WWClouds.cpp's; never touches the game's RNG).
struct ProcRng {
    uint32_t s;
    float Next01() {
        s = s * 1664525u + 1013904223u;
        return (float)(s >> 8) * (1.0f / 16777216.0f);
    }
    float Range(float a, float b) {
        return a + (b - a) * Next01();
    }
};

static float SmoothStep(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

struct Blob {
    float cx, cy, rx, ry;
};

// White-to-slate shading shared by both texture kinds; t = shadow amount 0..1.
static void ShadePx(uint8_t* px, float alpha, float t) {
    px[0] = (uint8_t)(246.0f - t * (246.0f - 148.0f));
    px[1] = (uint8_t)(248.0f - t * (248.0f - 176.0f));
    px[2] = (uint8_t)(250.0f - t * (250.0f - 205.0f));
    px[3] = (uint8_t)(alpha * 255.0f);
}

// ---------------------------------------------------------------------------------------------------
// Puffy sprites: max-union of soft ellipses (guaranteed connected), crown bumps along the top arc.
// ---------------------------------------------------------------------------------------------------

static void GenSprite(uint32_t seed, uint8_t* out) {
    ProcRng rng{ seed };
    Blob blobs[5];
    int n = 0;
    blobs[n++] = { 32.0f, 38.0f, 17.0f, 9.0f }; // wide flat core
    for (int k = 0; k < 4; k++) {
        float u = (k + 0.5f) / 4.0f + rng.Range(-0.05f, 0.05f);
        float r = rng.Range(7.0f, 11.0f);
        blobs[n++] = { 14.0f + 36.0f * u, 36.0f - sinf(u * (float)M_PI) * rng.Range(3.0f, 7.0f), r, r * 0.85f };
    }
    for (int y = 0; y < kSpriteSize; y++) {
        for (int x = 0; x < kSpriteSize; x++) {
            float alpha = 0.0f, interior = 0.0f;
            for (int b = 0; b < n; b++) {
                float dx = (x + 0.5f - blobs[b].cx) / blobs[b].rx;
                float dy = (y + 0.5f - blobs[b].cy) / blobs[b].ry;
                float dn = sqrtf(dx * dx + dy * dy);
                float a = SmoothStep(1.0f, 0.80f, dn);
                if (a > alpha) {
                    alpha = a;
                }
                if (dn < 1.0f) {
                    interior += 1.0f - dn;
                }
            }
            float t = SmoothStep(0.48f, 0.80f, (float)y / kSpriteSize) * 0.62f;
            t *= 1.0f - 0.35f * SmoothStep(1.0f, 2.2f, interior);
            ShadePx(&out[(y * kSpriteSize + x) * 4], alpha, t);
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// Horizon band strips: clustered wide puffs on the bottom edge, metaball-summed, periodic in X.
// ---------------------------------------------------------------------------------------------------

static float BandKernel(float d2) {
    if (d2 >= 1.0f) {
        return 0.0f;
    }
    float u = 1.0f - d2;
    return u * u;
}

static void GenBand(uint32_t seed, int clusters, int perCluster, float tallness, bool floorFill, uint8_t* out) {
    ProcRng rng{ seed };
    Blob blobs[32];
    int n = 0;
    for (int c = 0; c < clusters; c++) {
        float base = (c + rng.Range(0.1f, 0.9f)) * (float)kBandW / clusters;
        for (int k = 0; k < perCluster && n < 32; k++) {
            float cx = base + rng.Range(-26.0f, 26.0f);
            float rx = rng.Range(13.0f, 26.0f);
            float ry = rng.Range(8.0f, 13.0f) * tallness;
            float cy = kBandH - rng.Range(0.0f, 8.0f) - ry * 0.30f;
            blobs[n++] = { cx - kBandW * floorf(cx / kBandW), cy, rx, ry };
        }
    }
    for (int y = 0; y < kBandH; y++) {
        for (int x = 0; x < kBandW; x++) {
            float f = 0.0f;
            for (int b = 0; b < n; b++) {
                float dx = x + 0.5f - blobs[b].cx;
                dx -= kBandW * roundf(dx / kBandW); // periodic wrap in X
                float dy = y + 0.5f - blobs[b].cy;
                float nx = dx / blobs[b].rx, ny = dy / blobs[b].ry;
                f += BandKernel(nx * nx + ny * ny);
            }
            if (floorFill) {
                // continuous mass along the bottom edge (the naka/back strip)
                f += 1.5f * SmoothStep(kBandH - 12.0f, kBandH + 3.0f, y + 0.5f);
            }
            float alpha = SmoothStep(0.40f, 0.75f, f);
            float t = SmoothStep(0.50f, 0.96f, (float)y / kBandH) * 0.55f;
            t *= 1.0f - 0.40f * SmoothStep(1.5f, 3.0f, f);
            ShadePx(&out[(y * kBandW + x) * 4], alpha, t);
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// Lazy singletons
// ---------------------------------------------------------------------------------------------------

WWCloudTexture WWCloudTex_Sprite(int idx) {
    static uint8_t sData[3][kSpriteSize * kSpriteSize * 4];
    static bool sDone = false;
    if (!sDone) {
        const uint32_t seeds[3] = { 0x1a2b3c4du, 0x2b3c4d5eu, 0x3c4d5e6fu };
        for (int i = 0; i < 3; i++) {
            GenSprite(seeds[i], sData[i]);
        }
        sDone = true;
    }
    return { sData[idx], kSpriteSize, kSpriteSize };
}

WWCloudTexture WWCloudTex_Band(int idx) {
    static uint8_t sData[2][kBandW * kBandH * 4];
    static bool sDone = false;
    if (!sDone) {
        GenBand(0x5e6f7081u, 4, 4, 1.0f, false, sData[0]); // mae: scattered clusters, gaps between
        GenBand(0x4d5e6f70u, 5, 5, 1.5f, true, sData[1]);  // naka: taller, continuous along the bottom
        sDone = true;
    }
    return { sData[idx], kBandW, kBandH };
}
