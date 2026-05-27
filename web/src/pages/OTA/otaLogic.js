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
