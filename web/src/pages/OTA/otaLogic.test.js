import { test, expect } from 'vitest';

import {
  updateOtaChannel,
  canFlashTaggedRelease,
  canUpdateOnAcknowledgedChannel,
} from './otaLogic.js';

test('updates OTA channel to nightly without changing other form fields', () => {
  const next = updateOtaChannel({ channel: 'latest', displayVersion: '2.0.3' }, 'nightly');

  expect(next.channel).toBe('nightly');
  expect(next.displayVersion).toBe('2.0.3');
});

test('updates OTA channel to beta without changing other form fields', () => {
  const next = updateOtaChannel({ channel: 'latest', displayVersion: '2.0.3' }, 'beta');

  expect(next.channel).toBe('beta');
  expect(next.displayVersion).toBe('2.0.3');
});

test('updates OTA channel to beta without changing other form fields', () => {
  const next = updateOtaChannel({ channel: 'latest', displayVersion: '2.0.3' }, 'beta');

  assert.equal(next.channel, 'beta');
  assert.equal(next.displayVersion, '2.0.3');
});

test('falls back to stable for unexpected OTA channel values', () => {
  const next = updateOtaChannel({ channel: 'nightly' }, 'garbage-channel');

  expect(next.channel).toBe('latest');
});

test('passes through tag:<semver> for a specific stable tag', () => {
  const next = updateOtaChannel({ channel: 'latest', displayVersion: '2.0.10' }, 'tag:2.0.8');

  expect(next.channel).toBe('tag:2.0.8');
  expect(next.displayVersion).toBe('2.0.10');
});

test('falls back to latest for empty tag prefix', () => {
  const next = updateOtaChannel({ channel: 'nightly' }, 'tag:');

  expect(next.channel).toBe('latest');
});

test('passes through latest unchanged', () => {
  const next = updateOtaChannel({ channel: 'tag:2.0.8' }, 'latest');

  expect(next.channel).toBe('latest');
});

// canFlashTaggedRelease gate

test('canFlashTaggedRelease: enabled when device confirmed the pinned tag', () => {
  const formData = { channel: 'tag:2.0.8', status: '2.0.8' };
  expect(canFlashTaggedRelease({ formData, pendingChannel: 'tag:2.0.8' })).toBe(true);
});

test('canFlashTaggedRelease: disabled while OTA is still Checking...', () => {
  const formData = { channel: 'tag:2.0.8', status: 'Checking...' };
  expect(canFlashTaggedRelease({ formData, pendingChannel: 'tag:2.0.8' })).toBe(false);
});

test('canFlashTaggedRelease: disabled when status is empty (no check yet)', () => {
  const formData = { channel: 'tag:2.0.8', status: '' };
  expect(canFlashTaggedRelease({ formData, pendingChannel: 'tag:2.0.8' })).toBe(false);
});

test('canFlashTaggedRelease: disabled when last check reported failure', () => {
  const formData = { channel: 'tag:2.0.8', status: 'Update failed' };
  expect(canFlashTaggedRelease({ formData, pendingChannel: 'tag:2.0.8' })).toBe(false);
});

test('canFlashTaggedRelease: disabled when user has unsaved dropdown change', () => {
  const formData = { channel: 'latest', status: '2.0.10' };
  expect(canFlashTaggedRelease({ formData, pendingChannel: 'tag:2.0.8' })).toBe(false);
});

test('canFlashTaggedRelease: disabled when reported tag mismatches pinned tag', () => {
  // Device-acknowledged channel is tag:2.0.8 but checkForUpdates has only
  // resolved _latest_url for the previous channel (e.g. still showing 2.0.10).
  const formData = { channel: 'tag:2.0.8', status: '2.0.10' };
  expect(canFlashTaggedRelease({ formData, pendingChannel: 'tag:2.0.8' })).toBe(false);
});

test('canFlashTaggedRelease: returns false for non-tag channels', () => {
  expect(canFlashTaggedRelease({ formData: { channel: 'latest', status: '2.0.10' }, pendingChannel: 'latest' })).toBe(false);
  expect(canFlashTaggedRelease({ formData: { channel: 'nightly', status: 'nightly' }, pendingChannel: 'nightly' })).toBe(false);
});

test('canFlashTaggedRelease: returns false for malformed input', () => {
  expect(canFlashTaggedRelease()).toBe(false);
  expect(canFlashTaggedRelease({})).toBe(false);
  expect(canFlashTaggedRelease({ formData: null, pendingChannel: 'tag:2.0.8' })).toBe(false);
  expect(canFlashTaggedRelease({ formData: { channel: 'tag:', status: 'whatever' }, pendingChannel: 'tag:' })).toBe(false);
});

// v-prefix tolerance — GitHub release tags occasionally carry a leading `v`
// (`v1.8.2`). The firmware resolver strips it before reporting, but the
// channel string keeps it. Both directions must compare equal so legacy
// v-prefixed tags can flash and stripped-v device reports don't lock the
// gate forever.

test('canFlashTaggedRelease: enabled when pinned tag is v-prefixed and status is stripped', () => {
  // Channel stored as "tag:v1.8.2", device reports semver as "1.8.2".
  const formData = { channel: 'tag:v1.8.2', status: '1.8.2' };
  expect(canFlashTaggedRelease({ formData, pendingChannel: 'tag:v1.8.2' })).toBe(true);
});

test('canFlashTaggedRelease: enabled when pinned tag has no v but device reports v-prefixed', () => {
  // Inverse case — defensive: if a future firmware ever reported "v1.8.2"
  // verbatim, a non-v-prefixed channel ("tag:1.8.2") should still match.
  const formData = { channel: 'tag:1.8.2', status: 'v1.8.2' };
  expect(canFlashTaggedRelease({ formData, pendingChannel: 'tag:1.8.2' })).toBe(true);
});

test('canFlashTaggedRelease: still rejects mismatched semver even with v-prefix', () => {
  // Channel "tag:v1.8.2" vs status "1.8.3" — different versions, must not match.
  const formData = { channel: 'tag:v1.8.2', status: '1.8.3' };
  expect(canFlashTaggedRelease({ formData, pendingChannel: 'tag:v1.8.2' })).toBe(false);
});

// canUpdateOnAcknowledgedChannel gate — protects the "Update Display"/"Update
// Controller" buttons when the channel is "latest" or "nightly" but the
// dropdown holds an unsaved selection.

test('canUpdateOnAcknowledgedChannel: enabled when dropdown matches device channel', () => {
  const formData = { channel: 'latest', displayUpdateAvailable: true };
  expect(canUpdateOnAcknowledgedChannel({ formData, pendingChannel: 'latest' })).toBe(true);
});

test('canUpdateOnAcknowledgedChannel: disabled when user has unsaved tag selection on a "latest" device', () => {
  // The trap from the P1 codex review: device is on `latest` with an update
  // available, user picks `tag:2.0.8` from the dropdown but does NOT click
  // Save & Refresh. Without this gate, the "Update Display" button stays
  // enabled and clicking it would send `req:ota-start` while the device's
  // `_latest_url` still points at `latest`.
  const formData = { channel: 'latest', displayUpdateAvailable: true };
  expect(canUpdateOnAcknowledgedChannel({ formData, pendingChannel: 'tag:2.0.8' })).toBe(false);
});

test('canUpdateOnAcknowledgedChannel: disabled when user has unsaved nightly selection on a "latest" device', () => {
  const formData = { channel: 'latest', displayUpdateAvailable: true };
  expect(canUpdateOnAcknowledgedChannel({ formData, pendingChannel: 'nightly' })).toBe(false);
});

test('canUpdateOnAcknowledgedChannel: enabled on initial load when pendingChannel is undefined', () => {
  // Initial mount: parent has not yet seeded `pendingChannel` from formData.
  // We trust formData.channel; the gate is for unsaved deltas only.
  const formData = { channel: 'latest', displayUpdateAvailable: true };
  expect(canUpdateOnAcknowledgedChannel({ formData, pendingChannel: undefined })).toBe(true);
});

test('canUpdateOnAcknowledgedChannel: returns false for malformed input', () => {
  expect(canUpdateOnAcknowledgedChannel()).toBe(false);
  expect(canUpdateOnAcknowledgedChannel({})).toBe(false);
  expect(canUpdateOnAcknowledgedChannel({ formData: null, pendingChannel: 'latest' })).toBe(false);
  expect(canUpdateOnAcknowledgedChannel({ formData: { channel: 42 }, pendingChannel: 'latest' })).toBe(false);
});

// ---------------------------------------------------------------------------
// Composite-rule regression tests — these mirror the exact disable-decision
// the OTA page applies in `ActionButtonsSection`. They lock down the contract
// across the helpers so a future refactor can't accidentally re-introduce
// the "stale _latest_url + tag:* settings + force=true" footgun that Codex
// flagged in pullrequestreview-4376209156.
//
// Decision (must match index.jsx):
//   if isTagPinned                     -> disabled := !tagFlashReady
//   else if !channelAcknowledged       -> disabled := true
//   else                                -> disabled := !*UpdateAvailable

function decideDisabled(formData, pendingChannel, target /* 'display' | 'controller' */) {
  const tagFlashReady = canFlashTaggedRelease({ formData, pendingChannel });
  const channelAcknowledged = canUpdateOnAcknowledgedChannel({ formData, pendingChannel });
  const isTagPinned = typeof formData?.channel === 'string' && formData.channel.startsWith('tag:');
  const flagKey = target === 'controller' ? 'controllerUpdateAvailable' : 'displayUpdateAvailable';
  if (isTagPinned) return !tagFlashReady;
  if (!channelAcknowledged) return true;
  return !formData[flagKey];
}

test('disable rule: tag-pinned + Checking... must NOT re-enable from stale displayUpdateAvailable', () => {
  // The exact regression: device acknowledged tag:2.0.8, but status is still
  // "Checking..." for that tag. The previous channel's check left
  // displayUpdateAvailable=true. WITHOUT the fix we'd fall back to that flag
  // and the Flash button would enable; firmware would force-flash from a
  // stale _latest_url.
  const formData = {
    channel: 'tag:2.0.8',
    status: 'Checking...',
    displayUpdateAvailable: true,
    controllerUpdateAvailable: true,
  };
  expect(decideDisabled(formData, 'tag:2.0.8', 'display')).toBe(true);
  expect(decideDisabled(formData, 'tag:2.0.8', 'controller')).toBe(true);
});

test('disable rule: tag-pinned + status mismatch must NOT re-enable from stale flags', () => {
  // status reports the previous channel's resolved version, not the pinned tag.
  const formData = {
    channel: 'tag:2.0.8',
    status: '2.0.10', // stale from when channel was 'latest'
    displayUpdateAvailable: true,
    controllerUpdateAvailable: true,
  };
  expect(decideDisabled(formData, 'tag:2.0.8', 'display')).toBe(true);
  expect(decideDisabled(formData, 'tag:2.0.8', 'controller')).toBe(true);
});

test('disable rule: tag-pinned + Update failed must NOT re-enable from stale flags', () => {
  const formData = {
    channel: 'tag:2.0.8',
    status: 'Update failed',
    displayUpdateAvailable: true,
    controllerUpdateAvailable: true,
  };
  expect(decideDisabled(formData, 'tag:2.0.8', 'display')).toBe(true);
  expect(decideDisabled(formData, 'tag:2.0.8', 'controller')).toBe(true);
});

test('disable rule: tag-pinned + tagFlashReady all-true -> enabled', () => {
  const formData = {
    channel: 'tag:2.0.8',
    status: '2.0.8',
    // *UpdateAvailable flags can be anything — they are not consulted on the
    // tag-pinned path. Set them false to prove that.
    displayUpdateAvailable: false,
    controllerUpdateAvailable: false,
  };
  expect(decideDisabled(formData, 'tag:2.0.8', 'display')).toBe(false);
  expect(decideDisabled(formData, 'tag:2.0.8', 'controller')).toBe(false);
});

test('disable rule: latest channel + unsaved tag selection -> disabled (P1 from prior review)', () => {
  const formData = {
    channel: 'latest',
    status: '2.0.8',
    displayUpdateAvailable: true,
    controllerUpdateAvailable: true,
  };
  expect(decideDisabled(formData, 'tag:2.0.8', 'display')).toBe(true);
  expect(decideDisabled(formData, 'tag:2.0.8', 'controller')).toBe(true);
});

test('disable rule: latest channel + saved + flag true -> enabled', () => {
  const formData = {
    channel: 'latest',
    status: '2.0.8',
    displayUpdateAvailable: true,
    controllerUpdateAvailable: false,
  };
  expect(decideDisabled(formData, 'latest', 'display')).toBe(false);
  expect(decideDisabled(formData, 'latest', 'controller')).toBe(true);
});

test('disable rule: latest channel + saved + flag false -> disabled', () => {
  const formData = {
    channel: 'latest',
    status: '2.0.8',
    displayUpdateAvailable: false,
    controllerUpdateAvailable: false,
  };
  expect(decideDisabled(formData, 'latest', 'display')).toBe(true);
  expect(decideDisabled(formData, 'latest', 'controller')).toBe(true);
});
