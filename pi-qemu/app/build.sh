#!/usr/bin/env bash
# Build pi-qemu (Qt 6, C++20). Needs: brew install qt cmake ninja mtools dtc
#
# The -nostdinc++ / -isystem pair works around Command Line Tools installs that
# carry a stale /Library/Developer/CommandLineTools/usr/include/c++/v1 (left by
# an old release, almost empty): clang searches it before the SDK's own libc++
# headers and then cannot find <utility>. Naming the SDK's copy is harmless on a
# clean install.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# The SDK of the selected toolchain (xcode-select). A bare --show-sdk-path can
# name the Command Line Tools' SDK even with Xcode selected, and once that is
# newer than Xcode's linker (CLT 27's against Xcode 26.4's) linking fails with
# "tapi error: malformed file ... unknown architecture".
SDK="$(xcrun --sdk macosx --show-sdk-path)"
cmake -S "$HERE" -B "$HERE/build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt -DCMAKE_OSX_SYSROOT="$SDK" \
    -DCMAKE_CXX_FLAGS="-nostdinc++ -isystem $SDK/usr/include/c++/v1" >/dev/null
ninja -C "$HERE/build"
echo "==> $HERE/build/pi-qemu"
