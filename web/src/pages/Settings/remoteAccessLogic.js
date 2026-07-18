export const DEFAULT_REMOTE_PAGES_ORIGIN = 'https://carloshrdezc.github.io/gaggimate';

// Sentinel emitted by the firmware on /api/settings GET in place of the real
// secret. Mirrors `kSecretSentinel` in src/display/plugins/WebUIPlugin.cpp.
// Keep these two literals in sync.
export const SECRET_SENTINEL = '---unchanged---';

export function buildRemoteAccessLink({
  relayEnabled,
  relayUrl = '',
  relayToken = '',
  pagesOrigin = DEFAULT_REMOTE_PAGES_ORIGIN,
}) {
  if (!relayEnabled || !relayUrl || !relayToken) return null;
  // The firmware masks `cloudRelayToken` in /api/settings GET. If the form
  // round-trips that mask into buildRemoteAccessLink, we must NOT emit a URL
  // with the literal sentinel as the token.
  if (relayToken === SECRET_SENTINEL) return null;
  return `${pagesOrigin}#relay=${encodeURIComponent(relayUrl)}&token=${encodeURIComponent(relayToken)}`;
}
