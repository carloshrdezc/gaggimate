#!/usr/bin/env bash

# Note: `find src lib ...` emits paths WITHOUT a leading `./`, and `find -path`
# uses shell-glob `*` (which spans `/`), not `**`. The exclusion patterns must
# therefore match the bare `src/display/...` form (no `./` prefix, single `*`),
# otherwise generated LVGL UI (src/display/ui/**) and vendored drivers
# (src/display/drivers/**) get reformatted. See PRO-228.
#
# `-print0 | xargs -0` handles paths containing spaces/newlines safely, and
# `-r` (no-run-if-empty) avoids invoking clang-format with no arguments when
# the find produces no matches. See PRO-230.
find src lib \( -iname '*.h' -o -iname '*.c' -o -iname '*.cpp' \) ! -path 'src/display/ui/*' ! -path 'src/display/drivers/*' -print0 | xargs -0 -r clang-format -i
