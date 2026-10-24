# Shared native transport

Header-only transport primitives shared by the macOS Helper and Service; both Xcode targets include the same files.

- `Framing.hpp`: bounded incremental native-endian uint32 framing and a nonblocking writer that retains short-write suffixes. A message must contain 1…1,048,576 bytes; a writer holds at most 4 MiB plus framing headers. Stream EOF is clean only at a frame boundary.
- `LocalSocket.hpp`: move-only descriptor ownership, nonblocking / close-on-exec configuration, SIGPIPE suppression, same-uid peer verification and validation of a private runtime directory.

The production endpoint defaults to `/tmp/argus-<effective uid>/channel.sock`. Both processes can inherit `ARGUS_RUNTIME_DIR` for an isolated private directory. The Service owns an exclusive flock for its entire lifetime; this prevents concurrent Argus instances from unlinking each other's endpoint. Lock files are intentionally retained across clean shutdowns to keep the locked inode stable.

See [`native-tests`](../native-tests/README.md) for byte-boundary tests, real subprocess integration and UI / concurrency checks.
