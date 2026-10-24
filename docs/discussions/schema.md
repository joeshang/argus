# Native Messaging 协议 v2

扩展 2.1.0+、Helper 与 Service 需要一起更新。v1 的 renderer 单值映射无法区分浏览器实例，新 Service 不再使用 v1 消息建立映射。

```json
{
  "type": "snapshot",
  "protocolVersion": 2,
  "browser": "chrome",
  "OS": "macOS",
  "sourceId": "浏览器 profile 会话的随机 UUID",
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
      "documentId": "当前文档 UUID",
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

## 身份与归属

`rendererClientId` 来自 getAllFrames 的 processId，不是 OS PID。原生端以 **browserPid + browserStartTime + rendererClientId** 关联渲染进程；根 PID 和启动 ticks 区分同时运行的 Chrome / Edge 实例及 PID 复用。

扩展不提供可信的 OS 进程身份。Helper 从自己的浏览器父进程读取 PID 和 ri_proc_start_abstime，并覆盖消息里的 browserPid / browserStartTime；启动 ticks 用十进制字符串传输，避免 JavaScript 整数精度损失。Service 根据实际进程树和根进程启动时间匹配；不会把另一个浏览器实例的相同 renderer ID 关联过来。

sourceId 存储在 storage.session，同一 profile 的 worker 重启仍保留，浏览器新会话重新生成。一个根浏览器进程可以拥有多个 profile，各自的 tabId / frameId 可以重复。每个 profile 保留完整的 tab / frame / document / URL 列表，共享 renderer 的记录不再去重丢失。

## 快照与清理

每轮全量采集生成递增 generation；分块 partIndex 从 0 开始，partCount 表示总块数。Native Messaging 本身仍使用 4 字节本机字节序长度头。扩展以约 512 KiB 分块，完整快照限制为 3 MiB / 10,000 个 frame，单个 URL 上限 64 KiB；超限会在弹窗显示采集错误，不静默丢弃剩余记录。

Service 仅在所有块按顺序收到且校验通过后替换整份快照。坏消息、重复 frame、乱序块及旧 generation 不覆盖上一份完整数据。未完成的分块 5 秒过期，已提交快照 15 秒未更新则过期。连接断开立即清理该连接数据；采样时清理已退出的浏览器根实例。新连接替代同一 profile 的旧连接，迟到的旧连接不能覆盖新数据。

`frames: []` 的完整快照用于清除最后一个标签页关闭后的旧映射。浏览器启停、tab / frame 关闭、profile 重连和根 PID 复用都不会无限保留旧 URL。

## 指标口径

只有顶层 frame 的当前 documentId 与采集会话相符时才带 withMetric 和指标。子 frame 仍保留身份和 URL，不把其资源读数重复累加到多个标签页。

| 字段 | 口径 |
| --- | --- |
| requestCount | 当前顶层文档及其已识别子 frame 内观察到并完成的 HTTP(S) 请求数 |
| httpStatusCount*xx | 已完成请求的最终状态码；重定向链按最终完成一次计数，失败 / 取消不计入 |
| webRequestDurationMs | 顶层主请求从 onBeforeRequest 到 onCompleted，重定向沿用最初起点 |
| pageLoadTotalDurationMs | Navigation Timing 的 loadEventEnd − startTime |
| pageLoadNetworkDurationMs | responseEnd − (redirectStart 或 fetchStart)，受浏览器跨源 Timing 限制 |
| pageLoadRenderDurationMs | loadEventEnd − responseEnd，包含脚本、子资源等待，不等同于纯渲染 CPU 时长 |

时长为非负整数毫秒，0 可表示尚未获取到计时或不足 1ms。SPA 同文档更新保留加载会话，完整导航建立新会话；BFCache 使用原文档加载计时。暂停清空会话，恢复后已有网页需刷新才能重新获得加载计时。突发终止可能丢失检查点之后的请求计数。

CPU / Memory 始终是**进程级**读数。一个 renderer 的多个 URL 一起展示，页面指标按浏览器 / profile / tab 分行展示；无法把共享进程资源精确分摊到每个 tab。
