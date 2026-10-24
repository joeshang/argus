import assert from 'node:assert/strict';
import { test } from 'node:test';
import { MonitoringService, HEARTBEAT } from '../extension/scripts/monitor/MonitoringService.js';
import { registerChromeEvents } from '../extension/scripts/monitor/ChromeEventSource.js';
import { createAPI, userAgent, tick, frame } from './helpers.mjs';

async function setup(t, options = {}) {
  const fixture = createAPI(options);
  const service = new MonitoringService(fixture.api, userAgent);
  registerChromeEvents(fixture.api, service);
  t.after(() => service.native.stop());
  await service.start();
  await tick();
  fixture.sent.length = 0;
  return { ...fixture, service };
}
const request = (overrides = {}) => ({ tabId: 1, requestId: 'r1', type: 'main_frame',
  frameId: 0, url: 'https://example.com/', timeStamp: Date.now(), ...overrides });
const commit = (overrides = {}) => ({ tabId: 1, frameId: 0, documentId: 'doc-1',
  url: 'https://example.com/', ...overrides });
const loadMessage = { type: 'argus:page-load', pageLoadTotalMs: 100.8,
  pageLoadNetworkMs: 60.2, pageLoadRenderMs: 40.6 };
function sender(api, overrides = {}) {
  return { id: api.runtime.id, tab: { id: 1 }, frameId: 0,
    documentId: 'doc-1', url: 'https://example.com/', ...overrides };
}

test('starts a single native connection without a popup and creates a wake-up alarm', async t => {
  const { service, ports, alarms } = await setup(t);
  await service.heartbeat();
  await service.heartbeat();
  assert.equal(ports.length, 1);
  assert.equal(ports[0].name, 'com.argus.extension.helper');
  assert.equal(alarms.get(HEARTBEAT).periodInMinutes, 0.5);
});

test('counts redirects once, tracks HTTP status classes, and deletes failed requests', async t => {
  const { service } = await setup(t);
  const start = request();
  service.onRequest(start);
  service.onRequest({ ...start, url: 'https://example.com/final', timeStamp: start.timeStamp + 10 });
  service.onCommitted(commit({ url: 'https://example.com/final' }));
  service.onCompleted({ ...start, statusCode: 200, timeStamp: start.timeStamp + 35.9 });
  for (const [i, statusCode] of [204, 302, 404, 503].entries()) {
    const resource = request({ requestId: `resource-${i}`, type: 'script', documentId: 'doc-1' });
    service.onRequest(resource);
    service.onCompleted({ ...resource, statusCode });
  }
  const failed = request({ requestId: 'failed', type: 'script', documentId: 'doc-1' });
  service.onRequest(failed);
  service.onRequestError(failed);
  const session = service.sessions.get(1);
  assert.equal(session.requestCount, 5);
  assert.equal(session.webRequestDurationMs, 35);
  assert.deepEqual([session.httpStatusCount2xx, session.httpStatusCount3xx,
    session.httpStatusCount4xx, session.httpStatusCount5xx], [2, 1, 1, 1]);
  assert.equal(service.requests.size, 0);
});

test('network completion before commit preserves the new navigation metrics', async t => {
  const { service } = await setup(t);
  service.onRequest(request());
  service.onCompleted({ ...request(), statusCode: 200 });
  service.onCommitted(commit());
  assert.equal(service.sessions.get(1).requestCount, 1);
  assert.equal(service.sessions.get(1).documentId, 'doc-1');
});

test('commit before webRequest still associates network and page-load metrics', async t => {
  const { service, api } = await setup(t, { frames: { 1: [frame()] } });
  service.onCommitted(commit());
  service.onRequest(request());
  service.onCompleted({ ...request(), statusCode: 200 });
  await service.onPageLoad(loadMessage, sender(api));
  assert.equal(service.sessions.get(1).requestCount, 1);
  assert.equal(service.sessions.get(1).pageLoadTotalDurationMs, 100);
});

test('cross-site navigation resets counters and rejects late resources and document messages', async t => {
  const { service, api, frames } = await setup(t, { frames: { 1: [frame()] } });
  service.onRequest(request());
  service.onCommitted(commit());
  const oldResource = request({ type: 'script', requestId: 'old-resource', documentId: 'doc-1' });
  service.onRequest(oldResource);
  await service.onPageLoad(loadMessage, sender(api));
  service.onRequest(request({ requestId: 'new-navigation', url: 'https://other.com/' }));
  service.onCommitted(commit({ documentId: 'doc-2', url: 'https://other.com/' }));
  frames[1] = [frame({ documentId: 'doc-2', url: 'https://other.com/' })];
  service.onCompleted({ ...oldResource, statusCode: 200 });
  await service.onPageLoad(loadMessage, sender(api));
  assert.equal(service.sessions.get(1).requestCount, 0);
  assert.equal(service.sessions.get(1).pageLoadTotalDurationMs, 0);
});

test('validates timing senders and values; accepts valid main document timings', async t => {
  const { service, api } = await setup(t, { frames: { 1: [frame()] } });
  for (const override of [{ frameId: 4 }, { tab: undefined }, { id: 'foreign-extension' }, { documentId: 'stale' }]) {
    await service.onPageLoad(loadMessage, sender(api, override));
  }
  await service.onPageLoad({ ...loadMessage, pageLoadTotalMs: NaN }, sender(api));
  assert.equal(service.sessions.size, 0);
  await service.onPageLoad(loadMessage, sender(api));
  assert.equal(service.sessions.get(1).pageLoadTotalDurationMs, 100);
});

test('does not send page A metrics with page B process metadata', async t => {
  const { service, api, frames, sent } = await setup(t, {
    tabs: [{ id: 1, url: 'https://example.com/' }], frames: { 1: [frame()] },
  });
  await service.onPageLoad(loadMessage, sender(api));
  frames[1] = [frame({ documentId: 'doc-2' })];
  await service.collect();
  assert.equal(sent.at(-1).frames[0].withMetric, undefined);
});

test('unordered frames, duplicate processes, prerenders and unavailable tabs are handled', async t => {
  const { service, api, sent } = await setup(t, {
    tabs: [{ id: 1, url: 'https://example.com/' }, { id: 2, url: 'https://closed.com/' },
      { id: 3, url: 'chrome://settings/' }, { id: 4, url: 'https://discarded.com/', discarded: true }],
    frames: { 1: [frame({ frameId: 2, processId: 201, documentId: 'child', url: 'https://child.com/' }),
      frame({ frameId: 3, processId: 201 }), frame({ frameId: 4 }),
      frame({ processId: 999, documentLifecycle: 'prerender' }), frame()], 2: null },
  });
  await service.onPageLoad(loadMessage, sender(api));
  await service.collect();
  const records = sent.flatMap(message => message.frames);
  assert.deepEqual(records.map(message => message.rendererClientId).sort(), [101, 101, 201, 201]);
  assert.equal(records.find(message => message.rendererClientId === 101 && !message.isSubframe).isSubframe, false);
  assert.equal(records.find(message => message.rendererClientId === 101 && !message.isSubframe).pageLoadTotalDurationMs, 100);
  assert.equal(records.find(message => message.rendererClientId === 201).isSubframe, true);
});

test('shared renderers preserve main frames and iframes across tabs', async t => {
  const { service, sent } = await setup(t, {
    tabs: [{ id: 1, url: 'https://example.com/' }, { id: 2, url: 'https://child.com/' }],
    frames: { 1: [frame(), frame({ frameId: 2, processId: 201 })],
      2: [frame({ processId: 201, url: 'https://child.com/' })] },
  });
  await service.collect();
  const records = sent.flatMap(message => message.frames);
  assert.equal(records.filter(message => message.rendererClientId === 201).length, 2);
});

test('reconnects after native host disconnect without creating duplicate ports', async t => {
  const { service, api, ports } = await setup(t);
  api.runtime.lastError = { message: 'Native host has exited.' };
  ports[0].onDisconnect.emit();
  delete api.runtime.lastError;
  assert.equal(service.status().connected, false);
  assert.equal(service.status().error, 'Native host has exited.');
  await service.heartbeat();
  assert.equal(ports.length, 2);
  assert.equal(service.status().connected, true);
});

test('pause persists across worker restarts and resume makes one connection', async t => {
  const { service, api, ports, local, session } = await setup(t);
  await service.setEnabled(false);
  assert.equal(local.monitorEnabled, false);
  await service.heartbeat();
  assert.equal(ports.length, 1);
  const next = new MonitoringService(api, userAgent);
  t.after(() => next.native.stop());
  await next.start();
  assert.equal(next.status().enabled, false);
  assert.equal(ports.length, 1);
  assert.deepEqual(session.tabSessions, {});
  await next.setEnabled(true);
  assert.equal(ports.length, 2);
});

test('hydrates checkpoints before replaying events dispatched at worker startup', async t => {
  const fixture = createAPI({ local: { monitorEnabled: false },
    tabs: [{ id: 1, url: 'https://example.com/' }],
    session: { tabSessions: { 1: { documentId: 'old', url: 'https://old.com/' } } } });
  fixture.local.monitorEnabled = true;
  const service = new MonitoringService(fixture.api, userAgent);
  t.after(() => service.native.stop());
  service.onCommitted(commit());
  await service.start();
  assert.equal(service.sessions.get(1).documentId, 'doc-1');
});

test('tab closure and expired requests release all related state', async t => {
  const { service } = await setup(t);
  service.onRequest(request());
  service.onCommitted(commit());
  service.removeTab(1);
  assert.equal(service.sessions.size, 0);
  assert.equal(service.requests.size, 0);
  service.onRequest(request({ timeStamp: Date.now() - 6 * 60 * 1000 }));
  service.pruneRequests(Date.now());
  assert.equal(service.requests.size, 0);
  assert.equal(service.navigations.size, 0);
});

test('Edge detection works without userAgentData', async t => {
  const { api } = createAPI({ local: { monitorEnabled: false } });
  const service = new MonitoringService(api, { userAgent: 'Chrome/140.0 Edg/140.0' });
  t.after(() => service.native.stop());
  await service.start();
  assert.equal(service.status().browser, 'edge');
});

test('same-URL reload clears a session first discovered through a content report', async t => {
  const { service, api, frames } = await setup(t, { frames: { 1: [frame()] } });
  await service.onPageLoad(loadMessage, sender(api));
  service.onRequest(request({ type: 'script', requestId: 'old-script', documentId: 'doc-1' }));
  service.onCompleted({ ...request({ requestId: 'old-script', documentId: 'doc-1' }), statusCode: 200 });
  service.onRequest(request({ requestId: 'reload' }));
  service.onCommitted(commit({ documentId: 'doc-2' }));
  frames[1] = [frame({ documentId: 'doc-2' })];
  service.onCompleted({ ...request({ requestId: 'reload' }), statusCode: 200 });
  const session = service.sessions.get(1);
  assert.equal(session.requestCount, 1);
  assert.equal(session.pageLoadTotalDurationMs, 0);
});

test('iframe requests belong to their current top document and cannot leak across top navigations', async t => {
  const { service } = await setup(t);
  service.onCommitted(commit());
  service.onCommitted(commit({ frameId: 2, documentId: 'child-1' }));
  const resource = request({ type: 'script', frameId: 2, requestId: 'child-script', documentId: 'child-1' });
  service.onRequest(resource);
  service.onCompleted({ ...resource, statusCode: 200 });
  assert.equal(service.sessions.get(1).requestCount, 1);
  service.onCommitted(commit({ documentId: 'doc-2', url: 'https://other.com/' }));
  service.onRequest({ ...resource, requestId: 'stale-child' });
  service.onCompleted({ ...resource, requestId: 'stale-child', statusCode: 200 });
  assert.equal(service.sessions.get(1).requestCount, 0);
});
