import { afterEach, expect, test, vi } from 'vitest';
import { h } from 'preact';
import { cleanup, fireEvent, render, screen } from '@testing-library/preact';

import { ManualConsole } from './DashboardMerged.jsx';

const baseProps = {
  active: false,
  finished: false,
  draft: { targetType: 'pressure', pressure: 9, flow: 2, temperature: 93 },
  selectedBean: '',
  dose: 18,
  currentWeight: 0,
  bluetoothConnected: false,
  scaleName: '',
  pressure: 0,
  flow: 0,
  temperature: 93,
  controlLabels: { pressure: 'PRESSURE TARGET', flow: 'FLOW LIMIT' },
  isBeanDropdownOpen: true,
  beanOptions: [],
  loadingBeans: false,
  beanError: 'Failed to load',
  primaryAction: vi.fn(),
  primaryActionAccent: 'var(--dm-accent)',
  primaryActionLabel: 'START MANUAL',
  onBeanClick: vi.fn(),
  onBeanSelect: vi.fn(),
  onBeanDropdownClose: vi.fn(),
  onDoseCommit: vi.fn(),
  onEditingChange: vi.fn(),
  onManualUpdate: vi.fn(),
};

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

test('retries and recovers a failed manual-console bean load', () => {
  const onBeanRetry = vi.fn();
  const { rerender } = render(h(ManualConsole, { ...baseProps, onBeanRetry }));

  expect(screen.getByRole('status').textContent).toBe('FAILED TO LOAD SELECT BEAN');
  fireEvent.click(screen.getByRole('button', { name: 'RETRY' }));
  expect(onBeanRetry).toHaveBeenCalledTimes(1);

  rerender(h(ManualConsole, {
    ...baseProps,
    onBeanRetry,
    beanError: null,
    beanOptions: [{ id: 'bean-1', name: 'Recovered bean' }],
  }));

  expect(screen.getByRole('button', { name: 'Recovered bean' })).toBeTruthy();
});
