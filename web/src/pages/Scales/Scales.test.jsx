import { afterEach, beforeEach, describe, expect, test, vi } from 'vitest';
import { h } from 'preact';
import { render, screen, cleanup, fireEvent, waitFor, act } from '@testing-library/preact';

vi.mock('@fortawesome/react-fontawesome', () => ({
  FontAwesomeIcon: () => h('span', { 'data-testid': 'fa-icon' }),
}));

vi.mock('preact-fetching', () => ({
  useQuery: () => ({
    isLoading: false,
    isError: false,
    data: {
      info: { connected: false },
      scales: [{ uuid: 'scale-uuid-1', name: 'Test Scale', rssi: -55 }],
    },
  }),
}));

import { machine } from '../../services/ApiService.js';
import { Scales } from './index.jsx';

beforeEach(() => {
  machine.value = {
    ...machine.value,
    status: {
      ...machine.value.status,
      mode: 1,
    },
  };
});

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

describe('Scales', () => {
  test('connects with the stored bearer token and preserves the POST form request', async () => {
    localStorage.setItem('gaggimate_local_admin_token', 'scale-token');
    const fetchMock = vi.spyOn(globalThis, 'fetch').mockResolvedValue({
      ok: true,
      json: async () => ({ success: true }),
    });

    render(h(Scales, {}));

    fireEvent.click(screen.getAllByRole('button')[1]);

    await waitFor(() => {
      expect(fetchMock).toHaveBeenCalledWith('/api/scales/connect', {
        method: 'post',
        body: expect.any(FormData),
        headers: { Authorization: 'Bearer scale-token' },
      });
    });
    expect(fetchMock.mock.calls[0][1].body.get('uuid')).toBe('scale-uuid-1');
  });

  test('surfaces connect API errors inline', async () => {
    vi.spyOn(globalThis, 'fetch').mockResolvedValue({
      ok: false,
      json: async () => ({ success: false, error: 'Missing or empty UUID' }),
    });

    render(h(Scales, {}));

    fireEvent.click(screen.getAllByRole('button')[1]);

    await waitFor(() => {
      expect(screen.getByText('Missing or empty UUID')).toBeTruthy();
    });
  });

  test('clears connect errors when scale mode changes to standby', async () => {
    vi.spyOn(globalThis, 'fetch').mockResolvedValue({
      ok: false,
      json: async () => ({ success: false, error: 'Missing or empty UUID' }),
    });

    render(h(Scales, {}));

    fireEvent.click(screen.getAllByRole('button')[1]);

    await waitFor(() => {
      expect(screen.getByText('Missing or empty UUID')).toBeTruthy();
    });

    act(() => {
      machine.value = {
        ...machine.value,
        status: {
          ...machine.value.status,
          mode: 0,
        },
      };
    });

    await waitFor(() => {
      expect(screen.queryByText('Missing or empty UUID')).toBeNull();
    });
  });
});
