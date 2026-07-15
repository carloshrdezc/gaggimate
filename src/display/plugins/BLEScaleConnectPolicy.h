#pragma once

#include <string_view>

constexpr bool isValidBleScaleConnectUuid(std::string_view uuid) { return !uuid.empty(); }
