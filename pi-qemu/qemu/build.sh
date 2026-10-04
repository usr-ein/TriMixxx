#!/usr/bin/env bash
# Build the QEMU that emulates the deck: upstream QEMU, rpi-qemu's raspi4b
# patch series (GENET, PCIe root complex, firmware fix-ups, watchdog, ...), then
# ours (trimixxx-patches.py: HVF, working PCIe/xHCI, SD DMA, full RAM, the
# deck's DAC and its 44.1 kHz, and every device saved, for snapshots).
#
#   ./build.sh            -> .build/qemu-<ver>/build/qemu-system-aarch64
#
# Everything is pinned, so a rebuild is the same QEMU. Needs Homebrew's qemu
# dependencies (brew install qemu pulls them all) and python3.12. Takes about
# ten minutes the first time on an M1; reruns only recompile what changed.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="$HERE/.build"

QEMU_VERSION=11.1.0
QEMU_SHA256=6ee1d1a61f68212476b27108c26da5f449dc09b626d42f8279ba0dc2e08fa858
RPI_QEMU_REPO=https://github.com/fpgas-online/rpi-qemu.git
RPI_QEMU_COMMIT=0eca340c841430e06ea4e0f749d599bdfcdb6765   # 2026-09-30
# Raspberry Pi's dtmerge: applies dtoverlay=/dtparam= lines, parameters and
# all, as the firmware does. pi-qemu uses it to build the board's device tree.
RPI_UTILS_REPO=https://github.com/raspberrypi/utils.git
RPI_UTILS_COMMIT=e0484c848f9c9d1aecc7bd0738930db97bcf18df  # 2026-10-02

mkdir -p "$WORK"
cd "$WORK"

if [ ! -f "qemu-$QEMU_VERSION.tar.xz" ]; then
    curl -fL --max-time 900 -o "qemu-$QEMU_VERSION.tar.xz.part" \
        "https://download.qemu.org/qemu-$QEMU_VERSION.tar.xz"
    mv "qemu-$QEMU_VERSION.tar.xz.part" "qemu-$QEMU_VERSION.tar.xz"
fi
if [ -n "$QEMU_SHA256" ]; then
    echo "$QEMU_SHA256  qemu-$QEMU_VERSION.tar.xz" | shasum -a 256 -c -
else
    echo "qemu-$QEMU_VERSION.tar.xz sha256: $(shasum -a 256 "qemu-$QEMU_VERSION.tar.xz" | cut -d' ' -f1) (pin it in build.sh)"
fi

if [ ! -d rpi-qemu ]; then
    git clone -q "$RPI_QEMU_REPO" rpi-qemu
fi
git -C rpi-qemu fetch -q origin "$RPI_QEMU_COMMIT" 2>/dev/null || true
git -C rpi-qemu checkout -q "$RPI_QEMU_COMMIT"

SRC="qemu-$QEMU_VERSION"
# The stamp is the patch script's hash: changing a patch re-applies them all to
# a fresh tree.
PATCHES_SHA="$(shasum -a 256 "$HERE/trimixxx-patches.py" | cut -d' ' -f1)"
if [ "$(cat "$SRC/.patched" 2>/dev/null)" != "$PATCHES_SHA" ]; then
    rm -rf "$SRC"
    tar xf "qemu-$QEMU_VERSION.tar.xz"
    (
        cd "$SRC"
        git init -q && git add -A >/dev/null 2>&1
        git -c user.name=build -c user.email=build@localhost commit -qm base
        for p in ../rpi-qemu/ci/qemu-patches/*.patch; do
            git -c user.name=build -c user.email=build@localhost am -q --3way "$p"
        done
        python3 "$HERE/trimixxx-patches.py" .
        echo "$PATCHES_SHA" > .patched
    )
fi

mkdir -p "$SRC/build"
cd "$SRC/build"
if [ ! -f build.ninja ]; then
    ../configure --python=/opt/homebrew/bin/python3.12 \
        --target-list=aarch64-softmmu \
        --extra-cflags=-I/opt/homebrew/include --extra-ldflags=-L/opt/homebrew/lib \
        --enable-slirp --enable-cocoa --enable-hvf --enable-fdt=system --enable-vnc \
        --enable-zstd --disable-fuse --disable-docs --disable-guest-agent \
        --disable-bsd-user --disable-sdl --disable-gtk --disable-werror
fi
nice ninja qemu-system-aarch64

cd "$WORK"
if [ ! -d rpi-utils ]; then
    git clone -q "$RPI_UTILS_REPO" rpi-utils
fi
git -C rpi-utils fetch -q origin "$RPI_UTILS_COMMIT" 2>/dev/null || true
git -C rpi-utils checkout -q "$RPI_UTILS_COMMIT"
mkdir -p bin
cc -O2 -I/opt/homebrew/include -o bin/dtmerge \
    rpi-utils/dtmerge/dtmerge.c rpi-utils/dtmerge/dtoverlay.c -L/opt/homebrew/lib -lfdt
# A symlink, not a copy: the binary carries its hypervisor entitlement.
ln -sf "../$SRC/build/qemu-system-aarch64" bin/qemu-system-aarch64
echo "==> $WORK/bin: $(ls bin | tr '\n' ' ')"
