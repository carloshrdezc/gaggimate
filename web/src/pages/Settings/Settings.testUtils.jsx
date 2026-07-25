import { cleanup, render } from '@testing-library/preact';
import { h } from 'preact';
import { vi } from 'vitest';

// Shared test scaffolding for the Settings page test suites
// (Settings.formData.test.jsx, Settings.pluginRemount.test.jsx,
// Settings.saveError.test.jsx, ...). Extracted per PRO-580 (ref PRO-578) to
// remove the near-verbatim duplication of the vi.mock scaffolding,
// renderSettings() helper, and beforeEach/afterEach boilerplate across those
// files.
//
// IMPORTANT — vi.mock hoisting: `vi.mock(...)` calls are hoisted to the top of
// the file that contains them, so they cannot live in this shared module and
// still apply to a consuming test file. Each test file therefore keeps its own
// `vi.mock('...', factory)` calls, but delegates the factory body to the
// helpers below so the mocked module shapes stay identical across suites. The
// factories that DIFFER per suite (PluginCard and GoogleDriveBackupCard: some
// suites stub them, some use the real component) intentionally stay in the
// individual files. The fetched-settings object also stays inline in each
// suite's `vi.hoisted(...)` block: `vi.hoisted` runs before this module's
// imports resolve, so it cannot reference an export from here.

// Factory for the `../../services/localAuthFetch.js` mock module. Pass the
// hoisted `authenticatedFetch` spy so the mocked `authenticatedFetch` export is
// the same reference the test asserts against.
export function localAuthFetchMock(authenticatedFetch) {
  return {
    authenticatedFetch,
    bootstrapLocalAuth: vi.fn(),
    getLocalAuthToken: () => null,
    isValidLocalAuthToken: token => /^[0-9a-f]{32}$/.test(String(token).trim()),
    LOCAL_AUTH_TOKEN_ERROR: 'err',
    localAuthDownloadUrl: url => url,
    localAuthHandoffUrl: hostname => `http://${hostname}.local/#localAuthToken=t`,
    bootstrapLocalAuthFromHash: vi.fn(),
    MDNS_NAME_ERROR: 'mdns err',
  };
}

// Factory for the `@fortawesome/react-fontawesome` mock module (icon rendered
// as null — the Settings suites never assert on the icon itself).
export function fontAwesomeMock() {
  return { FontAwesomeIcon: () => null };
}

// Factory for the `../../components/ImportButton.jsx` mock module.
export function importButtonMock() {
  return { ImportButton: () => null };
}

// Render the Settings page inside the ApiServiceContext provider the component
// requires. The `ApiServiceContext` and `Settings` component are INJECTED by
// the caller (rather than imported here) on purpose: importing the real
// component from this shared module would pull it in before the caller's
// `vi.mock(...)` calls have been hoisted, defeating the mocks. The name spells
// out that dependency injection so a call site is self-explanatory without
// scrolling back to this comment. Returns the render result so callers that
// need `rerender` (e.g. the plugin remount suite) can use it.
export function renderSettingsWithInjectedComponent(ApiServiceContext, Settings) {
  return render(
    h(ApiServiceContext.Provider, { value: { authenticateLocal: vi.fn() } }, h(Settings)),
  );
}

// Shared beforeEach globals setup: stub alert, silence console.error, stub the
// clipboard, and enable the ledControl capability on the machine signal. Pass
// the imported `machine` signal. The per-suite useQuery state reset (queryState
// vs queryController) stays in each file since those objects differ.
export function installSettingsTestGlobals(machine) {
  vi.stubGlobal('alert', vi.fn());
  vi.spyOn(console, 'error').mockImplementation(() => {});
  vi.stubGlobal('navigator', { clipboard: { writeText: vi.fn() } });
  machine.value = {
    ...machine.value,
    capabilities: { ...machine.value.capabilities, ledControl: true },
  };
}

// Shared afterEach teardown mirroring installSettingsTestGlobals.
export function teardownSettingsTest() {
  cleanup();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
}
