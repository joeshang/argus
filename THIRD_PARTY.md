# Native dependencies

Native builds use CMake FetchContent. Both dependencies are built/consumed from immutable source archives with SHA-256 verification in [`cmake/Dependencies.cmake`](cmake/Dependencies.cmake); no precompiled libraries or duplicated third-party headers are stored in this repository.

| Dependency | Version | Commit | License |
| --- | --- | --- | --- |
| [FTXUI](https://github.com/ArthurSonzogni/FTXUI) | 7.0.3 | `f921fad208912747c17d129a8ef75ec7624b6eec` | MIT |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | `55f93686c01528224f448c19128836e7df245f72` | MIT |

FTXUI archive SHA-256: `00e1426f5b13d7748af44925768cca3bc4f64aa3b02e4c82567fd85716911a5b`.

nlohmann/json archive SHA-256: `67f4cdd9ca930c9c1e130af4a437c7fc98fab77a2846fc2d2a14b4943831f8ef`.

CMake compiles FTXUI for the same compiler, architecture and deployment target as Argus (macOS 12.3 by default). Override with `CMAKE_OSX_ARCHITECTURES` or `CMAKE_OSX_DEPLOYMENT_TARGET` when configuring; this applies to all dependency objects as well. FTXUI is linked statically. `cmake --install` installs both executables and dependency license files under `share/licenses/argus/`.

Initial configuration requires HTTPS access to the two archives. Normal reconfiguration/rebuilds reuse CMake's populated dependency sources. For offline builds, supply reviewed local source trees with `FETCHCONTENT_SOURCE_DIR_FTXUI` and `FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON`; callers supplying these overrides are responsible for matching the pinned revisions.

FTXUI's [official CMake guidance](https://github.com/ArthurSonzogni/FTXUI#build-using-cmake) recommends FetchContent. The old 2022 FTXUI binaries/headers, duplicated JSON 3.11.2 headers, and hand-maintained Xcode projects were removed. Generate an Xcode project from CMake instead of editing a generated project or retaining binary libraries in Git.
