#!/usr/bin/env bash
# Build a deck's SD card image on the emulated Pi: the stock Raspberry Pi OS
# card, then everything pi_config/fresh-install.md does to a new deck --
# its hand steps as code, then the repo's own deploy scripts, run exactly as
# they are run against a deck (HOST=...), over ssh into pi-qemu.
#
#   image/build.sh [HOSTNAME]          -> .cache/build/<HOSTNAME>.img
#
# HOSTNAME defaults to trimixxx0, the emulated deck (a virtual trimixxx2: see
# mixxx_config/units/trimixxx0.json). Needs: qemu/build.sh and app/build.sh
# done, Docker running (the Pi binaries are arm64 Docker builds), mtools, and
# the key in SSH_KEY (default ~/.ssh/no_pass/rsa_sam) to log in with.
#
# The slow first part -- first boot, cloud-init and deploy.sh base (apt) -- is
# kept as a card in .cache/base/ and reused while its inputs are unchanged:
# the stock image, the hostname, the key, the password, deploy/base.sh, the
# deck's panelOverlay, and this script's base-stage section. REBUILD_BASE=1
# makes it again anyway.
#
# For trimixxx0 the build ends by refreshing the golden snapshot (instance.sh
# golden): agents' emulated decks restore it rather than boot.
#
# The emulated deck is SILENT the whole time (pi-qemu's default), and has no
# window: watch it with `pi-qemu screenshot FILE.png` if you want to. The deck
# itself is set up by ../deploy.sh, the same script that updates a real deck.
set -euo pipefail

DECK="${1:-trimixxx0}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIQ="$(cd "$HERE/.." && pwd)"
REPO="$(cd "$PIQ/.." && pwd)"
CACHE="$PIQ/.cache"
OUT="$CACHE/build"
CARD="$OUT/$DECK.img"
SSH_KEY="${SSH_KEY:-$HOME/.ssh/no_pass/rsa_sam}"
SSH_PORT="${SSH_PORT:-2223}"
HOST=trimixxx-build

BASE_NAME=2026-09-15-raspios-trixie-arm64-lite
BASE_URL=https://downloads.raspberrypi.com/raspios_lite_arm64/images/raspios_lite_arm64-2026-09-15/$BASE_NAME.img.xz
BASE_SHA256=cdf4f3bfac35ae947b46e4e767f935453810549779ac3290e05a6754aee627e5

say() { printf '\n==> [%s] %s\n' "$(date +%H:%M:%S)" "$*"; }

# ---- preflight -------------------------------------------------------------------
for f in "$PIQ/qemu/.build/bin/qemu-system-aarch64" "$PIQ/app/build/pi-qemu" "$SSH_KEY" "$SSH_KEY.pub"; do
    [ -e "$f" ] || { echo "missing: $f (qemu/build.sh, app/build.sh, or SSH_KEY)" >&2; exit 1; }
done
command -v mcopy >/dev/null || { echo "mtools missing: brew install mtools" >&2; exit 1; }
docker info >/dev/null 2>&1 || { echo "Docker is not running" >&2; exit 1; }
# Docker Desktop's VM disk fills with Mixxx builds; a full one fails the build
# half-way. Stop here instead of pruning anything: that is the user's call.
free_gb="$(docker run --rm --pull missing debian:trixie df -BG --output=avail / 2>/dev/null | tail -1 | tr -dc 0-9 || echo 0)"
if [ "${free_gb:-0}" -lt 6 ]; then
    echo "Docker's disk has ${free_gb:-?} GB free; the Mixxx build needs ~6. Free some first." >&2
    exit 1
fi

# The build talks to whatever answers on its ssh port: make sure that is nothing yet.
if lsof -nP -iTCP:"$SSH_PORT" -sTCP:LISTEN >/dev/null 2>&1; then
    echo "port $SSH_PORT is already in use (another emulated deck?): stop it, or SSH_PORT=..." >&2
    exit 1
fi

mkdir -p "$OUT/bin"
# ---- the deck scripts reach the emulated Pi as HOST, through these -------------
# They call plain ssh/scp; a config of our own keeps ~/.ssh/config untouched.
cat > "$OUT/ssh_config" <<EOF
Host $HOST
  HostName 127.0.0.1
  Port $SSH_PORT
  User sam1902
  IdentityFile $SSH_KEY
  IdentitiesOnly yes
  StrictHostKeyChecking no
  UserKnownHostsFile /dev/null
  LogLevel ERROR
  ConnectTimeout 10
  ServerAliveInterval 15
  SetEnv LC_ALL=C.UTF-8
EOF
for tool in ssh scp; do
    printf '#!/bin/sh\nexec /usr/bin/%s -F "%s" "$@"\n' "$tool" "$OUT/ssh_config" > "$OUT/bin/$tool"
    chmod +x "$OUT/bin/$tool"
done
export PATH="$OUT/bin:$PATH"

# ---- boot it --------------------------------------------------------------------
PIQ_LOG="$OUT/pi-qemu.log"
start_pi() {
    "$PIQ/app/build/pi-qemu" run --no-controls --display none --ssh "$SSH_PORT" --deck "$DECK" "$CARD" \
        >> "$PIQ_LOG" 2>&1 &
    PIQ_PID=$!
}
wait_ssh() { # first boot: resize, a reboot, cloud-init -- a few minutes
    local end=$(( $(date +%s) + ${1:-420} ))
    until ssh "$HOST" true 2>/dev/null; do
        kill -0 "$PIQ_PID" 2>/dev/null || { echo "pi-qemu exited; see $PIQ_LOG" >&2; exit 1; }
        [ "$(date +%s)" -lt "$end" ] || { echo "no ssh after ${1:-420}s; see $PIQ_LOG" >&2; exit 1; }
        sleep 5
    done
}
cleanup() { [ -n "${PIQ_PID:-}" ] && kill "$PIQ_PID" 2>/dev/null || true; }
trap cleanup EXIT
stop_pi() { # power the Pi off cleanly; pi-qemu exits with it
    ssh "$HOST" 'sudo systemctl poweroff' || true
    for _ in $(seq 1 60); do kill -0 "$PIQ_PID" 2>/dev/null || break; sleep 2; done
    kill -0 "$PIQ_PID" 2>/dev/null && { echo "the Pi did not power off; see $PIQ_LOG" >&2; exit 1; }
    PIQ_PID=
}
: > "$PIQ_LOG"

# ---- 0. the stock card --------------------------------------------------------
say "base image"
if [ ! -f "$CACHE/$BASE_NAME.img" ]; then
    curl -fL --max-time 1800 -o "$CACHE/$BASE_NAME.img.xz.part" "$BASE_URL"
    echo "$BASE_SHA256  $CACHE/$BASE_NAME.img.xz.part" | shasum -a 256 -c -
    mv "$CACHE/$BASE_NAME.img.xz.part" "$CACHE/$BASE_NAME.img.xz"
    xz -dkT0 "$CACHE/$BASE_NAME.img.xz"
fi
# The console password (image/secrets.env, gitignored; secrets.example.env is
# the template). Only its hash reaches the card.
SAM1902_PASSWORD=
[ -f "$HERE/secrets.env" ] && . "$HERE/secrets.env"

# The base card's key: everything that goes into it (see the header).
base_key="$( {
    echo "$BASE_SHA256 $DECK"
    cat "$SSH_KEY.pub" "$PIQ/deploy/base.sh"
    printf '%s\n' "$SAM1902_PASSWORD"
    grep '"panelOverlay"' "$REPO/mixxx_config/units/$DECK.json" || true
    awk '/^# >>> base stage/,/^# <<< base stage/' "$HERE/build.sh"
} | shasum -a 256 | cut -c1-16)"
BASE_CARD="$CACHE/base/$DECK-$base_key.img"

# >>> base stage: everything in here is in the base card's key
if [ -f "$BASE_CARD" ] && [ -z "${REBUILD_BASE:-}" ]; then
    say "base (fresh-install.md 1-2): cached, .cache/base/$(basename "$BASE_CARD")"
    rm -f "$CARD"
    cp -c "$BASE_CARD" "$CARD" 2>/dev/null || cp "$BASE_CARD" "$CARD"
    say "booting the cached base card into the deck's boot flags"
    start_pi
    wait_ssh 300
else
    rm -f "$CARD"
    cp -c "$CACHE/$BASE_NAME.img" "$CARD" 2>/dev/null || cp "$CACHE/$BASE_NAME.img" "$CARD"
    truncate -s 8G "$CARD" # QEMU's SD card is a power of two; first boot grows root into it

    # ---- 0.1 first-boot seed: what Raspberry Pi Imager would write -----------------
    # fresh-install.md section 0: user sam1902, the hostname, ssh key login,
    # passwordless sudo (1.1), eth0 on DHCP as the image ships it. Cloud-init reads
    # these off the boot partition at first boot -- the image's own mechanism.
    say "seeding cloud-init: $DECK, user sam1902"
    # Without a password, the account has none at all, and only keys get in.
    if [ -n "$SAM1902_PASSWORD" ]; then
        pw_lines="    lock_passwd: false
    passwd: '$(openssl passwd -6 "$SAM1902_PASSWORD")'"
    else
        echo "no SAM1902_PASSWORD in image/secrets.env: the console has no password login" >&2
        pw_lines="    lock_passwd: true"
    fi
    boot_offset=$(( $(fdisk -d "$CARD" | head -1 | cut -d, -f1) * 512 ))
    cat > "$OUT/user-data" <<EOF
#cloud-config
hostname: $DECK
manage_etc_hosts: true
users:
  - name: sam1902
    groups: [users, adm, dialout, audio, netdev, video, plugdev, input, gpio, spi, i2c, render, sudo]
    shell: /bin/bash
$pw_lines
    sudo: "ALL=(ALL) NOPASSWD:ALL"
    ssh_authorized_keys:
      - $(cat "$SSH_KEY.pub")
ssh_pwauth: false
runcmd:
  - [systemctl, enable, --now, ssh]
EOF
    # eth0 bound by name: a bare "eth0:" renders as match-anything, and in the
    # emulator the management USB NIC comes up first and takes it -- leaving eth0
    # without a profile for prolink-eth0.sh, and the build's own link on the one
    # it would switch to link-local. On a deck, eth0 is the only Ethernet anyway.
    cat > "$OUT/network-config" <<'EOF'
network:
  version: 2
  ethernets:
    eth0:
      match:
        name: eth0
      dhcp4: true
      optional: true
EOF
    : > "$OUT/ssh"
    for f in user-data network-config ssh; do
        MTOOLS_SKIP_CHECK=1 mcopy -o -i "$CARD@@$boot_offset" "$OUT/$f" "::/$f"
    done

    say "booting the stock card (silent, no window)"
    start_pi
    wait_ssh 600
    ssh "$HOST" 'cloud-init status --wait >/dev/null 2>&1 || true; hostname; uname -r'
    # The management USB NIC needs a profile of its own. Cloud-init renders
    # eth0's as match-anything, the USB NIC comes up first and takes it, and
    # prolink-eth0.sh then makes it link-local -- no more ssh. Binding eth0's
    # profile to eth0 does not last: NetworkManager writes it back to netplan as
    # `match: {}` again. So the USB NIC's own DHCP profile, kept in
    # /etc/NetworkManager where netplan does not reach, outranks it on usb0 at
    # every boot. Run detached: the switch blips the ssh session riding it.
    ssh "$HOST" 'sudo systemd-run --quiet --unit=pi-qemu-home-nic sh -c "
        nmcli connection add type ethernet ifname usb0 con-name pi-qemu-home ipv4.method auto ipv6.method auto \
            connection.autoconnect-priority 100 &&
        nmcli connection up pi-qemu-home"' || true
    sleep 8
    wait_ssh 120
    ssh "$HOST" 'nmcli -t -f NAME,DEVICE connection show' | tee /dev/stderr | grep -q '^pi-qemu-home:usb0' ||
        { echo "the management NIC did not get its own profile" >&2; exit 1; }

    # ---- the deck: pi-qemu/deploy.sh, as against any deck ---------------------------
    say "pi-qemu/deploy.sh $HOST"
    "$PIQ/deploy.sh" "$HOST" base

    # The boot flags just changed (the PL011 for the S3, no serial console): they
    # apply from the next boot, here as on a deck. pi-qemu reads config.txt and
    # cmdline.txt again on the way back up, as the firmware does. Powered off
    # rather than rebooted, to keep the card as it is now as the base card.
    say "rebooting into the deck's boot flags (keeping the base card)"
    stop_pi
    mkdir -p "$(dirname "$BASE_CARD")"
    rm -f "$CACHE/base/$DECK-"*.img
    cp -c "$CARD" "$BASE_CARD" 2>/dev/null || cp "$CARD" "$BASE_CARD"
    start_pi
    wait_ssh 300
fi
# <<< base stage
ssh "$HOST" 'echo "serial0 -> $(readlink -f /dev/serial0)"'

"$PIQ/deploy.sh" "$HOST" ttymidi system launcher mixxx config
# Mixxx's first start, from that last step, made its database: answer its
# "Choose music library directory" there, then the rest.
"$PIQ/deploy.sh" "$HOST" library doom

# ---- seal ------------------------------------------------------------------------
# Cloud-init was the first boot's provisioning; off now (fresh-install.md 1.4),
# so later boots do not spend seconds in it.
say "sealing: cloud-init off, caches cleared, power off"
ssh "$HOST" 'sudo touch /etc/cloud/cloud-init.disabled && sudo apt-get clean' || true
stop_pi

# ---- golden snapshot -----------------------------------------------------------------
# The emulated deck's card, booted until Mixxx plays and saved with its whole
# machine: what every agent's instance (instance.sh up) starts from, in seconds.
if [ "$DECK" = trimixxx0 ]; then
    say "golden snapshot: the deck agents' instances start from"
    "$PIQ/instance.sh" golden "$CARD"
fi
say "done: $CARD"
echo "run it:  $PIQ/app/build/pi-qemu run $CARD"
