#!/usr/bin/env bash

# Note: `find src lib ...` emits paths WITHOUT a leading `./`, and `find -path`
# uses shell-glob `*` (which spans `/`), not `**`. The exclusion patterns must
# therefore match the bare `src/display/...` form (no `./` prefix, single `*`),
# otherwise generated LVGL UI (src/display/ui/**) and vendored drivers
# (src/display/drivers/**) get reformatted. See PRO-228.
find src lib \( -iname '*.h' -o -iname '*.c' -o -iname '*.cpp' \) ! -path 'src/display/ui/*' ! -path 'src/display/drivers/*' | xargs clang-format -i
