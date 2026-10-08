#!/usr/bin/env bash
# Deploy step 001 (lib.sh has the contract): the serial <-> MIDI bridge,
# mixxx_config/ttymidi (a submodule), as a static arm64 binary built in
# Docker, into /usr/local/bin.
#
# The same binary is left alone. A new one restarts the bridge if it runs
# (on a fresh deck its unit only comes with 002_system), and Mixxx, which
# opened the bridge's port at its start, then needs a new session: exit 11.
# needs: docker
. "$(dirname "$0")/lib.sh"

make -C mixxx_config/ttymidi docker-arm
scp mixxx_config/ttymidi/dist/ttymidi deck:/tmp/ttymidi.new
outcome="$(ssh deck 'set -e
    if cmp -s /tmp/ttymidi.new /usr/local/bin/ttymidi; then
        rm -f /tmp/ttymidi.new; echo same; exit
    fi
    sudo install -m 0755 /tmp/ttymidi.new /usr/local/bin/ttymidi
    rm -f /tmp/ttymidi.new
    if systemctl is-active --quiet trimixxx-bridge.service; then
        sudo systemctl restart trimixxx-bridge.service; echo restarted
    else
        echo installed
    fi')"
echo "==> deck:/usr/local/bin/ttymidi: $outcome"
[ "$outcome" = restarted ] && exit $RESTART_SESSION
exit $DONE
