import { afterEach, beforeEach, describe, expect, test, vi } from 'vitest';
import { h } from 'preact';
import { render, screen, cleanup, fireEvent, waitFor } from '@testing-library/preact';

vi.mock('@fortawesome/react-fontawesome', () => ({
  FontAwesomeIcon: () => h('span', { 'data-testid': 'fa-icon' }),
}));

vi.mock('preact-iso', () => ({
  useLocation: () => ({ path: '/history', route: vi.fn() }),
}));

vi.mock('./HistoryChart.jsx', () => ({
  HistoryChart: () => h('div', { 'data-testid': 'history-chart' }),
}));

vi.mock('./ShotNotesCard.jsx', () => ({
  default: () => h('div', { 'data-testid': 'shot-notes-card' }),
}));

vi.mock('../ShotAnalyzer/services/IndexedDBService.js', () => ({
  indexedDBService: {
    getAllShots: vi.fn(),
    getShot: vi.fn(),
    deleteShot: vi.fn(),
    deleteNotes: vi.fn(),
  },
}));

vi.mock('../ShotAnalyzer/services/NotesService.js', () => ({
  notesService: {
    setApiService: vi.fn(),
    loadNotes: vi.fn(),
  },
}));

vi.mock('../../utils/beanManager.js', () => ({
  inferBeanForShot: shot => shot.beanName || '',
  inferBeanIdForShot: shot => shot.beanId || '',
  isBeanRecordedForShot: shot => Boolean(shot.beanRecorded),
  listBeans: vi.fn(async () => []),
}));

vi.mock('../../utils/grinderManager.js', () => ({
  inferGrinderForShot: shot => shot.grinder || '',
  inferGrindSettingForShot: shot => shot.grindSetting || '',
  isGrinderRecordedForShot: shot => Boolean(shot.grinderRecorded),
}));

import { ApiServiceContext, machine } from '../../services/ApiService.js';
import { indexedDBService } from '../ShotAnalyzer/services/IndexedDBService.js';
import { notesService } from '../ShotAnalyzer/services/NotesService.js';
import { ShotHistory } from './index.jsx';

function renderShotHistory(apiService = {}) {
  return render(h(ApiServiceContext.Provider, { value: apiService }, h(ShotHistory, {})));
}

beforeEach(() => {
  vi.clearAllMocks();
  machine.value = { ...machine.value, connected: false };
  window.history.replaceState(null, '', '/history');
});

afterEach(() => {
  cleanup();
});

describe('ShotHistory visible-page rating hydration', () => {
  test('syncs top-level ratings from existing notes before rated filter uses the visible page', async () => {
    indexedDBService.getAllShots.mockResolvedValue([
      {
        id: 'low-top-level',
        storageKey: 'low-key',
        source: 'browser',
        profile: 'Stale top-level',
        timestamp: 1000,
        duration: 25000,
        rating: 2,
        notes: { rating: 8.7, notes: 'imported decimal rating' },
      },
      {
        id: 'missing-top-level',
        storageKey: 'missing-key',
        source: 'browser',
        profile: 'Missing top-level',
        timestamp: 900,
        duration: 26000,
        notes: { rating: 9.4, notes: 'browser archive notes' },
      },
    ]);

    renderShotHistory();

    await screen.findByText(/2 \/ 2 shots/);
    expect(notesService.loadNotes).not.toHaveBeenCalled();

    fireEvent.change(screen.getByDisplayValue('All Shots'), {
      target: { value: 'rated' },
    });

    await waitFor(() => {
      expect(screen.getByText(/2 \/ 2 shots/)).toBeTruthy();
      expect(screen.getByText('Stale top-level')).toBeTruthy();
      expect(screen.getByText('Missing top-level')).toBeTruthy();
      expect(screen.getByText('8.7/10')).toBeTruthy();
      expect(screen.getByText('9.4/10')).toBeTruthy();
    });
  });
});
