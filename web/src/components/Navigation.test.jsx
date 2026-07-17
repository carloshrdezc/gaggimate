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
    // BASE_URL-robust: the Dashboard link points at the app root route ('/').
    // Navigation.jsx builds every href as `_NAV_BASE + link` where
    // `_NAV_BASE = BASE_URL.replace(/\/$/, '')`, so the Dashboard href is
    // exactly `${navBase}/` — which equals '/' under Vite's default base and
    // '/gaggimate/' under GITHUB_PAGES=1. Mirror that derivation and assert
    // equality so a regression pointing Dashboard at the wrong target (e.g.
    // '/profiles/' or a bad base '/oops/') fails, instead of any trailing-slash
    // href slipping through (PRO-296, was the loose /\/$/ match from PRO-285).
    const navBase = import.meta.env.BASE_URL?.replace(/\/$/, '') ?? '';
    expect(dashboard.getAttribute('href')).toBe(`${navBase}/`);

    expect(screen.getByRole('link', { name: /Settings/i })).toBeTruthy();
    expect(screen.getByRole('link', { name: /Profiles/i })).toBeTruthy();
  });

  test('renders the open drawer as a modal dialog', () => {
    renderNavigation({ open: true });

    const drawer = screen.getByRole('dialog', { name: /Navigation menu/i });
    expect(drawer.getAttribute('aria-modal')).toBe('true');
    expect(drawer.getAttribute('aria-hidden')).toBeNull();
  });

  test('hides the closed drawer from assistive technology', () => {
    renderNavigation({ open: false });

    const drawer = document.querySelector('#app-navigation-drawer');
    expect(drawer?.getAttribute('aria-hidden')).toBe('true');
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
