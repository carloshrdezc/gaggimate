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

The throwaway footprint-measurement build lives in `../../lib/NanoPbSpike/` +
the `[env:display-nanopb-spike]` env in `platformio.ini`. **Delete both before
any production merge** — they exist only so the +7,776 B flash number is
reproducible.
