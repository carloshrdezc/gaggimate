#!/usr/bin/env bash
# PRO-239 nanopb spike — build + run the host round-trip test.
# Self-contained: uses the vendored nanopb runtime in runtime/ and the
# generated messages in gen/. No PlatformIO, no Arduino — pure host g++.
set -euo pipefail
cd "$(dirname "$0")"

CXX="${CXX:-g++}"
"$CXX" -std=gnu++17 -Wall -Wextra -O2 \
    -I runtime -I gen \
    roundtrip_test.cpp \
    gen/comms.pb.c \
    runtime/pb_encode.c runtime/pb_decode.c runtime/pb_common.c \
    -o roundtrip_test

./roundtrip_test
