import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { h } from 'preact';
import { act, cleanup, fireEvent, render, screen } from '@testing-library/preact';

// Regression test for PRO-574 (test-only follow-up to PR #562).
//
// What this guards: a Google Drive restore completes -> Settings'
// `onRestoreComplete` bumps `gen` -> the `settings/${gen}` useQuery key changes
// -> the form unmounts behind the loading spinner and then remounts fresh from
// the refetched settings. This drives that end-to-end restore path and asserts
// the user-observable outcome: a plugin that was already enabled stays expanded,
// with no click into its sub-card.
//
// Why it exists alongside PluginCard.test.jsx: that test unit-checks the
// `useState(enabled)` initialiser on PluginSubCard in isolation. At the Settings
// level, expansion instead relies on TWO cooperating mechanisms — the refetch
// briefly resets formData to `{}`, so the sub-card sees enabled=false then true,
// which also trips the OFF->ON auto-expand effect. This is the acceptance guard
// for that whole remount path (the trigger the reviewer flagged as uncovered).
//
// Failure mode it catches: a plugin left collapsed after a Drive restore.

const { authenticatedFetch, settingsWithHomekitEnabled } = vi.hoisted(() => ({
  authenticatedFetch: vi.fn(),
  settingsWithHomekitEnabled: {
    pid: '3.000,0.100,40.000,0.000',
    autowakeupSchedules: '07:00|1111111',
    standbyBrightness: 8,
    wifiSsid: 'net',
    wifiPassword: '',
    mdnsName: 'gaggimate',
    // HomeKit already enabled in the settings fetched from /api/settings, both
    // before and after the restore — this is the "already enabled" precondition
    // PR #562 targeted. The restore does not flip it on; the sub-card must be
    // expanded purely because it remounts with enabled=true.
    homekit: true,
  },
}));

// Stateful useQuery mock keyed by the query key string (`settings/${gen}`), so
// changing `gen` (what onRestoreComplete does) drives a realistic
// loading -> loaded transition rather than returning a static object. On the
// first render for a never-seen key we report isLoading=true with no data
// (mirroring a refetch in flight -> Settings shows the spinner and unmounts the
// form); a test-driven `resolveAll()` then flips that key to loaded so the
// form remounts. The initial key `settings/0` is pre-seeded as already loaded.
const queryController = vi.hoisted(() => {
  const loaded = new Map(); // key -> boolean (true once resolved)
  let rerender = () => {};
  return {
    loaded,
    setRerender(fn) {
      rerender = fn;
    },
    reset() {
      // Called from beforeEach to isolate each scenario. If you add more it()
      // blocks to this file, confirm this reset() still fully resets whatever
      // state your new case touches so scenarios stay independent.
      loaded.clear();
      loaded.set('settings/0', true); // initial mount is already loaded
    },
    // Mark every currently-pending key as loaded and force a re-render.
    resolveAll() {
      for (const key of loaded.keys()) loaded.set(key, true);
      rerender();
    },
    read(key) {
      if (!loaded.has(key)) {
        // First time we see this key (e.g. settings/1 after a restore): the
        // refetch is "in flight" -> loading, no data. Records the key so a
        // later resolveAll() can flip it.
        loaded.set(key, false);
        return { isLoading: true, data: undefined };
      }
      const isReady = loaded.get(key);
      return isReady
        ? { isLoading: false, data: settingsWithHomekitEnabled }
        : { isLoading: true, data: undefined };
    },
  };
});

vi.mock('preact-fetching', () => ({
  useQuery: key => queryController.read(key),
}));

vi.mock('../../services/localAuthFetch.js', () => ({
  authenticatedFetch,
  bootstrapLocalAuth: vi.fn(),
  getLocalAuthToken: () => null,
  isValidLocalAuthToken: token => /^[0-9a-f]{32}$/.test(String(token).trim()),
  LOCAL_AUTH_TOKEN_ERROR: 'err',
  localAuthDownloadUrl: url => url,
  localAuthHandoffUrl: hostname => `http://${hostname}.local/#localAuthToken=t`,
  bootstrapLocalAuthFromHash: vi.fn(),
  MDNS_NAME_ERROR: 'mdns err',
}));

// Use the REAL PluginCard (the component under test). Stub only the FontAwesome
// icon and the homekit image asset it pulls in.
vi.mock('@fortawesome/react-fontawesome', () => ({ FontAwesomeIcon: () => null }));
vi.mock('../../assets/homekit.png', () => ({ default: 'homekit.png' }));

// Stub GoogleDriveBackupCard down to a single button that invokes the real
// `onRestoreComplete` callback Settings passes in (which bumps `gen`). This is
// the exact hook the real card fires after a successful restore, without
// dragging in the Drive OAuth/network machinery.
vi.mock('./GoogleDriveBackupCard.jsx', () => ({
  GoogleDriveBackupCard: ({ onRestoreComplete }) =>
    h(
      'button',
      { type: 'button', onClick: () => onRestoreComplete?.() },
      'simulate-restore-complete',
    ),
}));

vi.mock('../../components/ImportButton.jsx', () => ({ ImportButton: () => null }));

import { ApiServiceContext, machine } from '../../services/ApiService.js';
import { Settings } from './index.jsx';

// HomeKit sub-card body has stable, unique text. "Visible" = present in the DOM
// AND not inside a `hidden` wrapper (PRO-572 keeps enabled bodies mounted and
// hides collapsed ones via the `hidden` attribute).
const HOMEKIT_BODY = /Open the Home app on your iOS device/i;
function homekitBodyVisible() {
  const el = screen.queryByText(HOMEKIT_BODY);
  return el != null && el.closest('[hidden]') == null;
}

function renderSettings() {
  return render(
    h(ApiServiceContext.Provider, { value: { authenticateLocal: vi.fn() } }, h(Settings)),
  );
}

beforeEach(() => {
  queryController.reset();
  authenticatedFetch.mockResolvedValue({ ok: true, json: async () => ({}) });
  vi.stubGlobal('alert', vi.fn());
  vi.spyOn(console, 'error').mockImplementation(() => {});
  vi.stubGlobal('navigator', { clipboard: { writeText: vi.fn() } });
  machine.value = {
    ...machine.value,
    capabilities: { ...machine.value.capabilities, ledControl: true },
  };
});

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});

describe('Settings PluginSubCard survives a Drive-restore remount expanded (PRO-574)', () => {
  it('keeps an already-enabled plugin expanded after onRestoreComplete triggers a refetch remount, with no click', async () => {
    const { rerender } = renderSettings();
    // Wire the query controller so resolveAll() re-renders this instance.
    queryController.setRerender(() =>
      rerender(
        h(ApiServiceContext.Provider, { value: { authenticateLocal: vi.fn() } }, h(Settings)),
      ),
    );

    // Expand the Plugins card so the HomeKit sub-card is reachable. HomeKit is
    // enabled in the fetched settings, so its body is already visible here
    // (that is PR #562's initial-mount behaviour). openMap.plugins lives on the
    // Settings component, which does NOT unmount across the gen bump.
    fireEvent.click(screen.getByRole('button', { name: 'Plugins' }));
    expect(homekitBodyVisible()).toBe(true);

    // Fire the Drive-restore-complete callback -> Settings bumps `gen` ->
    // useQuery key becomes `settings/1`, which the mock reports as loading:
    // the form (incl. PluginCard) unmounts behind the spinner.
    await act(async () => {
      fireEvent.click(screen.getByRole('button', { name: 'simulate-restore-complete' }));
    });

    // Mid-restore: the form is gone (the loading-spinner branch is shown), so
    // the HomeKit body has been unmounted from the DOM. Assert both the body is
    // gone AND the loading branch's recovery card is present, to prove this is a
    // genuine unmount/remount cycle (not a same-instance re-render).
    expect(screen.queryByText(HOMEKIT_BODY)).toBeNull();
    expect(screen.getByLabelText('Paste admin token')).toBeTruthy();

    // The refetch resolves -> isLoading flips false -> the form REMOUNTS fresh
    // with HomeKit still enabled in the refetched formData.
    await act(async () => {
      queryController.resolveAll();
    });

    // THE ASSERTION: after the refetch-driven remount, the HomeKit sub-card is
    // expanded WITHOUT any click on its chevron. This is the user-observable
    // outcome PR #562 was about and that the reviewer flagged as uncovered by
    // the PluginCard-only test: pre-#562 an already-enabled plugin could render
    // collapsed after a Drive restore, leaving the user staring at an empty
    // plugin card until they clicked to expand it manually.
    expect(homekitBodyVisible()).toBe(true);
    // And the collapse chevron is present (proves the sub-card is enabled and
    // rendered its expanded controls, not just that some text leaked through).
    expect(screen.getByRole('button', { name: /Collapse HomeKit/ })).toBeTruthy();
  });
});
