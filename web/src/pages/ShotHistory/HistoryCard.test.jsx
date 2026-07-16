import { afterEach, describe, expect, test, vi } from 'vitest';
import { h } from 'preact';
import { cleanup, render, screen } from '@testing-library/preact';

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

vi.mock('../../components/VisualizerUploadModal.jsx', () => ({
  default: () => null,
}));

vi.mock('../../services/VisualizerService.js', () => ({
  visualizerService: {
    validateShot: () => true,
    uploadShot: vi.fn(),
  },
}));

import HistoryCard from './HistoryCard.jsx';
import { addToComparison, clearComparison } from '../../state/comparisonShots.js';

function makeShot(shotOverrides = {}) {
  return {
    id: 'shot-1',
    timestamp: 1710000000,
    duration: 25300,
    volume: 36.5,
    rating: 8.2,
    source: 'device',
    profileId: 'profile-1',
    profile: 'Turbo Bloom',
    beanName: 'Ethiopia Worka',
    grinder: 'Niche Zero',
    ...shotOverrides,
  };
}

function renderHistoryCard(shotOverrides = {}) {
  const shot = makeShot(shotOverrides);

  return render(h(HistoryCard, { shot, onDelete: vi.fn(), onLoad: vi.fn() }));
}

afterEach(() => {
  cleanup();
  clearComparison();
});

describe('HistoryCard footer metadata', () => {
  test('renders bean, profile, and grinder metadata chips with existing stats', () => {
    renderHistoryCard();

    expect(screen.getByText('25.3s')).toBeTruthy();
    expect(screen.getByText('36.5g')).toBeTruthy();
    expect(screen.getByText('8.2/10')).toBeTruthy();
    expect(screen.getByLabelText('Bean: Ethiopia Worka')).toBeTruthy();
    expect(screen.getByLabelText('Profile: Turbo Bloom')).toBeTruthy();
    expect(screen.getByLabelText('Grinder: Niche Zero')).toBeTruthy();
  });

  test('omits missing metadata chips without placeholders', () => {
    renderHistoryCard({ beanName: '', profile: '', grinder: '' });

    expect(screen.queryByLabelText(/Bean:/)).toBeNull();
    expect(screen.queryByLabelText(/Profile:/)).toBeNull();
    expect(screen.queryByLabelText(/Grinder:/)).toBeNull();
    expect(screen.queryByText('Unknown Profile', { selector: '[aria-label]' })).toBeNull();
  });

  test('keeps compare button conditional classes separated from the base class', () => {
    const shot = makeShot();
    addToComparison(shot);

    renderHistoryCard(shot);

    const compareButton = screen.getByLabelText('Remove from comparison');
    expect(compareButton.classList.contains('nd-action-btn')).toBe(true);
    expect(compareButton.classList.contains('nd-action-btn--active')).toBe(true);
    expect(compareButton.className).not.toContain('nd-action-btnnd-action-btn--active');
  });

  test('keeps disabled compare button utility classes separated from the base class', () => {
    addToComparison(makeShot({ id: 'comparison-1' }));
    addToComparison(makeShot({ id: 'comparison-2' }));
    addToComparison(makeShot({ id: 'comparison-3' }));
    addToComparison(makeShot({ id: 'comparison-4' }));

    renderHistoryCard({ id: 'shot-5' });

    const compareButton = screen.getByLabelText('Add to comparison');
    expect(compareButton.classList.contains('nd-action-btn')).toBe(true);
    expect(compareButton.classList.contains('cursor-not-allowed')).toBe(true);
    expect(compareButton.classList.contains('opacity-40')).toBe(true);
    expect(compareButton.className).not.toContain('nd-action-btncursor-not-allowed');
  });
});
