#!/usr/bin/env bash
# Set a deck up, or bring it up to date: every piece of the deck's software, in
# pi_config/fresh-install.md's order, by the repo's own deploy scripts.
#
#   pi-qemu/deploy.sh HOST [STEP...]
#
# HOST is an ssh alias: a real deck (trimixxx2) or an emulated one. The same
# commands either way -- this is also what image/build.sh runs to make a card.
# Every step is idempotent: run it again and it brings the deck back to what
# the repo says, and does nothing where nothing changed. It never reboots; if
# the boot flags changed, it says so at the end.
#
# Steps (all, in this order, when none are named):
#   base      fresh-install.md 1-2: boot flags, ssh, background load, packages, autologin
#   ttymidi   the serial<->MIDI bridge binary (mixxx_config/ttymidi)
#   system    pi_config/upload.sh: systemd units, session, splash, eth0, Wi-Fi fallback, dj-usb
#   launcher  trimixxx-launcher: boot modes, SysEx daemon, deck keys
#   mixxx     the Mixxx fork over apt's binary (mixxx/upload.sh, a Docker build)
#   config    mixxx_config/upload.sh: mapping, skin, fonts, mixxx.cfg, this deck's wiring
#   library   answer "Choose music library directory" with ~/Music, once
#   doom      doom/install.sh
#
# The deck's wiring, accent and panel come from mixxx_config/units/<hostname>.json.
set -euo pipefail

HOST="${1:?usage: deploy.sh HOST [STEP...]}"
shift
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ALL=(base ttymidi system launcher mixxx config library doom)
STEPS=("$@")
[ "${#STEPS[@]}" -gt 0 ] || STEPS=("${ALL[@]}")
for s in "${STEPS[@]}"; do
    [[ " ${ALL[*]} " == *" $s "* ]] || { echo "unknown step: $s (steps: ${ALL[*]})" >&2; exit 2; }
done

say() { printf '\n==> [%s] %s: %s\n' "$(date +%H:%M:%S)" "$HOST" "$*"; }
reboot_needed=0

DECK="$(ssh -o ConnectTimeout=10 "$HOST" hostname)" || { echo "cannot reach $HOST" >&2; exit 1; }
UNIT="$REPO/mixxx_config/units/$DECK.json"
unit() { [ -f "$UNIT" ] && python3 -c "import json,sys; print(json.load(open(sys.argv[1])).get(sys.argv[2], ''))" "$UNIT" "$1" || true; }
say "deck $DECK, steps: ${STEPS[*]}"

for step in "${STEPS[@]}"; do
    case "$step" in
    base)
        say "base (fresh-install.md 1-2)"
        out="$(ssh "$HOST" "sudo PANEL_OVERLAY='$(unit panelOverlay)' bash -s" < "$REPO/pi-qemu/deploy/base.sh" | tee /dev/stderr)"
        [[ "$out" == *"REBOOT NEEDED"* ]] && reboot_needed=1
        ;;
    ttymidi)
        say "ttymidi"
        # SERVICE= on a fresh unit: the bridge unit is not there yet, and
        # try-restart exits 5 for a missing unit (fresh-install.md 3.1).
        if ssh "$HOST" 'systemctl cat trimixxx-bridge.service >/dev/null 2>&1'; then
            make -C "$REPO/mixxx_config/ttymidi" install-remote HOST="$HOST"
        else
            make -C "$REPO/mixxx_config/ttymidi" install-remote HOST="$HOST" SERVICE=
        fi
        ;;
    system)
        say "system (pi_config/upload.sh)"
        HOST="$HOST" "$REPO/pi_config/upload.sh"
        ;;
    launcher)
        say "launcher"
        make -C "$REPO/trimixxx-launcher" install-remote HOST="$HOST"
        ;;
    mixxx)
        say "Mixxx fork"
        HOST="$HOST" "$REPO/mixxx/upload.sh"
        ;;
    config)
        say "Mixxx config (mixxx_config/upload.sh)"
        ssh "$HOST" 'mkdir -p ~/Music ~/.mixxx'
        (cd "$REPO/mixxx_config" && HOST="$HOST" ./upload.sh)
        ;;
    library)
        # Mixxx asks "Choose music library directory" at every start until its
        # database names one: database state, not mixxx.cfg (fresh-install.md
        # 3.4). A deck that already has one keeps it.
        say "music library directory"
        ssh "$HOST" 'set -e
            for i in $(seq 1 60); do [ -s ~/.mixxx/mixxxdb.sqlite ] && break; sleep 2; done
            [ -s ~/.mixxx/mixxxdb.sqlite ] || { echo "no Mixxx database yet: start Mixxx once, then rerun this step"; exit 1; }
            n=$(sqlite3 ~/.mixxx/mixxxdb.sqlite "SELECT COUNT(*) FROM directories")
            if [ "$n" = 0 ]; then
                sudo systemctl stop getty@tty1
                sqlite3 ~/.mixxx/mixxxdb.sqlite "INSERT INTO directories (directory) VALUES ('"'"'/home/sam1902/Music'"'"')"
                sudo systemctl start getty@tty1
            fi
            echo "library: $(sqlite3 ~/.mixxx/mixxxdb.sqlite "SELECT directory FROM directories")"'
        ;;
    doom)
        say "Doom"
        HOST="$HOST" "$REPO/doom/install.sh"
        ;;
    esac
done

say "done"
[ "$reboot_needed" = 1 ] && echo "The boot flags changed: they apply from $HOST's next reboot (ssh $HOST sudo reboot)."
exit 0
