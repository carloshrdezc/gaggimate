// PRO-549 — ApiService.authenticateLocalAndConfirm.
//
// The interactive "paste admin token" recovery card must NOT claim success
// until the device confirms the token with a `res:auth {ok}` frame. Unlike the
// fire-and-forget `authenticateLocal` (used at `_onOpen`), this method returns
// a Promise that resolves with the reply, rejects when the socket isn't open,
// and rejects on timeout if no reply ever arrives. A fake WebSocket keeps this
// deterministic and off the network.

import { afterEach, beforeEach, describe, expect, test, vi } from 'vitest';

class FakeWebSocket {
  static CONNECTING = 0;
  static OPEN = 1;
  static CLOSING = 2;
  static CLOSED = 3;
  static instances = [];
  constructor(url) {
    this.url = url;
    this.readyState = FakeWebSocket.CONNECTING;
    this.listeners = {};
    this.sent = [];
    FakeWebSocket.instances.push(this);
  }
  addEventListener(type, cb) {
    (this.listeners[type] ||= []).push(cb);
  }
  removeEventListener(type, cb) {
    this.listeners[type] = (this.listeners[type] || []).filter(l => l !== cb);
  }
  close() {
    this.readyState = FakeWebSocket.CLOSED;
  }
  send(data) {
    this.sent.push(data);
  }
}

let ApiService;
let WebSocketDisconnectedError;

beforeEach(async () => {
  vi.useFakeTimers();
  vi.setSystemTime(0);
  FakeWebSocket.instances = [];
  globalThis.WebSocket = FakeWebSocket;
  vi.resetModules();
  const mod = await import('../services/ApiService.js');
  ApiService = mod.default;
  WebSocketDisconnectedError = mod.WebSocketDisconnectedError;
});

afterEach(() => {
  vi.useRealTimers();
  vi.restoreAllMocks();
});

const TOKEN = 'a1b2c3d4e5f60718293a4b5c6d7e8f90';

function openApi() {
  const api = new ApiService();
  const socket = FakeWebSocket.instances[0];
  socket.readyState = FakeWebSocket.OPEN;
  return { api, socket };
}

describe('ApiService.authenticateLocalAndConfirm (PRO-549)', () => {
  test('sends req:auth and resolves {ok:true} on a res:auth {ok:true} reply', async () => {
    const { api, socket } = openApi();

    const promise = api.authenticateLocalAndConfirm(TOKEN);
    // req:auth was sent over the (open) socket.
    expect(socket.sent.some(s => s.includes('"tp":"req:auth"') && s.includes(TOKEN))).toBe(true);

    // Firmware confirms.
    api._onMessage({ data: JSON.stringify({ tp: 'res:auth', ok: true }) });

    await expect(promise).resolves.toEqual({ ok: true, error: undefined });
  });

  test('resolves {ok:false, error} when the device rejects the token', async () => {
    const { api } = openApi();

    const promise = api.authenticateLocalAndConfirm(TOKEN);
    api._onMessage({
      data: JSON.stringify({ tp: 'res:auth', ok: false, error: 'Authentication failed' }),
    });

    await expect(promise).resolves.toEqual({ ok: false, error: 'Authentication failed' });
  });

  test('rejects with WebSocketDisconnectedError when the socket is not open', async () => {
    const api = new ApiService();
    FakeWebSocket.instances[0].readyState = FakeWebSocket.CONNECTING;

    await expect(api.authenticateLocalAndConfirm(TOKEN)).rejects.toBeInstanceOf(
      WebSocketDisconnectedError,
    );
  });

  test('rejects on timeout when no res:auth reply ever arrives', async () => {
    const { api } = openApi();

    const promise = api.authenticateLocalAndConfirm(TOKEN, 5000);
    // Attach the rejection handler before advancing timers so the rejection is
    // observed (avoids an unhandled rejection).
    const assertion = expect(promise).rejects.toThrow('Authentication timed out');
    vi.advanceTimersByTime(5000);
    await assertion;
  });

  test('does not resolve twice: a late res:auth after timeout is ignored', async () => {
    const { api } = openApi();

    const promise = api.authenticateLocalAndConfirm(TOKEN, 5000);
    const assertion = expect(promise).rejects.toThrow('Authentication timed out');
    vi.advanceTimersByTime(5000);
    await assertion;

    // A late reply must not throw or re-settle — the listener was removed.
    expect(() =>
      api._onMessage({ data: JSON.stringify({ tp: 'res:auth', ok: true }) }),
    ).not.toThrow();
  });
});
