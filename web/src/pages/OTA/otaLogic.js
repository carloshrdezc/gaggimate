// Normalize an OTA channel value chosen in the dropdown.
//
// Accepted values:
//   - "latest"           -> stable (most recent published, non-prerelease tag)
//   - "beta"             -> beta build (moving tag tracking the master branch)
//   - "nightly"          -> nightly build
//   - "tag:<semver>"     -> a specific stable tag offered in availableVersions
//                          (firmware allow-lists the tag; a malformed value
//                          will be rejected server-side back to "latest")
//
// Anything else falls back to "latest" so a stale dropdown value can't get
// us into a broken state on the device.
export function updateOtaChannel(formData, channel) {
  if (channel === 'beta') {
    return { ...formData, channel: 'beta' };
  }
  if (channel === 'nightly') {
    return { ...formData, channel: 'nightly' };
  }
  if (typeof channel === 'string' && channel.startsWith('tag:') && channel.length > 4) {
    return { ...formData, channel };
  }
  return { ...formData, channel: 'latest' };
}

// Decide whether the "Flash Display" / "Flash Controller" buttons should be
// enabled when the channel is pinned to a specific tag (tag:<semver>).
//
// Why this is restrictive:
//   GitHubOTA::update(controller, display, force=true) bypasses the
//   upgrade-only guard and downloads from whatever `_latest_url` was last
//   resolved by `checkForUpdates()`. If the page is force-enabled while the
//   device is still in `Checking...` for the newly-saved channel, a click
//   will flash from a stale `_latest_url` (the previous channel's URL or
//   even an empty string), and the user gets the wrong release silently.
//
// Gate (all four must hold):
//   1. The device-acknowledged channel is `tag:<semver>` (formData.channel).
//   2. There's no pending unsaved selection (pendingChannel === formData.channel).
//   3. A real status came back (not "Checking..." / "Update failed" / empty).
//   4. The reported status equals the pinned tag's semver — proves the
//      device has resolved `_latest_url` to *that* tag, not a stale one.
export function canFlashTaggedRelease({ formData, pendingChannel } = {}) {
  if (!formData || typeof formData !== 'object') return false;
  const { channel, status } = formData;
  if (typeof channel !== 'string' || !channel.startsWith('tag:') || channel.length <= 4) {
    return false;
  }
  if (pendingChannel !== channel) {
    return false;
  }
  if (typeof status !== 'string' || status.length === 0) {
    return false;
  }
  if (status === 'Checking...' || status === 'Update failed') {
    return false;
  }
  // Tag is "tag:2.0.8" -> compare against "2.0.8".
  // GitHub release tags occasionally carry a leading `v` prefix (e.g.
  // `v1.8.2`). The firmware's resolver (GitHubOTA::checkForUpdates) strips
  // it before reporting the version, but the channel string we stored does
  // not. Mirror the firmware's leading-`v` tolerance here so the Flash
  // buttons enable for v-prefixed legacy tags too — without this, a tag
  // like `tag:v1.8.2` would resolve to status `1.8.2` and the strict
  // equality below would leave `tagFlashReady` false forever.
  const pinned = channel.slice(4);
  return (
    status === pinned ||
    (pinned.startsWith('v') && status === pinned.slice(1)) ||
    (status.startsWith('v') && status.slice(1) === pinned)
  );
}

// Decide whether the "Update Display" / "Update Controller" buttons should be
// enabled in the *non-tag-pinned* path (channel is "latest" or "nightly", and
// the device says an update is available).
//
// The trap this guards against: the dropdown is bound to `pendingChannel`,
// but Save & Refresh has not been clicked yet. Clicking "Update Display"
// fires both `req:ota-start` *and* the form's `req:ota-settings` payload —
// so the device receives the new (e.g. tag:*) channel and immediately runs
// the OTA, but `GitHubOTA::checkForUpdates()` has not had a chance to
// resolve `_latest_url` for the new channel yet. Result: it flashes the
// previous channel's binary against the new channel's metadata, or fails on
// a relative URL.
//
// Rule: if the dropdown shows a value the device has not acknowledged yet
// (`pendingChannel !== formData.channel`), block the Update buttons. The
// user must click "Save & Refresh" first; once the device echoes the new
// channel back, the normal `*UpdateAvailable` flags (or the tag gate) will
// re-enable the right button.
export function canUpdateOnAcknowledgedChannel({ formData, pendingChannel } = {}) {
  if (!formData || typeof formData !== 'object') return false;
  const { channel } = formData;
  if (typeof channel !== 'string') return false;
  // If there's no pending value (initial load), trust formData.channel.
  if (pendingChannel === undefined) return true;
  return pendingChannel === channel;
}

// Decide whether the device-acknowledged channel differs from the channel the
// currently-installed firmware was built on.
//
// The firmware (PRO-400) persists the channel it last flashed from and reports
// it back in res:ota-settings as `installedChannel`. When the user selects a
// *different* channel (e.g. installed on `latest`, now switching to `beta`),
// the next OTA is a channel switch — it may install a different, possibly
// LOWER, version and force-flashes even when the device reports "current".
//
// Graceful fallback: older firmware that predates PRO-400 does not emit
// `installedChannel`. When it's absent/undefined we return false so the UI
// degrades to its pre-channel-switch behavior (no switch state, no warning).
export function otaChannelDiffersFromInstalled({ selectedChannel, installedChannel } = {}) {
  if (typeof installedChannel !== 'string' || installedChannel.length === 0) {
    return false;
  }
  if (typeof selectedChannel !== 'string' || selectedChannel.length === 0) {
    return false;
  }
  return selectedChannel !== installedChannel;
}

// Decide whether the flash/update button should be enabled in the
// channel-switch state (the device-acknowledged channel differs from the
// installed channel).
//
// Mirrors canFlashTaggedRelease's anti-stale-`_latest_url` discipline: after
// switching channels the firmware force-flashes, so we must not enable the
// button until the device has echoed the newly-saved channel back (proving
// checkForUpdates() has resolved `_latest_url` for the new channel).
//
// Gate (all must hold):
//   1. The dropdown selection has been saved & acknowledged
//      (pendingChannel === formData.channel).
//   2. A real status came back (not "Checking..." / "Update failed" / empty).
//   3. The acknowledged channel differs from formData.installedChannel.
export function canSwitchChannel({ formData, pendingChannel } = {}) {
  if (!formData || typeof formData !== 'object') return false;
  const { channel, status, installedChannel } = formData;
  if (typeof channel !== 'string' || channel.length === 0) return false;
  if (pendingChannel !== channel) return false;
  if (typeof status !== 'string' || status.length === 0) return false;
  if (status === 'Checking...' || status === 'Update failed') return false;
  return otaChannelDiffersFromInstalled({ selectedChannel: channel, installedChannel });
}

// Capitalize a channel name for display ("beta" -> "Beta"). Tag channels
// (`tag:<semver>`) are passed through unchanged since they aren't a
// human-friendly channel name.
function capitalizeChannel(channel) {
  if (typeof channel !== 'string' || channel.length === 0) return channel;
  if (channel.startsWith('tag:')) return channel;
  return channel.charAt(0).toUpperCase() + channel.slice(1);
}

// Contextual label for the flash/update action:
//   - Channel switch (acknowledged channel differs from installed) -> "Switch to <Channel>"
//   - Pinned-tag flash                                             -> "Flash"
//   - Within-channel update                                        -> "Update"
//
// Channel-switch takes precedence when the acknowledged channel differs from
// the installed channel. Reuses the existing predicates so the three states
// stay consistent with the button-enable logic.
//
// `pendingChannel` is part of the API contract (mirrors the other helpers'
// arity) but not consulted: the label keys off the already-acknowledged
// `formData.channel`, the same source the enable gate uses.
// eslint-disable-next-line no-unused-vars
export function otaActionLabel(formData, pendingChannel) {
  if (!formData || typeof formData !== 'object') return 'Update';
  const { channel } = formData;
  if (
    otaChannelDiffersFromInstalled({
      selectedChannel: channel,
      installedChannel: formData.installedChannel,
    })
  ) {
    return `Switch to ${capitalizeChannel(channel)}`;
  }
  if (typeof channel === 'string' && channel.startsWith('tag:')) {
    return 'Flash';
  }
  return 'Update';
}
