# Native tests

Requires macOS, Xcode Command Line Tools, CMake 3.24+, and Python 3.9+. Run from the repository root:

```sh
native-tests/run.sh
SANITIZER=undefined native-tests/run.sh
```

The wrapper configures CMake, builds, and runs CTest. Build caches stay under `.build/`; use `ARGUS_BUILD_DIR` and `ARGUS_BUILD_JOBS` to override the location and parallelism. The equivalent preset workflow is:

```sh
cmake --preset debug
cmake --build --preset debug --parallel 4
ctest --preset debug
```

Replace `debug` with `ubsan` for Undefined Behavior Sanitizer. For an Xcode build, run `ctest --test-dir <build-directory> -C Debug --output-on-failure`.

## Coverage

| CTest group | Checks |
| --- | --- |
| NativeTransport | Split/concatenated frames, bounds, EOF, short writes, backpressure, and broken pipes |
| CpuUsage | Real OS-counter calibration, monotonic intervals, PID reuse, counter resets, and failure recovery |
| TabSnapshots | Browser/profile isolation, shared renderers, atomic parts, replay rejection, and expiry |
| ConcurrentMonitor | Concurrent message writes, snapshot reads, sampling, and worker lifecycle |
| HelperOffline | Helper exit while the service is unavailable and browser stdin remains open |
| NativeIntegration | Real subprocess communication, identity stamping, socket permissions, service restarts, and bounded shutdown |
| BrowserInstances | Actual process trees with colliding renderer/tab IDs, multiple profiles, and mapping cleanup |
| NativeUI | PTY rendering, table switching, helper communication, and orderly Ctrl+C shutdown |

The suite uses the production transport, parser, sampler, and UI with source-built dependencies. Temporary runtime directories are private and cleaned after each test; browser host registrations and the default socket directory are untouched. The named browser fixtures simulate process identities without opening real browser profiles.

## Sanitizers

```sh
SANITIZER=thread native-tests/run.sh
SANITIZER=address,undefined native-tests/run.sh
```

Sanitizer flags apply to Argus and FTXUI. On the development machine with Xcode 26.0.1, TSan crashed and ASan stalled during runtime initialization before `main`, including in a minimal program. Those runs remain unverified; Debug and UBSan runs passed. Use a compatible toolchain to validate ASan/TSan.
