# Native service

The macOS process monitor and terminal UI. It associates Chrome/Edge renderer processes with page snapshots and reads Safari origin/display-name metadata through native APIs. See [how it works](../docs/how-it-works.md) and the [project quick start](../README.md#quick-start).

## Build and install

Run from the repository root. CMake presets provide `debug`, `release`, and `ubsan` configurations; dependencies are pinned in the [dependency manifest](../THIRD_PARTY.md).

For a stable installation path:

```sh
cmake --preset release
cmake --build --preset release --parallel 4
cmake --install .build/release --prefix "$HOME/.local"
node argus-chromium-extension/scripts/install-host.mjs \
  "$HOME/.local/bin/ArgusMacExtensionHelper" chrome
"$HOME/.local/bin/ArgusMacServices"
```

Use `edge` for Edge host registration. Safari-only monitoring requires just the service. The browser starts the helper automatically for Chromium collection.

To generate an Xcode project:

```sh
cmake -S . -B .build/xcode -G Xcode -DBUILD_TESTING=ON \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO
cmake --build .build/xcode --config Debug --parallel 4
ctest --test-dir .build/xcode -C Debug --output-on-failure
```

Open `.build/xcode/Argus.xcodeproj` in Xcode. CMake is the source of truth for generated projects.

## Runtime

Run the helper and service as the same user. The default socket is `/tmp/argus-<uid>/channel.sock`, inside a user-owned directory with mode 0700; the socket uses mode 0600. Peer-user validation and an exclusive `service.lock` protect the endpoint. Normal shutdown removes the service's socket; startup can recover a stale socket after a crash.

`ARGUS_RUNTIME_DIR` selects a different absolute, private directory. Both processes must inherit the same value. This is mainly useful for isolated tests.

The socket I/O thread and sampler share monitor state under a mutex. The sampler delivers owned snapshots outside the lock; the UI updates its tables through the FTXUI task queue. Shutdown joins the producers before destroying the monitor and screen.

Update the extension, helper, and service together. See the [protocol reference](../docs/discussions/schema.md) for snapshot ownership, framing, limits, and cleanup, and [native tests](../native-tests/README.md) for validation commands.
