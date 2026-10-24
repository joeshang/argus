import { NativeConnection } from './NativeConnection.js';
import { snapshotMessages } from './SnapshotProtocol.js';

export const HEARTBEAT = 'argus-heartbeat';
const SESSION_KEY = 'tabSessions';
const REQUEST_TTL_MS = 5 * 60 * 1000;
const MAX_OPEN_REQUESTS = 10000;
const METRIC_FIELDS = [
  'httpStatusCount2xx', 'httpStatusCount3xx', 'httpStatusCount4xx',
  'httpStatusCount5xx', 'requestCount', 'webRequestDurationMs',
  'pageLoadTotalDurationMs', 'pageLoadNetworkDurationMs', 'pageLoadRenderDurationMs',
];

function newSession(url, documentId = null) {
  return { url, documentId, documents: {}, navigationRequestId: null,
    ...Object.fromEntries(METRIC_FIELDS.map(key => [key, 0])) };
}

function monitoredURL(url) {
  return typeof url === 'string' && /^https?:\/\//i.test(url) &&
    !/^https?:\/\/[^/]*google\.[^/]+\/_\/chrome\/newtab(?:[/?#]|$)/i.test(url);
}

export class MonitoringService {
  constructor(api, userAgent = globalThis.navigator) {
    this.api = api;
    this.userAgent = userAgent;
    this.enabled = false;
    this.sessions = new Map();
    this.requests = new Map();
    this.navigations = new Map();
    this.timer = null;
    this.collecting = false;
    this.sourceId = null;
    this.generation = 0;
    this.collectionError = null;
    this.browser = 'unknown';
    this.OS = 'unknown';
    this.native = new NativeConnection(api, () => this.connectionChanged());
    // Event handlers may run before storage hydration. Queue those events so a
    // worker restart never replaces a fresh navigation with an old checkpoint.
    this.pendingEvents = [];
  }

  async start() {
    const [settings, checkpoint, platform] = await Promise.all([
      this.api.storage.local.get('monitorEnabled'),
      this.api.storage.session.get(null),
      this.api.runtime.getPlatformInfo(),
    ]);
    const brands = this.userAgent?.userAgentData?.brands || [];
    const ua = this.userAgent?.userAgent || '';
    this.browser = brands.some(item => item.brand === 'Microsoft Edge') || /Edg\//.test(ua)
      ? 'edge' : brands.some(item => item.brand === 'Google Chrome') || /Chrome\//.test(ua)
        ? 'chrome' : 'unknown';
    this.OS = ({ mac: 'macOS', win: 'Windows', linux: 'Linux', cros: 'Chrome OS' })[platform.os] || platform.os;
    this.sourceId = checkpoint.sourceId || crypto.randomUUID();
    await this.api.storage.session.set({ sourceId: this.sourceId });
    this.enabled = settings.monitorEnabled !== false;
    this.sessions = new Map(Object.entries(checkpoint[SESSION_KEY] || {}).map(([id, value]) => [Number(id), value]));
    const events = this.pendingEvents;
    this.pendingEvents = null;
    for (const apply of events) apply();
    await this.ensureAlarm();
    this.native.enabled = this.enabled;
    this.native.connect();
  }

  async ensureAlarm() {
    if (!await this.api.alarms.get(HEARTBEAT)) {
      await this.api.alarms.create(HEARTBEAT, { periodInMinutes: 0.5 });
    }
  }

  connectionChanged() {
    clearInterval(this.timer);
    this.timer = null;
    if (this.enabled && this.native.port) {
      void this.collect().catch(error => console.warn('Argus collection failed:', error));
      this.timer = setInterval(() => {
        void this.collect().catch(error => console.warn('Argus collection failed:', error));
      }, 2000);
    }
  }

  async heartbeat() {
    if (!this.enabled) return;
    this.native.connect();
    await this.collect();
  }

  async setEnabled(enabled) {
    await this.api.storage.local.set({ monitorEnabled: enabled });
    this.enabled = enabled;
    if (enabled) {
      this.native.enabled = true;
      this.native.connect();
    } else {
      this.native.stop();
      this.requests.clear();
      this.navigations.clear();
      this.sessions.clear();
      await this.save();
    }
    return this.status();
  }

  status() {
    return { enabled: this.enabled, connected: Boolean(this.native.port),
      error: this.native.error || this.collectionError, browser: this.browser, OS: this.OS };
  }

  defer(apply) {
    if (this.pendingEvents) {
      this.pendingEvents.push(apply);
      return true;
    }
    return false;
  }

  onRequest(details) {
    if (this.defer(() => this.onRequest(details))) return;
    if (!this.enabled || details.tabId < 0 || !monitoredURL(details.url)) return;
    let session = this.sessions.get(details.tabId);
    if (details.type === 'main_frame') {
      const previous = this.requests.get(details.requestId);
      if (previous) {
        previous.session.url = details.url;
        return; // Redirects keep the original start time and request counter.
      }
      // webRequest and webNavigation have no defined ordering. Keep pending
      // navigation data separate until commit, and adopt an unmatched commit
      // when the network event arrives later.
      if (!session?.awaitingRequest || session.url !== details.url) {
        session = newSession(details.url);
      }
      session.awaitingRequest = false;
      session.navigationRequestId = details.requestId;
      session.startedAt = details.timeStamp;
      this.navigations.set(details.tabId, session);
    } else if (!session && details.frameId === 0 && details.documentId) {
      session = newSession('', details.documentId);
      this.sessions.set(details.tabId, session);
    }
    if (!session) return;
    if (details.type !== 'main_frame' && details.documentId) {
      const documents = [session.documentId, ...Object.values(session.documents || {})];
      if (!documents.includes(details.documentId) && !documents.includes(details.parentDocumentId)) return;
    }
    // Bound memory even for requests that never complete (streams, cancelled tabs).
    if (this.requests.size >= MAX_OPEN_REQUESTS) this.pruneRequests(Date.now());
    if (this.requests.size >= MAX_OPEN_REQUESTS) this.requests.delete(this.requests.keys().next().value);
    if (!this.requests.has(details.requestId)) {
      this.requests.set(details.requestId, { session, tabId: details.tabId,
        start: details.timeStamp, type: details.type });
    }
  }

  onCompleted(details) {
    if (this.defer(() => this.onCompleted(details))) return;
    const request = this.requests.get(details.requestId);
    this.requests.delete(details.requestId);
    if (!this.enabled || !request || (this.sessions.get(details.tabId) !== request.session &&
        this.navigations.get(details.tabId) !== request.session)) return;
    const session = request.session;
    session.requestCount++;
    const statusClass = Math.floor(details.statusCode / 100);
    if (statusClass >= 2 && statusClass <= 5) session[`httpStatusCount${statusClass}xx`]++;
    if (request.type === 'main_frame') {
      session.webRequestDurationMs = Math.max(0, Math.floor(details.timeStamp - request.start));
    }
  }

  onRequestError(details) {
    if (this.defer(() => this.onRequestError(details))) return;
    const request = this.requests.get(details.requestId);
    this.requests.delete(details.requestId);
    if (request && this.navigations.get(details.tabId) === request.session) {
      this.navigations.delete(details.tabId);
    }
  }

  onCommitted(details) {
    if (this.defer(() => this.onCommitted(details))) return;
    if (!this.enabled || (details.documentLifecycle && details.documentLifecycle !== 'active')) return;
    if (details.frameId !== 0) {
      const session = this.sessions.get(details.tabId);
      if (session && details.documentId) {
        session.documents ||= {};
        session.documents[details.frameId] = details.documentId;
      }
      return;
    }
    if (!monitoredURL(details.url)) {
      this.removeTab(details.tabId);
      return;
    }
    let session = this.sessions.get(details.tabId);
    if (!session || (session.documentId && session.documentId !== details.documentId)) {
      session = this.navigations.get(details.tabId);
      if (!session) {
        session = newSession(details.url, details.documentId);
        session.awaitingRequest = true;
      }
      this.sessions.set(details.tabId, session);
    }
    this.navigations.delete(details.tabId);
    session.documentId = details.documentId;
    session.documents = { 0: details.documentId };
    session.url = details.url;
  }

  async onPageLoad(message, sender) {
    if (!this.enabled || sender.id !== this.api.runtime.id ||
        !sender.tab || sender.frameId !== 0 || !sender.documentId || !monitoredURL(sender.url)) return;
    const values = [message.pageLoadTotalMs, message.pageLoadNetworkMs, message.pageLoadRenderMs];
    if (!values.every(value => Number.isFinite(value) && value >= 0)) return;
    // A late message from the previous document must never contaminate this tab.
    const frames = await this.api.webNavigation.getAllFrames({ tabId: sender.tab.id });
    const top = frames?.find(frame => frame.frameId === 0 && frame.documentLifecycle !== 'prerender');
    if (!top || top.documentId !== sender.documentId || !this.enabled) return;
    let session = this.sessions.get(sender.tab.id);
    if (!session) {
      session = newSession(sender.url, sender.documentId);
      this.sessions.set(sender.tab.id, session);
    }
    if (session.documentId !== sender.documentId) return;
    session.awaitingRequest = false;
    [session.pageLoadTotalDurationMs, session.pageLoadNetworkDurationMs, session.pageLoadRenderDurationMs] = values.map(Math.floor);
    await this.save();
  }

  removeTab(tabId) {
    if (this.defer(() => this.removeTab(tabId))) return;
    this.sessions.delete(tabId);
    this.navigations.delete(tabId);
    for (const [id, request] of this.requests) {
      if (request.tabId === tabId) this.requests.delete(id);
    }
  }

  pruneRequests(now) {
    for (const [tabId, session] of this.navigations) {
      if (now - session.startedAt > REQUEST_TTL_MS) this.navigations.delete(tabId);
    }
    for (const [id, request] of this.requests) {
      if (now - request.start > REQUEST_TTL_MS) this.requests.delete(id);
    }
  }

  save() {
    return this.api.storage.session.set({ [SESSION_KEY]: Object.fromEntries(this.sessions) });
  }

  async collect() {
    if (!this.enabled || this.collecting) return;
    this.collecting = true;
    try {
      this.pruneRequests(Date.now());
      const tabs = await this.api.tabs.query({});
      const liveTabs = new Set(tabs.map(tab => tab.id));
      for (const id of this.sessions.keys()) if (!liveTabs.has(id)) this.removeTab(id);
      // Limit concurrent frame queries for profiles with hundreds of tabs.
      const records = [];
      for (let offset = 0; offset < tabs.length && this.enabled; offset += 8) {
        await Promise.all(tabs.slice(offset, offset + 8).map(async tab => {
          if (tab.discarded || !monitoredURL(tab.url)) return;
          let frames;
          try {
            frames = await this.api.webNavigation.getAllFrames({ tabId: tab.id });
          } catch {
            return; // Tabs may close or navigate while this query is in flight.
          }
          const active = (frames || []).filter(frame => !frame.errorOccurred &&
            (!frame.documentLifecycle || frame.documentLifecycle === 'active'));
          const top = active.find(frame => frame.frameId === 0);
          if (!top || !monitoredURL(top.url)) return;
          const tabSession = this.sessions.get(tab.id);
          if (tabSession?.documentId === top.documentId) {
            tabSession.documents = Object.fromEntries(active.map(frame => [frame.frameId, frame.documentId]));
          }
          for (const frame of active) {
            if (!Number.isInteger(frame.processId) || frame.processId <= 0 || !monitoredURL(frame.url)) continue;
            const isSubframe = frame.frameId !== 0;
            const session = this.sessions.get(tab.id);
            const withMetric = !isSubframe && Boolean(session?.documentId) && session.documentId === frame.documentId;
            if (!frame.documentId) continue;
            records.push({ tabId: tab.id, frameId: frame.frameId,
              parentFrameId: frame.parentFrameId ?? (isSubframe ? 0 : -1), documentId: frame.documentId,
              isSubframe, url: frame.url, rendererClientId: frame.processId,
              ...(withMetric ? { withMetric: true,
                ...Object.fromEntries(METRIC_FIELDS.map(key => [key, session[key]])) } : {}) });
          }
        }));
      }
      if (this.enabled) {
        await this.save();
        try {
          const messages = snapshotMessages({ browser: this.browser, OS: this.OS,
            sourceId: this.sourceId, generation: ++this.generation }, records);
          this.collectionError = null;
          for (const message of messages) if (!this.native.send(message)) break;
        } catch (error) {
          this.collectionError = error.message;
        }
      }
    } finally {
      this.collecting = false;
    }
  }
}
