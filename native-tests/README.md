# Native regression tests

需要 macOS、Xcode Command Line Tools、CMake 3.24+ 和 Python 3.9+。测试在 `/tmp` 创建独立的 0700 运行目录并清理，不修改用户 Host 注册或默认运行目录。CMake 构建缓存位于 `.build/`，重跑复用依赖和产物。

```sh
native-tests/run.sh
SANITIZER=undefined native-tests/run.sh
```

脚本调用 CMake / CTest，等价于：

```sh
cmake --preset debug
cmake --build --preset debug --parallel 4
ctest --preset debug
cmake --preset ubsan
cmake --build --preset ubsan --parallel 4
ctest --preset ubsan
```

通过 `ARGUS_BUILD_DIR` / `ARGUS_BUILD_JOBS` 指定脚本的构建目录和并发数。Xcode 多配置构建应使用 `ctest --test-dir <目录> -C Debug`。

8 个 CTest 测试组覆盖实际 Framing、Helper、SocketServer、BrowserMonitor 和终端界面代码，FTXUI 也从固定源码构建；不替换消息消费者或进程采样实现。NativeIntegration 组包含 12 个真实子进程用例。

- `transport.cpp`：每个字节位置的拆包、连续多帧、最大长度、非法长度、EOF、真实 socket 背压 / 短写和 EPIPE。
- `cpu-usage.cpp`：真实 getrusage 校准、单调时钟间隔、PID 复用、计数倒退、失败恢复和整数换算边界。
- `tab-snapshots.cpp`：多实例 / profile、共享 renderer、原子分块、乱序与重放、过期和 PID 复用。
- `browser-instances.py` / `browser-fixture.cpp`：真实的两个浏览器父子进程树和同根双 profile，故意碰撞 renderer / tab ID，经过真实 Helper 验证映射与清理。
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

将 thread 替换为 address 可复现 ASan 初始化问题。因此本机这两项不计为通过；应在兼容的 macOS / Xcode 组合上执行。UBSan 可单独运行。CMake 的 Sanitizer 标志同时应用于 Argus 与源码构建的 FTXUI；本次 Debug / UBSan 的 8 个 CTest 组均通过。
