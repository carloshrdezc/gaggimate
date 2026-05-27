import assert from 'node:assert/strict';
import test from 'node:test';

import { updateOtaChannel, canFlashTaggedRelease } from './otaLogic.js';

test('updates OTA channel to nightly without changing other form fields', () => {
  const next = updateOtaChannel({ channel: 'latest', displayVersion: '2.0.3' }, 'nightly');

  assert.equal(next.channel, 'nightly');
  assert.equal(next.displayVersion, '2.0.3');
});

test('falls back to stable for unexpected OTA channel values', () => {
  const next = updateOtaChannel({ channel: 'nightly' }, 'beta');

  assert.equal(next.channel, 'latest');
});

test('passes through tag:<semver> for a specific stable tag', () => {
  const next = updateOtaChannel({ channel: 'latest', displayVersion: '2.0.10' }, 'tag:2.0.8');

  assert.equal(next.channel, 'tag:2.0.8');
  assert.equal(next.displayVersion, '2.0.10');
});

test('falls back to latest for empty tag prefix', () => {
  const next = updateOtaChannel({ channel: 'nightly' }, 'tag:');

  assert.equal(next.channel, 'latest');
});

test('passes through latest unchanged', () => {
  const next = updateOtaChannel({ channel: 'tag:2.0.8' }, 'latest');

  assert.equal(next.channel, 'latest');
});

// canFlashTaggedRelease gate

test('canFlashTaggedRelease: enabled when device confirmed the pinned tag', () => {
  const formData = { channel: 'tag:2.0.8', status: '2.0.8' };
  assert.equal(canFlashTaggedRelease({ formData, pendingChannel: 'tag:2.0.8' }), true);
});

test('canFlashTaggedRelease: disabled while OTA is still Checking...', () => {
  const formData = { channel: 'tag:2.0.8', status: 'Checking...' };
  assert.equal(canFlashTaggedRelease({ formData, pendingChannel: 'tag:2.0.8' }), false);
});

test('canFlashTaggedRelease: disabled when status is empty (no check yet)', () => {
  const formData = { channel: 'tag:2.0.8', status: '' };
  assert.equal(canFlashTaggedRelease({ formData, pendingChannel: 'tag:2.0.8' }), false);
});

test('canFlashTaggedRelease: disabled when last check reported failure', () => {
  const formData = { channel: 'tag:2.0.8', status: 'Update failed' };
  assert.equal(canFlashTaggedRelease({ formData, pendingChannel: 'tag:2.0.8' }), false);
});

test('canFlashTaggedRelease: disabled when user has unsaved dropdown change', () => {
  const formData = { channel: 'latest', status: '2.0.10' };
  assert.equal(canFlashTaggedRelease({ formData, pendingChannel: 'tag:2.0.8' }), false);
});

test('canFlashTaggedRelease: disabled when reported tag mismatches pinned tag', () => {
  // Device-acknowledged channel is tag:2.0.8 but checkForUpdates has only
  // resolved _latest_url for the previous channel (e.g. still showing 2.0.10).
  const formData = { channel: 'tag:2.0.8', status: '2.0.10' };
  assert.equal(canFlashTaggedRelease({ formData, pendingChannel: 'tag:2.0.8' }), false);
});

test('canFlashTaggedRelease: returns false for non-tag channels', () => {
  assert.equal(
    canFlashTaggedRelease({ formData: { channel: 'latest', status: '2.0.10' }, pendingChannel: 'latest' }),
    false,
  );
  assert.equal(
    canFlashTaggedRelease({ formData: { channel: 'nightly', status: 'nightly' }, pendingChannel: 'nightly' }),
    false,
  );
});

test('canFlashTaggedRelease: returns false for malformed input', () => {
  assert.equal(canFlashTaggedRelease(), false);
  assert.equal(canFlashTaggedRelease({}), false);
  assert.equal(canFlashTaggedRelease({ formData: null, pendingChannel: 'tag:2.0.8' }), false);
  assert.equal(
    canFlashTaggedRelease({ formData: { channel: 'tag:', status: 'whatever' }, pendingChannel: 'tag:' }),
    false,
  );
});
