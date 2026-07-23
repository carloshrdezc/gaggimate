import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { h } from 'preact';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/preact';

vi.mock('../../components/Card.jsx', () => ({
  default: ({ title, children }) => h('section', {}, [h('h2', {}, title), children]),
}));
vi.mock('@fortawesome/react-fontawesome', () => ({ FontAwesomeIcon: () => null }));

import { ApiServiceContext } from '../../services/ApiService.js';
import { LocalAuthRecoveryCard } from './LocalAuthRecoveryCard.jsx';
import { LOCAL_AUTH_TOKEN_KEY } from '../../services/localAuthFetch.js';

const VALID_TOKEN = 'a1b2c3d4e5f60718293a4b5c6d7e8f90';

// Default mock: device confirms the token (res:auth {ok:true}).
function makeApi(overrides = {}) {
  return {
    authenticateLocal: vi.fn(),
    authenticateLocalAndConfirm: vi.fn().mockResolvedValue({ ok: true }),
    ...overrides,
  };
}

function renderCard(apiService = makeApi()) {
  render(h(ApiServiceContext.Provider, { value: apiService }, h(LocalAuthRecoveryCard)));
  return apiService;
}

beforeEach(() => {
  vi.spyOn(console, 'error').mockImplementation(() => {});
});

afterEach(() => {
  cleanup();
  localStorage.clear();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});

describe('LocalAuthRecoveryCard', () => {
  it('renders the paste field with no stored token (independent of any settings query)', () => {
    renderCard();

    expect(screen.getByLabelText('Paste admin token')).toBeTruthy();
    expect(screen.getByRole('button', { name: 'Apply' })).toBeTruthy();
    // No current-token display when nothing is stored yet.
    expect(screen.queryByText("This session's current token", { exact: false })).toBeNull();
  });

  it('persists a valid pasted token and confirms it with the device before showing success', async () => {
    const apiService = renderCard();

    fireEvent.input(screen.getByLabelText('Paste admin token'), {
      target: { value: `  ${VALID_TOKEN}  ` },
    });
    fireEvent.click(screen.getByRole('button', { name: 'Apply' }));

    // Success banner appears only after res:auth {ok:true} resolves.
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('Token applied'));
    expect(apiService.authenticateLocalAndConfirm).toHaveBeenCalledWith(VALID_TOKEN);
    expect(localStorage.getItem(LOCAL_AUTH_TOKEN_KEY)).toBe(VALID_TOKEN);
  });

  it('rejects an invalid token with a clear error and never touches storage or the socket', () => {
    const apiService = renderCard();

    fireEvent.input(screen.getByLabelText('Paste admin token'), {
      target: { value: 'not-a-real-token' },
    });
    fireEvent.click(screen.getByRole('button', { name: 'Apply' }));

    expect(localStorage.getItem(LOCAL_AUTH_TOKEN_KEY)).toBeNull();
    expect(apiService.authenticateLocalAndConfirm).not.toHaveBeenCalled();
    expect(screen.getByRole('alert')).toBeTruthy();
    expect(screen.queryByRole('status')).toBeNull();
  });

  it('shows an error (never the success banner) when the device rejects the token (res:auth {ok:false})', async () => {
    const apiService = renderCard(
      makeApi({
        authenticateLocalAndConfirm: vi
          .fn()
          .mockResolvedValue({ ok: false, error: 'Authentication failed' }),
      }),
    );

    fireEvent.input(screen.getByLabelText('Paste admin token'), { target: { value: VALID_TOKEN } });
    fireEvent.click(screen.getByRole('button', { name: 'Apply' }));

    await waitFor(() => expect(screen.getByRole('alert')).toBeTruthy());
    expect(screen.getByRole('alert').textContent).toContain('Device rejected this token');
    // No false success, and the rejected token is NOT persisted.
    expect(screen.queryByRole('status')).toBeNull();
    expect(localStorage.getItem(LOCAL_AUTH_TOKEN_KEY)).toBeNull();
    expect(apiService.authenticateLocalAndConfirm).toHaveBeenCalledWith(VALID_TOKEN);
  });

  it('shows an error (never the success banner) when the socket is not open / never confirms', async () => {
    renderCard(
      makeApi({
        authenticateLocalAndConfirm: vi
          .fn()
          .mockRejectedValue(new Error('WebSocket is not connected')),
      }),
    );

    fireEvent.input(screen.getByLabelText('Paste admin token'), { target: { value: VALID_TOKEN } });
    fireEvent.click(screen.getByRole('button', { name: 'Apply' }));

    await waitFor(() => expect(screen.getByRole('alert')).toBeTruthy());
    expect(screen.getByRole('alert').textContent).toContain('Could not confirm the token');
    expect(screen.queryByRole('status')).toBeNull();
    expect(localStorage.getItem(LOCAL_AUTH_TOKEN_KEY)).toBeNull();
  });

  it('shows a stored token for copying and writes it to the clipboard', async () => {
    localStorage.setItem(LOCAL_AUTH_TOKEN_KEY, VALID_TOKEN);
    const writeText = vi.fn().mockResolvedValue();
    vi.stubGlobal('navigator', { clipboard: { writeText } });

    renderCard();

    // Reveal, then copy.
    fireEvent.click(screen.getByRole('button', { name: 'Reveal current token' }));
    expect(screen.getByText(VALID_TOKEN)).toBeTruthy();

    fireEvent.click(screen.getByRole('button', { name: 'Copy' }));
    expect(writeText).toHaveBeenCalledWith(VALID_TOKEN);
    // Happy path: success label shown, no fallback alert.
    await waitFor(() => expect(screen.getByRole('button', { name: /Copied/ })).toBeTruthy());
    expect(screen.queryByRole('alert')).toBeNull();
  });

  it('shows a fallback alert (and no success) when navigator.clipboard is unavailable', async () => {
    localStorage.setItem(LOCAL_AUTH_TOKEN_KEY, VALID_TOKEN);
    // Plain-HTTP LAN access: Clipboard API requires a secure context, so it is
    // undefined. The copy must not silently no-op.
    vi.stubGlobal('navigator', {});

    renderCard();

    fireEvent.click(screen.getByRole('button', { name: 'Copy' }));

    await waitFor(() => expect(screen.getByRole('alert')).toBeTruthy());
    expect(screen.getByRole('alert').textContent).toContain('Could not copy automatically');
    // Success flag never set: the button still reads "Copy", not "Copied".
    expect(screen.queryByRole('button', { name: /Copied/ })).toBeNull();
  });

  it('shows a fallback alert (and no success) when clipboard.writeText rejects', async () => {
    localStorage.setItem(LOCAL_AUTH_TOKEN_KEY, VALID_TOKEN);
    const writeText = vi.fn().mockRejectedValue(new Error('NotAllowedError'));
    vi.stubGlobal('navigator', { clipboard: { writeText } });

    renderCard();

    fireEvent.click(screen.getByRole('button', { name: 'Copy' }));

    await waitFor(() => expect(screen.getByRole('alert')).toBeTruthy());
    expect(screen.getByRole('alert').textContent).toContain('Could not copy automatically');
    expect(writeText).toHaveBeenCalledWith(VALID_TOKEN);
    expect(screen.queryByRole('button', { name: /Copied/ })).toBeNull();
  });

  it('clears the fallback alert when a retry succeeds', async () => {
    localStorage.setItem(LOCAL_AUTH_TOKEN_KEY, VALID_TOKEN);
    const writeText = vi
      .fn()
      .mockRejectedValueOnce(new Error('NotAllowedError'))
      .mockResolvedValueOnce();
    vi.stubGlobal('navigator', { clipboard: { writeText } });

    renderCard();

    // First attempt fails -> alert shown.
    fireEvent.click(screen.getByRole('button', { name: 'Copy' }));
    await waitFor(() => expect(screen.getByRole('alert')).toBeTruthy());

    // Retry succeeds -> alert cleared, success label shown.
    fireEvent.click(screen.getByRole('button', { name: /Copy/ }));
    await waitFor(() => expect(screen.getByRole('button', { name: /Copied/ })).toBeTruthy());
    expect(screen.queryByRole('alert')).toBeNull();
  });

  it('disables Apply until something is typed', () => {
    renderCard();

    expect(screen.getByRole('button', { name: 'Apply' }).disabled).toBe(true);
    fireEvent.input(screen.getByLabelText('Paste admin token'), { target: { value: 'x' } });
    expect(screen.getByRole('button', { name: 'Apply' }).disabled).toBe(false);
  });
});
