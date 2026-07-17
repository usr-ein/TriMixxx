#!/usr/bin/env bash
# Deploy the deck Pi's SYSTEM-level config: systemd units + udev rules that need
# sudo on the far side. This is the counterpart to mixxx_config/upload.sh, which
# only ever touches ~/.mixxx (user-space, no sudo, restarts Mixxx). Splitting the
# two means a routine mapping tweak never risks the deck's system config, and a
# system change never has to go through the Mixxx-restart path.
#
# Everything here is idempotent -- safe to re-run. Installs:
#   * cpu-governor.service     -- pin cores to `performance` (low-latency audio)
#   * trimixxx-bridge.service  -- ttymidi serial<->MIDI bridge (gates Mixxx boot)
#   * dj-usb/*                 -- USB auto-mount (delegated to its own installer)
set -eux

# ssh alias for the deck's Pi. Override for a one-off: HOST=other ./upload.sh
HOST="${HOST:-trimixxx-pi}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---- systemd units (governor + bridge) ---------------------------------------
# Shipped together: both are plain unit files installed into /etc/systemd/system
# the same way. The bridge is restarted to pick up edits; the governor is a
# oneshot, so enable --now both installs and applies it.
scp "$HERE/cpu-governor.service" "$HERE/trimixxx-bridge.service" "$HOST":/tmp/
ssh "$HOST" '
    set -eux
    sudo install -m 0644 /tmp/cpu-governor.service    /etc/systemd/system/cpu-governor.service
    sudo install -m 0644 /tmp/trimixxx-bridge.service /etc/systemd/system/trimixxx-bridge.service
    rm -f /tmp/cpu-governor.service /tmp/trimixxx-bridge.service
    sudo systemctl daemon-reload

    sudo systemctl enable --now cpu-governor.service
    # Confirm the governor actually took (prints "performance").
    cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor

    # The bridge gates getty@tty1 (hence Mixxx) at boot; enable it, and restart
    # so an edited unit takes effect now. Restarting the bridge does not restart
    # Mixxx -- the virtual MIDI port just drops and reappears.
    sudo systemctl enable trimixxx-bridge.service
    sudo systemctl restart trimixxx-bridge.service
'

# ---- DJ USB auto-mount -------------------------------------------------------
# Self-contained installer (its own scp + sudo dance, incl. udev reload/trigger).
# Pass HOST through explicitly -- it is a local var here, not exported.
HOST="$HOST" "$HERE/dj-usb/install.sh"

echo "Pi system config uploaded."
