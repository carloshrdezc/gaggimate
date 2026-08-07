// PRO-632: the Beanconqueror export must be reachable from Shot History, and it
// must export exactly the filter-visible shots (the same scope the CSV export
// already uses) together with the whole bean library.
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

vi.mock('./parseBinaryIndex.js', () => ({
  parseBinaryIndex: vi.fn(() => ({})),
  indexToShotList: vi.fn(() => []),
}));

vi.mock('./parseBinaryShot.js', () => ({
  parseBinaryShot: vi.fn(() => ({ samples: [{ t: 0 }], volume: 36 })),
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
  inferBeanForShot: vi.fn(shot => shot.beanName || shot.notes?.beanType || ''),
  inferBeanIdForShot: vi.fn(shot => shot.beanId || shot.notes?.beanId || ''),
  isBeanRecordedForShot: vi.fn(shot =>
    Boolean(shot.beanRecorded || shot.notes?.beanType || shot.notes?.beanId),
  ),
  listBeans: vi.fn(async () => [{ id: 'bean-1', name: 'Pink Bourbon' }]),
}));

vi.mock('../../utils/grinderManager.js', () => ({
  inferGrinderForShot: vi.fn(shot => shot.notes?.grinderName || shot.grinder || ''),
  inferGrindSettingForShot: vi.fn(shot => shot.grindSetting || shot.notes?.grindSetting || ''),
  isGrinderRecordedForShot: vi.fn(shot => Boolean(shot.grinderRecorded || shot.notes?.grinderName)),
}));

vi.mock('../../utils/beanconqueror/beanconquerorDownload.js', () => ({
  downloadBeanconquerorBackup: vi.fn(async () => ({
    filename: 'Beanconqueror.zip',
    beanCount: 1,
    brewCount: 1,
  })),
}));

import { ApiServiceContext } from '../../services/ApiService.js';
import { indexedDBService } from '../ShotAnalyzer/services/IndexedDBService.js';
import { notesService } from '../ShotAnalyzer/services/NotesService.js';
import { downloadBeanconquerorBackup } from '../../utils/beanconqueror/beanconquerorDownload.js';
import { ShotHistory } from './index.jsx';

const BROWSER_SHOTS = [
  {
    id: 'shot-espresso',
    storageKey: 'key-espresso',
    source: 'browser',
    profile: 'Espresso',
    timestamp: 2000,
    duration: 27000,
    volume: 36,
    rating: 0,
  },
  {
    id: 'shot-lungo',
    storageKey: 'key-lungo',
    source: 'browser',
    profile: 'Lungo',
    timestamp: 1000,
    duration: 40000,
    volume: 90,
    rating: 0,
  },
];

function renderShotHistory(apiService = {}) {
  return render(h(ApiServiceContext.Provider, { value: apiService }, h(ShotHistory, {})));
}

beforeEach(() => {
  vi.clearAllMocks();
  window.history.replaceState(null, '', '/history');
  // jsdom's window.confirm/alert are "not implemented" stubs; stub them so the
  // export path is exercised rather than bailing out on a falsy confirm.
  vi.spyOn(window, 'confirm').mockReturnValue(true);
  vi.spyOn(window, 'alert').mockImplementation(() => {});
  vi.spyOn(window, 'open').mockReturnValue(null);
  indexedDBService.getAllShots.mockResolvedValue(BROWSER_SHOTS);
  indexedDBService.getShot.mockImplementation(async key =>
    BROWSER_SHOTS.find(shot => shot.storageKey === key),
  );
  notesService.loadNotes.mockResolvedValue({});
});

afterEach(() => {
  cleanup();
  localStorage.clear();
  vi.restoreAllMocks();
});

describe('ShotHistory Beanconqueror export control', () => {
  test('mounts a Beanconqueror export button in the header actions', async () => {
    renderShotHistory();
    await screen.findByText(/2 \/ 2 shots/);

    expect(screen.getByLabelText('Export Beanconqueror Backup')).toBeTruthy();
  });

  test('exports the bean library plus every filter-visible shot', async () => {
    renderShotHistory();
    await screen.findByText(/2 \/ 2 shots/);

    fireEvent.click(screen.getByLabelText('Export Beanconqueror Backup'));

    await waitFor(() => {
      expect(downloadBeanconquerorBackup).toHaveBeenCalledTimes(1);
    });

    const [payload] = downloadBeanconquerorBackup.mock.calls[0];
    expect(payload.beans).toEqual([{ id: 'bean-1', name: 'Pink Bourbon' }]);
    expect(payload.shots.map(shot => shot.id)).toEqual(['shot-espresso', 'shot-lungo']);
  });

  test('narrows the export to the search-filtered shots, matching the CSV scope', async () => {
    renderShotHistory();
    await screen.findByText(/2 \/ 2 shots/);

    fireEvent.change(screen.getByPlaceholderText('Search shots...'), {
      target: { value: 'lungo' },
    });
    await screen.findByText(/1 \/ 2 shots/);

    fireEvent.click(screen.getByLabelText('Export Beanconqueror Backup'));

    await waitFor(() => {
      expect(downloadBeanconquerorBackup).toHaveBeenCalledTimes(1);
    });
    expect(downloadBeanconquerorBackup.mock.calls[0][0].shots.map(shot => shot.id)).toEqual([
      'shot-lungo',
    ]);
  });

  test('does not export when the fresh-profile warning is declined', async () => {
    window.confirm.mockReturnValue(false);
    renderShotHistory();
    await screen.findByText(/2 \/ 2 shots/);

    fireEvent.click(screen.getByLabelText('Export Beanconqueror Backup'));

    await waitFor(() => {
      expect(window.confirm).toHaveBeenCalledTimes(1);
    });
    expect(downloadBeanconquerorBackup).not.toHaveBeenCalled();
  });

  test('surfaces a failed export instead of failing silently', async () => {
    downloadBeanconquerorBackup.mockRejectedValueOnce(new Error('zip exploded'));
    renderShotHistory();
    await screen.findByText(/2 \/ 2 shots/);

    fireEvent.click(screen.getByLabelText('Export Beanconqueror Backup'));

    await waitFor(() => {
      expect(window.alert).toHaveBeenCalledWith(
        expect.stringContaining('Beanconqueror export failed'),
      );
    });
  });
});
