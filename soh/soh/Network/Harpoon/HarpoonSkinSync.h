#pragma once

#ifdef __cplusplus

#include <string>
#include <vector>

namespace HarpoonSkinSync {

// Warn once per session when a remote player reports a pak skin name that
// we can't resolve locally (not present in mods/harpoon_skin_sync/).
void NotifyMissingPak(uint32_t clientId, const std::string& playerName, const std::string& skinName);

// Compare our enabled .o2r mod list against a remote's and emit one
// divergence notification per unique (clientId, direction, modName) tuple.
void NotifyO2rDivergence(uint32_t clientId, const std::string& playerName,
                         const std::vector<std::string>& remoteMods);

// Clear the dedupe cache (call on disconnect / fresh connect).
void Reset();

} // namespace HarpoonSkinSync

#endif // __cplusplus
