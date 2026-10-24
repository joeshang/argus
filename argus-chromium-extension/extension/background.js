import { registerChromeEvents } from './scripts/monitor/ChromeEventSource.js';
import { MonitoringService, HEARTBEAT } from './scripts/monitor/MonitoringService.js';

const service = new MonitoringService(chrome);
// Listener registration must precede all asynchronous initialization in MV3.
registerChromeEvents(chrome, service);
chrome.alarms.onAlarm.addListener(alarm => {
  if (alarm.name === HEARTBEAT) void ready.then(() => service.heartbeat()).catch(reportError);
});
chrome.runtime.onStartup.addListener(() => {
  void ready.then(() => service.ensureAlarm()).catch(reportError);
});
chrome.runtime.onInstalled.addListener(() => {
  void ready.then(() => service.ensureAlarm()).catch(reportError);
});
chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (sender.id !== chrome.runtime.id) return;
  if (message?.type === 'argus:page-load') {
    void ready.then(() => service.onPageLoad(message, sender)).catch(reportError);
    return;
  }
  if (sender.url !== chrome.runtime.getURL('main.html')) return;
  if (message?.type !== 'argus:status' && message?.type !== 'argus:set-enabled') return;
  ready.then(async () => {
    if (message.type === 'argus:set-enabled' && typeof message.enabled === 'boolean') {
      return service.setEnabled(message.enabled);
    }
    return service.status();
  }).then(sendResponse, error => sendResponse({ error: error.message }));
  return true;
});
function reportError(error) {
  console.warn('Argus:', error);
}
const ready = service.start();
void ready.catch(reportError);
