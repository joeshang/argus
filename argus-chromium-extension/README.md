# Chromium extension

A Manifest V3 extension for Chrome and Edge 120+. Follow the [project quick start](../README.md#quick-start) to build the native components, register the host, and load this directory's `extension` folder.

## Collection

The module service worker registers listeners synchronously and maintains one Native Messaging connection. While connected, it sends full frame snapshots every two seconds. Disconnections trigger exponential retries capped at 30 seconds; an alarm provides a recovery path after worker suspension.

HTTP(S) request observation is read-only. The extension counts completed requests and their final status codes without reading bodies or blocking requests. A top-level content script reports Navigation Timing after load and on back/forward-cache restoration. Document identities keep navigation data separate.

`storage.local` retains the enabled setting. `storage.session` retains the profile-session identity and recent metric checkpoints across worker restarts. In-flight requests are not persisted, and abrupt termination can lose counts since the last checkpoint. The popup controls collection; closing it leaves background collection running.

## Permissions

| Permission | Purpose |
| --- | --- |
| `nativeMessaging` | Connect to the local helper |
| `tabs` / `webNavigation` | Read tab, frame, document, URL, and renderer identities |
| `webRequest` | Observe request completion and duration |
| `storage` / `alarms` | Restore settings and recover background work |
| HTTP(S) host permissions | Observe requests and run the timing script |

Collection is local. Browser-internal pages, extension pages, file URLs, discarded tabs, and inactive prerenders are excluded. Restricted pages may have no content-script timings. Refresh existing pages after installing/reloading the extension or resuming a paused collection to obtain fresh load timings.

## Connection troubleshooting

The host name is `com.argus.extension.helper`; the fixed extension ID is `knldjmfmopnpolahpmmgbagdohdnhkik`. The host manifest must point to an executable **ArgusMacExtensionHelper** binary and allow that extension origin.

Registration locations on macOS:

- Chrome: `~/Library/Application Support/Google/Chrome/NativeMessagingHosts/com.argus.extension.helper.json`
- Edge: `~/Library/Application Support/Microsoft Edge/NativeMessagingHosts/com.argus.extension.helper.json`

Check the popup and the service worker's console at the browser's extension-management page. Ensure the service is running, its user matches the helper's user, and all three components use protocol v2. The helper exits on connection failure so the extension can retry.

## Tests

From this directory, with Node.js 22+ and no npm dependencies to install:

```sh
npm test
npm run test:browser -- "/absolute/path/to/Google Chrome for Testing"
```

The browser test uses a temporary profile and local HTTP fixture. It checks worker loading/recovery, popup controls, timing, and renderer-ID mapping without changing host registrations. Use Chromium or Chrome for Testing for command-line extension loading. Native communication has a separate [test suite](../native-tests/README.md).

See [how it works](../docs/how-it-works.md) and the [protocol reference](../docs/discussions/schema.md) for the mapping model and metric definitions.
