import { afterEach, beforeEach, expect, test, vi } from 'vitest';

import {
  RELAY_TOKEN_SESSION_KEY,
  RELAY_URL_STORAGE_KEY,
  beginRelayProvisioning,
  relayWebSocketProtocols,
} from './relayConfig.js';

beforeEach(() => {
  sessionStorage.clear();
  localStorage.clear();
  history.replaceState(null, '', '/');
});

afterEach(() => vi.restoreAllMocks());

test('provisions a fragment relay token only after confirming the displayed host', () => {
  history.replaceState(null, '', '/#relay=wss%3A%2F%2Frelay.example&token=secret-token');
  const confirm = vi.spyOn(window, 'confirm').mockReturnValue(true);

  expect(beginRelayProvisioning()).toEqual({ relayUrl: 'wss://relay.example', relayToken: 'secret-token' });
  expect(confirm).toHaveBeenCalledWith(expect.stringContaining('relay.example'));
  expect(localStorage.getItem(RELAY_URL_STORAGE_KEY)).toBe('wss://relay.example');
  expect(sessionStorage.getItem(RELAY_TOKEN_SESSION_KEY)).toBe('secret-token');
  expect(window.location.hash).toBe('');
});

test('rejects unconfirmed insecure relay provisioning without persisting a token', () => {
  history.replaceState(null, '', '/#relay=ws%3A%2F%2Frelay.example&token=secret-token');
  vi.spyOn(window, 'confirm').mockReturnValue(false);

  expect(beginRelayProvisioning()).toBeNull();
  expect(sessionStorage.getItem(RELAY_TOKEN_SESSION_KEY)).toBeNull();
  expect(localStorage.getItem(RELAY_URL_STORAGE_KEY)).toBeNull();
});

test('migrates a legacy query provisioning link by confirming and scrubbing it', () => {
  history.replaceState(null, '', '/?relay=wss%3A%2F%2Frelay.example&token=legacy-token');
  vi.spyOn(window, 'confirm').mockReturnValue(true);

  expect(beginRelayProvisioning()).toEqual({ relayUrl: 'wss://relay.example', relayToken: 'legacy-token' });
  expect(window.location.search).toBe('');
  expect(sessionStorage.getItem(RELAY_TOKEN_SESSION_KEY)).toBe('legacy-token');
});

test('uses a WebSocket subprotocol token rather than a URL query string', () => {
  expect(relayWebSocketProtocols('token+/='))
    .toEqual(['gaggimate-relay-v1', 'gaggimate-token-dG9rZW4rLz0']);
});
