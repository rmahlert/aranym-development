#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
build_dir=$(mktemp -d /tmp/aranym-tap-tests.XXXXXX)
cxx=${CXX:-g++}
"$cxx" -std=c++11 -Wall -Wextra -Werror -g -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Itests/win-tap/shim tests/win-tap/tap_test.cpp -o "$build_dir/tap-test"
"$build_dir/tap-test"
for cross_cxx in x86_64-w64-mingw32-g++ i686-w64-mingw32-g++; do
  "$cross_cxx" -std=c++11 -Wall -Wextra -Werror \
    -DTAP_REAL_WINDOWS -Itests/win-tap/shim -c src/Unix/cygwin/ethernet_cygwin.cpp \
    -o "$build_dir/$cross_cxx.o"
done
printf 'Build outputs: %s\n' "$build_dir"
