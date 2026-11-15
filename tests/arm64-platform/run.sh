#!/usr/bin/env bash
set -euo pipefail
source_root=$(cd "$(dirname "$0")/../.." && pwd)
output=${ARM64_TEST_OUTPUT:-"$source_root/build/arm64-platform"}
mkdir -p "$output"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    "$source_root/tests/arm64-platform/platform.cpp" -o "$output/platform-test"
if [[ $(uname -s) == Darwin ]]; then
    codesign --force --sign - --options runtime \
        --entitlements "$source_root/tests/arm64-platform/entitlements.plist" \
        "$output/platform-test"
    codesign --verify --strict --verbose=2 "$output/platform-test"
fi
"$output/platform-test" "$@" 2>&1 | tee "$output/result.txt"
