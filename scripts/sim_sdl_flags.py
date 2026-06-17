# SDL + socket build flags for the desktop simulator ([env:display-sim]), made
# cross-platform. [CAR-399]
#
# macOS / Linux: use `sdl2-config` (installed by `brew install sdl2` /
#   `apt install libsdl2-dev`), exactly as upstream did.
# Windows (MinGW): there is no `sdl2-config`. Point the build at an SDL2 MinGW
#   development package via the SDL2_DIR environment variable (the directory that
#   contains include/ and lib/, e.g. the extracted
#   SDL2-devel-<ver>-mingw/x86_64-w64-mingw32). Link order matters on Windows:
#   -lmingw32 -lSDL2main -lSDL2, plus -lws2_32 for the Winsock-based web server.
#   SDL_MAIN_HANDLED keeps SDL from redefining main() to SDL_main.
import os
import platform
import subprocess

Import("env")

# The sim include paths must be added here (not via ${PROJECT_DIR} in
# platformio.ini): on the native platform ${PROJECT_DIR} expands to a mangled
# builder-relative path on Windows, so the -I sim/* dirs never resolve and the
# Arduino.h shim isn't found. env["PROJECT_DIR"] is the real absolute path.
proj = env["PROJECT_DIR"]
env.Append(CPPPATH=[
    os.path.join(proj, "sim", "platform"),
    os.path.join(proj, "sim", "platform", "arduino"),
    os.path.join(proj, "sim", "comms"),
    os.path.join(proj, "sim", "driver"),
    os.path.join(proj, "sim", "web"),
    os.path.join(proj, "src"),
])

system = platform.system()

if system == "Windows":
    sdl_dir = os.environ.get("SDL2_DIR")
    if not sdl_dir:
        # A sensible default for this machine; override with SDL2_DIR if elsewhere.
        default = os.path.expanduser(r"~\SDL2")
        if os.path.isdir(os.path.join(default, "include")):
            sdl_dir = default
    if not sdl_dir or not os.path.isdir(os.path.join(sdl_dir, "include")):
        raise SystemExit(
            "[display-sim] SDL2 not found. Set SDL2_DIR to an SDL2 MinGW devel "
            "package (the dir containing include/ and lib/). See sim/README.md."
        )

    inc = os.path.join(sdl_dir, "include")
    inc_sdl2 = os.path.join(inc, "SDL2")
    lib = os.path.join(sdl_dir, "lib")

    # SDL_MAIN_HANDLED: we own main() (sim/main.cpp), so don't link SDL2main and
    # don't let SDL #define main->SDL_main. Libraries go in LIBS (not LINKFLAGS)
    # so SCons emits them AFTER the object files — static link order is
    # significant and -lSDL2/-lws2_32 must follow the .o that reference them.
    env.Append(
        CPPPATH=[inc, inc_sdl2],
        CPPDEFINES=["SDL_MAIN_HANDLED"],
        LIBPATH=[lib],
        LIBS=["mingw32", "SDL2", "ws2_32", "imm32", "version", "winmm", "setupapi", "ole32", "oleaut32", "gdi32", "user32"],
        LINKFLAGS=["-mconsole"],
    )
    print("[display-sim] SDL2 from %s (SDL_MAIN_HANDLED, ws2_32 Winsock)" % sdl_dir)
else:
    # macOS / Linux: defer to sdl2-config like upstream.
    try:
        cflags = subprocess.check_output(["sdl2-config", "--cflags"]).decode().split()
        libs = subprocess.check_output(["sdl2-config", "--libs"]).decode().split()
    except Exception as exc:  # noqa: BLE001
        raise SystemExit(
            "[display-sim] sdl2-config not found (install SDL2: brew install sdl2 / "
            "apt install libsdl2-dev). %s" % exc
        )
    env.Append(CCFLAGS=cflags, LINKFLAGS=libs)
    print("[display-sim] SDL2 via sdl2-config")
