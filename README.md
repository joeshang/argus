# Argus

在浏览器外监控 Chrome、Edge、Safari 的进程 CPU / Memory，并关联网页 URL 与页面加载数据。本项目实现了[技术调研文章](https://joeshang.github.io/investigation-of-getting-browser-tab-performance/)中的方案。

## 架构

```text
Chromium Extension (Manifest V3)
  tabs + webNavigation: URL / rendererClientId
  webRequest + Navigation Timing: 页面指标
          │ Native Messaging (长度前缀 + JSON)
          ▼
ArgusMacExtensionHelper
          │ Unix Domain Socket: /tmp/argus-<uid>/channel.sock
          ▼
ArgusMacServices
  renderer-client-id → macOS pid → CPU / Memory → 终端界面
  Safari: WebKit / LaunchServices 私有接口 → URL
```

- [`argus-chromium-extension`](argus-chromium-extension/README.md)：Chrome / Edge 扩展、安装脚本和测试。
- [`argus-extension-helper`](argus-extension-helper/ArgusMacExtensionHelper)：由浏览器启动的消息转发进程。
- [`argus-native-service`](argus-native-service/README.md)：独立运行的 macOS 进程监控服务。
- [`docs/discussions/schema.md`](docs/discussions/schema.md)：扩展发往原生端的协议及指标口径。
- [`docs/optimization-review.md`](docs/optimization-review.md)：此次迁移的验证结果与后续优化建议。

CPU / Memory 的采样单位是进程。同一 renderer 可被多个标签页共享，一个标签页也可包含多个 renderer；协议 v2 保留每个 renderer 的完整 tab / frame / profile / URL 列表，但仍不能把共享进程资源精确分摊给每个标签页。

## macOS 快速运行

需要 Xcode；安装脚本和测试需要 Node.js 22+，无需安装 npm 依赖。

在项目根目录编译两个原生程序：

```sh
xcodebuild -project argus-extension-helper/ArgusMacExtensionHelper/ArgusMacExtensionHelper.xcodeproj \
  -scheme ArgusMacExtensionHelper -configuration Debug \
  -derivedDataPath /tmp/argus-helper build CODE_SIGNING_ALLOWED=NO

xcodebuild -project argus-native-service/ArgusMacServices/ArgusMacServices.xcodeproj \
  -scheme ArgusMacServices -configuration Debug \
  -derivedDataPath /tmp/argus-service build CODE_SIGNING_ALLOWED=NO
```

为当前用户注册 Native Messaging Host（Edge 将最后一个参数改为 `edge`）：

```sh
node argus-chromium-extension/scripts/install-host.mjs \
  /tmp/argus-helper/Build/Products/Debug/ArgusMacExtensionHelper chrome
```

注册文件必须指向 **ArgusMacExtensionHelper**，不是 ArgusMacServices。安装脚本会将正确的绝对路径写入浏览器的 NativeMessagingHosts 目录；正式使用时应把 Helper 放到不会被构建清理删除的位置后再注册。

启动服务：

```sh
/tmp/argus-service/Build/Products/Debug/ArgusMacServices
```

在 Chrome 的 `chrome://extensions`（Edge 的 `edge://extensions`）启用开发者模式，选择「加载已解压的扩展程序」，加载 `argus-chromium-extension/extension` 目录。安装后刷新要采集的网页。扩展自动在后台运行，点击工具栏图标可查看连接状态、暂停或恢复采集。

服务未启动或重启时，Helper 会退出，由扩展自动重连。Helper 和 Service 必须由同一用户运行，不需要 sudo。P1 修复将 socket 从旧的 `/tmp/com.argus.channel` 迁到了当前用户的私有目录 `/tmp/argus-<uid>/channel.sock`，**需要同时重新编译、更新 Helper 和 Service**；原生 JSON 格式与扩展 ID 保持兼容。仓库目前只提供 macOS 原生实现；Windows / Linux 需要对应的原生 Host 与 Service。

原生端回归测试（含真实进程通信、并发采样及终端 UI）运行 `native-tests/run.sh`，详情见[测试说明](native-tests/README.md)。剩余优化见[检查报告](docs/optimization-review.md)。
