# Adds "run" and "smoke" custom targets to the display-sim env so the desktop
# simulator can be built and launched directly (CLI: `pio run -e display-sim -t run`,
# or as a clickable task under the env in the PlatformIO IDE tool window). [GM-107]
# `-t smoke` builds the sim and runs scripts/sim_smoke.py, the scripted per-slice
# migration gate (boot + embedded WebUI + WS brew against MockController). [PRO-656]
import os

Import("env")

program = "$BUILD_DIR/${PROGNAME}${PROGSUFFIX}"

env.AddCustomTarget(
    name="run",
    dependencies=[program],  # build the simulator first
    actions=[program],       # then launch it
    title="Run Simulator",
    description="Build and launch the GaggiMate desktop simulator",
)

env.AddCustomTarget(
    name="smoke",
    dependencies=[program],  # build the simulator first
    actions=['"$PYTHONEXE" "%s" --binary "%s"' % (os.path.join("$PROJECT_DIR", "scripts", "sim_smoke.py"), program)],
    title="Simulator Smoke Test",
    description="Build the simulator, then boot it headless and run scripts/sim_smoke.py",
)
