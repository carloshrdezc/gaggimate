// Static-analysis seam aggregation translation unit (PRO-608).
//
// WHY THIS FILE EXISTS
// The `*Policy.h` seam headers under src/display/{core,plugins} are header-only:
// every one is a pure, host-testable extraction of a decision that used to be
// buried inside a plugin. They are #included by the test/test_*/ suites (which
// exercise them) and, in a few cases, by a production .cpp. But clang-tidy only
// diagnoses a header when some translation unit IN ITS COMPILE DATABASE includes
// it — `HeaderFilterRegex` filters, it does not discover. Before PRO-608 the
// clang-tidy compile DB held three files, none of which reached any policy
// header, so all ~32 of them were silently unanalysed.
//
// This TU is compiled into [env:native-tidy]'s compile database (and nothing
// else) purely so those headers land in a TU that clang-tidy walks. It emits no
// code, defines no symbols, and is never linked.
//
// WHY NOT JUST COMPILE test/test_*/ INTO THE ANALYSIS DB?
// Those suites #include <unity.h>, which PlatformIO only unpacks when running
// `pio test`. Off a `pio run -t compiledb` database clang-tidy reports 41
// `clang-diagnostic-error: 'unity.h' file not found` and exits non-zero, which
// would flip this intentionally non-gating CI step into a hard failure.
//
// MAINTENANCE
// scripts/test_select_tidy_sources.py::SeamHeaderCoverage asserts this file
// includes EVERY `*Policy.h` under src/display/ (recursively), and that every
// `*Policy.h` include here resolves to a file that exists. Add a policy header,
// add a line here — the test tells you if you forget. Keep the list alphabetical
// within each directory group.

// src/display/core/
#include <display/core/MbedtlsPsramAllocatorPolicy.h>
#include <display/core/MdnsNamePolicy.h>
#include <display/core/StandbyTransitionPolicy.h>
#include <display/core/SteamButtonPolicy.h>

// src/display/core/process/
#include <display/core/process/GlobalWeightCutoffPolicy.h>

// src/display/plugins/
#include <display/plugins/ActiveShotFillPolicy.h>
#include <display/plugins/BLEScaleConnectPolicy.h>
#include <display/plugins/BLEScaleMeasurementPolicy.h>
#include <display/plugins/BLEScaleScanPolicy.h>
#include <display/plugins/BLEVolumetricOverridePolicy.h>
#include <display/plugins/BeanResolutionPolicy.h>
#include <display/plugins/ChangeModeDeferPolicy.h>
#include <display/plugins/ExtendedRecordingPolicy.h>
#include <display/plugins/LocalAuthPolicy.h>
#include <display/plugins/MqttConnectPolicy.h>
#include <display/plugins/OtaAsyncResolvePolicy.h>
#include <display/plugins/OtaChannelSwitchPolicy.h>
#include <display/plugins/OtaResolveHeapPolicy.h>
#include <display/plugins/OtaResolveReusePolicy.h>
#include <display/plugins/OtaUpdateCheckPolicy.h>
#include <display/plugins/PathTraversalPolicy.h>
#include <display/plugins/PostStopGracePolicy.h>
#include <display/plugins/RelayConnectionPolicy.h>
#include <display/plugins/ShotIndexMetadataPolicy.h>
#include <display/plugins/ShotNotesPersistencePolicy.h>
#include <display/plugins/StandbyReassertPolicy.h>
#include <display/plugins/StrictValidationPolicy.h>
#include <display/plugins/VolumetricSourcePolicy.h>
#include <display/plugins/WebUiLifecycleDeferPolicy.h>
#include <display/plugins/WsBroadcastClosePolicy.h>
#include <display/plugins/WsReassemblyPolicy.h>

// src/display/ui/default/ — the one policy header outside core/plugins. Note
// .clang-tidy's HeaderFilterRegex does NOT match src/display/ui/, so this
// include exists for completeness of the seam set rather than for diagnostics.
#include <display/ui/default/DisplayRestartPolicy.h>

// Other header-only seams that carry logic but are not named *Policy.h.
#include <display/core/SettingsPersistenceMutexInitialization.h>
#include <display/core/SettingsPersistenceTransaction.h>
#include <display/core/VolumetricCoalescer.h>
#include <display/core/VolumetricMeasurementSource.h>
#include <display/models/profile.h>
#include <display/models/shot_log_format.h>
#include <display/plugins/DiagLogFormat.h>
#include <display/plugins/OtaIntentState.h>

// Nothing to define. `inline` keeps this out of any hypothetical link.
namespace gaggimate_tidy_seams {
inline void anchor() {}
} // namespace gaggimate_tidy_seams
