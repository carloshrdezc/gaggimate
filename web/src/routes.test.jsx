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
// about content-swap behaviour, not the shell markup. The mock stamps a
// `data-shell` marker so a test can prove the shell renders OUTSIDE the lazy
// boundary (i.e. before the page module resolves) — the property the fix adds.
vi.mock('./components/PageShell.jsx', () => ({
  PageShell: ({ children }) => h('div', { 'data-shell': 'yes' }, children),
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
  createLazyRoute(() => Promise.resolve({ [exportName]: () => h('h1', null, label) }), exportName);

// Like makeLazyRoute, but the dynamic import is DEFERRED: it stays pending until
// you call .resolve(). This lets a test open the PRO-253 race window — navigate
// to a second lazy route while the first route's import is still unresolved —
// instead of awaiting full module resolution before navigating (which silently
// closes the race window and lets buggy code pass).
const makeDeferredLazyRoute = (label, exportName) => {
  let resolveFn;
  const promise = new Promise(res => {
    resolveFn = res;
  });
  return {
    component: createLazyRoute(() => promise, exportName),
    resolve: () => resolveFn({ [exportName]: () => h('h1', null, label) }),
  };
};

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
  // PRO-253 regression guard. This MUST exercise the actual race: navigate to a
  // SECOND lazy route while the FIRST route's import is still PENDING. Awaiting
  // full module resolution before navigating (as the original test did) closes
  // the race window and green-lights the buggy pre-fix factory.
  //
  // What actually catches the regression (verified empirically against a
  // reconstructed pre-fix factory — the `lazy -> props => <ShellRoute
  // component={Page}/>` double-wrap with a fresh arrow per load):
  //
  //   THE LOAD-BEARING GUARD is the shell-hoist assertion: `withShell(lazy(...))`
  //   keeps the shell chrome OUTSIDE the suspense boundary, so PageShell renders
  //   synchronously while a page module is still loading. The pre-fix design
  //   couples shell+page inside one suspend, so nothing — not even the shell —
  //   renders until the import resolves. The `[data-shell]` assertion at the
  //   PENDING ALPHA checkpoint below (the `.not.toBeNull()` right after the
  //   first flush, before `alpha.resolve()`) is therefore RED on the bug and is
  //   the assertion that fails on the pre-fix factory.
  //
  //   The content-swap assertions (ALPHA/BETA text + `location.pathname`) are
  //   kept as SUPPLEMENTARY coverage of the lazy->lazy transition, but they do
  //   NOT independently fail on the count-ref race in this jsdom/preact-iso
  //   harness: preact-iso self-heals the dropped-update window via its
  //   monotonic `count` ref un-suspend gate (router.js ~L190-193, keyed off the
  //   ~L154 component-identity check), so content reliably lands on the right
  //   page even with the buggy factory once `[data-shell]` is neutralized.
  //   They guard against future regressions in the swap path but are not the
  //   guard that catches the original PRO-253 bug.
  const alpha = makeDeferredLazyRoute('ALPHA', 'Alpha');
  const beta = makeDeferredLazyRoute('BETA', 'Beta');
  const routeTable = [
    { path: '/alpha', component: alpha.component },
    { path: '/beta', component: beta.component },
  ];

  let navigate;
  history.replaceState(null, '', '/alpha');
  render(h(Harness, { routeTable, onReady: r => (navigate = r) }), container);

  // ALPHA's import is still PENDING. The shell chrome must ALREADY be mounted:
  // the fix renders PageShell outside the lazy boundary. (Pre-fix: no shell yet.)
  await flush();
  expect(container.querySelector('[data-shell]')).not.toBeNull();
  expect(container.textContent).not.toContain('ALPHA');

  // Resolve ALPHA; it renders inside the already-present shell.
  alpha.resolve();
  await flush();
  await flush();
  expect(container.textContent).toContain('ALPHA');
  expect(container.textContent).not.toContain('BETA');

  // Navigate to the second lazy route (as a menu click would) BEFORE its import
  // resolves — this is the PRO-253 race window.
  navigate('/beta');
  await flush();
  // Shell stays mounted across the lazy->lazy transition.
  expect(container.querySelector('[data-shell]')).not.toBeNull();

  // Resolve BETA; the regression check: content must swap to BETA, not stay
  // stuck on ALPHA.
  beta.resolve();
  await flush();
  await flush();
  expect(location.pathname).toBe('/beta');
  expect(container.textContent).toContain('BETA');
  expect(container.textContent).not.toContain('ALPHA');

  // Navigate back to the (now-loaded) ALPHA; lazy->lazy again, must swap with no
  // refresh and without re-suspending.
  navigate('/alpha');
  await flush();
  await flush();
  expect(location.pathname).toBe('/alpha');
  expect(container.textContent).toContain('ALPHA');
  expect(container.textContent).not.toContain('BETA');
});
