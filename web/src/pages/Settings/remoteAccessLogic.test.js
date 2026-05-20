import assert from 'node:assert/strict';
import test from 'node:test';

import { buildRemoteAccessLink, SECRET_SENTINEL } from './remoteAccessLogic.js';

test('remote access link is hidden while relay is disabled', () => {
  const link = buildRemoteAccessLink({
    relayEnabled: false,
    relayUrl: 'wss://relay.example/connect',
    relayToken: 'secret-token',
  });

  assert.equal(link, null);
});

test('remote access link is generated only when relay is enabled and configured', () => {
  const link = buildRemoteAccessLink({
    relayEnabled: true,
    relayUrl: 'wss://relay.example/connect',
    relayToken: 'secret-token',
    pagesOrigin: 'https://example.test/gaggimate',
  });

  assert.equal(
    link,
    'https://example.test/gaggimate?relay=wss%3A%2F%2Frelay.example%2Fconnect&token=secret-token',
  );
});

test('remote access link is null when relayToken is the masked sentinel', () => {
  // The firmware masks `cloudRelayToken` on /api/settings GET. If the form
  // round-trips that mask into buildRemoteAccessLink, we must NOT emit a URL
  // with the literal sentinel as the token.
  const link = buildRemoteAccessLink({
    relayEnabled: true,
    relayUrl: 'wss://relay.example/connect',
    relayToken: SECRET_SENTINEL,
    pagesOrigin: 'https://example.test/gaggimate',
  });

  assert.equal(link, null);
});

test('remote access link is null when relayToken is empty', () => {
  const link = buildRemoteAccessLink({
    relayEnabled: true,
    relayUrl: 'wss://relay.example/connect',
    relayToken: '',
    pagesOrigin: 'https://example.test/gaggimate',
  });

  assert.equal(link, null);
});
