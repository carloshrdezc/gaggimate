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

function renderHistoryCard(shotOverrides = {}) {
  const shot = {
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

  return render(h(HistoryCard, { shot, onDelete: vi.fn(), onLoad: vi.fn() }));
}

afterEach(() => {
  cleanup();
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
});
