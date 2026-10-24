# 扩展 → Native Service JSON 协议

每条消息保持原有字段，可由当前 `BrowserMonitor::receiveMessage` / `decodeMessage` 读取。

```json
{
  "browser": "chrome",
  "OS": "macOS",
  "type": "tab",
  "isSubframe": false,
  "url": "https://example.com/",
  "rendererClientId": 42,
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
```

`rendererClientId` 是 `webNavigation.getAllFrames()` 返回的 `processId`，不是操作系统 PID；原生端通过进程启动参数中的 `--renderer-client-id` 关联 PID。`browser` 为 `chrome` / `edge`，无法识别的 Chromium 品牌为 `unknown`，当前原生端不处理 `unknown`。

只有当前顶层 documentId 匹配指标所属文档时才发送 `withMetric: true` 及指标字段。没有有效指标的顶层 frame、独立 renderer 的 iframe 仅发送身份字段。每轮每个 renderer 只发送一条，主 frame 优先；共享 renderer 的多个标签页或 iframe 使用一个代表 URL，不做资源或指标相加。

| 字段 | 口径 |
| --- | --- |
| `requestCount` | 当前顶层文档及其已识别子 frame 内观察到并完成的 HTTP(S) 请求数；子 frame 通过 documentId / parentDocumentId 归属当前导航 |
| `httpStatusCount*xx` | 已完成请求的最终状态码分组；重定向链只记录最终完成一次，失败 / 取消不计入 |
| `webRequestDurationMs` | 顶层主请求从 onBeforeRequest 到 onCompleted 的时长，重定向沿用最初起点 |
| `pageLoadTotalDurationMs` | Navigation Timing 的 loadEventEnd − startTime |
| `pageLoadNetworkDurationMs` | responseEnd − (redirectStart 或 fetchStart)；跨源重定向可能受浏览器 Timing 限制 |
| `pageLoadRenderDurationMs` | loadEventEnd − responseEnd，表示主响应结束到 load 完成的时段，包含脚本、子资源等等待，不等同于纯渲染 CPU 时长 |

时长单位为毫秒，取非负整数，0 可以表示尚未获取到计时或实际时长不足 1ms。SPA 的同文档 URL 更新保留当前加载会话，完整导航建立新会话；BFCache 恢复后使用原文档的加载计时。暂停会清空会话，恢复后的已有网页需刷新才能重新获得加载计时。HTTP 统计只覆盖扩展实际观察到的事件，worker 突发终止或扩展重载可能导致正在进行的请求不计入。

浏览器的 Site Isolation 意味着一个标签页可对应多个进程，一个进程也可被多个标签页共享。因此 CPU / Memory 仍是**进程级**读数，不能解释成独立标签页的精确资源占用。后续若要保留全部 URL / tab / frame 对应关系，需要同时升级原生端协议及展示层。
