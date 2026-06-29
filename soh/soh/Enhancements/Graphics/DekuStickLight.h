#pragma once

// Identifies the held Deku stick's dynamic light to the light-casting pass so it can give the stick its
// own cast-pool size (separate from torches). Returns the stick's LightInfo* (as void* to avoid pulling
// the C light headers into this header) while the light is active in the scene, otherwise nullptr.
// See DekuStickLight.cpp.
void* DekuStickLight_GetActiveLightInfo();
