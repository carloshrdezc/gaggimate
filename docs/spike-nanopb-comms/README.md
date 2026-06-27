# PRO-239 — nanopb comms spike

Throwaway spike evaluating replacing the delimiter-separated ASCII BLE protocol
(`lib/NimBLEComm`) with **nanopb** (Protocol Buffers for embedded C).

**Read `findings.md` for the verdict + numbers.**

Contents:
- `comms.proto` — draft schema for all 18 message types.
- `gen/` — real nanopb-generated `comms.pb.{c,h}` (nanopb 0.4.9.1).
- `runtime/` — vendored nanopb C runtime (nanopb@0.4.9.1), used by the harness.
- `roundtrip_test.cpp`, `run.sh` — lossless round-trip host harness (`./run.sh`).
- `findings.md` — the deliverable.

The throwaway footprint-measurement build (a probe lib under `lib/` plus an
extra `platformio.ini` env) that produced the +7,776 B flash number was
**removed in PRO-241** when the production nanopb infra landed. It is gone on
purpose; the number it produced is preserved in `findings.md` §3.
