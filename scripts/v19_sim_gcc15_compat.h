/* PRO-653: out-of-tree compat header for building UNMODIFIED upstream v1.9.0 `display-sim` on a host with
 * GCC 15 / libstdc++ 15, which no longer pull these headers in transitively. Used only by
 * scripts/v19_baseline.sh via SIM_COMPAT_HEADER (PLATFORMIO_BUILD_FLAGS="-include <this file>").
 *   <stdarg.h>  sim/platform/arduino/Print.cpp        va_start / va_copy / va_end
 *   <stdexcept> src/display/core/utils.h              std::runtime_error
 *   <memory>    src/display/plugins/BLEScalePlugin.h  std::unique_ptr
 * The real fix is adding those includes at the use sites during the simulator slice. */
#ifndef __ASSEMBLER__
#include <stdarg.h>
#ifdef __cplusplus
#include <memory>
#include <stdexcept>
#endif
#endif
