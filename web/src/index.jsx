// Only load debug mode in development
if (import.meta.env.DEV) {
  await import('preact/debug');
}
import './style.css';
import { useCallback, useContext, useEffect, useMemo, useState } from 'preact/hooks';
import { h, render } from 'preact';
import { initializeTheme } from './utils/themeManager.js';
import { importLocalAuthHandoff } from './services/localAuthFetch.js';
import { LocationProvider, Router, Route, ErrorBoundary } from 'preact-iso';
import { NotFound } from './pages/_404.jsx';
import ApiService, { ApiServiceContext } from './services/ApiService.js';
import { Navigation } from './components/Navigation.jsx';
import { ConnectionBanner } from './components/ConnectionBanner.jsx';
import { routes } from './routes.jsx';

const apiService = new ApiService();
importLocalAuthHandoff(apiService);

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
        <ConnectionBanner />
        <main id='main-content' className='flex-1'>
          <div className='mx-auto w-full px-4 py-4 lg:px-8 lg:py-6 xl:container'>
            <div className='min-w-0'>
              <ErrorBoundary>
                <Router>
                  {routes.map(({ path, component }) => (
                    <Route
                      key={path}
                      path={path}
                      component={component}
                      navOpen={navOpen}
                      onNavToggle={onNavToggle}
                    />
                  ))}
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
