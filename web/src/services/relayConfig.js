export const RELAY_URL_STORAGE_KEY = 'gaggimate_relay_url';
export const RELAY_TOKEN_SESSION_KEY = 'gaggimate_relay_token_session';
const LEGACY_RELAY_TOKEN_KEY = 'gaggimate_relay_token';

function parseRelayUrl(value) {
  try {
    const url = new URL(value);
    if (!['ws:', 'wss:'].includes(url.protocol) || url.username || url.password || url.search || url.hash) return null;
    return url.toString().replace(/\/$/, '');
  } catch {
    return null;
  }
}

function confirmationMessage(relayUrl) {
  const url = new URL(relayUrl);
  const warnings = [
    `Connect this browser to relay host: ${url.host}?`,
    'The relay can control your machine while this tab is open.',
  ];
  if (url.protocol === 'ws:') warnings.push('Warning: ws:// is unencrypted and can expose relay traffic.');
  if (!url.hostname.endsWith('.fly.dev')) warnings.push('Warning: this is a self-hosted or non-default relay. Verify its operator.');
  return warnings.join('\n\n');
}

export function relayCredentials() {
  const relayUrl = parseRelayUrl(localStorage.getItem(RELAY_URL_STORAGE_KEY) || '');
  let relayToken = sessionStorage.getItem(RELAY_TOKEN_SESSION_KEY);
  if (!relayToken) {
    relayToken = localStorage.getItem(LEGACY_RELAY_TOKEN_KEY);
    if (relayToken) {
      sessionStorage.setItem(RELAY_TOKEN_SESSION_KEY, relayToken);
      localStorage.removeItem(LEGACY_RELAY_TOKEN_KEY);
    }
  }
  return relayUrl && relayToken ? { relayUrl, relayToken } : null;
}

export function beginRelayProvisioning(location = window.location) {
  const fragmentParams = new URLSearchParams(location.hash.slice(1));
  const queryParams = new URLSearchParams(location.search);
  // Query parameters are accepted only as a one-time migration for links created
  // by pre-PRO-518 firmware/relay pages; they are immediately scrubbed.
  const params = fragmentParams.get('relay') && fragmentParams.get('token') ? fragmentParams : queryParams;
  const relayUrl = parseRelayUrl(params.get('relay') || '');
  const relayToken = params.get('token');
  if (!relayUrl || !relayToken) return null;
  if (!window.confirm(confirmationMessage(relayUrl))) return null;

  localStorage.setItem(RELAY_URL_STORAGE_KEY, relayUrl);
  sessionStorage.setItem(RELAY_TOKEN_SESSION_KEY, relayToken);
  const cleanUrl = new URL(window.location.href);
  cleanUrl.searchParams.delete('relay');
  cleanUrl.searchParams.delete('token');
  cleanUrl.hash = '';
  history.replaceState(null, '', cleanUrl.toString());
  return { relayUrl, relayToken };
}

export function confirmRelayOrigin(relayUrl, previousRelayUrl = '') {
  const normalizedRelayUrl = parseRelayUrl(relayUrl);
  if (!normalizedRelayUrl) throw new Error('Relay URL must be a ws:// or wss:// URL without credentials, query parameters, or fragments.');
  return normalizedRelayUrl === parseRelayUrl(previousRelayUrl || '') || window.confirm(confirmationMessage(normalizedRelayUrl));
}

export function saveRelayCredentials(relayUrl, relayToken, previousRelayUrl = '') {
  const normalizedRelayUrl = parseRelayUrl(relayUrl);
  if (!normalizedRelayUrl) throw new Error('Relay URL must be a ws:// or wss:// URL without credentials, query parameters, or fragments.');
  if (!confirmRelayOrigin(normalizedRelayUrl, previousRelayUrl)) {
    return false;
  }
  localStorage.setItem(RELAY_URL_STORAGE_KEY, normalizedRelayUrl);
  if (relayToken) sessionStorage.setItem(RELAY_TOKEN_SESSION_KEY, relayToken);
  else sessionStorage.removeItem(RELAY_TOKEN_SESSION_KEY);
  localStorage.removeItem(LEGACY_RELAY_TOKEN_KEY);
  return true;
}

export function relayWebSocketProtocols(relayToken) {
  const encoded = btoa(relayToken).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
  return ['gaggimate-relay-v1', `gaggimate-token-${encoded}`];
}

export function clearRelayCredentials() {
  localStorage.removeItem(RELAY_URL_STORAGE_KEY);
  localStorage.removeItem(LEGACY_RELAY_TOKEN_KEY);
  sessionStorage.removeItem(RELAY_TOKEN_SESSION_KEY);
}
