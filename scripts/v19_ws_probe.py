#!/usr/bin/env python3
"""Minimal WebSocket probe for a GaggiMate display (device or display-sim), stdlib only (PRO-653).

Usage: scripts/v19_ws_probe.py <host> [port] [seconds]
  Connects to ws://<host>:<port>/ws, sends {"tp":"req:ota-settings"}, and prints:
    - the handshake status line,
    - heapFree / heapLargest / heapTotal from res:ota-settings (internal DRAM, MALLOC_CAP_DEFAULT|INTERNAL),
    - a count of received frames by tp.
  On display-sim the heap numbers are fixed shim constants (sim/platform/esp_heap_caps.h), not real measurements.
"""
import base64
import json
import os
import socket
import sys
import time

host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
port = int(sys.argv[2]) if len(sys.argv) > 2 else 80
seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 6

s = socket.create_connection((host, port), timeout=5)
key = base64.b64encode(os.urandom(16)).decode()
s.sendall(
    (
        f"GET /ws HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
    ).encode()
)
buf = b""
while b"\r\n\r\n" not in buf:
    chunk = s.recv(4096)
    if not chunk:
        sys.exit("connection closed during handshake")
    buf += chunk
head, buf = buf.split(b"\r\n\r\n", 1)
print(head.split(b"\r\n")[0].decode())

# Client frames must be masked (RFC 6455 5.3).
data = json.dumps({"tp": "req:ota-settings"}).encode()
mask = os.urandom(4)
s.sendall(bytes([0x81, 0x80 | len(data)]) + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))


def need(n):
    global buf
    while len(buf) < n:
        chunk = s.recv(65536)
        if not chunk:
            raise EOFError
        buf += chunk


seen = {}
deadline = time.time() + seconds
try:
    while time.time() < deadline:
        need(2)
        op, n, off = buf[0] & 0x0F, buf[1] & 0x7F, 2
        if n == 126:
            need(4)
            n, off = int.from_bytes(buf[2:4], "big"), 4
        elif n == 127:
            need(10)
            n, off = int.from_bytes(buf[2:10], "big"), 10
        need(off + n)
        payload, buf = buf[off : off + n], buf[off + n :]
        if op != 1:
            continue
        try:
            msg = json.loads(payload)
        except ValueError:
            continue
        tp = msg.get("tp", "?")
        seen[tp] = seen.get(tp, 0) + 1
        if tp == "res:ota-settings":
            print({k: msg.get(k) for k in ("displayVersion", "heapFree", "heapLargest", "heapTotal")})
except (socket.timeout, EOFError):
    pass
print("frames by tp:", seen)
