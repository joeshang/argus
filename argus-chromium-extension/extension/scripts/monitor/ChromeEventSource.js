// Register every listener synchronously when the service worker is evaluated.
// Read-only webRequest observation works in MV3 without webRequestBlocking.
export function registerChromeEvents(api, service) {
  const filter = { urls: ['http://*/*', 'https://*/*'] };
  api.webRequest.onBeforeRequest.addListener(details => service.onRequest(details), filter);
  api.webRequest.onCompleted.addListener(details => service.onCompleted(details), filter);
  api.webRequest.onErrorOccurred.addListener(details => service.onRequestError(details), filter);
  api.webNavigation.onCommitted.addListener(details => service.onCommitted(details));
  api.tabs.onRemoved.addListener(tabId => service.removeTab(tabId));
  api.tabs.onReplaced.addListener((added, removed) => service.removeTab(removed));
}
