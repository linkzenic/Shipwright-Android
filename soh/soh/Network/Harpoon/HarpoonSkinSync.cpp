#include "HarpoonSkinSync.h"
#include "soh/Notification/Notification.h"
#include "soh/Enhancements/mod_menu.h"

#include <string>
#include <unordered_set>
#include <set>

namespace HarpoonSkinSync {

// Dedupe key: "<kind>:<clientId>:<name>" — kind in {missing, divR, divL}.
static std::unordered_set<std::string> sNotified;

static bool ShouldNotify(const std::string& key) {
    if (sNotified.count(key)) return false;
    sNotified.insert(key);
    return true;
}

void NotifyMissingPak(uint32_t clientId, const std::string& playerName, const std::string& skinName) {
    if (skinName.empty()) return;
    std::string key = "missing:" + std::to_string(clientId) + ":" + skinName;
    if (!ShouldNotify(key)) return;

    Notification::Emit({
        .prefix = playerName,
        .prefixColor = ImVec4(0.7f, 0.9f, 1.0f, 1.0f),
        .message = "uses skin",
        .suffix = "'" + skinName + "' (not installed)",
        .suffixColor = ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
        .remainingTime = 6.0f,
        .mute = true,
    });
}

void NotifyO2rDivergence(uint32_t clientId, const std::string& playerName,
                         const std::vector<std::string>& remoteMods) {
    const auto& localMods = ModMenu_GetEnabledMods();
    std::set<std::string> localSet(localMods.begin(), localMods.end());
    std::set<std::string> remoteSet(remoteMods.begin(), remoteMods.end());

    for (const auto& m : remoteSet) {
        if (localSet.count(m)) continue;
        std::string key = "divR:" + std::to_string(clientId) + ":" + m;
        if (!ShouldNotify(key)) continue;
        Notification::Emit({
            .prefix = playerName,
            .prefixColor = ImVec4(0.7f, 0.9f, 1.0f, 1.0f),
            .message = "has mod",
            .suffix = "'" + m + "' that you don't — visuals may differ",
            .suffixColor = ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
            .remainingTime = 6.0f,
            .mute = true,
        });
    }

    for (const auto& m : localSet) {
        if (remoteSet.count(m)) continue;
        std::string key = "divL:" + std::to_string(clientId) + ":" + m;
        if (!ShouldNotify(key)) continue;
        Notification::Emit({
            .prefix = "You",
            .prefixColor = ImVec4(0.7f, 0.9f, 1.0f, 1.0f),
            .message = "have mod",
            .suffix = "'" + m + "' that " + playerName + " doesn't",
            .suffixColor = ImVec4(0.7f, 0.8f, 1.0f, 1.0f),
            .remainingTime = 6.0f,
            .mute = true,
        });
    }
}

void Reset() {
    sNotified.clear();
}

} // namespace HarpoonSkinSync
