import { afterEach, describe, expect, it, vi } from 'vitest';
import { cleanup, renderHook, waitFor } from '@testing-library/preact';

import { useProfileData } from './useProfileData.js';

const proProfile = {
  type: 'pro',
  phases: [{ duration: 10, temperature: 93 }],
};

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

describe('useProfileData selected profile loading', () => {
  it('clears profile data when a switched profile fails to load', async () => {
    const api = {
      request: vi
        .fn()
        .mockResolvedValueOnce({ profile: proProfile })
        .mockRejectedValueOnce(new Error('profile B unavailable')),
    };
    const errorSpy = vi.spyOn(console, 'error').mockImplementation(() => {});
    const { result, rerender } = renderHook(
      ({ selectedProfileId }) => useProfileData(api, false, selectedProfileId),
      { initialProps: { selectedProfileId: 'profile-a' } },
    );

    await waitFor(() => expect(result.current.profileData).toEqual(proProfile));

    rerender({ selectedProfileId: 'profile-b' });

    await waitFor(() => expect(result.current.error.profile).toBeInstanceOf(Error));
    expect(result.current.profileData).toBeNull();
    expect(errorSpy).toHaveBeenCalled();
  });

  it('populates data and clears the error when retrying a failed profile load', async () => {
    const api = {
      request: vi
        .fn()
        .mockRejectedValueOnce(new Error('temporary failure'))
        .mockResolvedValueOnce({ profile: proProfile }),
    };
    vi.spyOn(console, 'error').mockImplementation(() => {});
    const { result } = renderHook(() => useProfileData(api, false, 'profile-a'));

    await waitFor(() => expect(result.current.error.profile).toBeInstanceOf(Error));
    expect(result.current.profileData).toBeNull();

    result.current.retry.profile();

    await waitFor(() => expect(result.current.profileData).toEqual(proProfile));
    expect(result.current.error.profile).toBeNull();
  });
});
