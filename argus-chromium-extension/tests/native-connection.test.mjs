import assert from 'node:assert/strict';
import { test } from 'node:test';
import { NativeConnection } from '../extension/scripts/monitor/NativeConnection.js';
import { createAPI } from './helpers.mjs';

test('failed host connections back off, and stop cancels retries', t => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { api, ports } = createAPI();
  const connection = new NativeConnection(api, () => {});
  t.after(() => connection.stop());
  connection.enabled = true;
  connection.connect();
  ports[0].onDisconnect.emit();
  t.mock.timers.tick(999);
  assert.equal(ports.length, 1);
  t.mock.timers.tick(1);
  assert.equal(ports.length, 2);
  ports[1].onDisconnect.emit();
  t.mock.timers.tick(1000);
  assert.equal(ports.length, 2);
  t.mock.timers.tick(1000);
  assert.equal(ports.length, 3);
  ports[2].onDisconnect.emit();
  connection.stop();
  t.mock.timers.tick(30000);
  assert.equal(ports.length, 3);
});

test('a stale disconnected port cannot clear a replacement connection', () => {
  const { api, ports } = createAPI();
  const connection = new NativeConnection(api, () => {});
  connection.enabled = true;
  connection.connect();
  ports[0].onDisconnect.emit();
  connection.connect();
  ports[0].onDisconnect.emit();
  assert.equal(connection.port, ports[1]);
  connection.stop();
});

test('postMessage exceptions disconnect safely and do not escape the collection loop', () => {
  const { api, ports } = createAPI();
  const connection = new NativeConnection(api, () => {});
  connection.enabled = true;
  connection.connect();
  ports[0].postMessage = () => { throw new Error('Port closed'); };
  assert.equal(connection.send({ type: 'tab' }), false);
  assert.equal(connection.error, 'Port closed');
  assert.equal(connection.port, null);
  connection.stop();
});
