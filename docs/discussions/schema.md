# Native Messaging protocol v2

The extension, helper, and service must be updated together. Protocol v1's renderer-only records do not carry enough identity to establish mappings in the current service.

```json
{
  "type": "snapshot",
  "protocolVersion": 2,
  "browser": "chrome",
  "OS": "macOS",
  "sourceId": "profile-session-uuid",
  "generation": 1,
  "partIndex": 0,
  "partCount": 1,
  "browserPid": 12345,
  "browserStartTime": "123456789012345",
  "frames": [
    {
      "tabId": 12,
      "frameId": 0,
      "parentFrameId": -1,
      "documentId": "document-uuid",
      "rendererClientId": 42,
      "url": "https://example.com/",
      "isSubframe": false,
      "withMetric": true,
      "webRequestDurationMs": 100,
      "pageLoadTotalDurationMs": 300,
      "pageLoadNetworkDurationMs": 150,
      "pageLoadRenderDurationMs": 150,
      "requestCount": 25,
      "httpStatusCount2xx": 25,
      "httpStatusCount3xx": 0,
      "httpStatusCount4xx": 0,
      "httpStatusCount5xx": 0
    }
  ]
}
```

## Identity

`rendererClientId` is the `processId` from `webNavigation.getAllFrames()`, not an OS PID. The service joins **browserPid + browserStartTime + rendererClientId** to renderer processes in the matching browser tree.

The helper overwrites `browserPid` and `browserStartTime` using its browser parent process. Start identity comes from `ri_proc_start_abstime` and travels as a decimal string to preserve precision. It is an opaque identity in Mach ticks, not a Unix timestamp.

`sourceId` identifies an extension/profile session and is retained in `storage.session` across worker restarts. Each profile retains all tab/frame/document/URL records, including records that share a renderer. A new browser session generates a new source identity.

## Snapshots and cleanup

A collection produces a full snapshot with an increasing `generation`. Parts arrive in order, numbered from zero through `partCount - 1`. Each transport frame has a four-byte native-endian length followed by a UTF-8 payload.

The extension targets roughly 512 KiB per part and limits a snapshot to 3 MiB / 10,000 frames, with a 64 KiB URL limit. Exceeding a limit produces a popup error rather than silently dropping records. Native transport rejects empty or over-1-MiB messages and bounds each pending write queue to about 4 MiB. The service accepts at most 64 clients.

The service atomically replaces a profile's records after every part passes validation. Invalid records, duplicate tab/frame keys, old generations, and out-of-order parts leave the last complete snapshot intact. Incomplete assemblies expire after 5 seconds; committed snapshots expire after 15 seconds without a complete update. Disconnects remove the connection's records, and sampling removes exited browser instances. A newer connection supersedes an older connection for the same profile.

A complete snapshot with `frames: []` clears closed pages. Renderer resources remain process-level measurements: all associated URLs are retained, and page metrics are displayed separately by browser/profile/tab.

## Metrics

Only a top-level frame whose `documentId` matches the current metric session carries `withMetric: true`. Child frames retain identity and URL data.

| Field | Meaning |
| --- | --- |
| `requestCount` | Observed, completed HTTP(S) requests belonging to the top document and its identified child frames |
| `httpStatusCount*xx` | Final status-code groups; a redirect chain completes once, while failed/cancelled requests are excluded |
| `webRequestDurationMs` | Top-level request time from onBeforeRequest to onCompleted, including redirects from the original start |
| `pageLoadTotalDurationMs` | Navigation Timing: loadEventEnd − startTime |
| `pageLoadNetworkDurationMs` | responseEnd − (redirectStart or fetchStart), subject to browser timing restrictions |
| `pageLoadRenderDurationMs` | loadEventEnd − responseEnd, including script/subresource waits; not rendering CPU time |

Durations are nonnegative integer milliseconds. Zero can mean unavailable timing or a duration below 1ms. Same-document SPA updates retain the load session; full navigation starts another session. Back/forward-cache restoration reports the original document's load timing. Pausing clears sessions; refresh existing pages after resuming to obtain fresh load timings. Abrupt termination may lose request counts after the last checkpoint.

See [how it works](../how-it-works.md) for the architectural reasoning.
