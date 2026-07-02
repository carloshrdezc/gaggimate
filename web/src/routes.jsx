import { withShell, createLazyRoute } from './routeFactory.jsx';
import { Home } from './pages/Home/index.jsx';
import { Scales } from './pages/Scales/index.jsx';

// PRO-253: route table. Each entry exposes a STABLE `component` identity, which
// is exactly what preact-iso's <Router> needs to detect route changes and
// deliver lazy un-suspend updates reliably. See routeFactory.jsx for the
// shell-hoisting rationale.

// Eager, shell-less route for Home (renders its own header via DashboardMerged).
function HomeRoute({ navOpen, onNavToggle }) {
  return <Home navOpen={navOpen} onNavToggle={onNavToggle} />;
}

// Eager, shell-wrapped route for Scales (eagerly imported app-wide already).
const ScalesRoute = withShell(Scales);

const SettingsRoute = createLazyRoute(() => import('./pages/Settings/index.jsx'), 'Settings');
const OTARoute = createLazyRoute(() => import('./pages/OTA/index.jsx'), 'OTA');
const ProfileListRoute = createLazyRoute(
  () => import('./pages/ProfileList/index.jsx'),
  'ProfileList',
);
const ProfileEditRoute = createLazyRoute(
  () => import('./pages/ProfileEdit/index.jsx'),
  'ProfileEdit',
);
const ShotHistoryRoute = createLazyRoute(
  () => import('./pages/ShotHistory/index.jsx'),
  'ShotHistory',
);
const BeansRoute = createLazyRoute(() => import('./pages/Beans/index.jsx'), 'BeansPage');
const AutotuneRoute = createLazyRoute(() => import('./pages/Autotune/index.jsx'), 'Autotune');
const ShotAnalyzerRoute = createLazyRoute(
  () => import('./pages/ShotAnalyzer/index.jsx'),
  'ShotAnalyzer',
);
const ShotToProfileRoute = createLazyRoute(
  () => import('./pages/ShotToProfile/index.jsx'),
  'ShotToProfile',
);
const StatisticsRoute = createLazyRoute(
  () => import('./pages/Statistics/index.jsx'),
  'StatisticsPage',
);

/**
 * Declarative route table. Order: more specific (param) paths precede their
 * bare-path siblings; `default` (NotFound) is added by index.jsx as the last
 * <Route>. Exported so route/shell resolution can be asserted headlessly.
 */
export const routes = [
  { path: '/', component: HomeRoute },
  { path: '/profiles/:id', component: ProfileEditRoute },
  { path: '/profiles', component: ProfileListRoute },
  { path: '/beans', component: BeansRoute },
  { path: '/settings', component: SettingsRoute },
  { path: '/ota', component: OTARoute },
  { path: '/scales', component: ScalesRoute },
  { path: '/pidtune', component: AutotuneRoute },
  { path: '/history', component: ShotHistoryRoute },
  { path: '/analyzer/:source/:id', component: ShotAnalyzerRoute },
  { path: '/analyzer', component: ShotAnalyzerRoute },
  { path: '/statistics/:sourceAlias/:profileName', component: StatisticsRoute },
  { path: '/statistics', component: StatisticsRoute },
  { path: '/shots/:id/to-profile', component: ShotToProfileRoute },
];
