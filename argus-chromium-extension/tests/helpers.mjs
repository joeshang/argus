export class ChromeEvent {
  listeners = [];
  addListener(listener) { this.listeners.push(listener); }
  emit(...args) { return this.listeners.map(listener => listener(...args)); }
}

export function createAPI({ local = {}, session = {}, tabs = [], frames = {} } = {}) {
  const ports = [];
  const sent = [];
  const alarms = new Map();
  const storageArea = data => ({
    async get(key) { return structuredClone(key ? { [key]: data[key] } : data); },
    async set(value) { Object.assign(data, structuredClone(value)); },
  });
  const api = {
    runtime: {
      id: 'knldjmfmopnpolahpmmgbagdohdnhkik',
      getURL: path => `chrome-extension://knldjmfmopnpolahpmmgbagdohdnhkik/${path}`,
      getPlatformInfo: async () => ({ os: 'mac' }),
      onMessage: new ChromeEvent(), onInstalled: new ChromeEvent(), onStartup: new ChromeEvent(),
      connectNative(name) {
        const port = { name, onMessage: new ChromeEvent(), onDisconnect: new ChromeEvent(),
          postMessage: message => sent.push(structuredClone(message)), disconnect() { this.onDisconnect.emit(); } };
        ports.push(port);
        return port;
      },
    },
    storage: { local: storageArea(local), session: storageArea(session) },
    alarms: { onAlarm: new ChromeEvent(), get: async name => alarms.get(name),
      create: async (name, value) => alarms.set(name, value) },
    tabs: { query: async () => tabs, onRemoved: new ChromeEvent(), onReplaced: new ChromeEvent() },
    webRequest: { onBeforeRequest: new ChromeEvent(), onCompleted: new ChromeEvent(), onErrorOccurred: new ChromeEvent() },
    webNavigation: { onCommitted: new ChromeEvent(), getAllFrames: async ({ tabId }) => frames[tabId] },
  };
  return { api, ports, sent, local, session, frames, tabs, alarms };
}

export const userAgent = { userAgentData: { brands: [{ brand: 'Google Chrome' }] } };
export const tick = () => new Promise(resolve => setImmediate(resolve));
export const frame = (overrides = {}) => ({ frameId: 0, processId: 101, documentId: 'doc-1',
  documentLifecycle: 'active', url: 'https://example.com/', ...overrides });
