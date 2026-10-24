import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { test } from 'node:test';
import vm from 'node:vm';

const source = await readFile(new URL('../extension/content.js', import.meta.url), 'utf8');
function runContent({ readyState = 'loading', timing = {}, reject = false } = {}) {
  const listeners = new Map();
  const tasks = [];
  const messages = [];
  vm.runInNewContext(source, {
    document: { readyState },
    window: { addEventListener: (type, callback) => listeners.set(type, callback) },
    performance: { getEntriesByType: () => [{ startTime: 0, fetchStart: 5,
      responseEnd: 60, loadEventEnd: 100, redirectStart: 0, ...timing }] },
    setTimeout: callback => tasks.push(callback),
    chrome: { runtime: { sendMessage(message) {
      messages.push(message);
      return reject ? Promise.reject(new Error('Extension reloaded')) : Promise.resolve();
    } } },
  });
  return { listeners, tasks, messages, flush: () => { while (tasks.length) tasks.shift()(); } };
}

test('reports once after load, after loadEventEnd is available, without reinjection', () => {
  const fixture = runContent();
  assert.equal(fixture.messages.length, 0);
  fixture.listeners.get('load')();
  assert.equal(fixture.messages.length, 0);
  fixture.flush();
  assert.equal(fixture.messages.length, 1);
  assert.equal(fixture.messages[0].pageLoadTotalMs, 100);
  assert.equal(fixture.messages[0].pageLoadNetworkMs, 55);
  assert.equal(fixture.messages[0].pageLoadRenderMs, 40);
  fixture.listeners.get('load')();
  fixture.flush();
  assert.equal(fixture.messages.length, 1);
});

test('handles completed documents, zero-duration cached responses and redirects', () => {
  for (const timing of [{ fetchStart: 60 }, { redirectStart: 2 }]) {
    const fixture = runContent({ readyState: 'complete', timing });
    fixture.flush();
    assert.equal(fixture.messages.length, 1);
    assert.equal(fixture.messages[0].pageLoadNetworkMs, timing.redirectStart ? 58 : 0);
  }
});

test('BFCache restore re-reports the existing document timing', () => {
  const fixture = runContent({ readyState: 'complete' });
  fixture.flush();
  fixture.listeners.get('pageshow')({ persisted: true });
  fixture.flush();
  assert.equal(fixture.messages.length, 2);
});

test('an invalidated extension context does not cause an unhandled rejection', async () => {
  const fixture = runContent({ readyState: 'complete', reject: true });
  fixture.flush();
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(fixture.messages.length, 1);
});
