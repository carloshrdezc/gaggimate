#!/usr/bin/env python3
"""Scripted display-sim smoke: the per-slice migration gate (PRO-656). Stdlib only.

Usage: scripts/sim_smoke.py [--binary PATH] [--timeout SECONDS] [--allow-empty-webui] [--skip-link-check]
       (or: pio run -e display-sim -t smoke)

Build first: `./scripts/build_webui.sh && pio run -e display-sim`. Then, against the built simulator:
  1. no sim/**/NimBLE* file / live NimBLE reference in sim/ code, and no NimBLE symbol linked into the binary
     (the sim speaks NanoPbComm's API with no BLE stack; the symbol check is skipped if `nm` is missing),
  2. the sim boots headless (SDL_VIDEODRIVER=offscreen) in a throwaway working dir and serves the embedded WebUI,
  3. a WebSocket client authenticates (req:auth with a token pre-seeded into the sim's NVS file),
     receives evt:status, and sees the MockController link come up,
  4. it switches to BREW, starts a brew (req:process:activate) and sees the process run with MockController
     pressure/flow telemetry in evt:status, then stops it (req:process:deactivate),
  5. the existing --link-check / --link-check-explicit-standby scenarios pass.
Prints PASS/FAIL per check; exits 1 on any failure. Never touches the repo's own sim_data/.
"""
import argparse
import base64
import gzip
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOKEN = "0123456789abcdef0123456789abcdef"  # 32 hex chars, same shape WebUIPlugin generates
MODE_STANDBY, MODE_BREW = 0, 1

failures = []


def check(ok, what, detail=""):
    print(f"[sim-smoke] {'PASS' if ok else 'FAIL'} {what}{(': ' + detail) if detail else ''}", flush=True)
    if not ok:
        failures.append(what)
    return ok


def default_binary():
    exe = ".exe" if os.name == "nt" else ""
    return os.path.join(ROOT, ".pio", "build", "display-sim", "program" + exe)


def check_no_nimble_in_sim_sources():
    """No live NimBLE include/type in sim/ code (comments explaining the history are fine)."""
    hits = []
    for dirpath, _, files in os.walk(os.path.join(ROOT, "sim")):
        for name in files:
            if name.lower().startswith("nimble"):
                hits.append(os.path.relpath(os.path.join(dirpath, name), ROOT))
            if not name.endswith((".h", ".hpp", ".c", ".cpp", ".S")):
                continue
            path = os.path.join(dirpath, name)
            with open(path, encoding="utf-8", errors="replace") as f:
                for n, line in enumerate(f, 1):
                    code = line.split("//", 1)[0]
                    if re.search(r"NimBLE", code, re.IGNORECASE):
                        hits.append(f"{os.path.relpath(path, ROOT)}:{n}")
    check(not hits, "no sim/**/NimBLE* file or live NimBLE reference in sim/ code", ", ".join(hits[:5]))


def check_no_nimble_symbols(binary):
    nm = shutil.which("nm")
    if not nm:
        print("[sim-smoke] SKIP NimBLE symbol check: `nm` not on PATH", flush=True)
        return
    out = subprocess.run([nm, "-C", binary], capture_output=True, text=True).stdout
    hits = sorted({line.split(None, 2)[-1] for line in out.splitlines() if "nimble" in line.lower()})
    check(not hits, "no NimBLE symbols linked into the sim binary", ", ".join(hits[:5]))


class Ws:
    """Minimal RFC 6455 client: masked text frames out, unfragmented text frames in."""

    def __init__(self, port):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=5)
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall(
            (
                f"GET /ws HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
            ).encode()
        )
        self.buf = b""
        while b"\r\n\r\n" not in self.buf:
            self._fill()
        head, self.buf = self.buf.split(b"\r\n\r\n", 1)
        self.status = head.split(b"\r\n")[0].decode()

    def _fill(self):
        chunk = self.s.recv(65536)
        if not chunk:
            raise EOFError("websocket closed")
        self.buf += chunk

    def _need(self, n):
        while len(self.buf) < n:
            self._fill()

    def send(self, obj):
        data = json.dumps(obj).encode()
        n = len(data)
        hdr = bytes([0x81]) + (bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + n.to_bytes(2, "big"))
        mask = os.urandom(4)
        self.s.sendall(hdr + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def recv(self):
        """Next JSON text message (dict), skipping control/non-JSON frames."""
        while True:
            self._need(2)
            op, n, off = self.buf[0] & 0x0F, self.buf[1] & 0x7F, 2
            if n == 126:
                self._need(4)
                n, off = int.from_bytes(self.buf[2:4], "big"), 4
            elif n == 127:
                self._need(10)
                n, off = int.from_bytes(self.buf[2:10], "big"), 10
            self._need(off + n)
            payload, self.buf = self.buf[off : off + n], self.buf[off + n :]
            if op == 8:
                raise EOFError("websocket close frame")
            if op != 1:
                continue
            try:
                return json.loads(payload)
            except ValueError:
                continue

    def wait_for(self, pred, seconds):
        """First message satisfying pred within `seconds`, else None. Also returns the last evt:status seen."""
        deadline = time.time() + seconds
        last_status = None
        while time.time() < deadline:
            self.s.settimeout(max(0.1, deadline - time.time()))
            try:
                msg = self.recv()
            except socket.timeout:
                break
            if msg.get("tp") == "evt:status":
                last_status = msg
            if pred(msg):
                return msg, last_status
        return None, last_status


def status(pred):
    return lambda m: m.get("tp") == "evt:status" and pred(m)


def run_live_checks(binary, workdir, timeout, allow_empty_webui):
    # Pre-seed the device-local admin token so the WS client can authenticate (req:auth), exactly as a
    # provisioned browser would. WebUIPlugin only generates a random token when this key is empty.
    nvs = os.path.join(workdir, "sim_data", "nvs")
    os.makedirs(nvs)
    with open(os.path.join(nvs, "controller.json"), "w") as f:
        json.dump({"admin_token": TOKEN}, f)

    env = dict(os.environ, SDL_VIDEODRIVER=os.environ.get("SDL_VIDEODRIVER", "offscreen"))
    proc = subprocess.Popen(
        [binary], cwd=workdir, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace"
    )
    log, port_box, port_ready = [], [], threading.Event()

    def reader():
        assert proc.stdout is not None
        for line in proc.stdout:
            log.append(line)
            m = re.search(r"\[sim-web\] WebUI server on http://localhost:(\d+)/", line)
            if m and not port_box:
                port_box.append(int(m.group(1)))
                port_ready.set()
        port_ready.set()

    threading.Thread(target=reader, daemon=True).start()
    try:
        if not check(port_ready.wait(timeout) and bool(port_box), "sim boots and starts the WebUI server"):
            return
        port = port_box[0]

        # Embedded WebUI is served from the firmware blob (serveWebAsset), not a filesystem dir.
        try:
            req = urllib.request.Request(f"http://127.0.0.1:{port}/", headers={"Accept-Encoding": "gzip"})
            with urllib.request.urlopen(req, timeout=5) as r:
                body = r.read()
                code, enc = r.status, r.headers.get("Content-Encoding", "")
            html = gzip.decompress(body) if enc == "gzip" else body
            check(code == 200 and b"<html" in html.lower(), "GET / serves the embedded WebUI index",
                  f"HTTP {code}, {len(html)} bytes")
        except urllib.error.HTTPError as e:
            if allow_empty_webui and e.code == 404:
                print("[sim-smoke] SKIP GET /: empty WebUI stub (run scripts/build_webui.sh)", flush=True)
            else:
                check(False, "GET / serves the embedded WebUI index", f"HTTP {e.code} (did build_webui.sh run?)")

        ws = Ws(port)
        if not check(" 101 " in ws.status + " ", "WebSocket /ws upgrade", ws.status):
            return
        ws.send({"tp": "req:auth", "token": TOKEN})
        msg, _ = ws.wait_for(lambda m: m.get("tp") == "res:auth", 5)
        if not check(bool(msg and msg.get("ok")), "req:auth accepted", json.dumps(msg)):
            return

        # rssi is -127 until the (mocked) controller link is up and verified.
        msg, last = ws.wait_for(status(lambda m: m.get("rssi", -127) != -127), timeout)
        if not check(msg is not None, "evt:status flows and the MockController link is up",
                     f"last status m={last and last.get('m')} rssi={last and last.get('rssi')}"):
            return

        ws.send({"tp": "req:change-mode", "mode": MODE_BREW})
        msg, last = ws.wait_for(status(lambda m: m.get("m") == MODE_BREW), 5)
        check(msg is not None, "req:change-mode -> BREW", f"m={last and last.get('m')}")

        ws.send({"tp": "req:process:activate"})
        msg, last = ws.wait_for(status(lambda m: (m.get("process") or {}).get("a") == 1), 5)
        check(msg is not None, "req:process:activate starts a brew", json.dumps(last and last.get("process")))

        msg, last = ws.wait_for(
            status(lambda m: (m.get("process") or {}).get("a") == 1 and (m.get("pr", 0) > 0.5 or m.get("fl", 0) > 0.1)),
            20,
        )
        check(msg is not None, "MockController reacts to the brew (pressure/flow in evt:status)",
              f"pr={last and last.get('pr')} fl={last and last.get('fl')}")

        ws.send({"tp": "req:process:deactivate"})
        msg, last = ws.wait_for(status(lambda m: (m.get("process") or {}).get("a", 0) != 1), 5)
        check(msg is not None, "req:process:deactivate stops the brew", json.dumps(last and last.get("process")))
        ws.s.close()
    except (OSError, EOFError) as e:
        check(False, "live sim session", repr(e))
    finally:
        proc.terminate()
        try:
            proc.wait(5)
        except subprocess.TimeoutExpired:
            proc.kill()
        if failures:
            sys.stdout.write("[sim-smoke] --- sim output (tail) ---\n" + "".join(log[-40:]))


def run_link_checks(binary, timeout):
    env = dict(os.environ, SDL_VIDEODRIVER=os.environ.get("SDL_VIDEODRIVER", "offscreen"))
    for flag in ("--link-check", "--link-check-explicit-standby"):
        with tempfile.TemporaryDirectory(prefix="gm-sim-smoke-") as wd:
            try:
                r = subprocess.run([binary, flag], cwd=wd, env=env, capture_output=True, text=True, timeout=timeout * 4)
                out = r.stdout + r.stderr
                rc = r.returncode
            except subprocess.TimeoutExpired:
                out, rc = "timed out", -1
        lines = [l for l in out.splitlines() if l.startswith("[link-check]")]
        check(rc == 0, f"sim {flag}", lines[-1] if lines else out[-300:])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", default=default_binary())
    ap.add_argument("--timeout", type=float, default=15, help="seconds to wait for boot / link up (default 15)")
    ap.add_argument("--allow-empty-webui", action="store_true", help="tolerate the empty-bundle stub (no web build)")
    ap.add_argument("--skip-link-check", action="store_true")
    a = ap.parse_args()

    if not os.path.isfile(a.binary):
        sys.exit(f"[sim-smoke] no simulator binary at {a.binary} — run `pio run -e display-sim` first")
    check_no_nimble_in_sim_sources()
    check_no_nimble_symbols(a.binary)
    with tempfile.TemporaryDirectory(prefix="gm-sim-smoke-") as wd:
        run_live_checks(a.binary, wd, a.timeout, a.allow_empty_webui)
    if not a.skip_link_check:
        run_link_checks(a.binary, a.timeout)

    print(f"[sim-smoke] {'ALL PASSED' if not failures else 'FAILED'} ({len(failures)} failure(s))", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
