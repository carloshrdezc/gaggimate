import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { h } from 'preact';
import { cleanup, fireEvent, render, screen } from '@testing-library/preact';

vi.mock('../../components/Card.jsx', () => ({ default: ({ title, children }) => h('section', {}, [h('h2', {}, title), children]) }));
vi.mock('@fortawesome/react-fontawesome', () => ({ FontAwesomeIcon: () => null }));

import { ApiServiceContext } from '../../services/ApiService.js';
import { LocalAuthRecoveryCard } from './LocalAuthRecoveryCard.jsx';
import { LOCAL_AUTH_TOKEN_KEY } from '../../services/localAuthFetch.js';

const VALID_TOKEN = 'a1b2c3d4e5f60718293a4b5c6d7e8f90';

function renderCard(apiService = { authenticateLocal: vi.fn() }) {
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

  it('persists a valid pasted token and authenticates the WebSocket', () => {
    const apiService = renderCard();

    fireEvent.input(screen.getByLabelText('Paste admin token'), { target: { value: `  ${VALID_TOKEN}  ` } });
    fireEvent.click(screen.getByRole('button', { name: 'Apply' }));

    expect(localStorage.getItem(LOCAL_AUTH_TOKEN_KEY)).toBe(VALID_TOKEN);
    expect(apiService.authenticateLocal).toHaveBeenCalledWith(VALID_TOKEN);
    expect(screen.getByRole('status').textContent).toContain('Token applied');
  });

  it('rejects an invalid token with a clear error and never touches storage or the socket', () => {
    const apiService = renderCard();

    fireEvent.input(screen.getByLabelText('Paste admin token'), { target: { value: 'not-a-real-token' } });
    fireEvent.click(screen.getByRole('button', { name: 'Apply' }));

    expect(localStorage.getItem(LOCAL_AUTH_TOKEN_KEY)).toBeNull();
    expect(apiService.authenticateLocal).not.toHaveBeenCalled();
    expect(screen.getByRole('alert')).toBeTruthy();
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
  });

  it('disables Apply until something is typed', () => {
    renderCard();

    expect(screen.getByRole('button', { name: 'Apply' }).disabled).toBe(true);
    fireEvent.input(screen.getByLabelText('Paste admin token'), { target: { value: 'x' } });
    expect(screen.getByRole('button', { name: 'Apply' }).disabled).toBe(false);
  });
});
