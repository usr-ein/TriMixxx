#!/usr/bin/env bash
# Ready a deck for its first release card, from the system it runs today
# (pi-qemu/PLAN.md §7). On the Mac, before the deck's card is made and flashed:
#
#   pi-qemu/release/prepare-deck.sh DECK SSH_HOST [--eeprom]
#
#   DECK      its name on the card: its hostname, mixxx_config/units/DECK.json
#   SSH_HOST  how the Mac reaches it today, e.g. the alias trimixxx-pi-2
#
# It only reads the deck, unless --eeprom is given:
#  1. its board's serial goes into mixxx_config/units/DECK.json ("serial"; the
#     file is made if there's none). The release's config.txt then gives this
#     board its own section, its panel overlay among it (boot/deck-sections.py).
#     The release must be built after this, and the unit file committed first.
#  2. its ssh host keys become its identity's (pi-qemu/.cache/decks/DECK/ssh),
#     unless that has keys already: the card keeps them, so known_hosts stays
#     right through the reflash.
#  3. its bootloader: one from before 2022-12-01 doesn't know tryboot_a_b, and
#     would start a fresh card's empty slot B: nothing would start (phase 1,
#     T0). Without --eeprom the script then fails.
#  4. its boot files, EEPROM config, NetworkManager profiles and ALSA state are
#     kept in pi-qemu/.cache/captures/DECK-<time>/ for reference (Wi-Fi
#     passwords included: gitignored, readable by you only). Lines of its
#     config.txt that a unit file may need are shown: a panel's dtoverlay goes
#     into "panelOverlay", other screen lines into "configTxt".
#
# --eeprom updates the bootloader and gives it its boot watchdog (PLAN.md
# invariant 14: BOOT_WATCHDOG_TIMEOUT=45, BOOT_WATCHDOG_PARTITION=2), once the
# firmware's log shows Linux starting well within 45 s. `rpi-eeprom-config
# --apply` writes the config into the newest bootloader image the deck's own
# rpi-eeprom package has, so one flash does both, at the next start, from the
# card in the deck now: the script reboots it, then checks the version and the
# config. Do it before the card is swapped. On an A/B card the update goes to
# p1, where the boot ROM looks (phase 1).
set -euo pipefail

[ $# -ge 2 ] || { sed -n '5,9p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2; }
DECK=$1 HOST=$2 WATCHDOG=${3:-}
[ -z "$WATCHDOG" ] || [ "$WATCHDOG" = --eeprom ] || { echo "unknown option: $WATCHDOG" >&2; exit 2; }
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
MAIN="$(dirname "$(git -C "$HERE" rev-parse --path-format=absolute --git-common-dir)")"
UNIT="$REPO/mixxx_config/units/$DECK.json"
IDENTITY="$MAIN/pi-qemu/.cache/decks/$DECK"
CAPTURE="$MAIN/pi-qemu/.cache/captures/$DECK-$(date +%Y%m%d-%H%M)"
D() { ssh -o ConnectTimeout=10 "$HOST" "$@"; }
say() { printf '\n==> %s\n' "$*"; }
status=0

say "$HOST, to become $DECK"
D 'echo "  runs: $(. /etc/os-release && echo "$PRETTY_NAME"), hostname $(hostname), $([ -e /etc/rauc/system.conf ] && echo "a release card ($(sed -n s/^VERSION=//p /etc/trimixxx-release))" || echo "a dev card")"'
now="$(D hostname)"
[ "$now" = "$DECK" ] || echo "  NOTE: its card names it $DECK, not $now: $now.local stops answering, and so does a router's DHCP reservation made by name (one by MAC address stays)"

# ---- 1. the board's serial -------------------------------------------------------
say "serial"
full="$(D 'tr -d "\0" < /proc/device-tree/serial-number 2>/dev/null' || true)"
if [[ "$full" =~ ^[0-9a-fA-F]{8,16}$ ]]; then
    serial="0x$(printf '%s' "${full: -8}" | tr 'A-F' 'a-f')"
    python3 - "$UNIT" "$DECK" "$serial" <<'EOF'
import json, sys
from pathlib import Path
path, deck, serial = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
unit = json.loads(path.read_text()) if path.exists() else {
    "deck": deck,
    "about": f"{deck}'s board, given its own section of the release's config.txt by its serial "
             "(pi-qemu/release/prepare-deck.sh). No S3 wiring of its own here: the canonical one.",
}
old = unit.get("serial")
unit["serial"] = serial
path.write_text(json.dumps(unit, indent=2, ensure_ascii=False) + "\n")
print(f"  {serial}: " + ("unchanged in" if old == serial else f"was {old}, now in" if old else "written to") + f" {path.name}")
EOF
else
    echo "  none readable (an emulated deck has none): $UNIT left as it is"
fi

# ---- 2. its ssh host keys --------------------------------------------------------
say "ssh host keys"
if ls "$IDENTITY"/ssh/ssh_host_*_key > /dev/null 2>&1; then
    echo "  $DECK's identity has keys already: kept (pi-qemu/.cache/decks/$DECK/ssh)"
else
    mkdir -p "$IDENTITY/ssh"
    D 'cd /etc/ssh && sudo tar -cf - ssh_host_*' | tar -C "$IDENTITY/ssh" -xf -
    chmod 600 "$IDENTITY"/ssh/ssh_host_*_key
    echo "  copied into pi-qemu/.cache/decks/$DECK/ssh: the card keeps them"
fi
for k in "$IDENTITY"/ssh/ssh_host_*_key.pub; do echo "  $(ssh-keygen -lf "$k")"; done

# ---- 3. its bootloader -------------------------------------------------------------
say "bootloader"
version="$(D 'vcgencmd bootloader_version 2>/dev/null | head -1' || true)"
if [[ "$version" =~ ^([0-9]{4})/([0-9]{2})/([0-9]{2}) ]]; then
    day="${BASH_REMATCH[1]}${BASH_REMATCH[2]}${BASH_REMATCH[3]}"
    if [ "$day" -lt 20221201 ]; then
        echo "  $version: TOO OLD. It would start the fresh card's empty slot B (phase 1, T0)."
        if [ -n "$WATCHDOG" ]; then echo "  --eeprom updates it, below"; else echo "  Run this again with --eeprom to update it."; status=1; fi
    else
        echo "  $version: knows tryboot_a_b"
    fi
    D 'vcgencmd bootloader_config 2>/dev/null | grep -E "^(BOOT_WATCHDOG|BOOT_ORDER|NET_INSTALL)" | sed "s/^/  /"' || true
else
    echo "  none readable (an emulated deck has none)"
fi

# ---- 4. what it runs today, for reference -------------------------------------------
say "captured into ${CAPTURE#"$MAIN"/}"
(umask 077; mkdir -p "$CAPTURE")
D 'sudo tar -czf - --ignore-failed-read -C / boot/firmware/config.txt boot/firmware/cmdline.txt \
    etc/NetworkManager/system-connections etc/netplan var/lib/alsa/asound.state 2>/dev/null' > "$CAPTURE/files.tgz" || true
D 'echo "hostname: $(hostname)"; ip -br link | grep -v "^lo"; echo; vcgencmd bootloader_version 2>/dev/null; \
    sudo rpi-eeprom-config 2>/dev/null; echo; apt-mark showmanual 2>/dev/null' > "$CAPTURE/info.txt" || true
chmod 600 "$CAPTURE"/*
echo "  $(tar -tzf "$CAPTURE/files.tgz" 2>/dev/null | grep -vc '/$') files, and info.txt (MACs, EEPROM config, packages)"
# Its own config.txt lines: what it has that a release's config.txt (Pi OS's,
# with the deck lines every card gets) doesn't, from the newest release built.
echo "  its own config.txt lines, for its unit file (a panel's dtoverlay: panelOverlay; others: configTxt):"
tar -xzOf "$CAPTURE/files.tgz" boot/firmware/config.txt > "$CAPTURE/config.txt" 2>/dev/null || true
base="$(ls -t "$MAIN"/pi-qemu/release/out/*/boot.vfat 2>/dev/null | head -1)"
if [ -s "$CAPTURE/config.txt" ] && [ -n "$base" ]; then
    norm() { sed -e 's/#.*//' -e 's/[[:space:]]*$//' -e '/^$/d' -e '/^\[all\]$/d'; }
    MTOOLS_SKIP_CHECK=1 mtype -i "$base" ::/config.txt | sed '/^# Each deck.s own lines/,$d; /^# ---- release/,$d' | norm | sort -u > "$CAPTURE/.base"
    lines="$(norm < "$CAPTURE/config.txt" | grep -vxF -f "$CAPTURE/.base" || true)"
    rm -f "$CAPTURE/.base"
    if [ -n "$lines" ]; then printf '%s\n' "$lines" | sed 's/^/    /'; else echo "    none: it starts as every deck does"; fi
else
    echo "    (no release built yet to compare with: see config.txt in the capture)"
fi

# ---- --eeprom ------------------------------------------------------------------------
if [ -n "$WATCHDOG" ]; then
    say "the bootloader: updated, with its boot watchdog"
    [[ "$version" =~ ^[0-9]{4}/ ]] || { echo "  no bootloader to update (an emulated deck?)" >&2; exit 1; }
    arm="$(D 'sudo vclog --msg 2>/dev/null | grep -m1 "Starting ARM"' || true)"
    ms="$(printf '%s' "$arm" | sed -n 's/^[[:space:]]*\([0-9][0-9]*\)\..*/\1/p')"; ms=$((10#${ms:-0}))
    [ "$ms" -gt 0 ] || { echo "  no 'Starting ARM' in the firmware's log (vclog --msg): not set" >&2; exit 1; }
    [ "$ms" -lt 30000 ] || { echo "  Linux starts $((ms / 1000)) s after power-on: too close to 45 s, not set" >&2; exit 1; }
    echo "  Linux starts $((ms / 100 / 10)).$((ms / 100 % 10)) s after power-on: well within 45 s"
    old="$(D 'cat /proc/sys/kernel/random/boot_id')"
    D 'set -e
        sudo rpi-eeprom-config > /tmp/boot.conf
        if grep -qx BOOT_WATCHDOG_TIMEOUT=45 /tmp/boot.conf && grep -qx BOOT_WATCHDOG_PARTITION=2 /tmp/boot.conf &&
                ! sudo rpi-eeprom-update | grep -q "UPDATE AVAILABLE"; then
            echo "  current, and the watchdog already set"; exit 3
        fi
        sed -i "/^BOOT_WATCHDOG_\(TIMEOUT\|PARTITION\)=/d" /tmp/boot.conf
        printf "BOOT_WATCHDOG_TIMEOUT=45\nBOOT_WATCHDOG_PARTITION=2\n" >> /tmp/boot.conf
        if [ -e /etc/rauc/system.conf ]; then
            # An A/B card: the boot ROM reads recovery.bin from p1, not from the
            # slot mounted at /boot/firmware.
            sudo mount /dev/disk/by-partuuid/5d0bc1ec-01 /mnt
            sudo env BOOTFS=/mnt rpi-eeprom-config --apply /tmp/boot.conf
            sync; sudo umount /mnt
        else
            sudo rpi-eeprom-config --apply /tmp/boot.conf
        fi
        echo "  staged; rebooting to flash it"
        sudo systemd-run --quiet --on-active=2 systemctl reboot' && rc=0 || rc=$?
    [ "$rc" = 3 ] && exit 0
    [ "$rc" = 0 ] || { echo "  staging failed" >&2; exit 1; }
    t0=$(date +%s)
    until b="$(D 'cat /proc/sys/kernel/random/boot_id' 2>/dev/null)" && [ -n "$b" ] && [ "$b" != "$old" ]; do
        [ $(( $(date +%s) - t0 )) -lt 240 ] || { echo "  no answer 240 s after the reboot: check the deck" >&2; exit 1; }
        sleep 5
    done
    echo "  back after $(( $(date +%s) - t0 )) s"
    version="$(D 'vcgencmd bootloader_version | head -1')"
    day="$(printf '%s' "$version" | sed -n 's|^\([0-9]\{4\}\)/\([0-9][0-9]\)/\([0-9][0-9]\).*|\1\2\3|p')"
    echo "  bootloader $version"
    D 'vcgencmd bootloader_config | grep "^BOOT_WATCHDOG" | sed "s/^/    /"'
    if [ "${day:-0}" -lt 20221201 ] || ! D 'vcgencmd bootloader_config | grep -qx BOOT_WATCHDOG_PARTITION=2'; then
        echo "  the flash didn't take: check the deck before swapping its card" >&2; exit 1
    fi
    status=0
fi

say "next"
cat <<EOF
  When every deck is prepared: commit the unit files, tag, build the release,
  and make each card (PLAN.md §7):
    git add mixxx_config/units && git commit -m "units: the decks' serials"
    git tag -m "TriMixxx X.Y.Z" pi/vX.Y.Z
    make -C pi-qemu/release release SSH_KEY=\$HOME/.ssh/with_pass/rsa_sam
    make -C pi-qemu/release card DECK=$DECK
EOF
exit $status
