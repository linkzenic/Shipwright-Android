#pragma once

// NEI: SwitchAge() is called from C mod TUs (item_time_gate.c) which expect C linkage
// (the contract the old mods.h provided before upstream #6677 split it out). Keep it
// extern "C" so the C caller links; C++ callers (OcarinaTimeTravel, menu) are unaffected.
#ifdef __cplusplus
extern "C" {
#endif

void SwitchAge();

#ifdef __cplusplus
}
#endif