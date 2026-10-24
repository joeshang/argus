# Native regression tests

macOS + Xcode Command Line Tools + Python 3。测试在 `/tmp` 创建独立的 0700 目录和临时二进制，结束后清理；不修改用户 Chrome / Edge 的 Host 注册，不触碰默认运行目录。

```sh
native-tests/run.sh
SANITIZER=undefined native-tests/run.sh
```

编译并运行实际的 Framing、Helper、SocketServer、BrowserMonitor 和终端界面代码，使用仓库中的 FTXUI 静态库与 macOS 进程 API，不替换消息消费者或进程采样实现。

- `transport.cpp`：每个字节位置的拆包、连续多帧、最大长度、非法长度、EOF、真实 socket 背压 / 短写和 EPIPE。
- `helper-offline.py`：服务离线且浏览器 stdin 仍保持打开时，Helper 必须退出；旧实现会超时，此项在修复前已复现失败。
- `integration.py`：实际进程间的拆包 / 粘包、非法客户端隔离、JSON schema 容错、权限 / 符号链接、重复服务、stale socket、连接清理、路径替换保护、服务重启、浏览器退出、完整大消息 EOF 排空和慢消费者的有界退出。
- `concurrency.cpp`：4 个线程写入 40,000 条消息，同时读取快照与实际采样；回调重入 snapshot、畸形 schema 及反复启动 / 析构采样线程。
- `ui-smoke.py`：PTY 中运行真实 Native Service，4 个客户端并发上报、切换性能 / 页面指标表格，实际 Helper 经 Native Messaging stdio 接收 greeting 并上报，服务退出后 Helper 传播断线。验证正常退出清理 socket。

测试回调仅用 stdout 记录消息，便于核对；生产 Helper 的 stdout 始终只包含 Native Messaging 帧，诊断输出使用 stderr。

## Sanitizer 边界

也支持：

```sh
SANITIZER=thread native-tests/run.sh
SANITIZER=address,undefined native-tests/run.sh
```

当前机器上的 Xcode 26.0.1 运行时不能完成这两项：TSan 在进入 main 前的 `__tsan::SlotLock → wrap_dispatch_once → get_dyld_hdr` 初始化链中 SIGSEGV；ASan 在 dyld / malloc 初始化时递归进入 `AsanInitInternal` 并自旋。最小程序也能复现：

```sh
printf '#include <cstdio>\nint main() { puts("entered main"); }\n' > /tmp/argus-sanitizer-probe.cpp
xcrun clang++ -fsanitize=thread /tmp/argus-sanitizer-probe.cpp -o /tmp/argus-sanitizer-probe
/tmp/argus-sanitizer-probe
```

将 thread 替换为 address 可复现 ASan 初始化问题。因此本机这两项不计为通过；应在兼容的 macOS / Xcode 组合上执行。UBSan 可单独运行。FTXUI 是已有预编译库，Sanitizer 无法覆盖其未插桩的内部实现；本项目 C++ 代码会按所选选项编译。
