import { parseBinaryIndex, indexToShotList } from '../pages/ShotHistory/parseBinaryIndex.js';
import {
  buildShotHistoryArchive,
  importShotHistoryArchive,
} from '../pages/ShotHistory/historyArchive.js';
import { indexedDBService } from '../pages/ShotAnalyzer/services/IndexedDBService.js';
import { notesService } from '../pages/ShotAnalyzer/services/NotesService.js';
import {
  exportBeanData,
  getCurrentBeanSelection,
  restoreBeanData,
} from './beanManager.js';
import { getDashboardLayout, setDashboardLayout } from './dashboardManager.js';
import {
  getStoredGoogleDriveClientId,
  setStoredGoogleDriveClientId,
} from './googleDriveBackup.js';
import { getStoredTheme, setStoredTheme } from './themeManager.js';

function sanitizeProfile(profile) {
  return { ...profile };
}

async function fetchSettingsSnapshot() {
  const response = await fetch('/api/settings');
  if (!response.ok) {
    throw new Error(`Failed to load settings (HTTP ${response.status}).`);
  }
  return response.json();
}

async function fetchProfilesSnapshot(apiService) {
  const response = await apiService.request({ tp: 'req:profiles:list' });
  return Array.isArray(response.profiles) ? response.profiles.map(sanitizeProfile) : [];
}

async function fetchShotHistorySnapshot(apiService) {
  notesService.setApiService(apiService);
  const shots = [];

  const browserShots = await indexedDBService.getAllShots();
  for (const shot of browserShots) {
    const storageKey = shot.storageKey || shot.name || shot.id;
    const notes = await notesService.loadNotes(storageKey, 'browser');
    shots.push({
      ...shot,
      id: String(shot.id || storageKey),
      source: 'browser',
      notes,
      loaded: Array.isArray(shot.samples) && shot.samples.length > 0,
    });
  }

  const indexResponse = await fetch('/api/history/index.bin');
  if (indexResponse.ok) {
    const indexData = parseBinaryIndex(await indexResponse.arrayBuffer());
    const deviceShots = indexToShotList(indexData);
    for (const shot of deviceShots) {
      const notes = await notesService.loadNotes(String(shot.id), 'gaggimate');
      shots.push({
        ...shot,
        id: String(shot.id),
        source: 'gaggimate',
        notes,
        loaded: false,
      });
    }
  } else if (indexResponse.status !== 404) {
    throw new Error(`Failed to load shot history index (HTTP ${indexResponse.status}).`);
  }

  return buildShotHistoryArchive(shots);
}

export async function createBackupBundle(apiService) {
  const [settings, profiles, shotHistory] = await Promise.all([
    fetchSettingsSnapshot(),
    fetchProfilesSnapshot(apiService),
    fetchShotHistorySnapshot(apiService),
  ]);

  return {
    type: 'gaggimate-google-drive-backup',
    version: 1,
    exportedAt: new Date().toISOString(),
    web: {
      theme: getStoredTheme(),
      dashboardLayout: getDashboardLayout(),
      googleDriveClientId: getStoredGoogleDriveClientId(),
    },
    settings,
    profiles,
    beans: exportBeanData(),
    shotHistory,
    selectedBean: getCurrentBeanSelection(),
  };
}

function appendIfTruthy(formData, key, value) {
  if (value) {
    formData.append(key, 'on');
  }
}

async function restoreSettingsSnapshot(settings) {
  const formData = new FormData();
  for (const [key, value] of Object.entries(settings || {})) {
    if (value === undefined || value === null) continue;
    if (typeof value === 'boolean') {
      appendIfTruthy(formData, key, value);
    } else {
      formData.append(key, String(value));
    }
  }

  const response = await fetch('/api/settings', {
    method: 'POST',
    body: formData,
  });

  if (!response.ok) {
    throw new Error(`Failed to restore settings (HTTP ${response.status}).`);
  }

  return response.json();
}

async function restoreProfilesSnapshot(apiService, profiles) {
  for (const profile of profiles || []) {
    await apiService.request({ tp: 'req:profiles:save', profile });
  }
}

async function restoreSelectedProfile(apiService, profiles) {
  const selected = (profiles || []).find(profile => profile.selected);
  if (selected?.id) {
    await apiService.request({ tp: 'req:profiles:select', id: selected.id });
  }
}

export async function restoreBackupBundle(apiService, bundle) {
  if (bundle?.type !== 'gaggimate-google-drive-backup') {
    throw new Error('Unsupported backup format.');
  }

  notesService.setApiService(apiService);

  if (bundle.web?.theme) {
    setStoredTheme(bundle.web.theme);
  }
  if (bundle.web?.dashboardLayout) {
    setDashboardLayout(bundle.web.dashboardLayout);
  }
  if (bundle.web?.googleDriveClientId) {
    setStoredGoogleDriveClientId(bundle.web.googleDriveClientId);
  }

  if (bundle.settings) {
    await restoreSettingsSnapshot(bundle.settings);
  }
  if (bundle.profiles) {
    await restoreProfilesSnapshot(apiService, bundle.profiles);
    await restoreSelectedProfile(apiService, bundle.profiles);
  }
  if (bundle.beans) {
    restoreBeanData(bundle.beans);
  }
  if (bundle.selectedBean?.beanName !== undefined) {
    apiService.send({ tp: 'req:beans:select', name: bundle.selectedBean?.beanName || '' });
  }
  if (bundle.shotHistory) {
    await importShotHistoryArchive(bundle.shotHistory);
  }
}
