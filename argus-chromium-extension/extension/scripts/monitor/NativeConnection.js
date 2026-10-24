const HOST = 'com.argus.extension.helper';

// A native port keeps an MV3 worker alive (Chrome 105+). Retry timers are only
// a fast path; the worker's alarm also retries after suspension or browser sleep.
export class NativeConnection {
  constructor(api, onChange) {
    this.api = api;
    this.onChange = onChange;
    this.enabled = false;
    this.port = null;
    this.retryTimer = null;
    this.retryDelay = 1000;
    this.error = null;
  }

  connect() {
    if (!this.enabled || this.port) return;
    clearTimeout(this.retryTimer);
    this.retryTimer = null;
    try {
      const port = this.api.runtime.connectNative(HOST);
      this.port = port;
      port.onMessage.addListener(() => {
        this.retryDelay = 1000;
        this.error = null;
      });
      port.onDisconnect.addListener(() => {
        const error = this.api.runtime.lastError?.message;
        if (this.port !== port) return;
        this.port = null;
        this.error = error || 'Native host disconnected';
        this.onChange();
        this.scheduleRetry();
      });
      this.error = null;
      this.onChange();
    } catch (error) {
      this.error = error.message;
      this.onChange();
      this.scheduleRetry();
    }
  }

  scheduleRetry() {
    if (!this.enabled || this.retryTimer) return;
    this.retryTimer = setTimeout(() => {
      this.retryTimer = null;
      this.connect();
    }, this.retryDelay);
    this.retryDelay = Math.min(this.retryDelay * 2, 30000);
  }

  send(message) {
    if (!this.port) return false;
    try {
      this.port.postMessage(message);
      return true;
    } catch (error) {
      const port = this.port;
      this.port = null;
      port.disconnect();
      this.error = error.message;
      this.onChange();
      this.scheduleRetry();
      return false;
    }
  }

  stop() {
    this.enabled = false;
    clearTimeout(this.retryTimer);
    this.retryTimer = null;
    const port = this.port;
    this.port = null;
    port?.disconnect();
    this.onChange();
  }
}
