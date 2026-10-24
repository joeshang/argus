# Chromium Extension

适用于 Chrome / Edge 的 Manifest V3 扩展，最低 Chromium 版本为 120。Chrome 已移除 Manifest V2 支持，见[官方时间表](https://developer.chrome.com/docs/extensions/develop/migrate/mv2-deprecation-timeline)。

## 后台运行方式

- `background.js` 是 ES module Service Worker，事件监听器在模块加载时同步注册。
- 原生 Host 连接成功时，每 2 秒查询标签页的 frame / renderer 映射并发送协议 v2 的分块完整快照。`connectNative()` 的连接会保持 worker 活动，见[Chrome 生命周期说明](https://developer.chrome.com/docs/extensions/develop/concepts/service-workers/lifecycle)。
- 原生端断开后停止 2 秒轮询，以 1、2、4 秒到最多 30 秒的退避重连；30 秒 alarm 提供 worker 休眠后的恢复入口。暂停时关闭连接与采集，保留暂停设置。
- `storage.local` 保存启停设置，`storage.session` 保存当前浏览器会话的已汇总指标。每轮采集与页面计时上报时保存检查点；worker 重启恢复最近检查点。尚未完成的请求不持久化，突发终止可能丢失检查点之后的计数；不提供跨浏览器重启的历史存储。
- 弹窗仅显示状态和控制启停；打开多个弹窗不会创建额外采集器，也不会在安装时自动打开采集页面。

## 数据采集与权限

`webRequest` 仅观察 HTTP(S) 请求，统计成功结束的请求和最终状态码，不读取 requestBody，也不阻塞网络。重定向链按一个请求计数；失败 / 取消的请求释放状态，不计入已完成请求数。导航以 documentId 隔离，旧文档的异步结果不计入新文档。

顶层页面的 `content.js` 静态注入一次，使用 `PerformanceNavigationTiming`，在 load 结束后上报；BFCache 恢复时重新上报原文档计时。没有每个请求重复执行脚本的问题。URL / renderer 映射来自 `tabs` 和 `webNavigation.getAllFrames()`，不需要 debugger 权限。

权限用途：

| 权限 | 用途 |
| --- | --- |
| `nativeMessaging` | 与本地 Argus Helper 通信 |
| `webRequest` | 只读统计请求完成状态和时长 |
| `tabs` | 枚举标签页并读取 URL |
| `webNavigation` | 查询 frame、documentId 和 renderer processId |
| `storage` | 保存启停设置和 worker 恢复检查点 |
| `alarms` | worker 休眠 / 浏览器唤醒后的恢复与重连 |
| HTTP(S) host permissions | 观察网页请求并运行页面计时脚本 |

扩展只通过 Native Messaging 向本地进程发送 URL、进程标识和聚合指标，不包含远程上传逻辑。`chrome://`、扩展页、file URL、已丢弃标签页及未激活的 prerender frame 不采集；Chrome Web Store 等受限页面可能无法注入脚本，因而没有页面计时。安装、更新或重新加载扩展后，需要刷新已有网页才能注入新的 content script；暂停时清空指标，恢复后已有页面需刷新才能重新获得加载计时。

每个快照保留所有 tab / frame / document 与 renderer 的对应关系；共享 renderer 的多个标签页、iframe 和不同 profile 不再互相覆盖。Native Helper 注入浏览器根 PID / 启动身份，Service 原子替换快照并清理断开的连接、关闭的标签页及过期 URL。协议 v2 需要扩展、Helper、Service 同时更新，详见[协议](../docs/discussions/schema.md)。

## 安装与连接排查

完整的编译、注册和加载步骤见[项目 README](../README.md)。保留了 manifest 公钥，扩展 ID 仍为 `knldjmfmopnpolahpmmgbagdohdnhkik`，兼容现有 `allowed_origins`。

```sh
node scripts/install-host.mjs /absolute/path/to/ArgusMacExtensionHelper chrome
node scripts/install-host.mjs /absolute/path/to/ArgusMacExtensionHelper edge
```

macOS 注册位置分别是：

- Chrome：`~/Library/Application Support/Google/Chrome/NativeMessagingHosts/com.argus.extension.helper.json`
- Edge：`~/Library/Application Support/Microsoft Edge/NativeMessagingHosts/com.argus.extension.helper.json`

弹窗显示「等待本地服务连接」时，检查注册文件中的 `path` 是否是存在且可执行的 Helper、扩展 ID 是否匹配，以及 Native Service 是否已启动。可在扩展管理页的 Service Worker 调试窗口查看错误。Helper 在连接失败或 socket 断开后会关闭 stdio，由扩展退避重连。Helper 与 Service 必须同时更新并由同一用户运行；默认 socket 位于 `/tmp/argus-<uid>/channel.sock`，目录权限 0700，socket 权限 0600。

## 验证

在本目录运行，无需安装 npm 依赖：

```sh
npm test
```

覆盖 manifest / 扩展 ID、请求统计、导航竞态、旧文档消息、renderer 去重、失败请求清理、断线重试、启停持久化、worker 恢复及 content script 计时。

真实浏览器 smoke test 使用临时独立 profile 和本地 HTTP fixture，不安装或改写用户的 Native Messaging 注册：

```sh
npm run test:browser -- "/absolute/path/to/Google Chrome for Testing"
```

需要 Node.js 22+ 和支持命令行加载扩展的 Chromium / Chrome for Testing。测试验证 MV3 注册、弹窗控制、弹窗关闭后的后台采集、页面计时、实际 renderer-client-id 映射（macOS）和 worker 终止恢复。普通 Google Chrome 限制 `--load-extension`，手动加载开发者扩展不受此测试脚本限制。该浏览器测试不改动 Native Host 注册。Helper ↔ Native Service 的真实进程通信和终端界面另由 `../native-tests/run.sh` 验证；未覆盖 Edge 与 Safari 的实机行为。
