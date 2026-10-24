(() => {
  // Static, top-frame-only injection runs once per document, independently of
  // the number of HTTP requests. No request bodies or page scripts are read.
  let sent = false;
  function report() {
    if (sent) return;
    const timing = performance.getEntriesByType('navigation')[0];
    if (!timing || timing.loadEventEnd <= 0) return;
    sent = true;
    const networkStart = timing.redirectStart || timing.fetchStart;
    try {
      void chrome.runtime.sendMessage({
        type: 'argus:page-load',
        pageLoadTotalMs: Math.max(0, timing.loadEventEnd - timing.startTime),
        pageLoadNetworkMs: Math.max(0, timing.responseEnd - networkStart),
        pageLoadRenderMs: Math.max(0, timing.loadEventEnd - timing.responseEnd),
      }).catch(() => {});
    } catch {
      // Reloading/uninstalling the extension can also throw synchronously.
    }
  }
  // loadEventEnd is populated after the load callback returns. A queued task
  // also covers content scripts installed after a document has already loaded.
  if (document.readyState === 'complete') setTimeout(report, 0);
  else window.addEventListener('load', () => setTimeout(report, 0), { once: true });
  window.addEventListener('pageshow', event => {
    if (event.persisted) {
      sent = false;
      setTimeout(report, 0);
    }
  });
})();
