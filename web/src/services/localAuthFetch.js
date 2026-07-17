export const LOCAL_AUTH_TOKEN_KEY = 'gaggimate_local_admin_token';

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
  const token = localStorage.getItem(LOCAL_AUTH_TOKEN_KEY);
  if (!token) return url;

  const separator = url.includes('?') ? '&' : '?';
  return `${url}${separator}localAuthToken=${encodeURIComponent(token)}`;
}
