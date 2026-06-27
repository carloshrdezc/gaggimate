// PRO-231 — first rendered-component smoke test for the web UI.
//
// PRO-229 deleted the last stale JSX component tests, leaving the suite
// 100% pure-logic with zero rendered-component coverage. This restores a
// minimal, robust smoke test under the existing vitest + jsdom setup using
// @testing-library/preact: the Navigation drawer must mount without throwing
// and render its primary nav surface. No live WebSocket / network is touched
// — Navigation only depends on preact-iso's useLocation, which we satisfy by
// wrapping it in a LocationProvider.

import { afterEach, beforeEach, describe, expect, test, vi } from 'vitest';
import { h } from 'preact';
import { LocationProvider } from 'preact-iso';
import { render, screen, cleanup } from '@testing-library/preact';

// Stub the FontAwesome icon component. Navigation pulls the FontAwesome-heavy
// graph (@fortawesome/react-fontawesome -> fontawesome-svg-core), which trips
// vitest's ESM JSON import-attribute handling and is irrelevant to a nav-
// structure smoke test. The existing routes.test.jsx mocks PageShell for the
// same reason. We only care that the drawer mounts and renders its links.
vi.mock('@fortawesome/react-fontawesome', () => ({
  FontAwesomeIcon: () => h('span', { 'data-testid': 'fa-icon' }),
}));

import { Navigation } from './Navigation.jsx';

const renderNavigation = (props = {}) => render(h(LocationProvider, null, h(Navigation, props)));

beforeEach(() => {
  history.replaceState(null, '', '/');
});

afterEach(() => {
  cleanup();
});

describe('Navigation', () => {
  test('mounts without throwing', () => {
    expect(() => renderNavigation({ open: true })).not.toThrow();
  });

  test('renders the brand and primary navigation links', () => {
    renderNavigation({ open: true });

    // Brand chrome.
    expect(screen.getByText('GAGGIMATE')).toBeTruthy();

    // A representative primary destination from each section renders as a link.
    const dashboard = screen.getByRole('link', { name: /Dashboard/i });
    expect(dashboard).toBeTruthy();
    // BASE_URL-robust: the Dashboard link points at the app root. Vite's base
    // is '/' by default and '/gaggimate/' under GITHUB_PAGES=1, so assert a
    // trailing slash rather than exact-matching '/' (PRO-285).
    expect(dashboard.getAttribute('href')).toMatch(/\/$/);

    expect(screen.getByRole('link', { name: /Settings/i })).toBeTruthy();
    expect(screen.getByRole('link', { name: /Profiles/i })).toBeTruthy();
  });

  test('marks the active route link with aria-current', () => {
    history.replaceState(null, '', '/settings');
    renderNavigation({ open: true });

    const settings = screen.getByRole('link', { name: /Settings/i });
    expect(settings.getAttribute('aria-current')).toBe('page');

    const dashboard = screen.getByRole('link', { name: /Dashboard/i });
    expect(dashboard.getAttribute('aria-current')).toBeNull();
  });
});
