# Argus

**Monitor browser tab performance from outside the browser.**

Argus maps page URLs to their renderer processes so an independent macOS service can report CPU, memory, page-load timings, and HTTP metrics while users browse normally.

## Why Argus

- **Works with Stable Chrome and Edge.** No Dev-channel browser build or `chrome.processes` API is required.
- **No Chrome DevTools Protocol (CDP).** Collection requires no DevTools attachment, remote-debugging port, or special browser launch flags.
- **Connects pages to OS processes.** Identify the web apps behind resource usage, with tab/frame mappings across browser instances and profiles, including shared renderers and out-of-process iframes.
- **Combines resource and loading metrics.** View native CPU and memory measurements alongside navigation timings, completed-request counts, and HTTP status groups, without opening the browser's Task Manager.
- **Collects locally.** A Manifest V3 extension sends page metadata through Native Messaging and local IPC to the native service. Collection continues when the extension popup is closed.

Chrome and Edge provide page metadata through the extension. Safari uses native WebKit origin and display-name lookup. CPU and memory values describe whole processes, including all pages sharing a renderer. See [how it works](docs/how-it-works.md) for the mapping principle and its boundaries.

## Requirements

- macOS 12.3+, Xcode Command Line Tools, and CMake 3.24+.
- Python 3.9+ for native tests; Node.js 22+ for host registration and extension tests.
- Chrome or Edge 120+ for the extension. Initial CMake configuration downloads pinned source dependencies.

## Quick start

From the repository root:

```sh
cmake --preset debug
cmake --build --preset debug --parallel 4
node argus-chromium-extension/scripts/install-host.mjs \
  "$PWD/.build/debug/bin/ArgusMacExtensionHelper" chrome
.build/debug/bin/ArgusMacServices
```

Keep the service running in a terminal, then:

1. Open `chrome://extensions` and enable **Developer mode**.
2. Choose **Load unpacked** and select `argus-chromium-extension/extension`.
3. Refresh the pages to monitor. Use the extension popup to check the connection or pause collection.

For Edge, register the host with `edge` instead of `chrome` and load the extension at `edge://extensions`. Safari origin lookup requires only the native service.

Run the helper and service as the same user, without `sudo`. Register **ArgusMacExtensionHelper** as the native host; the browser launches it automatically. Update the extension, helper, and service together.

## Development

```sh
ctest --preset debug
npm --prefix argus-chromium-extension test
```

Use the `release` or `ubsan` CMake presets for other build configurations. Native tests use isolated temporary runtime directories and leave browser host registrations unchanged.

## Documentation

- [How it works](docs/how-it-works.md): the page-to-process mapping and architecture.
- [Protocol v2](docs/discussions/schema.md): identities, snapshots, and metric definitions.
- [Native service](argus-native-service/README.md): installation, Xcode builds, and local IPC.
- [Chromium extension](argus-chromium-extension/README.md): permissions and connection troubleshooting.
- [Native tests](native-tests/README.md) and [third-party dependencies](THIRD_PARTY.md).
