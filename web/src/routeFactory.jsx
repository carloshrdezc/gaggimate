import { lazy } from 'preact-iso';
import { PageShell } from './components/PageShell.jsx';

// PRO-253: pure route/shell factory helpers, kept free of page imports so the
// routing mechanism can be exercised headlessly in tests without pulling the
// full (FontAwesome-heavy) page graph.
//
// Background: the previous design made each route's `component` a `lazy()`
// wrapper whose loader resolved to a brand-new `props => <ShellRoute .../>`
// arrow (lazy -> ShellRoute -> PageShell -> Page). preact-iso's <Router> keys
// route-change detection on `incoming.props.component` identity and gates the
// lazy un-suspend re-render on a monotonic `count` ref; navigating between two
// lazy routes could drop the pending content-swap update, leaving the URL/menu
// advanced while <main> stayed stale until a hard refresh.
//
// Fix: hoist PageShell OUT of the lazy boundary. `withShell` wraps a STABLE,
// module-scope lazy page once with the shell chrome; `createLazyRoute` builds
// that stable lazy component. Each route therefore has a distinct, stable
// `component` identity and `lazy()`'s cache means a route stops suspending
// after its first load — so content swaps reliably, every navigation.

/**
 * Wrap a (typically lazy) page component with the standard PageShell chrome,
 * returning a STABLE component. The shell render lives OUTSIDE the lazy
 * boundary, so the lazy un-suspend update lands in a stable place in the tree
 * and can't be invalidated by a sibling route load.
 *
 * @param {import('preact').ComponentType<any>} Page
 * @returns {import('preact').FunctionComponent<any>}
 */
export function withShell(Page) {
  function ShellRoute({ navOpen, onNavToggle, default: _default, path: _path, ...pageProps }) {
    return (
      <PageShell navOpen={navOpen} onNavToggle={onNavToggle}>
        <Page {...pageProps} />
      </PageShell>
    );
  }
  ShellRoute.displayName = `WithShell(${Page.displayName || Page.name || 'Lazy'})`;
  return ShellRoute;
}

/**
 * Build a stable, shell-wrapped lazy route component from a module loader.
 * `lazy()` is invoked once (at the call site's module scope), so its load
 * promise/cache persists across navigations.
 *
 * @param {() => Promise<Record<string, any>>} load dynamic import
 * @param {string} exportName named export to render
 */
export function createLazyRoute(load, exportName) {
  const LazyPage = lazy(() => load().then(module => ({ default: module[exportName] })));
  LazyPage.displayName = exportName;
  return withShell(LazyPage);
}
