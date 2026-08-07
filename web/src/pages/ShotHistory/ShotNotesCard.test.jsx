import { afterEach, beforeEach, describe, expect, test, vi } from 'vitest';
import { h } from 'preact';
import { cleanup, render, screen, waitFor } from '@testing-library/preact';

vi.mock('@fortawesome/react-fontawesome', () => ({
  FontAwesomeIcon: () => h('span', { 'data-testid': 'fa-icon' }),
}));

vi.mock('../ShotAnalyzer/services/NotesService.js', () => ({
  notesService: {
    setApiService: vi.fn(),
    loadNotes: vi.fn(async () => ({})),
    saveNotes: vi.fn(async () => undefined),
  },
}));

vi.mock('../../utils/beanManager.js', () => ({
  listBeans: vi.fn(async () => []),
}));

vi.mock('../../utils/grinderManager.js', () => ({
  listGrinders: vi.fn(async () => []),
  recordGrinder: vi.fn(async () => []),
  resolveGrinderPrefill: vi.fn(() => ''),
}));

import { ApiServiceContext } from '../../services/ApiService.js';
import { notesService } from '../ShotAnalyzer/services/NotesService.js';
import ShotNotesCard from './ShotNotesCard.jsx';

function renderCard(shotOverrides = {}) {
  const shot = { id: 'shot-1', source: 'gaggimate', ...shotOverrides };
  return render(h(ApiServiceContext.Provider, { value: {} }, h(ShotNotesCard, { shot })));
}

beforeEach(() => {
  vi.clearAllMocks();
  notesService.loadNotes.mockResolvedValue({});
});

afterEach(() => {
  cleanup();
});

// PRO-631
describe('ShotNotesCard — start target temperature', () => {
  test('renders the derived start target with label and unit', async () => {
    renderCard({
      samples: [
        { t: 0, tt: 93.5, ct: 88 },
        { t: 250, tt: 93.5, ct: 90 },
      ],
    });

    await waitFor(() => expect(screen.getByText('Start Target Temperature')).toBeTruthy());
    expect(screen.getByText('93.5 °C')).toBeTruthy();
  });

  test('degrades to N/A when the shot log carries no valid target', async () => {
    renderCard({
      samples: [
        { t: 0, ct: 91 },
        { t: 250, ct: 92 },
      ],
    });

    await waitFor(() => expect(screen.getByText('Start Target Temperature')).toBeTruthy());
    expect(screen.getByText('N/A')).toBeTruthy();
  });

  test('shows only the START target for a phase-changing profile', async () => {
    renderCard({
      samples: [
        { t: 0, tt: 92, phaseName: 'Preinfusion' },
        { t: 250, tt: 96.5, phaseName: 'Ramp' },
      ],
    });

    await waitFor(() => expect(screen.getByText('Start Target Temperature')).toBeTruthy());
    expect(screen.getByText('92.0 °C')).toBeTruthy();
    expect(screen.queryByText('96.5 °C')).toBeNull();
  });

  test('is read-only: no input is rendered for it while editing', async () => {
    renderCard({ samples: [{ t: 0, tt: 93.5 }] });

    await waitFor(() => expect(screen.getByText('Edit')).toBeTruthy());
    screen.getByText('Edit').click();

    await waitFor(() => expect(screen.getByText('Save')).toBeTruthy());
    // Still a plain value block, not an editable control.
    const valueBlock = screen.getByLabelText('Start target temperature: 93.5 °C');
    expect(valueBlock.tagName).toBe('DIV');
    expect(valueBlock.querySelector('input')).toBeNull();
  });

  test('is not persisted into the mutable notes blob on save', async () => {
    renderCard({ samples: [{ t: 0, tt: 93.5 }] });

    await waitFor(() => expect(screen.getByText('Edit')).toBeTruthy());
    screen.getByText('Edit').click();
    await waitFor(() => expect(screen.getByText('Save')).toBeTruthy());
    screen.getByText('Save').click();

    await waitFor(() => expect(notesService.saveNotes).toHaveBeenCalled());
    const savedNotes = notesService.saveNotes.mock.calls.at(-1)[2];
    expect(savedNotes).not.toHaveProperty('startTargetTemperature');
    expect(JSON.stringify(savedNotes)).not.toContain('93.5');
  });

  test('existing dose/grind note fields still render alongside it', async () => {
    notesService.loadNotes.mockResolvedValue({
      doseIn: '18.0',
      doseOut: '36.0',
      grindSetting: '2.5',
    });

    renderCard({ samples: [{ t: 0, tt: 93.5 }] });

    await waitFor(() => expect(screen.getByText('Start Target Temperature')).toBeTruthy());
    expect(screen.getByText('18.0')).toBeTruthy();
    expect(screen.getByText('36.0')).toBeTruthy();
    expect(screen.getByText('2.5')).toBeTruthy();
  });
});
