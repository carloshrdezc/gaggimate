// Deterministic RFC 4122 name-based (version 5, SHA-1) UUIDs.
//
// The Beanconqueror backup format keys every record by a UUID and every brew
// references its bean / mill / preparation by that UUID, so a re-export of the
// same GaggiMate library must produce the SAME UUIDs — otherwise each export
// looks like a brand-new set of records. `utils/uuid.js` (v4, random) cannot do
// that, and `crypto.subtle.digest` is async and only available in a secure
// context, which the device serves the web UI outside of (plain http on the LAN).
// A ~40-line synchronous SHA-1 keeps the serializer pure and testable.

/** SHA-1 of `bytes`, per RFC 3174. Returns 20 bytes. */
export function sha1Bytes(bytes) {
  const messageLengthBits = bytes.length * 8;
  // Message + 0x80 terminator + zero padding to a multiple of 64, leaving 8
  // trailing bytes for the big-endian bit length.
  const paddedLength = (((bytes.length + 8) >> 6) << 6) + 64;
  const block = new Uint8Array(paddedLength);
  block.set(bytes);
  block[bytes.length] = 0x80;
  // Bit lengths above 2^32 cannot occur here (inputs are short names), so only
  // the low 32 bits need writing.
  new DataView(block.buffer).setUint32(paddedLength - 4, messageLengthBits >>> 0, false);

  let [h0, h1, h2, h3, h4] = [0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0];
  const words = new Int32Array(80);
  const view = new DataView(block.buffer);

  for (let offset = 0; offset < paddedLength; offset += 64) {
    for (let i = 0; i < 16; i++) {
      words[i] = view.getInt32(offset + i * 4, false);
    }
    for (let i = 16; i < 80; i++) {
      const value = words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16];
      words[i] = (value << 1) | (value >>> 31);
    }

    let [a, b, c, d, e] = [h0, h1, h2, h3, h4];
    for (let i = 0; i < 80; i++) {
      let f;
      let k;
      if (i < 20) {
        f = (b & c) | (~b & d);
        k = 0x5a827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ed9eba1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8f1bbcdc;
      } else {
        f = b ^ c ^ d;
        k = 0xca62c1d6;
      }
      const temp = (((a << 5) | (a >>> 27)) + f + e + k + words[i]) | 0;
      e = d;
      d = c;
      c = (b << 30) | (b >>> 2);
      b = a;
      a = temp;
    }

    h0 = (h0 + a) | 0;
    h1 = (h1 + b) | 0;
    h2 = (h2 + c) | 0;
    h3 = (h3 + d) | 0;
    h4 = (h4 + e) | 0;
  }

  const digest = new Uint8Array(20);
  const digestView = new DataView(digest.buffer);
  digestView.setInt32(0, h0, false);
  digestView.setInt32(4, h1, false);
  digestView.setInt32(8, h2, false);
  digestView.setInt32(12, h3, false);
  digestView.setInt32(16, h4, false);
  return digest;
}

function namespaceToBytes(namespace) {
  const hex = String(namespace).replaceAll('-', '');
  if (!/^[0-9a-fA-F]{32}$/.test(hex)) {
    throw new Error(`Invalid UUID namespace: ${namespace}`);
  }
  const bytes = new Uint8Array(16);
  for (let i = 0; i < 16; i++) {
    bytes[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
  }
  return bytes;
}

/**
 * RFC 4122 version-5 UUID for `name` inside `namespace`.
 *
 * @param {string} name UTF-8 name, e.g. `bean:abc123`
 * @param {string} namespace canonical UUID string
 * @returns {string} canonical lowercase UUID with the v5 version + variant bits
 */
export function uuidV5(name, namespace) {
  const namespaceBytes = namespaceToBytes(namespace);
  const nameBytes = new TextEncoder().encode(String(name));

  const input = new Uint8Array(namespaceBytes.length + nameBytes.length);
  input.set(namespaceBytes);
  input.set(nameBytes, namespaceBytes.length);

  const hash = sha1Bytes(input);
  const uuidBytes = hash.slice(0, 16);
  uuidBytes[6] = (uuidBytes[6] & 0x0f) | 0x50; // version 5
  uuidBytes[8] = (uuidBytes[8] & 0x3f) | 0x80; // RFC 4122 variant

  const hex = [...uuidBytes].map(byte => byte.toString(16).padStart(2, '0')).join('');
  return [
    hex.slice(0, 8),
    hex.slice(8, 12),
    hex.slice(12, 16),
    hex.slice(16, 20),
    hex.slice(20, 32),
  ].join('-');
}
