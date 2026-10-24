# Native Service

macOS 进程监控与终端界面：识别 Chrome / Edge / Safari 子进程，关联 URL、renderer ID、CPU / Memory 和页面指标。完整构建、Host 注册及运行步骤见[项目 README](../README.md)。

## 进程和线程

- SocketServer 专用 poll 线程接收多个浏览器 Helper 的有界消息。
- BrowserMonitor 的消息写入、进程采样和快照读取共用互斥锁；采样回调在锁外交付一份完整快照。
- TerminalDisplayer 仅在 UI 线程更新和渲染表格，其他线程通过 FTXUI Post 传递快照。
- 退出时 join I/O 与采样线程，确保所有生产者结束后才释放 monitor / UI。

## 本地连接

Helper 和 Service 必须以同一用户运行，无需 sudo。默认路径为 `/tmp/argus-<uid>/channel.sock`；父目录由该用户拥有且必须为 0700，socket 为 0600，两端验证 peer uid。服务持有 `service.lock` 的独占锁，拒绝重复实例。正常退出清理自身 socket，崩溃后的自有 stale socket 在下一次启动时清理；锁文件保留以避免替换锁 inode 的竞态。

旧的 `/tmp/com.argus.channel` 不再使用，更新时需要同时重新编译 Helper 和 Service。

`ARGUS_RUNTIME_DIR` 可将两端指定到同一个绝对路径的私有目录，主要用于测试。使用自定义目录时必须确保 Helper 也能继承相同配置。

协议 v2 使用浏览器根 PID / 启动身份 + renderer ID，保留各 profile 的全部 tab / frame 映射；扩展、Helper、Service 需同时更新。完整分块快照原子替换，断开、关闭标签页、根进程退出和超时均清理旧映射。消息沿用 4 字节本机字节序长度 + payload 的 framing，单条上限 1 MiB，待发送队列上限 4 MiB，最大客户端数 64。EOF、错误长度及截断帧只影响对应连接，JSON 类型 / 数值非法的数据被 monitor 拒绝。Service 退出会关闭 Helper 的连接，Helper 随即退出，浏览器扩展自动重连。

回归、压力测试和 Sanitizer 说明见 [`native-tests`](../native-tests/README.md)。CPU 采样口径、多浏览器实例及依赖构建等剩余工作见[优化报告](../docs/optimization-review.md)。
