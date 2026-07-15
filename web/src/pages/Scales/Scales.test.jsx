import { afterEach, beforeEach, describe, expect, test, vi } from 'vitest';
import { h } from 'preact';
import { render, screen, cleanup, fireEvent, waitFor } from '@testing-library/preact';

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
});
