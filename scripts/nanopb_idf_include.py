#
# PlatformIO pre-build hook: keep nanopb's runtime include path visible under the
# ESP-IDF/CMake build model (PRO-358).
#
# WHY THIS EXISTS
# ---------------
# PRO-358 sets `custom_sdkconfig` on [env:display] / [env:display-headless] to
# flip NimBLE's allocator to PSRAM (CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y).
# Setting custom_sdkconfig makes the pioarduino platform build the WHOLE project
# via the "Arduino as ESP-IDF component" / CMake model (builder/frameworks/
# espidf.py) instead of the plain-SCons Arduino model. Under that model:
#
#   * `build_src_filter` is ignored (espidf.py prints the "src_filter cannot be
#     used with ESP-IDF" warning) and src/ is glob-compiled by CMake as a
#     component -- so NOTHING in the SCons library graph #includes <pb.h>.
#   * PlatformIO's Library Dependency Finder therefore reports "No dependencies"
#     for the Nanopb lib_deps entry and never propagates the Nanopb library ROOT
#     (where pb.h lives) onto CPPPATH.
#   * nanopb's own bundled generator (platformio_generator.py) still runs; it
#     generates comms.pb.{c,h} into $BUILD_DIR/nanopb/generated-src, appends that
#     dir to CPPPATH, and BuildSources()es the generated comms.pb.c. But that
#     generated comms.pb.c does `#include <pb.h>`, and pb.h lives at the Nanopb
#     library ROOT (not in generated-src) -- so the compile fails with
#     "fatal error: pb.h: No such file or directory".
#
# The Nanopb RUNTIME (pb_encode/pb_decode/pb_common .c) is still compiled + linked
# for us -- PlatformIO's soft-compatibility LDF builds every compatible library
# regardless of the "No dependencies" line. So the ONLY thing missing under the
# IDF model is the include path for <pb.h>. We must NOT re-add the runtime
# sources ourselves or they collide (multiple definition of pb_encode/...).
#
# THE FIX (least-invasive, IDF-model-only)
# ----------------------------------------
# Put the Nanopb library root on CPPPATH so <pb.h> (and pb_encode.h/pb_decode.h/
# pb_common.h) resolve for the generated comms.pb.c. This ONLY runs when
# custom_sdkconfig is in effect (i.e. the IDF build model); the plain Arduino
# build model and the host native/sim envs are untouched -- there the LDF wires
# the Nanopb include path up the normal way.
#
# Ref PRO-358.
#
import os

Import("env")  # noqa: F821 -- provided by PlatformIO/SCons


def _is_idf_build_model():
    # custom_sdkconfig (env-level or board-level espidf.custom_sdkconfig) is what
    # flips the pioarduino platform into the ESP-IDF/CMake build model. Only then
    # does the LDF stop propagating the Nanopb include path, so only then do we
    # need to step in.
    try:
        if env.GetProjectOption("custom_sdkconfig", ""):  # noqa: F821
            return True
    except Exception:
        pass
    board_cfg = env.BoardConfig()  # noqa: F821
    return bool(board_cfg.get("espidf.custom_sdkconfig", ""))


def _nanopb_root():
    # Nanopb is installed per-env under $PROJECT_LIBDEPS_DIR/$PIOENV/Nanopb.
    libdeps_dir = env.subst("$PROJECT_LIBDEPS_DIR")  # noqa: F821
    pioenv = env.subst("$PIOENV")  # noqa: F821
    candidate = os.path.join(libdeps_dir, pioenv, "Nanopb")
    if os.path.isfile(os.path.join(candidate, "pb.h")):
        return candidate
    return None


if not _is_idf_build_model():
    # Plain Arduino / native / sim model -> the LDF handles the include path.
    pass
else:
    nanopb_root = _nanopb_root()
    if not nanopb_root:
        print("[nanopb-idf] WARNING: could not locate the Nanopb library root; "
              "leaving the include path to the LDF.")
    else:
        # Make <pb.h> resolve for the generated comms.pb.c that nanopb's generator
        # BuildSources()es. The runtime .c is already compiled + linked by the LDF,
        # so we deliberately do NOT rebuild it here (that would duplicate symbols).
        env.Append(CPPPATH=[nanopb_root])  # noqa: F821
        print("[nanopb-idf] added Nanopb include path for IDF build model: %s" % nanopb_root)
