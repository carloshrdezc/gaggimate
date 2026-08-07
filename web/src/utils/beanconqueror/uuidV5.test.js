import { describe, test, expect } from 'vitest';

import { sha1Bytes, uuidV5 } from './uuidV5.js';

// RFC 3174 test vectors — a wrong SHA-1 would still produce stable-looking
// UUIDs, so the digest itself is pinned rather than trusted.
describe('sha1Bytes', () => {
  test('matches the RFC 3174 "abc" vector', () => {
    const hex = [...sha1Bytes(new TextEncoder().encode('abc'))]
      .map(b => b.toString(16).padStart(2, '0'))
      .join('');
    expect(hex).toBe('a9993e364706816aba3e25717850c26c9cd0d89d');
  });

  test('matches the empty-input vector', () => {
    const hex = [...sha1Bytes(new Uint8Array(0))]
      .map(b => b.toString(16).padStart(2, '0'))
      .join('');
    expect(hex).toBe('da39a3ee5e6b4b0d3255bfef95601890afd80709');
  });

  test('matches a vector longer than one 64-byte block', () => {
    const input = new TextEncoder().encode('a'.repeat(1000));
    const hex = [...sha1Bytes(input)].map(b => b.toString(16).padStart(2, '0')).join('');
    // Independently verifiable: printf 'a%.0s' {1..1000} | sha1sum
    expect(hex).toBe('291e9a6c66994949b57ba5e650361e98fc36b1ba');
  });
});

describe('uuidV5', () => {
  // RFC 4122 appendix-style vector: the DNS namespace + "python.org" is the
  // canonical uuid5 example and is reproducible with Python's uuid module.
  const DNS_NAMESPACE = '6ba7b810-9dad-11d1-80b4-00c04fd430c8';

  test('reproduces the canonical DNS/python.org UUIDv5', () => {
    expect(uuidV5('python.org', DNS_NAMESPACE)).toBe('886313e1-3b8a-5372-9b90-0c9aee199e5d');
  });

  test('is deterministic for the same name and namespace', () => {
    expect(uuidV5('bean:abc', DNS_NAMESPACE)).toBe(uuidV5('bean:abc', DNS_NAMESPACE));
  });

  test('differs for different names', () => {
    expect(uuidV5('bean:abc', DNS_NAMESPACE)).not.toBe(uuidV5('bean:abd', DNS_NAMESPACE));
  });

  test('sets the version-5 and RFC 4122 variant bits', () => {
    const uuid = uuidV5('anything', DNS_NAMESPACE);
    expect(uuid).toMatch(/^[0-9a-f]{8}-[0-9a-f]{4}-5[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/);
  });

  test('handles non-ASCII names via UTF-8 encoding', () => {
    // Python: uuid.uuid5(uuid.NAMESPACE_DNS, 'café')
    expect(uuidV5('café', DNS_NAMESPACE)).toBe('5e2e2331-a683-5e18-b56d-666e31574b41');
  });
});
