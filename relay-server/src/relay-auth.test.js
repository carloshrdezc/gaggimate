import assert from 'node:assert/strict';
import test from 'node:test';

import worker from './index.js';

function createRelayEnv() {
  const names = [];
  return {
    names,
    env: {
      RELAY: {
        idFromName(name) {
          names.push(name);
          return `id:${name}`;
        },
        get() {
          return { fetch: () => new Response('ok') };
        },
      },
    },
  };
}

test('worker routes a valid authenticated subprotocol without putting its token in the URL', async () => {
  const { env, names } = createRelayEnv();
  const response = await worker.fetch(
    new Request('https://relay.example/connect?role=device', {
      headers: { 'Sec-WebSocket-Protocol': 'gaggimate-relay-v1, gaggimate-token-c2VjcmV0' },
    }),
    env,
  );

  assert.equal(response.status, 200);
  assert.deepEqual(names, ['secret']);
});

test('worker rejects query-string credentials and missing protocol authentication', async () => {
  const queryCredential = await worker.fetch(
    new Request('https://relay.example/connect?role=device&token=secret'),
    createRelayEnv().env,
  );
  const missingProtocol = await worker.fetch(
    new Request('https://relay.example/connect?role=device'),
    createRelayEnv().env,
  );

  assert.equal(queryCredential.status, 400);
  assert.equal(missingProtocol.status, 400);
});
