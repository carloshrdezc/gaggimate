// Only load debug mode in development
if (import.meta.env.DEV) {
  await import('preact/debug');
}
import './style.css';
import { useCallback, useContext, useEffect, useMemo, useState } from 'preact/hooks';
import { h, render } from 'preact';
import { initializeTheme } from './utils/themeManager.js';
import { LocationProvider, Router, Route, ErrorBoundary, lazy } from 'preact-iso';
import { PageShell } from './components/PageShell.jsx';
import { Home } from './pages/Home/index.jsx';
import { NotFound } from './pages/_404.jsx';
import ApiService, { ApiServiceContext } from './services/ApiService.js';
import { Navigation } from './components/Navigation.jsx';
import { Scales } from './pages/Scales/index.jsx';

function ShellRoute({
  component: Component,
  navOpen,
  onNavToggle,
  default: _defaultRoute,
  path: _path,
  ...pageProps
}) {
  return (
    <PageShell navOpen={navOpen} onNavToggle={onNavToggle}>
      <Component {...pageProps} />
    </PageShell>
  );
}

function createLazyShellRoute(load, exportName) {
  return lazy(() =>
    load().then(module => {
      const Component = module[exportName];
      return {
        default: props => <ShellRoute {...props} component={Component} />,
      };
    }),
  );
}

const SettingsRoute = createLazyShellRoute(
  () => import('./pages/Settings/index.jsx'),
  'Settings',
);
const OTARoute = createLazyShellRoute(() => import('./pages/OTA/index.jsx'), 'OTA');
const ProfileListRoute = createLazyShellRoute(
  () => import('./pages/ProfileList/index.jsx'),
  'ProfileList',
);
const ProfileEditRoute = createLazyShellRoute(
  () => import('./pages/ProfileEdit/index.jsx'),
  'ProfileEdit',
);
const ShotHistoryRoute = createLazyShellRoute(
  () => import('./pages/ShotHistory/index.jsx'),
  'ShotHistory',
);
const BeansRoute = createLazyShellRoute(() => import('./pages/Beans/index.jsx'), 'BeansPage');
const AutotuneRoute = createLazyShellRoute(
  () => import('./pages/Autotune/index.jsx'),
  'Autotune',
);
const ShotAnalyzerRoute = createLazyShellRoute(
  () => import('./pages/ShotAnalyzer/index.jsx'),
  'ShotAnalyzer',
);
const ShotToProfileRoute = createLazyShellRoute(
  () => import('./pages/ShotToProfile/index.jsx'),
  'ShotToProfile',
);
const StatisticsRoute = createLazyShellRoute(
  () => import('./pages/Statistics/index.jsx'),
  'StatisticsPage',
);
const ScalesRoute = props => <ShellRoute {...props} component={Scales} />;

const apiService = new ApiService();

function AppContent() {
  const [navOpen, setNavOpen] = useState(false);
  const onNavToggle = useCallback(() => setNavOpen(o => !o), []);

  useEffect(() => {
    document.body.classList.toggle('nav-drawer-open', navOpen);
    return () => {
      document.body.classList.remove('nav-drawer-open');
    };
  }, [navOpen]);

  return (
    <div
      className='dm-shell relative min-h-screen overflow-hidden'
      style={{ background: 'var(--dm-bg-0)' }}
    >
      <div className='app-shell-glow pointer-events-none absolute inset-0' />
      <div className='relative flex min-h-screen flex-col'>
        <a href='#main-content' className='skip-link'>
          Skip to main content
        </a>
        <Navigation open={navOpen} onClose={() => setNavOpen(false)} />
        <main id='main-content' className='flex-1'>
          <div className='mx-auto w-full px-4 py-4 lg:px-8 lg:py-6 xl:container'>
            <div className='min-w-0'>
              <ErrorBoundary>
                <Router>
                  <Route
                    path='/'
                    component={() => <Home navOpen={navOpen} onNavToggle={onNavToggle} />}
                  />
                  <Route
                    path='/profiles'
                    component={ProfileListRoute}
                    navOpen={navOpen}
                    onNavToggle={onNavToggle}
                  />
                  <Route
                    path='/profiles/:id'
                    component={ProfileEditRoute}
                    navOpen={navOpen}
                    onNavToggle={onNavToggle}
                  />
                  <Route
                    path='/beans'
                    component={BeansRoute}
                    navOpen={navOpen}
                    onNavToggle={onNavToggle}
                  />
                  <Route
                    path='/settings'
                    component={SettingsRoute}
                    navOpen={navOpen}
                    onNavToggle={onNavToggle}
                  />
                  <Route
                    path='/ota'
                    component={OTARoute}
                    navOpen={navOpen}
                    onNavToggle={onNavToggle}
                  />
                  <Route
                    path='/scales'
                    component={ScalesRoute}
                    navOpen={navOpen}
                    onNavToggle={onNavToggle}
                  />
                  <Route
                    path='/pidtune'
                    component={AutotuneRoute}
                    navOpen={navOpen}
                    onNavToggle={onNavToggle}
                  />
                  <Route
                    path='/history'
                    component={ShotHistoryRoute}
                    navOpen={navOpen}
                    onNavToggle={onNavToggle}
                  />
                  <Route
                    path='/analyzer'
                    component={ShotAnalyzerRoute}
                    navOpen={navOpen}
                    onNavToggle={onNavToggle}
                  />
                  <Route
                    path='/statistics'
                    component={StatisticsRoute}
                    navOpen={navOpen}
                    onNavToggle={onNavToggle}
                  />
                  <Route
                    path='/statistics/:sourceAlias/:profileName'
                    component={StatisticsRoute}
                    navOpen={navOpen}
                    onNavToggle={onNavToggle}
                  />
                  <Route
                    path='/analyzer/:source/:id'
                    component={ShotAnalyzerRoute}
                    navOpen={navOpen}
                    onNavToggle={onNavToggle}
                  />
                  <Route
                    path='/shots/:id/to-profile'
                    component={ShotToProfileRoute}
                    navOpen={navOpen}
                    onNavToggle={onNavToggle}
                  />
                  <Route default component={NotFound} />
                </Router>
              </ErrorBoundary>
            </div>
          </div>
        </main>
      </div>
    </div>
  );
}

// preact-iso has no built-in base support; strip the deploy base from path so
// routes like path='/' match when the app lives at /gaggimate/.
const _BASE = import.meta.env.BASE_URL?.replace(/\/$/, '') ?? '';

function BasePathProvider({ children }) {
  const ctx = useContext(LocationProvider.ctx);
  const value = useMemo(() => {
    const path =
      _BASE && ctx.path.startsWith(_BASE) ? ctx.path.slice(_BASE.length) || '/' : ctx.path;
    return { ...ctx, path };
  }, [ctx]);
  return h(LocationProvider.ctx.Provider, { value }, children);
}

export function App() {
  return (
    <LocationProvider>
      <BasePathProvider>
        <ApiServiceContext.Provider value={apiService}>
          <AppContent />
        </ApiServiceContext.Provider>
      </BasePathProvider>
    </LocationProvider>
  );
}

// Must be called before render
initializeTheme();
render(<App />, document.getElementById('app'));
