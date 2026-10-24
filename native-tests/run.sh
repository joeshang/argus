#!/bin/bash
set -euo pipefail
native_root=$(cd "$(dirname "$0")/.." && pwd)
native_build=$(mktemp -d /tmp/argus-native-tests.XXXXXX)
trap 'rm -rf "$native_build"' EXIT
cd "$native_root"
service=argus-native-service/ArgusMacServices/ArgusMacServices
helper=argus-extension-helper/ArgusMacExtensionHelper/ArgusMacExtensionHelper
flags=(-std=c++20 -g -O1 -pthread -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -I"$service/library/include")
if [[ "${SANITIZER:-}" != "" ]]; then flags+=(-fsanitize="$SANITIZER" -fno-omit-frame-pointer); fi
xcrun clang++ "${flags[@]}" native-tests/transport.cpp -o "$native_build/transport"
xcrun clang++ "${flags[@]}" -I"$helper/library/include" "$helper/main.cpp" -o "$native_build/helper"
# Build actual platform implementations; no fake process/URL providers.
xcrun clang++ "${flags[@]}" -c "$service/BrowserMonitor.cpp" -o "$native_build/monitor.o"
xcrun clang++ "${flags[@]}" -c "$service/ProcessUtils.mm" -o "$native_build/process.o"
xcrun clang -g -O1 -fobjc-arc -c "$service/WebKitUtils.m" -o "$native_build/webkit.o"
xcrun clang++ "${flags[@]}" -c "$service/SocketServer.cpp" -o "$native_build/socket.o"
xcrun clang++ "${flags[@]}" native-tests/server-fixture.cpp "$native_build/socket.o" \
  "$native_build/monitor.o" "$native_build/process.o" "$native_build/webkit.o" \
  -framework Foundation -framework CoreServices -o "$native_build/server"
xcrun clang++ "${flags[@]}" native-tests/concurrency.cpp \
  "$native_build/monitor.o" "$native_build/process.o" "$native_build/webkit.o" \
  -framework Foundation -framework CoreServices -o "$native_build/concurrency"
xcrun clang++ "${flags[@]}" "$service/main.cpp" "$service/TerminalDisplayer.cpp" \
  "$native_build/socket.o" "$native_build/monitor.o" "$native_build/process.o" "$native_build/webkit.o" \
  -L"$service/library/lib" -lftxui-component -lftxui-dom -lftxui-screen \
  -framework Foundation -framework CoreServices -o "$native_build/ui"
"$native_build/transport"
python3 native-tests/helper-offline.py "$native_build/helper"
python3 native-tests/integration.py "$native_build/server" "$native_build/helper"
"$native_build/concurrency"
python3 native-tests/ui-smoke.py "$native_build/ui" "$native_build/helper"
