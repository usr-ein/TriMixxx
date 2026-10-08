#!/usr/bin/env bash
# Deploy step 003 (lib.sh has the contract): trimixxx-launcher -- the boot
# modes, the SysEx daemon and the deck's keys -- as static arm64 binaries
# built in Docker, and its unit.
#
# Left alone when nothing changed. Restarted otherwise: it sees the mode file
# exists and skips the boot gesture, so the session goes on, but it drops and
# recreates its virtual MIDI port, which a running Mixxx opened at its start
# and has now lost: exit 11, and the session restarts once the steps are over.
# needs: docker
. "$(dirname "$0")/lib.sh"

make -C trimixxx-launcher docker-arm
scp trimixxx-launcher/dist/trimixxx-launchd trimixxx-launcher/dist/trimixxx-deckkeys \
    trimixxx-launcher/trimixxx-launchd.service deck:/tmp/
outcome="$(ssh deck 'set -eu
    same=1
    for f in trimixxx-launchd trimixxx-deckkeys; do cmp -s /tmp/$f /usr/local/bin/$f || same=0; done
    cmp -s /tmp/trimixxx-launchd.service /etc/systemd/system/trimixxx-launchd.service || same=0
    systemctl is-active --quiet trimixxx-launchd.service || same=0
    if [ "$same" = 1 ]; then
        rm -f /tmp/trimixxx-launchd /tmp/trimixxx-deckkeys /tmp/trimixxx-launchd.service
        echo same; exit
    fi
    sudo install -m 0755 /tmp/trimixxx-launchd /tmp/trimixxx-deckkeys /usr/local/bin/
    sudo install -m 0644 /tmp/trimixxx-launchd.service /etc/systemd/system/
    rm -f /tmp/trimixxx-launchd /tmp/trimixxx-deckkeys /tmp/trimixxx-launchd.service
    if systemctl list-unit-files | grep -q "^pi-midi-daemon.service"; then
        echo "removing the old pi-midi-daemon.service: this daemon replaces it" >&2
        sudo systemctl disable --now pi-midi-daemon.service >&2 || true
        sudo rm -f /etc/systemd/system/pi-midi-daemon.service /usr/local/bin/pi-midi-daemon
    fi
    sudo systemctl daemon-reload
    sudo systemctl enable --quiet trimixxx-launchd.service
    sudo systemctl restart trimixxx-launchd.service
    echo restarted')"
ssh deck 'systemctl --no-pager status trimixxx-launchd.service | head -8; echo "mode: $(cat /run/trimixxx/mode 2>/dev/null)"'
echo "==> the launcher: $outcome"
[ "$outcome" = restarted ] && exit $RESTART_SESSION
exit $DONE
