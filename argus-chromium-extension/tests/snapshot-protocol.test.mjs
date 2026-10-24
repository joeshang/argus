import assert from 'node:assert/strict';
import { test } from 'node:test';
import { MonitoringService } from '../extension/scripts/monitor/MonitoringService.js';
import { createAPI, userAgent, tick, frame } from './helpers.mjs';

test('a shared renderer keeps every tab and iframe in an atomic snapshot', async t => {
  const fixture = createAPI({ tabs: [{ id: 1, url: 'https://example.com/' }, { id: 2, url: 'https://other.com/' }],
    frames: { 1: [frame(), frame({ frameId: 2, documentId: 'child' })],
      2: [frame({ documentId: 'doc-2', url: 'https://other.com/' })] } });
  const service = new MonitoringService(fixture.api, userAgent);
  t.after(() => service.native.stop());
  await service.start();
  await tick();
  const message = fixture.sent.at(-1);
  assert.equal(message.type, 'snapshot');
  assert.equal(message.protocolVersion, 2);
  assert.equal(message.frames.length, 3);
  assert.deepEqual(message.frames.map(f => [f.tabId, f.frameId]).sort(), [[1, 0], [1, 2], [2, 0]]);
});

test('chunking preserves every record within Native Messaging byte limits', async () => {
  const { snapshotMessages } = await import('../extension/scripts/monitor/SnapshotProtocol.js');
  const frames = Array.from({ length: 300 }, (_, tabId) => ({ tabId, frameId: 0,
    rendererClientId: 7, url: `https://example.com/${'中'.repeat(2200)}`, documentId: `doc-${tabId}` }));
  const messages = snapshotMessages({ sourceId: 'profile', browser: 'chrome', generation: 1 }, frames);
  assert.ok(messages.length > 1);
  assert.deepEqual(messages.flatMap(message => message.frames), frames);
  for (const [partIndex, message] of messages.entries()) {
    assert.equal(message.partIndex, partIndex);
    assert.equal(message.partCount, messages.length);
    assert.ok(new TextEncoder().encode(JSON.stringify(message)).length < 1024 * 1024);
  }
  assert.throws(() => snapshotMessages({}, [{ url: 'https://example.com/' + 'x'.repeat(65536) }]), /64 KiB/);
  assert.throws(() => snapshotMessages({}, Array.from({ length: 10001 }, () => frames[0])), /10000/);
  assert.throws(() => snapshotMessages({}, Array.from({ length: 1000 }, () => frames[0])), /3 MiB/);
});

test('empty snapshots clear closed tabs, and profile identity survives worker restart', async t => {
  const fixture = createAPI({ tabs: [{ id: 1, url: 'https://example.com/' }], frames: { 1: [frame()] } });
  const service = new MonitoringService(fixture.api, userAgent);
  t.after(() => service.native.stop());
  await service.start();
  await tick();
  fixture.tabs.splice(0);
  await service.collect();
  assert.deepEqual(fixture.sent.at(-1).frames, []);
  service.native.stop();
  const next = new MonitoringService(fixture.api, userAgent);
  t.after(() => next.native.stop());
  await next.start();
  assert.equal(next.sourceId, service.sourceId);
});
