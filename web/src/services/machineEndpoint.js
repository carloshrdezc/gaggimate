const STORAGE_KEY = 'gaggimate.machineOrigin';
const QUERY_KEY = 'machine';

function normalizeOrigin(value) {
  if (!value) return '';

  const trimmed = value.trim();
  if (!trimmed) return '';

  const candidate = /^https?:\/\//i.test(trimmed) ? trimmed : `http://${trimmed}`;

  try {
    const url = new URL(candidate);
    return url.origin;
  } catch {
    return '';
  }
}

function readQueryOrigin() {
  if (typeof window === 'undefined') return '';
  const params = new URLSearchParams(window.location.search);
  return normalizeOrigin(params.get(QUERY_KEY) || '');
}

function readStoredOrigin() {
  if (typeof window === 'undefined') return '';
  try {
    return normalizeOrigin(window.localStorage.getItem(STORAGE_KEY) || '');
  } catch {
    return '';
  }
}

export function getConfiguredMachineOrigin() {
  const queryOrigin = readQueryOrigin();
  if (queryOrigin) {
    return queryOrigin;
  }

  const envOrigin = normalizeOrigin(import.meta.env.VITE_GAGGIMATE_MACHINE_URL || '');
  if (envOrigin) {
    return envOrigin;
  }

  return readStoredOrigin();
}

export function setConfiguredMachineOrigin(value) {
  if (typeof window === 'undefined') return '';

  const normalized = normalizeOrigin(value);
  try {
    if (normalized) {
      window.localStorage.setItem(STORAGE_KEY, normalized);
    } else {
      window.localStorage.removeItem(STORAGE_KEY);
    }
  } catch {
    return '';
  }
  return normalized;
}

export function getApiBaseUrl() {
  const configuredOrigin = getConfiguredMachineOrigin();
  return configuredOrigin || window.location.origin;
}

export function getApiUrl(path) {
  return new URL(path, `${getApiBaseUrl()}/`).toString();
}

export function getWebSocketUrl(path = '/ws') {
  const apiBaseUrl = getApiBaseUrl();
  const url = new URL(path, `${apiBaseUrl}/`);
  url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
  return url.toString();
}

export function shouldWarnAboutMixedContent() {
  if (typeof window === 'undefined' || window.location.protocol !== 'https:') {
    return false;
  }

  const configuredOrigin = getConfiguredMachineOrigin();
  return Boolean(configuredOrigin && configuredOrigin.startsWith('http://'));
}

export function installApiFetchInterceptor() {
  if (typeof window === 'undefined' || window.__gaggimateFetchPatched) {
    return;
  }

  const originalFetch = window.fetch.bind(window);

  window.fetch = (input, init) => {
    if (typeof input === 'string' && input.startsWith('/api/')) {
      return originalFetch(getApiUrl(input), init);
    }

    if (input instanceof Request) {
      const url = new URL(input.url);
      if (url.origin === window.location.origin && url.pathname.startsWith('/api/')) {
        const nextUrl = getApiUrl(`${url.pathname}${url.search}`);
        return originalFetch(new Request(nextUrl, input), init);
      }
    }

    return originalFetch(input, init);
  };

  window.__gaggimateFetchPatched = true;
}
