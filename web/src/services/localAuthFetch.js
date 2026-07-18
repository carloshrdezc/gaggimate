export const LOCAL_AUTH_TOKEN_KEY = 'gaggimate_local_admin_token';
export const MDNS_NAME_ERROR = 'Hostname must be 1-63 ASCII letters, digits, or hyphens, without a leading or trailing hyphen.';

export function isValidMdnsName(hostname) {
  return typeof hostname === 'string' && /^[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?$/.test(hostname);
}

export function bootstrapLocalAuth(token, apiService) {
  const previousToken = localStorage.getItem(LOCAL_AUTH_TOKEN_KEY);
  localStorage.setItem(LOCAL_AUTH_TOKEN_KEY, token);
  if (previousToken !== token) apiService.authenticateLocal(token);
}

export function localAuthHeaders(headers = {}) {
  const token = localStorage.getItem(LOCAL_AUTH_TOKEN_KEY);
  return token ? { ...headers, Authorization: `Bearer ${token}` } : { ...headers };
}

export function authenticatedFetch(url, options = {}) {
  return fetch(url, { ...options, headers: localAuthHeaders(options.headers) });
}

export function localAuthDownloadUrl(url) {
  if (!['/api/diag/log.txt', '/api/diag/log.1'].includes(url.split('?')[0])) return url;

  const token = localStorage.getItem(LOCAL_AUTH_TOKEN_KEY);
  if (!token) return url;

  const separator = url.includes('?') ? '&' : '?';
  return `${url}${separator}localAuthToken=${encodeURIComponent(token)}`;
}

export function localAuthHandoffUrl(hostname, token = localStorage.getItem(LOCAL_AUTH_TOKEN_KEY)) {
  if (!isValidMdnsName(hostname) || !token) return null;

  const label = hostname.toLowerCase();
  const url = new URL(`http://${label}.local/`);
  const expectedOrigin = `http://${label}.local`;
  if (url.origin !== expectedOrigin || url.hostname !== `${label}.local`) return null;
  url.hash = new URLSearchParams({ localAuthToken: token }).toString();
  return url.toString();
}

export function importLocalAuthHandoff(apiService, location = window.location) {
  const params = new URLSearchParams(location.hash.slice(1));
  const token = params.get('localAuthToken');
  if (!token) return false;

  bootstrapLocalAuth(token, apiService);
  history.replaceState(null, '', `${location.pathname}${location.search}`);
  return true;
}
