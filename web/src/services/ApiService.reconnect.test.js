// PRO-7 — ApiService reconnect signals + reconnectNow().
//
// Pins the transport-facing half of the connection-lost banner: the
// connectionState / nextReconnectAt signals transition correctly across the
// WebSocket lifecycle, exponential backoff schedules the countdown target, and
// reconnectNow() resets the backoff and retries immediately. A fake WebSocket
// keeps the tests off the network and fully deterministic.

import { afterEach, beforeEach, describe, expect, test, vi } from 'vitest';

// Controllable fake WebSocket. Constructing one records the instance so a test
// can drive open/close manually; nothing connects to the network.
class FakeWebSocket {
  static OPEN = 1;
  static instances = [];
  constructor(url) {
    this.url = url;
    this.readyState = 0;
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
    this.readyState = 3;
  }
  send() {}
}

let ApiService;
let connectionState;
let nextReconnectAt;
let machine;

beforeEach(async () => {
  vi.useFakeTimers();
  vi.setSystemTime(0);
  FakeWebSocket.instances = [];
  globalThis.WebSocket = FakeWebSocket;

  // Re-import fresh each test so module-level signals start clean.
  vi.resetModules();
  const mod = await import('../services/ApiService.js');
  ApiService = mod.default;
  connectionState = mod.connectionState;
  nextReconnectAt = mod.nextReconnectAt;
  machine = mod.machine;
});

afterEach(() => {
  vi.useRealTimers();
  vi.restoreAllMocks();
});

describe('ApiService reconnect signals (PRO-7)', () => {
  test('starts optimistic (connected, no countdown target)', () => {
    new ApiService();
    expect(connectionState.value).toBe('connected');
    expect(nextReconnectAt.value).toBeNull();
  });

  test('_onClose transitions to reconnecting and schedules a countdown target', () => {
    const api = new ApiService();
    api._onClose();

    expect(connectionState.value).toBe('reconnecting');
    expect(machine.value.connected).toBe(false);
    // First backoff is baseReconnectDelay (1000ms) from now (t=0).
    expect(nextReconnectAt.value).toBe(1000);
  });

  test('backoff grows exponentially across successive drops', () => {
    const api = new ApiService();

    api._onClose(); // attempt 0 -> 1000ms
    expect(nextReconnectAt.value).toBe(1000);

    // Fire the scheduled reconnect (increments reconnectAttempts to 1) then drop again.
    vi.advanceTimersByTime(1000);
    api._onClose(); // attempt 1 -> 2000ms
    expect(nextReconnectAt.value).toBe(1000 + 2000);
  });

  test('_onOpen clears the banner (connected, no target)', () => {
    const api = new ApiService();
    api._onClose();
    expect(connectionState.value).toBe('reconnecting');

    api._onOpen();
    expect(connectionState.value).toBe('connected');
    expect(nextReconnectAt.value).toBeNull();
    expect(machine.value.connected).toBe(true);
    expect(api.reconnectAttempts).toBe(0);
  });

  test('reconnectNow() resets backoff, clears the pending timer, and retries immediately', () => {
    const api = new ApiService();

    // Simulate a few failed attempts so backoff has grown.
    api._onClose();
    vi.advanceTimersByTime(1000);
    api._onClose();
    vi.advanceTimersByTime(2000);
    expect(api.reconnectAttempts).toBeGreaterThan(0);

    const connectSpy = vi.spyOn(api, 'connect');
    const socketsBefore = FakeWebSocket.instances.length;

    api.reconnectNow();

    // Backoff reset so the NEXT failure starts from baseReconnectDelay again.
    expect(api.reconnectAttempts).toBe(0);
    expect(api.reconnectTimeout).toBeNull();
    expect(nextReconnectAt.value).toBeNull();
    // Retried immediately (connect() called -> a new socket opened).
    expect(connectSpy).toHaveBeenCalledTimes(1);
    expect(FakeWebSocket.instances.length).toBe(socketsBefore + 1);

    // And the reset backoff shows up on the next drop as a 1000ms target.
    vi.setSystemTime(10_000);
    api._onClose();
    expect(nextReconnectAt.value).toBe(11_000);
  });
});
