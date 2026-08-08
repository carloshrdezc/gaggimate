import { h } from 'preact';
import { afterEach, expect, test } from 'vitest';
import { cleanup, render, screen } from '@testing-library/preact';

import { StandbyBlock } from './DashboardMerged.jsx';

afterEach(cleanup);

test('announces a failed standby profile-curve load', () => {
  render(
    h(StandbyBlock, {
      profileName: 'Profile',
      curve: null,
      profileError: new Error('Failed to load'),
      onRetryProfile: () => {},
    }),
  );

  const status = screen.getByRole('status');
  expect(status.textContent).toContain('PROFILE CURVE FAILED TO LOAD');
  expect(status.getAttribute('aria-live')).toBe('polite');
});
