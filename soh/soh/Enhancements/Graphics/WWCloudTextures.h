// Built-in procedural cloud textures for the Wind Waker-style clouds (WWClouds.cpp).
//
// These are original, generated look-alikes — NOT Wind Waker's art — so the mod can ship and run with no
// Nintendo assets at all. If a mods o2r (e.g. a WW-themed texture pack) provides textures under
// textures/wind-waker/clouds/*, those override these (see WWClouds.cpp).

#pragma once

#include <cstdint>

struct WWCloudTexture {
    const uint8_t* data; // RGBA32
    int width;
    int height;
};

WWCloudTexture WWCloudTex_Sprite(int idx); // 0..2: the drifting puffy-cloud sprites
WWCloudTexture WWCloudTex_Band(int idx);   // 0 = mae (front strip), 1 = naka (back strip)
