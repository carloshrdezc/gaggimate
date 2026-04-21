/**
 * ShotAnalyzer.jsx - Dark Precision restyle
 */
import { useState, useEffect, useContext, useRef } from 'preact/hooks';
import { useRoute } from 'preact-iso';
import { LibraryPanel } from './components/LibraryPanel';
import { AnalysisTable } from './components/AnalysisTable';
import { ShotChart } from './components/ShotChart';
import { calculateShotMetrics, detectAutoDelay } from './services/AnalyzerService';
import { libraryService } from './services/LibraryService';
import { notesService } from './services/NotesService';
import { ApiServiceContext } from '../../services/ApiService';
import { getDefaultColumns, cleanName, ANALYZER_DB_KEYS, loadFromStorage } from './utils/analyzerUtils';
import { buildStatisticsProfileHref } from '../Statistics/utils/statisticsRoute';
import { EmptyState } from './components/EmptyState.jsx';
import { Spinner } from '../../components/Spinner.jsx';

const clampNonNegativeDelay = v => {
  const p = Number(v);
  return Number.isFinite(p) ? Math.max(0, Math.round(p)) : 0;
};

const PROFILE_AUTO_MATCH_INITIAL_DELAY_MS = 250;
const PROFILE_AUTO_MATCH_RETRY_DELAY_MS = 450;
const PROFILE_AUTO_MATCH_MAX_ATTEMPTS = 4;

function findPreferredProfileMatch(allProfiles, shotProfileName, shotProfileId, shotSource) {
  const targetId = String(shotProfileId || '').trim();
  if (targetId) {
    const idMatches = allProfiles.filter(profile => {
      const profileId = String(profile.profileId || profile.id || '').trim();
      return profileId && profileId === targetId;
    });
    const preferredIdMatch = idMatches.find(profile => profile.source === shotSource) || idMatches[0] || null;
    if (preferredIdMatch) return preferredIdMatch;
  }
  const targetName = cleanName(shotProfileName).trim().toLowerCase();
  if (!targetName) return null;
  const nameMatches = allProfiles.filter(profile =>
    cleanName(profile.name || profile.label || '').trim().toLowerCase() === targetName,
  );
  return nameMatches.find(profile => profile.source === shotSource) || nameMatches[0] || null;
}

function getProfileLookupId(profileMatch) {
  return profileMatch.source === 'gaggimate' ? profileMatch.profileId || profileMatch.id : profileMatch.name;
}

function normalizeMatchedProfileSource(profileData, profileSource) {
  if (profileSource && (profileSource === 'gaggimate' || profileSource === 'browser') && !profileData?.source) {
    return { ...profileData, source: profileSource };
  }
  return profileData;
}

function shouldAutoScrollAnalyzerOnSelection() {
  const viewportWindow = globalThis.window;
  if (!viewportWindow || typeof viewportWindow.matchMedia !== 'function') return false;
  return viewportWindow.matchMedia('(max-width: 1023px)').matches;
}

async function loadPreferredAutoMatchedProfile(shotWithMetadata, allProfiles) {
  const preferredMatch = findPreferredProfileMatch(allProfiles, shotWithMetadata.profile, shotWithMetadata.profileId, shotWithMetadata.source);
  if (!preferredMatch) return null;
  const profileName = preferredMatch.label || preferredMatch.name;
  const profileId = getProfileLookupId(preferredMatch);
  const fullProfile = preferredMatch.data ? preferredMatch.data : await libraryService.loadProfile(profileId, preferredMatch.source);
  if (!fullProfile) return null;
  return { profile: normalizeMatchedProfileSource(fullProfile, preferredMatch.source), profileName };
}

export function ShotAnalyzer() {
  const apiService = useContext(ApiServiceContext);
  const { params } = useRoute();
  const [currentShot, setCurrentShot] = useState(null);
  const [loading, setLoading] = useState(false);
  const [currentProfile, setCurrentProfile] = useState(null);
  const [currentShotName, setCurrentShotName] = useState('No Shot Loaded');
  const [currentProfileName, setCurrentProfileName] = useState('No Profile Loaded');
  const [importMode, setImportMode] = useState('temp');
  const [isMatchingProfile, setIsMatchingProfile] = useState(false);
  const [isSearchingProfile, setIsSearchingProfile] = useState(false);
  const [activeColumns, setActiveColumns] = useState(() => {
    const userStandard = loadFromStorage(ANALYZER_DB_KEYS.USER_STANDARD);
    return userStandard ? new Set(userStandard) : getDefaultColumns();
  });
  const [settings, setSettings] = useState({ scaleDelay: 200, sensorDelay: 200, autoDelay: true });
  const [analysisResults, setAnalysisResults] = useState(null);
  const [pendingMobileAnalysisScroll, setPendingMobileAnalysisScroll] = useState(false);
  const analysisSectionRef = useRef(null);
  const profileMatchIdRef = useRef(0);
  const analysisIdRef = useRef(0);
  const profileSearchTimerRef = useRef(null);

  const handleSettingsChange = nextSettings => {
    setSettings(prevSettings => ({
      ...prevSettings,
      ...nextSettings,
      scaleDelay: clampNonNegativeDelay(nextSettings?.scaleDelay ?? prevSettings.scaleDelay),
      sensorDelay: clampNonNegativeDelay(nextSettings?.sensorDelay ?? prevSettings.sensorDelay),
      autoDelay: Boolean(nextSettings?.autoDelay ?? prevSettings.autoDelay),
    }));
  };

  const scheduleProfileAutoMatchRetry = (attempt, callback) => {
    if (attempt + 1 >= PROFILE_AUTO_MATCH_MAX_ATTEMPTS) return false;
    profileSearchTimerRef.current = setTimeout(() => { callback(attempt + 1); }, PROFILE_AUTO_MATCH_RETRY_DELAY_MS);
    return true;
  };

  useEffect(() => {
    return () => { if (profileSearchTimerRef.current) clearTimeout(profileSearchTimerRef.current); };
  }, []);

  useEffect(() => {
    const loadDeepLink = async () => {
      if (params.source && params.id) {
        let serviceSource = params.source;
        if (params.source === 'internal') serviceSource = 'gaggimate';
        if (params.source === 'external') serviceSource = 'browser';
        if (currentShot && currentShot.id === params.id && currentShot.source === serviceSource) return;
        try {
          setLoading(true);
          const shot = await libraryService.loadShot(params.id, serviceSource);
          if (shot) {
            shot.source = serviceSource;
            await handleShotLoad(shot, shot.name || params.id);
          }
        } catch (e) { console.error('Deep Link Load Failed:', e); }
      }
    };
    if (apiService) loadDeepLink();
  }, [params.source, params.id, apiService]);

  useEffect(() => {
    if (apiService) { libraryService.setApiService(apiService); notesService.setApiService(apiService); }
  }, [apiService]);

  useEffect(() => {
    if (!currentShot) { setAnalysisResults(null); return; }
    const id = ++analysisIdRef.current;
    setTimeout(() => { if (id !== analysisIdRef.current) return; performAnalysis(); }, 0);
  }, [currentShot, currentProfile, settings]);

  useEffect(() => {
    if (!pendingMobileAnalysisScroll || !currentShot) return;
    const timer = window.setTimeout(() => {
      analysisSectionRef.current?.scrollIntoView({ behavior: 'smooth', block: 'start' });
      setPendingMobileAnalysisScroll(false);
    }, 90);
    return () => window.clearTimeout(timer);
  }, [pendingMobileAnalysisScroll, currentShot]);

  const performAnalysis = () => {
    if (!currentShot) return;
    try {
      let usedSensorDelay = settings.sensorDelay;
      let isAutoAdjusted = false;
      if (settings.autoDelay && currentProfile) {
        const detection = detectAutoDelay(currentShot, currentProfile, settings.sensorDelay);
        usedSensorDelay = detection.delay;
        isAutoAdjusted = detection.auto;
      }
      const results = calculateShotMetrics(currentShot, currentProfile, {
        scaleDelayMs: settings.scaleDelay, sensorDelayMs: usedSensorDelay, isAutoAdjusted,
      });
      setAnalysisResults(results);
    } catch (e) { console.error('Analysis failed:', e); setAnalysisResults(null); }
  };

  const handleShotLoad = async (shotData, name) => {
    const shotWithMetadata = { ...shotData, source: shotData.source || importMode };
    setCurrentShot(shotWithMetadata);
    setCurrentShotName(name);
    if (profileSearchTimerRef.current) { clearTimeout(profileSearchTimerRef.current); profileSearchTimerRef.current = null; }
    setCurrentProfile(null);
    setCurrentProfileName(shotWithMetadata.profile ? cleanName(shotWithMetadata.profile) : 'No Profile Loaded');

    if (shotWithMetadata.profile) {
      const matchId = ++profileMatchIdRef.current;
      setIsMatchingProfile(true);
      setIsSearchingProfile(true);
      const attemptProfileAutoMatch = async (attempt = 0) => {
        profileSearchTimerRef.current = null;
        try {
          const allProfiles = await libraryService.getAllProfiles('both');
          if (matchId !== profileMatchIdRef.current) return;
          const matchedProfile = await loadPreferredAutoMatchedProfile(shotWithMetadata, allProfiles);
          if (matchId !== profileMatchIdRef.current) return;
          if (matchedProfile) {
            setCurrentProfile(matchedProfile.profile);
            setCurrentProfileName(matchedProfile.profileName);
            return;
          }
          if (scheduleProfileAutoMatchRetry(attempt, attemptProfileAutoMatch)) return;
        } catch (e) {
          if (matchId !== profileMatchIdRef.current) return;
          if (scheduleProfileAutoMatchRetry(attempt, attemptProfileAutoMatch)) return;
          console.warn('Profile auto-match failed:', e);
        } finally {
          if (matchId === profileMatchIdRef.current && !profileSearchTimerRef.current) {
            setIsMatchingProfile(false);
            setIsSearchingProfile(false);
          }
        }
      };
      profileSearchTimerRef.current = setTimeout(() => { attemptProfileAutoMatch(0); }, PROFILE_AUTO_MATCH_INITIAL_DELAY_MS);
    } else {
      profileMatchIdRef.current++;
      setIsMatchingProfile(false);
      setIsSearchingProfile(false);
    }
    setLoading(false);
  };

  const handleProfileLoad = (data, name, source) => {
    const nextProfile = normalizeMatchedProfileSource(data, source);
    setCurrentProfile(nextProfile);
    setCurrentProfileName(data?.label || data?.name || name);
  };

  const statsHref = buildStatisticsProfileHref({ source: currentProfile?.source, profileName: currentProfileName });

  if (loading) {
    return (
      <div class="space-y-3">
        {[1, 2, 3].map(i => (
          <div key={i} class="h-48 rounded-xl skeleton" style="background: linear-gradient(90deg, var(--bg-elevated) 0%, var(--border) 50%, var(--bg-elevated) 100%); background-size: 200% 100%; animation: shimmer 1.5s infinite;" />
        ))}
      </div>
    );
  }

  return (
    <div class="pb-20 space-y-6">
      {/* Page header */}
      <div class="flex items-center justify-between">
        <div>
          <h1 class="text-2xl font-semibold text-[--text-primary]">Deep Dive Shot Analyzer</h1>
          <p class="text-sm text-[--text-secondary] mt-1">Analyze shot profiles with precision metrics</p>
        </div>
      </div>

      {/* Library Panel */}
      <LibraryPanel
        currentShot={currentShot}
        currentProfile={currentProfile}
        currentShotName={currentShotName}
        currentProfileName={currentProfileName}
        onShotLoadStart={() => setLoading(true)}
        onShotLoad={handleShotLoad}
        onProfileLoad={handleProfileLoad}
        onShotUnload={() => { setCurrentShot(null); setCurrentShotName('No Shot Loaded'); setAnalysisResults(null); }}
        onProfileUnload={() => { setCurrentProfile(null); setCurrentProfileName('No Profile Loaded'); }}
        onShowStats={() => {
          sessionStorage.setItem('statsInitialContext', JSON.stringify({ profileName: currentProfileName, source: 'both' }));
        }}
        statsHref={statsHref}
        importMode={importMode}
        onImportModeChange={setImportMode}
        onShotLoadedFromLibrary={() => { if (shouldAutoScrollAnalyzerOnSelection()) setPendingMobileAnalysisScroll(true); }}
        isMatchingProfile={isMatchingProfile}
        isSearchingProfile={isSearchingProfile}
      />

      {currentShot ? (
        <div ref={analysisSectionRef} class="animate-fade-in space-y-4">
          <div class="rounded-xl p-5" style="background: var(--bg-elevated); border: 1px solid var(--border);">
            <ShotChart shotData={currentShot} results={analysisResults} />
          </div>
          {analysisResults && (
            <AnalysisTable
              results={analysisResults}
              activeColumns={activeColumns}
              onColumnsChange={setActiveColumns}
              settings={settings}
              onSettingsChange={handleSettingsChange}
              onAnalyze={performAnalysis}
            />
          )}
        </div>
      ) : (
        <EmptyState loading={loading} />
      )}
    </div>
  );
}