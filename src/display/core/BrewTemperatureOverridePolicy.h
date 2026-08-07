#ifndef BREW_TEMPERATURE_OVERRIDE_POLICY_H
#define BREW_TEMPERATURE_OVERRIDE_POLICY_H

#include <cmath>
#include <string>

struct BrewTemperatureOverrideState {
    std::string profileId;
    float temperature = 0.0f;
    bool enabled = false;

    bool appliesTo(const std::string &selectedProfileId) const { return enabled && profileId == selectedProfileId; }
    void clear() {
        profileId.clear();
        temperature = 0.0f;
        enabled = false;
    }
};

enum class BrewTemperatureTargetUpdate { PASSIVE_REASSERT, EXPLICIT_EDIT };

inline bool shouldClearBrewTemperatureOverrideOnProfileSelection(const std::string &currentProfileId,
                                                                 const std::string &requestedProfileId) {
    return currentProfileId != requestedProfileId;
}

constexpr bool shouldPersistBrewTemperatureOverride(BrewTemperatureTargetUpdate update) {
    return update == BrewTemperatureTargetUpdate::EXPLICIT_EDIT;
}

inline float effectiveBrewTemperature(float profileTemperature, const BrewTemperatureOverrideState &overrideState,
                                      const std::string &selectedProfileId) {
    return overrideState.appliesTo(selectedProfileId) ? overrideState.temperature : profileTemperature;
}

inline bool isValidBrewTemperatureOverride(float temperature) {
    return std::isfinite(temperature) && temperature >= 0.0f && temperature <= 160.0f;
}

#endif // BREW_TEMPERATURE_OVERRIDE_POLICY_H
