include(FetchContent)

set(FTXUI_BUILD_DOCS OFF CACHE BOOL "Build dependency documentation")
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "Build dependency examples")
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "Build dependency tests")
set(FTXUI_BUILD_MODULES OFF CACHE BOOL "Build dependency C++ modules")
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "Install dependency targets")
set(JSON_BuildTests OFF CACHE BOOL "Build dependency tests")
set(JSON_Install OFF CACHE BOOL "Install dependency targets")

# FTXUI v7.0.3; immutable commit plus archive hash, rather than a moving branch/tag.
FetchContent_Declare(ftxui
  URL https://codeload.github.com/ArthurSonzogni/FTXUI/tar.gz/f921fad208912747c17d129a8ef75ec7624b6eec
  URL_HASH SHA256=00e1426f5b13d7748af44925768cca3bc4f64aa3b02e4c82567fd85716911a5b
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# nlohmann/json v3.12.0: replace the two independent vendored header copies.
FetchContent_Declare(nlohmann_json
  URL https://codeload.github.com/nlohmann/json/tar.gz/55f93686c01528224f448c19128836e7df245f72
  URL_HASH SHA256=67f4cdd9ca930c9c1e130af4a437c7fc98fab77a2846fc2d2a14b4943831f8ef
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(ftxui nlohmann_json)
