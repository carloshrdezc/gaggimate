# Include paths for the host static-analysis env ([env:native-tidy], PRO-608).
#
# These MUST be injected from an extra_script rather than written as `-I` entries
# in platformio.ini's build_flags: on the `native` platform ${PROJECT_DIR}
# expands to a mangled, builder-relative path (notably on Windows), so
# `-I ${PROJECT_DIR}/sim/platform` never resolves and the Arduino.h shim isn't
# found. env["PROJECT_DIR"] is the real absolute path. This is the same
# constraint scripts/sim_sdl_flags.py documents for [env:display-sim].
#
# Deliberately a strict subset of sim_sdl_flags.py's CPPPATH: no SDL2 include
# dirs and no SDL2 link flags. This env never links and never opens a window; it
# only needs the shim headers so src/display/{core,plugins} translation units
# parse on the host. Keeping SDL2 out means the CI analysis job does not need
# libsdl2-dev (that stays a `simulator` job dependency). sim/driver IS on the
# path because Controller.cpp's `#ifdef GAGGIMATE_SIM` branch includes
# <SdlDriver.h>; that header itself pulls in no SDL2 headers, so it parses fine
# without the SDL2 dev package.
import os

Import("env")  # noqa: F821  (SCons injects this)

proj = env["PROJECT_DIR"]
env.Append(
    CPPPATH=[
        os.path.join(proj, "sim", "platform"),
        os.path.join(proj, "sim", "platform", "arduino"),
        os.path.join(proj, "sim", "comms"),
        os.path.join(proj, "sim", "web"),
        os.path.join(proj, "sim", "driver"),
        os.path.join(proj, "src"),
    ]
)
print("[native-tidy] host analysis include paths injected (sim shims, no SDL2)")
