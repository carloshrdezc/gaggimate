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

let shotNotesCardUpdate;

vi.mock('./ShotNotesCard.jsx', () => ({
  default: ({ onNotesUpdate }) => {
    shotNotesCardUpdate = onNotesUpdate;
    return h('div', { 'data-testid': 'shot-notes-card' });
  },
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
  inferBeanForShot: vi.fn(shot => shot.beanName || shot.notes?.beanType || ''),
  inferBeanIdForShot: vi.fn(shot => shot.beanId || shot.notes?.beanId || ''),
  isBeanRecordedForShot: vi.fn(shot =>
    Boolean(shot.beanRecorded || shot.notes?.beanType || shot.notes?.beanId),
  ),
  listBeans: vi.fn(async () => []),
}));

vi.mock('../../utils/grinderManager.js', () => ({
  inferGrinderForShot: vi.fn(shot => shot.notes?.grinderName || shot.grinder || ''),
  inferGrindSettingForShot: vi.fn(shot => shot.grindSetting || shot.notes?.grindSetting || ''),
  isGrinderRecordedForShot: vi.fn(shot =>
    Boolean(shot.grinderRecorded || shot.notes?.grinderName),
  ),
}));

import { ApiServiceContext, machine } from '../../services/ApiService.js';
import { indexedDBService } from '../ShotAnalyzer/services/IndexedDBService.js';
import { notesService } from '../ShotAnalyzer/services/NotesService.js';
import { ShotHistory } from './index.jsx';
import {
  inferBeanForShot,
  inferBeanIdForShot,
  isBeanRecordedForShot,
} from '../../utils/beanManager.js';
import {
  inferGrinderForShot,
  inferGrindSettingForShot,
  isGrinderRecordedForShot,
} from '../../utils/grinderManager.js';

function renderShotHistory(apiService = {}) {
  return render(h(ApiServiceContext.Provider, { value: apiService }, h(ShotHistory, {})));
}

beforeEach(() => {
  vi.clearAllMocks();
  shotNotesCardUpdate = undefined;
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

  test('refreshes collapsed footer bean and grinder chips from hydrated notes', async () => {
    indexedDBService.getAllShots.mockResolvedValue([
      {
        id: 'metadata-browser-row',
        storageKey: 'browser-key',
        source: 'browser',
        profile: 'Browser archive shot',
        timestamp: 1000,
        duration: 25000,
        rating: 0,
        notes: { rating: 0, beanType: 'Old Bean', notes: 'stale embedded notes' },
        beanName: 'Old Bean',
        beanId: 'old-bean-id',
        beanRecorded: true,
      },
    ]);
    const persistedNotes = {
      id: 'browser-key',
      rating: 0,
      beanId: 'new-bean-id',
      beanType: 'New Bean',
      grinderName: 'Hydrated Grinder',
      notes: '',
    };
    Object.defineProperty(persistedNotes, '__shotHistoryPersistedNotes', {
      value: true,
      enumerable: false,
    });
    notesService.loadNotes.mockResolvedValue(persistedNotes);

    renderShotHistory();

    await screen.findByText(/1 \/ 1 shots/);
    expect(screen.getByLabelText('Bean: Old Bean')).toBeTruthy();
    expect(screen.queryByLabelText('Bean: New Bean')).toBeNull();
    expect(screen.queryByLabelText('Grinder: Hydrated Grinder')).toBeNull();

    await waitFor(() => {
      expect(screen.getByLabelText('Bean: New Bean')).toBeTruthy();
      expect(screen.getByLabelText('Grinder: Hydrated Grinder')).toBeTruthy();
    });
    expect(screen.queryByLabelText('Bean: Old Bean')).toBeNull();
    expect(screen.queryByTestId('shot-notes-card')).toBeNull();
  });

  test('refreshes footer bean from saved notes over stale pre-derived metadata', async () => {
    indexedDBService.getAllShots.mockResolvedValue([
      {
        id: 'save-browser-row',
        storageKey: 'browser-key',
        source: 'browser',
        profile: 'Browser archive shot',
        timestamp: 1000,
        duration: 25000,
        rating: 0,
        loaded: true,
        samples: [{ t: 0 }],
        notes: { rating: 0, beanType: 'Old Bean', notes: 'stale embedded notes' },
        beanName: 'Old Bean',
        beanId: 'old-bean-id',
        beanRecorded: true,
      },
    ]);
    notesService.loadNotes.mockResolvedValue({ rating: 0, beanType: 'Old Bean', notes: '' });

    renderShotHistory();

    await screen.findByText(/1 \/ 1 shots/);
    expect(screen.getByLabelText('Bean: Old Bean')).toBeTruthy();

    fireEvent.click(screen.getByLabelText('Expand shot details'));
    await screen.findByTestId('shot-notes-card');
    shotNotesCardUpdate({ rating: 0, beanId: 'new-bean-id', beanType: 'New Bean', notes: '' });

    await waitFor(() => {
      expect(screen.getByLabelText('Bean: New Bean')).toBeTruthy();
    });
    expect(screen.queryByLabelText('Bean: Old Bean')).toBeNull();
  });

  test('hydrates duplicate browser ids independently by storage key', async () => {
    indexedDBService.getAllShots.mockResolvedValue([
      {
        id: 'shared-import-id',
        storageKey: 'browser-key-a',
        source: 'browser',
        profile: 'Browser archive shot A',
        timestamp: 2000,
        duration: 25000,
        rating: 0,
      },
      {
        id: 'shared-import-id',
        storageKey: 'browser-key-b',
        source: 'browser',
        profile: 'Browser archive shot B',
        timestamp: 1000,
        duration: 25000,
        rating: 0,
      },
    ]);
    notesService.loadNotes.mockImplementation(async key => ({
      id: key,
      rating: 0,
      beanType: key === 'browser-key-a' ? 'Bean A' : 'Bean B',
      grinderName: key === 'browser-key-a' ? 'Grinder A' : 'Grinder B',
      notes: '',
    }));

    renderShotHistory();

    await screen.findByText(/2 \/ 2 shots/);

    await waitFor(() => {
      expect(screen.getByLabelText('Bean: Bean A')).toBeTruthy();
      expect(screen.getByLabelText('Grinder: Grinder A')).toBeTruthy();
      expect(screen.getByLabelText('Bean: Bean B')).toBeTruthy();
      expect(screen.getByLabelText('Grinder: Grinder B')).toBeTruthy();
    });
  });

  test('updates only the edited duplicate browser-id row after notes save', async () => {
    indexedDBService.getAllShots.mockResolvedValue([
      {
        id: 'shared-import-id',
        storageKey: 'browser-key-a',
        source: 'browser',
        profile: 'Browser archive shot A',
        timestamp: 2000,
        duration: 25000,
        rating: 0,
        loaded: true,
        samples: [{ t: 0 }],
        notes: { rating: 0, beanType: 'Bean A', grinderName: 'Grinder A', notes: '' },
      },
      {
        id: 'shared-import-id',
        storageKey: 'browser-key-b',
        source: 'browser',
        profile: 'Browser archive shot B',
        timestamp: 1000,
        duration: 25000,
        rating: 0,
        loaded: true,
        samples: [{ t: 0 }],
        notes: { rating: 0, beanType: 'Bean B', grinderName: 'Grinder B', notes: '' },
      },
    ]);
    notesService.loadNotes.mockImplementation(async key => ({
      id: key,
      rating: 0,
      beanType: key === 'browser-key-a' ? 'Bean A' : 'Bean B',
      grinderName: key === 'browser-key-a' ? 'Grinder A' : 'Grinder B',
      notes: '',
    }));

    renderShotHistory();

    await screen.findByText(/2 \/ 2 shots/);
    await waitFor(() => {
      expect(screen.getByLabelText('Bean: Bean A')).toBeTruthy();
      expect(screen.getByLabelText('Bean: Bean B')).toBeTruthy();
    });

    const expandButtons = screen.getAllByLabelText('Expand shot details');
    fireEvent.click(expandButtons[0]);
    await screen.findByTestId('shot-notes-card');
    shotNotesCardUpdate({
      id: 'browser-key-a',
      rating: 0,
      beanType: 'Edited Bean A',
      grinderName: 'Edited Grinder A',
      notes: '',
    });

    await waitFor(() => {
      expect(screen.getByLabelText('Bean: Edited Bean A')).toBeTruthy();
      expect(screen.getByLabelText('Grinder: Edited Grinder A')).toBeTruthy();
    });
    expect(screen.getByLabelText('Bean: Bean B')).toBeTruthy();
    expect(screen.getByLabelText('Grinder: Grinder B')).toBeTruthy();
    expect(screen.queryByLabelText('Bean: Edited Bean B')).toBeNull();
  });

  test('does not infer browser-local metadata fallbacks for explicitly blank persisted notes', async () => {
    indexedDBService.getAllShots.mockResolvedValue([
      {
        id: 'cleared-browser-row',
        storageKey: 'browser-key',
        source: 'browser',
        profile: 'Browser archive shot',
        timestamp: 1000,
        duration: 25000,
        rating: 0,
        beanName: 'Fallback Bean',
        beanId: 'fallback-bean-id',
        grinder: 'Fallback Grinder',
        grindSetting: '18',
      },
    ]);
    const persistedNotes = {
      id: 'browser-key',
      rating: 0,
      beanId: '',
      beanType: '',
      grinderName: '',
      grinder: '',
      grindSetting: '',
      notes: '',
    };
    Object.defineProperty(persistedNotes, '__shotHistoryPersistedNotes', {
      value: true,
      enumerable: false,
    });
    notesService.loadNotes.mockResolvedValue(persistedNotes);

    renderShotHistory();

    await screen.findByText(/1 \/ 1 shots/);
    expect(screen.getByLabelText('Bean: Fallback Bean')).toBeTruthy();
    expect(screen.getByLabelText('Grinder: Fallback Grinder')).toBeTruthy();
    inferBeanForShot.mockClear();
    inferBeanIdForShot.mockClear();
    isBeanRecordedForShot.mockClear();
    inferGrinderForShot.mockClear();
    inferGrindSettingForShot.mockClear();
    isGrinderRecordedForShot.mockClear();

    await waitFor(() => {
      expect(screen.queryByLabelText('Bean: Fallback Bean')).toBeNull();
      expect(screen.queryByLabelText('Grinder: Fallback Grinder')).toBeNull();
    });
    expect(inferBeanForShot).not.toHaveBeenCalled();
    expect(inferBeanIdForShot).not.toHaveBeenCalled();
    expect(isBeanRecordedForShot).not.toHaveBeenCalled();
    expect(inferGrinderForShot).not.toHaveBeenCalled();
    expect(inferGrindSettingForShot).not.toHaveBeenCalled();
    expect(isGrinderRecordedForShot).not.toHaveBeenCalled();
  });
});
