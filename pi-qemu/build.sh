#!/usr/bin/env bash
# Build pi-qemu, the TriMixxx tool: the patched QEMU the emulated decks run on
# (qemu/), then pi-qemu itself (app/, Qt 6, C++20), with its tests; in the main
# checkout, pi-qemu also goes onto PATH as ~/.local/bin/pi-qemu. Everything
# else is a pi-qemu command: `pi-qemu help`.
#
#   pi-qemu/build.sh          QEMU (only what changed) and pi-qemu
#   pi-qemu/build.sh app      pi-qemu only
#   pi-qemu/build.sh qemu     QEMU only
#
# Needs: brew install qemu qt cmake ninja mtools dtc python@3.12 (qemu pulls
# its build dependencies). QEMU takes about ten minutes the first time on an
# M1, and is pinned, so a rebuild is the same QEMU. A git worktree builds its
# own pi-qemu (app/build/pi-qemu, run by its path) and uses the main checkout's
# QEMU, which is what pi-qemu runs anyway.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
what="${1:-all}"
case "$what" in all|app|qemu) ;; *) sed -n '/^#   pi-qemu\/build.sh/s/^# *//p' "$0" >&2; exit 2 ;; esac
main=0
[ "$(git -C "$HERE" rev-parse --path-format=absolute --git-common-dir)" = "$(git -C "$HERE" rev-parse --path-format=absolute --git-dir)" ] && main=1

# The SDK of the selected toolchain (xcode-select). A bare `cc` or a bare
# --show-sdk-path can take the Command Line Tools' SDK even with Xcode
# selected, and once that is newer than Xcode's linker (CLT 27's against Xcode
# 26.4's) configure and links fail: "tapi error: malformed file ... unknown
# architecture".
SDK="$(xcrun --sdk macosx --show-sdk-path)"
export SDKROOT="$SDK"

# ---- QEMU: upstream, rpi-qemu's raspi4b series, then trimixxx-patches.py ---------
# rpi-qemu's patches: GENET, the PCIe root complex, firmware fix-ups, the
# watchdog, ... Ours: HVF, working PCIe/xHCI, SD DMA, full RAM, the deck's DAC
# and its 44.1 kHz, and every device saved, for snapshots. Plus Raspberry Pi's
# dtmerge, which applies dtoverlay=/dtparam= lines as the firmware does.
build_qemu() {
    local work="$HERE/qemu/.build"
    local QEMU_VERSION=11.1.0
    local QEMU_SHA256=6ee1d1a61f68212476b27108c26da5f449dc09b626d42f8279ba0dc2e08fa858
    local RPI_QEMU_REPO=https://github.com/fpgas-online/rpi-qemu.git
    local RPI_QEMU_COMMIT=0eca340c841430e06ea4e0f749d599bdfcdb6765   # 2026-09-30
    local RPI_UTILS_REPO=https://github.com/raspberrypi/utils.git
    local RPI_UTILS_COMMIT=e0484c848f9c9d1aecc7bd0738930db97bcf18df  # 2026-10-02
    mkdir -p "$work"
    (
        cd "$work"
        if [ ! -f "qemu-$QEMU_VERSION.tar.xz" ]; then
            curl -fL --max-time 900 -o "qemu-$QEMU_VERSION.tar.xz.part" \
                "https://download.qemu.org/qemu-$QEMU_VERSION.tar.xz"
            mv "qemu-$QEMU_VERSION.tar.xz.part" "qemu-$QEMU_VERSION.tar.xz"
        fi
        echo "$QEMU_SHA256  qemu-$QEMU_VERSION.tar.xz" | shasum -a 256 -c - >/dev/null
        [ -d rpi-qemu ] || git clone -q "$RPI_QEMU_REPO" rpi-qemu
        git -C rpi-qemu fetch -q origin "$RPI_QEMU_COMMIT" 2>/dev/null || true
        git -C rpi-qemu checkout -q "$RPI_QEMU_COMMIT"
        local src="qemu-$QEMU_VERSION"
        # The stamp is the patch script's hash: changing a patch re-applies them
        # all to a fresh tree.
        local patches_sha
        patches_sha="$(shasum -a 256 "$HERE/qemu/trimixxx-patches.py" | cut -d' ' -f1)"
        if [ "$(cat "$src/.patched" 2>/dev/null)" != "$patches_sha" ]; then
            rm -rf "$src"
            tar xf "qemu-$QEMU_VERSION.tar.xz"
            (
                cd "$src"
                git init -q && git add -A >/dev/null 2>&1
                git -c user.name=build -c user.email=build@localhost commit -qm base
                for p in ../rpi-qemu/ci/qemu-patches/*.patch; do
                    git -c user.name=build -c user.email=build@localhost am -q --3way "$p"
                done
                python3 "$HERE/qemu/trimixxx-patches.py" .
                echo "$patches_sha" > .patched
            )
        fi
        mkdir -p "$src/build"
        (
            cd "$src/build"
            if [ ! -f build.ninja ]; then
                ../configure --python=/opt/homebrew/bin/python3.12 \
                    --target-list=aarch64-softmmu \
                    --extra-cflags=-I/opt/homebrew/include --extra-ldflags=-L/opt/homebrew/lib \
                    --enable-slirp --enable-cocoa --enable-hvf --enable-fdt=system --enable-vnc \
                    --enable-zstd --disable-fuse --disable-docs --disable-guest-agent \
                    --disable-bsd-user --disable-sdl --disable-gtk --disable-werror
            fi
            nice ninja qemu-system-aarch64
        )
        [ -d rpi-utils ] || git clone -q "$RPI_UTILS_REPO" rpi-utils
        git -C rpi-utils fetch -q origin "$RPI_UTILS_COMMIT" 2>/dev/null || true
        git -C rpi-utils checkout -q "$RPI_UTILS_COMMIT"
        mkdir -p bin
        cc -O2 -I/opt/homebrew/include -o bin/dtmerge \
            rpi-utils/dtmerge/dtmerge.c rpi-utils/dtmerge/dtoverlay.c -L/opt/homebrew/lib -lfdt
        # A symlink, not a copy: the binary carries its hypervisor entitlement.
        ln -sf "../$src/build/qemu-system-aarch64" bin/qemu-system-aarch64
    )
    echo "==> QEMU: $work/bin ($(ls "$work/bin" | tr '\n' ' '))"
}

# ---- pi-qemu: the tool ---------------------------------------------------------------
# The -nostdinc++ / -isystem pair works around Command Line Tools installs that
# carry a stale /Library/Developer/CommandLineTools/usr/include/c++/v1 (left by
# an old release, almost empty): clang searches it before the SDK's own libc++
# headers and then cannot find <utility>. Naming the SDK's copy is harmless on
# a clean install.
build_app() {
    local app="$HERE/app"
    cmake -S "$app" -B "$app/build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt -DCMAKE_OSX_SYSROOT="$SDK" \
        -DCMAKE_CXX_FLAGS="-nostdinc++ -isystem $SDK/usr/include/c++/v1" >/dev/null
    ninja -C "$app/build"
    (cd "$app/build" && ctest --output-on-failure >/dev/null) ||
        { (cd "$app/build" && ctest --output-on-failure | tail -40); echo "pi-qemu's tests failed" >&2; exit 1; }
    # The binary's own icon, as Finder shows it (its windows and Dock tile get
    # icons/trimixxx.ico from the copy compiled in). Set after every build, as a
    # link can write a fresh file without it; cosmetic, so it never fails one.
    osascript -l JavaScript -e 'ObjC.import("AppKit")
    function run(argv) {
        return $.NSWorkspace.sharedWorkspace.setIconForFileOptions(
            $.NSImage.alloc.initWithContentsOfFile(argv[0]), argv[1], 0)
    }' "$app/icons/trimixxx.ico" "$app/build/pi-qemu" >/dev/null 2>&1 || true
    if [ "$main" = 1 ]; then
        mkdir -p "$HOME/.local/bin"
        ln -sf "$app/build/pi-qemu" "$HOME/.local/bin/pi-qemu"
        echo "==> pi-qemu: $app/build/pi-qemu, as ~/.local/bin/pi-qemu"
        case ":$PATH:" in *":$HOME/.local/bin:"*) ;; *) echo "    (~/.local/bin is not on PATH: add it)" ;; esac
    else
        echo "==> pi-qemu: $app/build/pi-qemu (a worktree's: run it by this path)"
    fi
}

case "$what" in
    qemu) build_qemu ;;
    app) build_app ;;
    all)
        if [ "$main" = 1 ]; then build_qemu; else echo "==> QEMU: the main checkout's (a worktree uses it as it is)"; fi
        build_app
        ;;
esac
