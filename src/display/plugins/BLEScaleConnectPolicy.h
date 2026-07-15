#pragma once

#include <string_view>

// PRO-522: single source of truth for the BLE-scale connect-UUID validity
// predicate. A UUID is valid when non-empty; this weeds out misconfigured
// device entries before attempting a connection. Lives here (host-includable)
// rather than inline in BLEScalePlugin so host unit tests can exercise the
// predicate without pulling in BLE includes.
constexpr bool isValidBleScaleConnectUuid(std::string_view uuid) { return !uuid.empty(); }
