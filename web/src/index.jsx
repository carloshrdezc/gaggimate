// Only load debug mode in development
if (import.meta.env.DEV) {
  await import('preact/debug');
}
import './style.css';
import { useState } from 'preact/hooks';
import { initializeTheme } from './utils/themeManager.js';
import { render } from 'preact';
import { LocationProvider, Router, Route, ErrorBoundary } from 'preact-iso';
import { TopBar } from './components/TopBar.jsx';
import { Home } from './pages/Home/index.jsx';
import { NotFound } from './pages/_404.jsx';
import { Settings } from './pages/Settings/index.jsx';
import { OTA } from './pages/OTA/index.jsx';
import { Scales } from './pages/Scales/index.jsx';
import ApiService, { ApiServiceContext } from './services/ApiService.js';
import { ProfileList } from './pages/ProfileList/index.jsx';
import { ProfileEdit } from './pages/ProfileEdit/index.jsx';
import { BeansPage } from './pages/Beans/index.jsx';
import { Autotune } from './pages/Autotune/index.jsx';
import { ShotHistory } from './pages/ShotHistory/index.jsx';
import { ShotAnalyzer } from './pages/ShotAnalyzer/index.jsx';
import { StatisticsPage } from './pages/Statistics/index.jsx';

const apiService = new ApiService();

export function App() {
  return (
    <LocationProvider>
      <ApiServiceContext.Provider value={apiService}>
        <div class='relative min-h-screen bg-[--bg-base] text-[--text-primary]'>
          {/* Global Top Bar */}
          <TopBar />

          {/* Main content - offset for fixed topbar */}
          <main class='pt-12'>
            <div class='max-w-[1400px] mx-auto px-4 py-6'>
              <ErrorBoundary>
                <Router>
                  <Route path='/' component={Home} />
                  <Route path='/profiles' component={ProfileList} />
                  <Route path='/profiles/:id' component={ProfileEdit} />
                  <Route path='/beans' component={BeansPage} />
                  <Route path='/settings' component={Settings} />
                  <Route path='/ota' component={OTA} />
                  <Route path='/scales' component={Scales} />
                  <Route path='/pidtune' component={Autotune} />
                  <Route path='/history' component={ShotHistory} />
                  <Route path='/analyzer' component={ShotAnalyzer} />
                  <Route path='/statistics' component={StatisticsPage} />
                  <Route
                    path='/statistics/:sourceAlias/:profileName'
                    component={StatisticsPage}
                  />
                  <Route path='/analyzer/:source/:id' component={ShotAnalyzer} />
                  <Route default component={NotFound} />
                </Router>
              </ErrorBoundary>
            </div>
          </main>
        </div>
      </ApiServiceContext.Provider>
    </LocationProvider>
  );
}

// Must be called before render
initializeTheme();
render(<App />, document.getElementById('app'));