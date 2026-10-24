# Argus 迁移与优化检查

检查日期：2026-10-25。已完成 Chromium 扩展的 Manifest V3 重写，以及本报告四类原生端 P1 修复。P2 / P3 仍为后续工作。

## 已完成

- 后台从持久页面迁移到 module Service Worker；同步注册事件，不再自动打开页面或依赖 popup 生命周期。
- 删除 `webRequestBlocking`、`scripting` 和 requestBody 读取；保留只读 webRequest 与必要的 HTTP(S) host permissions。
- content script 每个顶层文档静态注入一次，以 Navigation Timing 代替已废弃的 `performance.timing`；处理 BFCache 和扩展上下文失效。
- documentId 隔离导航和计时，清理失败、关闭标签页和超时请求，限制在途请求数量。
- Native Port 单连接、退避重连、alarm 恢复、暂停设置持久化、会话检查点恢复。
- frame 顺序无关、无效 / prerender 过滤、同 renderer 去重、主 frame 优先；帧查询并发上限为 8，并防止采集轮次重叠。
- 保留现有 manifest key、扩展 ID 和原生 JSON 字段；增加可生成正确绝对路径的 macOS Host 注册脚本，修正旧模板误指向 Native Service 的问题。
- `SocketServer.hpp` / `BrowserMonitor.hpp` 显式包含 `<functional>`，修复当前 Xcode 编译失败。

## P1 修复结果

### 已修复：消息流拆包与长度校验

共用 `native-common/Framing.hpp` 的增量解码器和非阻塞发送缓冲区。4 字节长度头和 payload 均支持任意拆分、连续多帧；头部通过 memcpy 读取，拒绝 0 或超过 1 MiB 的长度，待发送数据上限 4 MiB。处理 EINTR、EAGAIN 和短写，EOF 可区分完整消息与截断消息，不使用按外部长度分配的栈数组。

Service 只关闭协议错误的客户端，不退出整个进程。`BrowserMonitor::receiveMessage` 同时校验 JSON 类型、必填字段和数值范围，避免类型转换异常终止服务。Helper 保留既有的 Service 文本 → JSON 字符串响应方式。

### 已修复：监控状态与 UI 并发访问

`BrowserMonitor` 使用一把互斥锁保护消息写入、采样、进程列表和整份快照读取。采样线程在锁外交付独立的 `BrowserSnapshot`，避免调用 UI 时持有状态锁。

`TerminalDisplayer::showMetrics` 通过 FTXUI 的任务队列把快照交给 UI 线程，table map 的更新和渲染均在 UI 线程执行。采样线程可停止、可 join；主程序保证 SocketServer、采样线程退出后才销毁 monitor / screen。新采样线程访问 Cocoa 时使用显式 autorelease pool。

### 已修复：Helper 向浏览器传播服务故障

Helper 在初始连接失败、Service EOF / socket 错误、非法帧或 stdout 写入失败时退出，让浏览器关闭 Native Port 并触发扩展已有的重连机制。SIGPIPE 转为普通 I/O 错误，不再让异常 socket / pipe 写入直接杀死程序。

浏览器 stdin 正常 EOF 时，Helper 最多用 2 秒发送已完整接收的剩余消息；慢消费者不会让 Helper 无限存活。半帧 EOF 作为错误处理。已验证服务未启动、启动后重新连接、服务退出与重启、浏览器关闭 stdin / stdout 等路径。

### 已修复：Unix Socket 生命周期与访问范围

默认 endpoint 为 `/tmp/argus-<uid>/channel.sock`，父目录要求当前用户所有、权限严格为 0700，socket 权限为 0600。拒绝符号链接和不可信已有路径；两端通过 getpeereid 验证对方是同一用户。生产代码不再修改进程全局 umask。

Service 持有独占 flock，防止第二个实例删除活跃实例的 socket；崩溃留下的自有 stale socket 在获得锁后清理。使用专用 poll 线程和 RAII 管理所有 fd，accept 正确处理 EINTR / EAGAIN / ECONNABORTED，并限制连接数为 64。停止时 join I/O 线程，再关闭客户端与监听器；仅清理与自身绑定 inode 匹配的 socket，不删除后来替换到同一路径的其他文件。

**升级要求：Helper 与 Service 必须同时重新编译、更新并由同一用户运行。** 扩展 ID、Host 名称和 JSON 字段不变；旧的全局 socket 路径不再使用。`ARGUS_RUNTIME_DIR` 可为两端指定同一私有测试目录。

## 后续建议

### 已修复 P2：规范采样失败与 CPU 差分

位置：`BrowserMonitor.cpp` 的 queryBrowserProcesses、getProcessUsage。

新增共用 `ProcessUsageTracker`，按每个 PID 的真实 steady_clock 采样间隔计算 CPU，进程启动 ticks 改变、计数倒退、读取失败和零时间间隔均重建基线；清理不再监测的 PID。首次采样 / 读取失败在 UI 显示不可用，不伪装为 0% 或 0 MB，多核读数可超过 100%。

本机真实校准确认 rusage 的 CPU 值来自 Mach ticks，需按 timebase 换算；保留转换并用 128 位中间值避免乘法溢出。与独立 getrusage 的 100ms CPU 工作负载校准比例为 1.00014，已加入漂移、PID 复用、失败恢复及边界回归。


### P2：协议增加浏览器实例和一对多映射

位置：`BrowserMonitor.cpp` 的 receiveMessage / queryBrowserProcesses；`BrowserMonitor.hpp` 的 m_chromeTabInfos / m_edgeTabInfos。

当前每种浏览器只记一个根 PID，每个 rendererClientId 只记一个 WebMetricData。不同 user-data-dir 启动的独立浏览器实例可复用 renderer ID；共享 renderer 的多个 tab 会互相覆盖，历史 URL map 也没有过期策略。

建议以浏览器实例 / 根 PID 与 renderer ID 做复合键，保留 tabId、frameId、documentId 和 URL 列表，增加快照代次或关闭事件来清理失效映射；明确 UI 展示的是进程资源而非精确 tab 分摊。此项需扩展、Helper 和原生消费者共同升级，故此次保留原协议。

### P2：让构建依赖可复现

仓库提交了预编译 FTXUI 静态库，当前 Service target 最低版本为 macOS 12.3，而这些库至少部分对象以 12.6 构建，链接产生版本警告。建议通过固定版本的构建配置重新构建依赖，统一最低系统版本，记录架构与许可证；加上 CI 的 Node 测试及 macOS 原生编译检查。

### P3：进一步降低监控本身的开销

目前扩展保留 2 秒快照以兼容原生端。大规模标签页场景可进一步改成导航 / tab 事件驱动的 frame 缓存加低频全量校准，仅发送变化；需要额外处理子 frame 销毁、prerender 激活与 Service 重连后的全量同步。原生进程扫描可缓存成功解析的进程启动参数，并以 PID + 启动时间失效，避免每次采样重复 sysctl 与解析。

## 验证与边界

- `npm test`：manifest / 身份、请求聚合、导航竞态、消息校验、计时、重连、清理与持久化测试通过。
- Chrome for Testing 145.0.7632.6（macOS arm64，独立临时 profile）：MV3 worker 真实加载，弹窗关闭后正常采集，页面计时上报、真实 frame processId 与 renderer-client-id 对应、暂停 / 恢复及 worker 终止恢复通过。
- Xcode 26.0.1：Helper 与 Native Service 的 Debug 编译通过；Service 保留上述已有的 FTXUI 部署版本警告。
- `native-tests/run.sh`：实际 SocketServer / Helper / BrowserMonitor 的拆包、边界、生命周期、权限、40,000 条并发消息与快照读取，以及真实终端界面通信 / 切换 / 退出均有回归测试。测试使用私有临时目录和模拟浏览器 stdio，不改写用户 Native Host 注册。
- `native-tests/run.sh` 与 `SANITIZER=undefined native-tests/run.sh` 本次均通过，包含 11 项真实进程集成测试、并发与真实 UI 检查；ASan / TSan 在当前 Xcode 26.0.1 与本机 dyld 初始化阶段分别卡住 / 崩溃，最小 hello-world 同样复现，不能计为通过。具体复现见[原生测试说明](../native-tests/README.md)。
- 未在 Edge / Safari 实机验证，也未把测试 Host 注册到真实 Chrome 验证整条浏览器原生链路。扩展安装 / 重载后需刷新网页。

Chrome 规范参考：[MV2 时间表](https://developer.chrome.com/docs/extensions/develop/migrate/mv2-deprecation-timeline)、[Service Worker 迁移](https://developer.chrome.com/docs/extensions/develop/migrate/to-service-workers)、[生命周期与 Native Port](https://developer.chrome.com/docs/extensions/develop/concepts/service-workers/lifecycle)、[真实浏览器测试](https://developer.chrome.com/docs/extensions/how-to/test/end-to-end-testing)。当前 `getAllFrames` 仍返回 renderer processId，见 [Chromium 实现](https://github.com/chromium/chromium/blob/main/chrome/browser/extensions/api/web_navigation/web_navigation_api.cc)。
