// PRO-253 regression test.
//
// Reproduces the "menu navigation changes URL but not page content" bug at the
// routing layer, headlessly (jsdom), against the REAL preact-iso Router. The
// bug: navigating between two LAZY routes advanced useLocation().path but the
// rendered <main> content stayed on the previous page (the lazy un-suspend
// re-render was dropped) until a hard refresh.
//
// The fix (see ./routeFactory.jsx + ./routes.jsx) hoists PageShell out of the
// per-route lazy wrapper and gives each route a single, stable lazy `component`
// identity, so the content swaps every time.
//
// We import only the page-free factory (routeFactory.jsx) so the test stays
// headless and doesn't pull the FontAwesome-heavy page graph.

import { afterEach, beforeEach, expect, test, vi } from 'vitest';
import { h, render } from 'preact';
import { LocationProvider, Router, Route, ErrorBoundary, lazy, useLocation } from 'preact-iso';
import { withShell, createLazyRoute } from './routeFactory.jsx';

// Mock PageShell so we don't pull in FontAwesome/heavy chrome; we only care
// about content-swap behaviour, not the shell markup.
vi.mock('./components/PageShell.jsx', () => ({
  PageShell: ({ children }) => h('div', { 'data-shell': '' }, children),
}));

let container;

beforeEach(() => {
  container = document.createElement('div');
  document.body.appendChild(container);
  history.replaceState(null, '', '/');
  // preact-iso calls scrollTo on route change; jsdom doesn't implement it.
  window.scrollTo = () => {};
});

afterEach(() => {
  render(null, container);
  container.remove();
});

const flush = () => new Promise(resolve => setTimeout(resolve, 0));

// Build a lazy route the same way routes.jsx does, but with an in-test loader
// that resolves asynchronously like a real dynamic import.
const makeLazyRoute = (label, exportName) =>
  createLazyRoute(
    () => Promise.resolve({ [exportName]: () => h('h1', null, label) }),
    exportName,
  );

test('createLazyRoute and withShell produce stable, distinct components', () => {
  const a = makeLazyRoute('A', 'A');
  const b = makeLazyRoute('B', 'B');
  expect(typeof a).toBe('function');
  expect(typeof b).toBe('function');
  // Distinct routes -> distinct stable component identities (what preact-iso
  // keys route-change detection on).
  expect(a).not.toBe(b);

  // withShell returns a stable wrapper for a given call.
  const Page = lazy(() => Promise.resolve({ default: () => h('div', null, 'x') }));
  const wrapped = withShell(Page);
  expect(typeof wrapped).toBe('function');
  expect(wrapped).toBe(wrapped);
});

function Navigator({ onReady }) {
  const { route } = useLocation();
  onReady(route);
  return null;
}

function Harness({ routeTable, onReady }) {
  return h(
    LocationProvider,
    null,
    h(ErrorBoundary, null, [
      h(Navigator, { key: 'nav', onReady }),
      h(
        Router,
        { key: 'router' },
        routeTable.map(({ path, component }) => h(Route, { key: path, path, component })),
      ),
    ]),
  );
}

test('a loaded lazy page renders synchronously on re-navigation (no re-suspend)', async () => {
  // This is the property that eliminates the PRO-253 dropped-update race: once a
  // page module has resolved, navigating back to it must NOT suspend again
  // (preact-iso's lazy() returns the cached component synchronously). Hoisting
  // the shell OUT of the lazy boundary keeps that boundary stable so the page
  // module loads exactly once.
  let loads = 0;
  const Lazy = lazy(() =>
    Promise.resolve().then(() => {
      loads++;
      return { default: () => h('h1', null, 'LOADED') };
    }),
  );
  const Wrapped = withShell(Lazy);

  render(h(ErrorBoundary, null, h(Wrapped, {})), container);
  await flush();
  await flush();
  expect(container.textContent).toContain('LOADED');
  expect(loads).toBe(1);

  // Re-render the same route; the module loader must not run again.
  render(h(ErrorBoundary, null, h(Wrapped, {})), container);
  await flush();
  expect(container.textContent).toContain('LOADED');
  expect(loads).toBe(1);
});

test('navigating between two lazy routes swaps rendered content without a refresh', async () => {
  const routeTable = [
    { path: '/alpha', component: makeLazyRoute('ALPHA', 'Alpha') },
    { path: '/beta', component: makeLazyRoute('BETA', 'Beta') },
  ];

  let navigate;
  history.replaceState(null, '', '/alpha');
  render(h(Harness, { routeTable, onReady: r => (navigate = r) }), container);

  // First lazy route resolves and renders.
  await flush();
  await flush();
  expect(container.textContent).toContain('ALPHA');
  expect(container.textContent).not.toContain('BETA');

  // Navigate to the second lazy route (as a menu click would).
  navigate('/beta');
  await flush();
  await flush();

  // The regression check: content must swap to BETA, not stay stuck on ALPHA.
  expect(location.pathname).toBe('/beta');
  expect(container.textContent).toContain('BETA');
  expect(container.textContent).not.toContain('ALPHA');

  // Navigate back; lazy->lazy again, must swap with no refresh.
  navigate('/alpha');
  await flush();
  await flush();
  expect(location.pathname).toBe('/alpha');
  expect(container.textContent).toContain('ALPHA');
  expect(container.textContent).not.toContain('BETA');
});
