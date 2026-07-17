// PRO-18 — ApiService reconnect idempotency + explicit connection state machine.
//
// Complements ApiService.reconnect.test.js (PRO-7, banner signals). These tests
// pin the race fix: a single transport failure must schedule EXACTLY ONE
// reconnect (no double-increment / competing timers), a socket that errors
// before ever opening must still get a reconnect scheduled even if no `close`
// event follows, and _onOpen must reset the reconnect bookkeeping. A fake
// WebSocket keeps everything off the network and deterministic.

import { afterEach, beforeEach, describe, expect, test, vi } from 'vitest';

// Controllable fake WebSocket. `readyState` is settable so tests can model a
// socket that errored before opening (readyState !== OPEN). close() flips to
// CLOSED but does NOT auto-fire a `close` event — tests drive lifecycle
// explicitly to exercise the "no close after error" browser edge.
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
  send() {}
}

let ApiService;

beforeEach(async () => {
  vi.useFakeTimers();
  vi.setSystemTime(0);
  FakeWebSocket.instances = [];
  globalThis.WebSocket = FakeWebSocket;

  // Re-import fresh each test so module-level signals start clean.
  vi.resetModules();
  const mod = await import('../services/ApiService.js');
  ApiService = mod.default;
});

afterEach(() => {
  vi.useRealTimers();
  vi.restoreAllMocks();
});

describe('ApiService reconnect idempotency (PRO-18)', () => {
  test('a single failure schedules exactly ONE reconnect even when _onError->close AND _onClose both fire', () => {
    const api = new ApiService();
    const socket = FakeWebSocket.instances[0];
    // Socket never opened.
    socket.readyState = FakeWebSocket.CONNECTING;

    // Failure path: error fires first (closes the socket), then the browser
    // delivers the close event -> _onClose. Both feed reconnect.
    api._onError(new Event('error'));
    api._onClose();

    // Exactly one reconnect timer armed; nothing has incremented yet.
    expect(api.reconnectAttempts).toBe(0);

    // Fire the scheduled reconnect once.
    vi.advanceTimersByTime(1000);

    // Exactly ONE increment for the single failure — not two.
    expect(api.reconnectAttempts).toBe(1);
  });

  test('_scheduleReconnect is a no-op while a reconnect is already armed', () => {
    const api = new ApiService();
    api._onClose(); // arms the first timer
    const firstTimer = api.reconnectTimeout;

    api._scheduleReconnect(); // should be ignored — already reconnecting
    api._scheduleReconnect();

    // Same timer, no re-arm, no attempt bump before it fires.
    expect(api.reconnectTimeout).toBe(firstTimer);
    expect(api.reconnectAttempts).toBe(0);

    vi.advanceTimersByTime(1000);
    expect(api.reconnectAttempts).toBe(1);
  });

  test('_onError on a socket that never opened schedules a reconnect even if no close event follows', () => {
    const api = new ApiService();
    const socket = FakeWebSocket.instances[0];
    socket.readyState = FakeWebSocket.CONNECTING; // never reached OPEN

    // Error fires, but the browser NEVER delivers a `close` event.
    api._onError(new Event('error'));

    // Before the fallback window, nothing scheduled the actual reconnect yet.
    // After the ~1s fallback fires, a reconnect must be armed.
    const socketsBefore = FakeWebSocket.instances.length;
    vi.advanceTimersByTime(1000); // fallback -> _scheduleReconnect
    vi.advanceTimersByTime(1000); // the reconnect backoff (baseReconnectDelay)

    // A reconnect attempt happened -> connect() opened a new socket.
    expect(api.reconnectAttempts).toBeGreaterThanOrEqual(1);
    expect(FakeWebSocket.instances.length).toBeGreaterThan(socketsBefore);
  });

  test('_onError fallback does not double-schedule when _onClose arrives promptly', () => {
    const api = new ApiService();
    const socket = FakeWebSocket.instances[0];
    socket.readyState = FakeWebSocket.CONNECTING;

    api._onError(new Event('error'));
    api._onClose(); // close arrives before the fallback window

    // Advance past the fallback window AND the reconnect delay.
    vi.advanceTimersByTime(1000); // fallback would fire here if not cancelled
    // The reconnect scheduled by _onClose fires at t=1000 too; only ONE bump.
    expect(api.reconnectAttempts).toBe(1);

    // Nudge further to prove the cancelled fallback never armed a second one.
    vi.advanceTimersByTime(5000);
    // With a fresh socket now CONNECTING (not OPEN), no extra failures were
    // injected, so attempts must not climb from the phantom fallback.
    expect(api.reconnectAttempts).toBe(1);
  });

  test('_onOpen resets reconnectAttempts to 0 and clears reconnecting state', () => {
    const api = new ApiService();
    api._onClose();
    vi.advanceTimersByTime(1000);
    api._onClose();
    expect(api.reconnectAttempts).toBeGreaterThan(0);

    const socket = FakeWebSocket.instances[FakeWebSocket.instances.length - 1];
    socket.readyState = FakeWebSocket.OPEN;
    api._onOpen();

    expect(api.reconnectAttempts).toBe(0);
    expect(api.reconnectTimeout).toBeNull();
  });
});
