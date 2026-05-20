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
  // The firmware masks `cloudRelayToken` in /api/settings responses, so any
  // caller that pulls `relayToken` from `formData` may receive the sentinel.
  // Refusing to build a link in that case keeps users from copying a URL with
  // an invalid token instead of the real one.
  if (relayToken === SECRET_SENTINEL) return null;
  return `${pagesOrigin}?relay=${encodeURIComponent(relayUrl)}&token=${encodeURIComponent(relayToken)}`;
}
