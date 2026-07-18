#ifndef BLE_VOLUMETRIC_OVERRIDE_POLICY_H
#define BLE_VOLUMETRIC_OVERRIDE_POLICY_H

// BLE may keep volumetric availability true only while this plugin is active
// and its scale remains connected. Teardown makes both conditions false.
constexpr bool shouldEnableBleVolumetricOverride(bool pluginActive, bool scaleConnected) {
    return pluginActive && scaleConnected;
}

#endif // BLE_VOLUMETRIC_OVERRIDE_POLICY_H
