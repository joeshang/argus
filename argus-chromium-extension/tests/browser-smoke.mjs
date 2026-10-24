// Integration smoke test using only Node's built-in CDP/WebSocket support.
// Use Chromium or Chrome for Testing: branded Chrome restricts --load-extension.
import assert from 'node:assert/strict';
import { spawn, execFileSync } from 'node:child_process';
import { once } from 'node:events';
import { mkdtemp, rm } from 'node:fs/promises';
import http from 'node:http';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const executable = process.argv[2] || process.env.ARGUS_CHROME;
if (!executable) throw new Error('Pass a Chromium/Chrome for Testing executable path or set ARGUS_CHROME');
const extension = fileURLToPath(new URL('../extension/', import.meta.url));
const extensionID = 'knldjmfmopnpolahpmmgbagdohdnhkik';
const profile = await mkdtemp(join(tmpdir(), 'argus-browser-'));
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(check, label) {
  const deadline = Date.now() + 15000;
  while (Date.now() < deadline) {
    const value = await check();
    if (value) return value;
    await delay(100);
  }
  throw new Error(`Timed out: ${label}`);
}
const server = http.createServer((request, response) => {
  if (request.url === '/asset.js') {
    response.setHeader('Content-Type', 'text/javascript');
    response.end('document.title = "Argus fixture";');
  } else if (request.url === '/child') {
    response.setHeader('Content-Type', 'text/html');
    response.end('<!doctype html><script src="/asset.js"></script><p>Child frame</p>');
  } else {
    response.setHeader('Content-Type', 'text/html');
    response.end('<!doctype html><title>Argus</title><script src="/asset.js"></script><iframe src="/child"></iframe><p>Argus browser fixture</p>');
  }
});
server.listen(0, '127.0.0.1');
await once(server, 'listening');
let child;
let cdp;
class CDP {
  constructor(ws) {
    this.ws = ws;
    this.nextId = 0;
    this.pending = new Map();
    ws.addEventListener('message', event => {
      const message = JSON.parse(event.data);
      if (!message.id) return;
      const handlers = this.pending.get(message.id);
      if (!handlers) return;
      this.pending.delete(message.id);
      if (message.error) handlers.reject(new Error(JSON.stringify(message.error)));
      else handlers.resolve(message.result);
    });
  }
  async send(method, params = {}, sessionId) {
    const id = ++this.nextId;
    const result = new Promise((resolve, reject) => this.pending.set(id, { resolve, reject }));
    this.ws.send(JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) }));
    return result;
  }
  async attach(targetId) {
    return (await this.send('Target.attachToTarget', { targetId, flatten: true })).sessionId;
  }
  async evaluate(expression, sessionId) {
    const result = await this.send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true }, sessionId);
    if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
    return result.result.value;
  }
}
try {
  child = spawn(executable, ['--headless=new', `--user-data-dir=${profile}`,
    '--remote-debugging-port=0', '--no-first-run', '--no-default-browser-check',
    `--disable-extensions-except=${extension}`, `--load-extension=${extension}`, 'about:blank'],
  { stdio: ['ignore', 'ignore', 'pipe'] });
  let stderr = '';
  child.stderr.on('data', buffer => { stderr += buffer; });
  const endpoint = await until(() => {
    if (child.exitCode !== null) throw new Error(`Browser exited: ${stderr}`);
    return stderr.match(/DevTools listening on (ws:\/\/\S+)/)?.[1];
  }, 'browser startup');
  const ws = new WebSocket(endpoint);
  await once(ws, 'open');
  cdp = new CDP(ws);
  const version = await cdp.send('Browser.getVersion');
  const worker = await until(async () => (await cdp.send('Target.getTargets')).targetInfos.find(
    target => target.type === 'service_worker' && target.url === `chrome-extension://${extensionID}/background.js`), 'MV3 worker registration');
  const workerSession = await cdp.attach(worker.targetId);
  const popupTarget = await cdp.send('Target.createTarget', { url: `chrome-extension://${extensionID}/main.html` });
  const popupSession = await cdp.attach(popupTarget.targetId);
  await until(() => cdp.evaluate('document.getElementById("toggle")?.disabled === false', popupSession), 'popup ready');
  const initial = await cdp.evaluate('chrome.runtime.sendMessage({type:"argus:status"})', popupSession);
  assert.equal(initial.enabled, true);

  // The popup is closed while the content script and worker collect metrics.
  await cdp.send('Target.closeTarget', { targetId: popupTarget.targetId });
  const url = `http://127.0.0.1:${server.address().port}/`;
  const page = await cdp.send('Target.createTarget', { url });
  const session = await until(async () => {
    const checkpoint = await cdp.evaluate('chrome.storage.session.get("tabSessions")', workerSession);
    return Object.values(checkpoint.tabSessions || {}).find(item => item.url === url && item.pageLoadTotalDurationMs > 0);
  }, 'content timing with popup closed');
  assert.ok(session.requestCount >= 4, JSON.stringify(session));
  const frames = await cdp.evaluate(`(async () => {
    const tab = (await chrome.tabs.query({})).find(tab => tab.url === ${JSON.stringify(url)});
    return chrome.webNavigation.getAllFrames({tabId: tab.id});
  })()`, workerSession);
  const top = frames.find(item => item.frameId === 0);
  assert.ok(top.processId > 0);
  if (process.platform === 'darwin') {
    const commands = execFileSync('ps', ['-axo', 'command='], { encoding: 'utf8' });
    assert.ok(commands.split('\n').some(line => line.includes(profile) &&
      new RegExp(`--renderer-client-id=${top.processId}(?:\\s|$)`).test(line)), 'frame processId matches native renderer-client-id');
  }
  const popup2 = await cdp.send('Target.createTarget', { url: `chrome-extension://${extensionID}/main.html` });
  const popupSession2 = await cdp.attach(popup2.targetId);
  await until(() => cdp.evaluate('document.getElementById("toggle")?.disabled === false', popupSession2), 'second popup ready');
  await cdp.evaluate('document.getElementById("toggle").click()', popupSession2);
  await until(() => cdp.evaluate('document.getElementById("status").textContent === "采集已暂停"', popupSession2), 'pause control');
  assert.equal((await cdp.evaluate('chrome.storage.local.get("monitorEnabled")', popupSession2)).monitorEnabled, false);
  // Stop the actual worker and send a new popup message to wake it again.
  await cdp.send('Target.detachFromTarget', { sessionId: workerSession });
  await cdp.send('ServiceWorker.enable', {}, popupSession2);
  await cdp.send('ServiceWorker.stopAllWorkers', {}, popupSession2);
  await until(async () => !(await cdp.send('Target.getTargets')).targetInfos.some(target => target.targetId === worker.targetId), 'worker stopped');
  const restarted = await cdp.evaluate('chrome.runtime.sendMessage({type:"argus:status"})', popupSession2);
  assert.equal(restarted.enabled, false);
  await cdp.evaluate('document.getElementById("toggle").click()', popupSession2);
  await until(() => cdp.evaluate('chrome.storage.local.get("monitorEnabled").then(s => s.monitorEnabled === true)', popupSession2), 'resume control');
  await cdp.send('Target.closeTarget', { targetId: page.targetId });
  console.log(JSON.stringify({ browser: version.product, extensionID, rendererClientId: top.processId,
    requestCount: session.requestCount, pageLoadTotalMs: session.pageLoadTotalDurationMs,
    checks: ['MV3 registration', 'popup controls', 'background collection with popup closed',
      'navigation timing', 'renderer ID mapping', 'worker termination/recovery', 'pause persistence'] }, null, 2));
} finally {
  if (cdp) {
    await cdp.send('Browser.close').catch(() => {});
    cdp.ws.close();
  }
  if (child && child.exitCode === null) {
    child.kill('SIGTERM');
    await once(child, 'exit');
  }
  server.close();
  server.closeAllConnections();
  await rm(profile, { recursive: true, force: true });
}
