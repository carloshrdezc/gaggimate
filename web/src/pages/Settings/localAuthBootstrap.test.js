import { expect, test, vi } from 'vitest';

import { bootstrapLocalAuth, LOCAL_AUTH_TOKEN_KEY } from '../../services/localAuthFetch.js';

test('persists a non-sentinel AP local admin token and authenticates the WebSocket', () => {
  const apiService = { authenticateLocal: vi.fn() };

  bootstrapLocalAuth('ap-device-token', apiService);

  expect(localStorage.getItem(LOCAL_AUTH_TOKEN_KEY)).toBe('ap-device-token');
  expect(apiService.authenticateLocal).toHaveBeenCalledWith('ap-device-token');
});
