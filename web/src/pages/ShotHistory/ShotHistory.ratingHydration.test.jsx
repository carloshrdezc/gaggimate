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
  test('prefers newer persisted browser notes over stale embedded notes for list state', async () => {
    indexedDBService.getAllShots.mockResolvedValue([
      {
        id: 'stale-browser-row',
        storageKey: 'browser-key',
        source: 'browser',
        profile: 'Browser archive shot',
        timestamp: 1000,
        duration: 25000,
        rating: 4.2,
        notes: { rating: 4.2, notes: 'stale embedded notes' },
      },
    ]);
    notesService.loadNotes.mockResolvedValue({ rating: 8.4, notes: 'newer persisted notes' });

    renderShotHistory();

    await screen.findByText(/1 \/ 1 shots/);
    await waitFor(() => {
      expect(notesService.loadNotes).toHaveBeenCalledWith('browser-key', 'browser');
    });

    fireEvent.change(screen.getByDisplayValue('All Shots'), {
      target: { value: 'rated' },
    });

    await waitFor(() => {
      expect(screen.getByText(/1 \/ 1 shots/)).toBeTruthy();
      expect(screen.getByText('Browser archive shot')).toBeTruthy();
      expect(screen.getByText('8.4/10')).toBeTruthy();
    });
  });

  test('honors persisted cleared browser notes over stale embedded notes for list state', async () => {
    indexedDBService.getAllShots.mockResolvedValue([
      {
        id: 'cleared-browser-row',
        storageKey: 'browser-key',
        source: 'browser',
        profile: 'Browser archive shot',
        timestamp: 1000,
        duration: 25000,
        rating: 7.1,
        notes: { rating: 7.1, notes: 'stale embedded notes' },
      },
    ]);
    const persistedNotes = {
      id: 'browser-key',
      rating: 0,
      beanId: '',
      beanType: '',
      doseIn: '',
      doseOut: '',
      ratio: '',
      grinder: '',
      grindSetting: '',
      balanceTaste: 'balanced',
      notes: '',
    };
    Object.defineProperty(persistedNotes, '__shotHistoryPersistedNotes', {
      value: true,
      enumerable: false,
    });
    notesService.loadNotes.mockResolvedValue(persistedNotes);

    renderShotHistory();

    await screen.findByText(/1 \/ 1 shots/);
    await waitFor(() => {
      expect(notesService.loadNotes).toHaveBeenCalledWith('browser-key', 'browser');
    });

    fireEvent.change(screen.getByDisplayValue('All Shots'), {
      target: { value: 'rated' },
    });

    await waitFor(() => {
      expect(screen.getByText('Unrated')).toBeTruthy();
      expect(screen.queryByText('7.1/10')).toBeNull();
    });
  });
});
