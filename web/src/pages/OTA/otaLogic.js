// Normalize an OTA channel value chosen in the dropdown.
//
// Accepted values:
//   - "latest"           -> stable (most recent published, non-prerelease tag)
//   - "nightly"          -> nightly build
//   - "tag:<semver>"     -> a specific stable tag offered in availableVersions
//                          (firmware allow-lists the tag; a malformed value
//                          will be rejected server-side back to "latest")
//
// Anything else falls back to "latest" so a stale dropdown value can't get
// us into a broken state on the device.
export function updateOtaChannel(formData, channel) {
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
  return status === channel.slice(4);
}
