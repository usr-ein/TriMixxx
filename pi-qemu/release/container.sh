#!/usr/bin/env bash
# The release container's half of `pi-qemu release` (pi-qemu/PLAN.md Part 1);
# the Mac's half is pi-qemu/app/src/pipeline/release.cpp, which builds the
# container (Dockerfile) and runs this in it. The tools are Debian trixie's,
# the decks' own. Never run on the Mac.
#
#   container.sh seal     the release, from the built system card
#                         (/build/trimixxx-release.img): out/VERSION/
#                         trimixxx-VERSION.img (every deck's card),
#                         trimixxx-VERSION.raucb (the update), rootfs.squashfs,
#                         boot.vfat, manifest.txt. Privileged: it mounts the
#                         built card, read-only.
#   container.sh card     DECK's identity (/deck) onto p7 of its copy of the
#                         release card, out/VERSION/DECK-VERSION.img
#   container.sh faults   broken updates made from release VERSION, each signed
#                         like a real one, for the fault tests (PLAN.md §5.4):
#                         out/VERSION-nonet, -nossh, -noinitramfs, -panic, -hang
#
# Mounts: /release (this directory, its out/ the main checkout's), /repo (the
# checkout, read-only), /build (seal: pi-qemu/.cache/build, read-only), /deck
# (card: the deck's identity, read-only). Environment: VERSION; COMMIT and HASH
# (seal); DECK (card); TZ, the Mac's offset, so the step lines carry the Mac's
# time.
set -euo pipefail
if [ -z "${IN_CONTAINER:-}" ]; then
    echo "container.sh runs in the release container: pi-qemu release build|card|faults" >&2
    exit 2
fi
cd /release
: "${VERSION:?}"

# The card's MBR disk signature, the same on every card for good (PLAN.md
# invariant 7): partitions are PARTUUID=5d0bc1ec-0N, so the command lines never
# change. Any fixed value works; this is the first 32 bits of sha256("trimixxx").
export DISK_ID=0x5d0bc1ec
ID=${DISK_ID#0x}
export WORK=/work
export MTOOLS_SKIP_CHECK=1
SYSTEM=trimixxx-release
# The decks' screens, as their framebuffers report them (WxHxBPPxSTRIDE):
# trimixxx1's panel, and trimixxx2's (the emulated trimixxx0's too). The splash
# carries a labelled image for each (pi_config/trimixxx-splash.sh).
SPLASH_SCREENS="1024x600x16x2048 1280x800x16x2560"

# A step, for the build window: a line as pi-qemu's own say prints them.
say() { echo; echo "==> [$(date +%H:%M:%S)] release: $*"; }

# The release's kernel command line, for slot $1: its root ($2) and boot ($3)
# partitions.
slot_cmdline() {
    sed -e "s/@ID@/$ID/g" -e "s/@SLOT@/$1/" -e "s/@ROOT@/$2/" -e "s/@BOOT@/$3/" boot/cmdline.txt.in
}

seal() {
    # A pi-qemu built before HASH was passed comes here after building the
    # system: say how to finish, rather than only what is missing.
    : "${COMMIT:?}" "${HASH:?not passed by this pi-qemu, which is older than this script: rebuild it (pi-qemu/build.sh app), then release build --keep-system to seal the system just built}"
    local o=out/$VERSION card=/build/$SYSTEM.img r=/merged p=/repo/pi_config
    local boot_start root_start root_size
    # The release's date: when it was sealed, in the release file and the
    # manifest alike.
    local built
    built=$(date -u +%Y-%m-%dT%H:%MZ)
    rm -rf "$WORK" "$o"
    mkdir -p "$WORK"/boot "$WORK"/bootsel "$WORK"/root "$WORK"/bundle "$WORK"/img "$o"
    read -r boot_start _ < <(partx -g -o START,SECTORS -n 1 "$card")
    read -r root_start root_size < <(partx -g -o START,SECTORS -n 2 "$card")

    # ---- the system: the built card's root, sealed, as SquashFS ----------------
    say "the system sealed into SquashFS"
    # Read-only underneath, the seal's changes in RAM on top: the built card is
    # never written.
    mkdir -p /lower /up "$r"
    mount -t tmpfs tmpfs /up && mkdir -p /up/u /up/w
    mount -o loop,ro,offset=$((root_start * 512)),sizelimit=$((root_size * 512)) -t ext4 "$card" /lower
    mount -t overlay overlay -o lowerdir=/lower,upperdir=/up/u,workdir=/up/w "$r"
    trap 'umount "$r" /lower /up 2>/dev/null || true' EXIT
    # The build's identity and leftovers (exclude.txt). Each deck's comes from
    # /data at every start (trimixxx-identity).
    shopt -s nullglob dotglob
    while read -r g; do for f in $r/$g; do rm -rf "$f"; done; done \
        < <(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' exclude.txt)
    find "$r"/var/log -type f -delete
    : > "$r"/etc/machine-id
    echo trimixxx > "$r"/etc/hostname
    sed -i 's/^127\.0\.1\.1.*/127.0.1.1\ttrimixxx/' "$r"/etc/hosts
    # The release's own files, from the repo whatever the build deployed: its
    # mounts, RAUC's configuration and backend, the identity and health units,
    # eth0's profile.
    install -m 0644 rootfs/etc/fstab "$r"/etc/fstab
    install -D -m 0644 $p/rauc/system.conf "$r"/etc/rauc/system.conf
    install -D -m 0644 $p/rauc/keyring.pem "$r"/etc/rauc/keyring.pem
    install -D -m 0755 $p/rauc/rpi-tryboot "$r"/usr/lib/rauc/rpi-tryboot
    install -D -m 0755 $p/trimixxx-identity "$r"/usr/local/lib/trimixxx/identity
    install -D -m 0755 $p/trimixxx-health "$r"/usr/local/lib/trimixxx/health
    install -m 0644 $p/trimixxx-identity.service $p/trimixxx-health.service "$r"/etc/systemd/system/
    install -D -m 0600 $p/eth0-link-local.nmconnection "$r"/etc/NetworkManager/system-connections/eth0-link-local.nmconnection
    install -m 0755 $p/trimixxx-splash.sh "$r"/usr/local/bin/trimixxx-splash
    mkdir -p "$r"/data "$r"/var/lib/rauc
    # The splash says which slot started: an image per screen and per start,
    # labelled with the slot, the version, and the trial.
    local g w h bpp stride dst s
    for g in $SPLASH_SCREENS; do
        IFS=x read -r w h bpp stride <<< "$g"
        dst=$r/usr/local/share/trimixxx/splash.d/$g
        mkdir -p "$dst"
        for s in A B; do
            python3 $p/splash-render.py $p/trimixxx_logo_crt.svg -o "$dst/$s.raw" \
                --width "$w" --height "$h" --bpp "$bpp" --stride "$stride" --label "$s  ·  v$VERSION" > /dev/null
            python3 $p/splash-render.py $p/trimixxx_logo_crt.svg -o "$dst/$s-trial.raw" \
                --width "$w" --height "$h" --bpp "$bpp" --stride "$stride" --label "$s  ·  trial  ·  v$VERSION" --trial > /dev/null
        done
    done
    # Started at every start: the deck's identity, and the health check.
    # Masked: the first-start jobs a read-only root would run at every start
    # (systemd counts each as a first start, /etc/machine-id being empty): new
    # ssh host keys over the deck's own, and growing the root.
    mkdir -p "$r"/etc/systemd/system/sysinit.target.wants "$r"/etc/systemd/system/multi-user.target.wants
    ln -sf /etc/systemd/system/trimixxx-identity.service "$r"/etc/systemd/system/sysinit.target.wants/
    ln -sf /etc/systemd/system/trimixxx-health.service "$r"/etc/systemd/system/multi-user.target.wants/
    for u in regenerate_ssh_host_keys.service rpi-resize.service; do ln -sf /dev/null "$r"/etc/systemd/system/$u; done
    # What the deck runs, for anything on it that asks (the health check's
    # log, Mixxx's Diagnostics): the version, `git describe` of the commit
    # (the tag, or how far past it, -dirty, rehearsal), the commit's hash, and
    # the date it was sealed. Readers take the lines they know.
    printf 'VERSION=%s\nCOMMIT=%s\nHASH=%s\nBUILT=%s\n' "$VERSION" "$COMMIT" "$HASH" "$built" > "$r"/etc/trimixxx-release
    dpkg-query --admindir="$r"/var/lib/dpkg -W -f='${Package} ${Version}\n' > "$WORK"/packages.txt
    # POSIX ACLs (the journal's directory) have no place in SquashFS: left out.
    mksquashfs "$r" "$WORK"/img/rootfs.squashfs -comp zstd -xattrs -xattrs-exclude '^system\.posix_acl' \
        -noappend -quiet -no-progress
    umount "$r" /lower /up
    trap - EXIT

    # ---- the boot files: the built card's, with both slots' command lines -------
    say "the card: boot files and genimage"
    mcopy -snm -i "$card@@$((boot_start * 512))" '::*' "$WORK"/boot/
    rm -f "$WORK"/boot/{cmdline.txt,user-data,network-config,meta-data,config.txt.pre-trimixxx,cmdline.txt.pre-trimixxx}
    # Each deck's own lines (its panel), under its board's serial: one card for
    # every deck (boot/deck-sections.py, from mixxx_config/units/<deck>.json).
    python3 boot/deck-sections.py /repo/mixxx_config/units >> "$WORK"/boot/config.txt
    cat boot/config-release.txt >> "$WORK"/boot/config.txt
    slot_cmdline A 05 02 > "$WORK"/boot/cmdline-a.txt
    slot_cmdline B 06 03 > "$WORK"/boot/cmdline-b.txt
    cp boot/autoboot.txt "$WORK"/bootsel/

    # ---- the card -------------------------------------------------------------------
    # All of it made in the container's own filesystem, then copied out. Read
    # through Docker's share of the Mac's disk, a file just written can claim to
    # be one big hole, and `cp` then copies nothing but zeros.
    genimage --config genimage.cfg --rootpath "$WORK"/root --inputpath "$WORK"/img \
        --tmppath "$WORK"/tmp --outputpath "$WORK"/img
    # The release's p1 holds no firmware: a damaged autoboot.txt then starts p2
    # (PLAN.md invariant 13).
    if mdir -b -i "$WORK/img/trimixxx.img@@$((16384 * 512))" ::/ | grep -i '\.elf$'; then
        echo "p1 holds firmware: a damaged autoboot.txt would start it (invariant 13)" >&2
        exit 1
    fi

    # ---- the update: a RAUC bundle of both slot images ----------------------------
    say "the update: a RAUC bundle"
    # Signed with signing-key.pem, public on purpose (Sam, 2026-10-08): TriMixxx
    # is open hardware, and anyone may sign a release for it. The signature,
    # which RAUC requires, guards against a damaged bundle, not a hostile one.
    sed -e "s|@VERSION@|$VERSION|" -e "s|@BUILD@|$COMMIT|" manifest.raucm.in > "$WORK"/bundle/manifest.raucm
    ln "$WORK"/img/rootfs.squashfs "$WORK"/img/boot.vfat "$WORK"/bundle/
    # Made and checked in the container's own filesystem: RAUC won't read a
    # plain bundle off a FUSE mount such as Docker's share of the Mac's disk.
    rauc bundle --cert=$p/rauc/keyring.pem --key=signing-key.pem "$WORK"/bundle "$WORK/trimixxx-$VERSION.raucb"
    rauc info --keyring=$p/rauc/keyring.pem "$WORK/trimixxx-$VERSION.raucb" | grep -E "Compatible|Version|Build"
    cp --sparse=always "$WORK"/img/trimixxx.img "$o/trimixxx-$VERSION.img"
    cp "$WORK"/img/rootfs.squashfs "$WORK"/img/boot.vfat "$WORK/trimixxx-$VERSION.raucb" "$o"/

    # ---- what went in -----------------------------------------------------------------
    {
        echo "TriMixxx $VERSION, from $COMMIT, built $built"
        echo
        (cd "$o" && sha256sum "trimixxx-$VERSION.img" "trimixxx-$VERSION.raucb" rootfs.squashfs boot.vfat)
        echo
        cat "$WORK"/packages.txt
    } > "$o"/manifest.txt
    ls -l "$o"
}

# The deck's identity onto p7 of its copy of the release card (an APFS clone
# made on the Mac).
card() {
    : "${DECK:?}"
    local img=out/$VERSION/$DECK-$VERSION.img start size
    read -r start size < <(partx -g -o START,SECTORS -n 7 "$img")
    rm -rf "$WORK" && mkdir -p "$WORK"
    mke2fs -q -t ext4 -L trimixxx-data -d /deck "$WORK"/data.ext4 $((size / 2))k
    dd if="$WORK"/data.ext4 of="$img" bs=1M seek=$((start * 512)) oflag=seek_bytes conv=notrunc,sparse status=none
    debugfs -R 'ls -l /' "$WORK"/data.ext4 2>/dev/null | awk 'NR > 2 {print $NF}' | tr '\n' ' '
    echo
    ls -l "$img"
}

# Broken releases from release VERSION's images, each signed like a real one:
# out/VERSION-NAME/trimixxx-VERSION-NAME.raucb.
faults() {
    local o=out/$VERSION p=/repo/pi_config f n u c
    [ -f "$o"/rootfs.squashfs ] || { echo "no release $VERSION: pi-qemu release build first" >&2; exit 1; }
    rm -rf "$WORK" && mkdir -p "$WORK"
    # Plain reads off Docker's share of the Mac's disk, never cp (see seal).
    dd if="$o"/rootfs.squashfs of="$WORK"/rootfs.squashfs bs=4M status=none
    dd if="$o"/boot.vfat of="$WORK"/boot.vfat bs=4M status=none
    bundle() { # NAME ROOTFS BOOT
        local v=$VERSION-$1 d=$WORK/b-$1
        mkdir -p "$d" "out/$v"
        sed -e "s|@VERSION@|$v|" -e "s|@BUILD@|a fault test, from $VERSION|" manifest.raucm.in > "$d"/manifest.raucm
        ln "$2" "$d"/rootfs.squashfs && ln "$3" "$d"/boot.vfat
        rauc bundle --cert=$p/rauc/keyring.pem --key=signing-key.pem --keyring=$p/rauc/keyring.pem "$d" "$WORK/$v.raucb" > /dev/null
        cp "$WORK/$v.raucb" "out/$v/trimixxx-$v.raucb"
        echo "out/$v/trimixxx-$v.raucb"
    }
    # F4a, nonet: NetworkManager masked, so the deck joins no network. F4b,
    # nossh: sshd masked. Either way it can't take the next release, which is
    # all the health check asks.
    unsquashfs -q -no-progress -d "$WORK"/root "$WORK"/rootfs.squashfs
    for f in nonet:NetworkManager.service nossh:ssh.service; do
        n=${f%%:*} u=${f#*:}
        ln -s /dev/null "$WORK"/root/etc/systemd/system/"$u"
        sed -i "s/^VERSION=.*/VERSION=$VERSION-$n/" "$WORK"/root/etc/trimixxx-release
        mksquashfs "$WORK"/root "$WORK/rootfs-$n.squashfs" -comp zstd -xattrs -noappend -quiet -no-progress
        rm "$WORK"/root/etc/systemd/system/"$u"
        bundle "$n" "$WORK/rootfs-$n.squashfs" "$WORK"/boot.vfat
    done
    rm -rf "$WORK"/root
    # F5, noinitramfs: the kernel can't mount its SquashFS root, panics, and
    # panic=10 reboots it.
    cp "$WORK"/boot.vfat "$WORK"/boot-noinitramfs.vfat
    mdel -i "$WORK"/boot-noinitramfs.vfat ::/initramfs8
    bundle noinitramfs "$WORK"/rootfs.squashfs "$WORK"/boot-noinitramfs.vfat
    # F6a, panic: the initramfs gives up (break=premount, as for a root it
    # can't find), and with panic=10 initramfs-tools reboots after 10 s.
    # F6b, hang: the same without panic=, so a shell nobody answers, which only
    # the watchdog ends.
    for f in panic hang; do
        cp "$WORK"/boot.vfat "$WORK/boot-$f.vfat"
        for c in cmdline-a.txt cmdline-b.txt; do
            mtype -i "$WORK/boot-$f.vfat" ::/$c | sed -e '1 s/$/ break=premount/' > "$WORK/$c"
            [ "$f" = panic ] || sed -i 's/ panic=10 / /' "$WORK/$c"
            mcopy -o -i "$WORK/boot-$f.vfat" "$WORK/$c" ::/$c
        done
        bundle "$f" "$WORK"/rootfs.squashfs "$WORK/boot-$f.vfat"
    done
}

case "${1:-}" in
    seal|card|faults) "$1" ;;
    *) echo "usage: container.sh seal|card|faults" >&2; exit 2 ;;
esac
