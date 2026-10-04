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
#   * trimixxx-lights-off.*    -- every button light off at shutdown
#   * trimixxx-swap-sizes.conf, panel drivers in the initramfs -- boot speed
#   * 99-prolink-ports.conf    -- let Mixxx bind UDP/111 (Pro DJ Link serving)
#   * getty-tty1-stop-mixxx.conf -- quit Mixxx before the session (and X) go
#   * trimixxx-splash.*        -- logo on the panel for the first seconds of boot
#   * prolink-eth0.sh          -- eth0 to IPv4 link-local, for the CDJ network
#   * wifi-fallback/*          -- the deck's own hotspot when there is no Wi-Fi
#   * ~/.xinitrc               -- the X session startx runs (WM + Mixxx loop)
#   * ~/.bash_profile, trimixxx-debug, rescue-* -- the session entry point and
#                                 the rescue console (hold CUE at boot)
#   * dj-usb/*                 -- USB auto-mount (delegated to its own installer)
set -eux

# ssh alias for the deck's Pi. Override for a one-off: HOST=other ./upload.sh
HOST="${HOST:-trimixxx-pi}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---- preflight -----------------------------------------------------------------
# The bridge is enabled and RESTARTED below, but its binary is not installed by
# this script -- it comes from the ttymidi submodule. On a fresh unit it is not
# there yet, so the restart fails and set -e stops this script half-way, with
# some pieces installed and the rest not. Check before changing anything.
ssh "$HOST" 'test -x /usr/local/bin/ttymidi' || {
    echo "ABORT: $HOST has no /usr/local/bin/ttymidi. Install it first:" >&2
    echo "       make -C $HERE/../mixxx_config/ttymidi install-remote HOST=$HOST" >&2
    exit 1
}


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
    # Confirm the governor actually took (prints "performance"). An emulated
    # deck (pi-qemu) has no cpufreq; the unit stands down there.
    g=/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
    if [ -e "$g" ]; then cat "$g"; else echo "no cpufreq here: governor left alone"; fi

    # The bridge gates getty@tty1 (hence Mixxx) at boot; enable it, and restart
    # so an edited unit takes effect now. Restarting the bridge does not restart
    # Mixxx -- the virtual MIDI port just drops and reappears.
    sudo systemctl enable trimixxx-bridge.service
    sudo systemctl restart trimixxx-bridge.service
'

# ---- button lights off at shutdown -------------------------------------------
# The S3 outlives the Pi -- it stays powered after a halt -- so without this a
# deck that was shut down mid-track keeps its PLAY lamp and pads lit. The unit
# does its work when it STOPS (see its header for the ordering that makes that
# land between Mixxx going and ttymidi going), so it is started but never
# restarted here: a restart would black the deck out under a running Mixxx.
# The script is read at shutdown, so an edited one needs no restart anyway.
scp "$HERE/trimixxx-lights-off" "$HERE/trimixxx-lights-off.service" "$HOST":/tmp/
ssh "$HOST" '
    set -eux
    sudo install -m 0755 /tmp/trimixxx-lights-off         /usr/local/bin/trimixxx-lights-off
    sudo install -m 0644 /tmp/trimixxx-lights-off.service /etc/systemd/system/trimixxx-lights-off.service
    rm -f /tmp/trimixxx-lights-off /tmp/trimixxx-lights-off.service
    sudo systemctl daemon-reload
    sudo systemctl enable trimixxx-lights-off.service
    sudo systemctl start trimixxx-lights-off.service
'

# ---- boot speed: swap sizes, panel drivers in the initramfs -------------------
# Both explained in fresh-install.md (1.4), where the config.txt and EEPROM
# halves of the same work are, as manual steps.
#
# The swap drop-in pins what rpi-swap computes anyway, so its generator stops
# running Perl on every boot.
#
# The initramfs already carries vc4 and tries to start the display there, but
# without the DSI panel driver and the I2C mux in front of it, vc4 has to wait
# for the root filesystem: the panel came up at ~6 s instead of ~1.7 s. The
# standard place to add them is /etc/initramfs-tools/modules, which every kernel
# update re-reads. Only on a deck with this panel that boots through an
# initramfs, and the initramfs is only rebuilt when the list changed.
scp "$HERE/trimixxx-swap-sizes.conf" "$HOST":/tmp/
ssh "$HOST" '
    set -eux
    sudo install -d -m 0755 /etc/rpi/swap.conf.d
    sudo install -m 0644 /tmp/trimixxx-swap-sizes.conf /etc/rpi/swap.conf.d/trimixxx-swap-sizes.conf
    rm -f /tmp/trimixxx-swap-sizes.conf

    list=/etc/initramfs-tools/modules
    if grep -q "^dtoverlay=vc4-kms-dsi-waveshare-panel" /boot/firmware/config.txt &&
       grep -q "^auto_initramfs=1" /boot/firmware/config.txt; then
        changed=0
        for m in i2c_mux_pinctrl panel_waveshare_dsi; do
            grep -qx "$m" "$list" && continue
            if [ "$changed" = 0 ]; then
                echo "# TriMixxx: the DSI panel and its I2C mux, see pi_config/fresh-install.md 1.4" |
                    sudo tee -a "$list" >/dev/null
            fi
            echo "$m" | sudo tee -a "$list" >/dev/null
            changed=1
        done
        if [ "$changed" = 1 ]; then
            sudo update-initramfs -u -k "$(uname -r)"
        fi
    fi
'

# ---- unprivileged port floor -------------------------------------------------
# Lets Mixxx bind UDP/111 (the RPC portmapper) without root, which Pro DJ Link
# serving requires -- a CDJ asks the portmapper for the mountd/nfsd ports before
# it will even list us as a source, and retries forever if nothing answers.
# The unit file explains why this rather than setcap. Applied immediately as
# well as installed, so serving works without a reboot.
scp "$HERE/99-prolink-ports.conf" "$HOST":/tmp/
ssh "$HOST" '
    set -eux
    sudo install -m 0644 /tmp/99-prolink-ports.conf /etc/sysctl.d/99-prolink-ports.conf
    rm -f /tmp/99-prolink-ports.conf
    sudo sysctl --system >/dev/null
    # Confirm it took (prints "111"). A kernel older than 4.11 has no such knob.
    # Read /proc rather than `sysctl -n`: sysctl lives in /usr/sbin, which is not
    # on the PATH of a non-login ssh shell (the sudo above only works because
    # sudo has its own secure_path).
    cat /proc/sys/net/ipv4/ip_unprivileged_port_start
'

# ---- font fallback -----------------------------------------------------------
# The deck's UI font (MesloLGL Nerd Font, shipped by mixxx_config/upload.sh) is a
# terminal font: ~13k codepoints, no CJK, no Arabic or Hebrew, no Indic scripts,
# no emoji. Mixxx can only be told ONE family name, so everything else in a track
# title is resolved by fontconfig against whatever is installed -- which on a
# stock Raspberry Pi OS is close to nothing, hence tofu boxes.
#
# Two halves, and both are needed: the Noto families to fall back TO, and the
# conf.d rule saying to fall back to them in that order.
#
# Sizes are the reason this is spelled out rather than `apt install fonts-noto`:
# the metapackage pulls every script Noto has. These four are the ones a track
# title actually hits. fonts-noto-extra (Tibetan, Yi, and the other rare scripts)
# is deliberately NOT here -- add it if you want them, it is a few hundred MB.
#
# Validate the XML locally first: fontconfig ignores a malformed conf.d file with
# only a warning on stderr that nothing on the deck will ever show you.
python3 -c "import xml.dom.minidom,sys; xml.dom.minidom.parse('$HERE/60-trimixxx-fonts.conf')" \
    || { echo "ABORT: 60-trimixxx-fonts.conf is not well-formed XML." >&2; exit 1; }
scp "$HERE/60-trimixxx-fonts.conf" "$HOST":/tmp/
ssh "$HOST" '
    set -eux
    sudo apt-get update -qq
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        fonts-noto-core fonts-noto-cjk fonts-noto-color-emoji fonts-dejavu-core
    sudo install -m 0644 /tmp/60-trimixxx-fonts.conf /etc/fonts/conf.d/60-trimixxx-fonts.conf
    rm -f /tmp/60-trimixxx-fonts.conf
    sudo fc-cache -f >/dev/null

    # Prove the chain resolves rather than trusting the install. fc-match reports
    # which font a request actually lands on, so asking for the deck UI family
    # with a CJK and an emoji character present is the real end-to-end check.
    # Each must NOT come back as the default sans -- if it does, the package or
    # the conf.d rule did not take.
    fc-match "MesloLGL Nerd Font" >/dev/null
    fc-match -s "MesloLGL Nerd Font" | head -8
'

# ---- X session ---------------------------------------------------------------
# The session startx runs on tty1 login: no screen blanking, a minimal WM, and
# the Mixxx restart loop. Deliberately not a system file -- it belongs to the
# login user, so no sudo here. startx *execs* ~/.xinitrc rather than sourcing
# it, so the exec bit is load-bearing: without it X comes up to a grey screen
# and no Mixxx. Takes effect on the next tty1 login (or `sudo systemctl restart
# getty@tty1`), not immediately.
scp "$HERE/xinitrc" "$HOST":'~/.xinitrc'
ssh "$HOST" 'chmod 0755 ~/.xinitrc'

# ---- session entry point: which KIND of session ------------------------------
# ~/.bash_profile is what the autologin shell runs. It reads /run/trimixxx/mode
# and either starts X (Mixxx or Doom, ~/.xinitrc picks) or runs the debug
# console on the bare tty. See the file's own header, and
# ../trimixxx-launcher/README.md for who writes that mode file.
#
# Backed up rather than overwritten, and ~/.profile is checked out loud. Bash
# reads ~/.bash_profile INSTEAD of ~/.profile for a login shell, so the new file
# sources ~/.profile itself to keep PATH and ~/.local/bin/env working -- which
# means a `startx` line left in ~/.profile would now run BEFORE the mode is
# looked at, and the boot gesture would silently do nothing.
#
# The rescue console rides along: trimixxx-debug, the touch session it starts
# (rescue-session: a terminal beside an on-screen keyboard), and that
# keyboard's layout. A malformed layout crashes the keyboard and leaves only
# the bare-console fallback, so it is checked here first, like the fonts rule.
python3 -c "import xml.dom.minidom; xml.dom.minidom.parse('$HERE/rescue-keyboard.xml')" \
    || { echo "ABORT: rescue-keyboard.xml is not well-formed XML." >&2; exit 1; }
scp "$HERE/bash_profile" "$HOST":/tmp/bash_profile
scp "$HERE/trimixxx-debug" "$HERE/rescue-session" "$HERE/rescue-keyboard.xml" "$HOST":/tmp/
ssh "$HOST" '
    set -eu
    if [ -f ~/.bash_profile ] && ! grep -q "TriMixxx" ~/.bash_profile; then
        cp ~/.bash_profile ~/.bash_profile.pre-trimixxx
        echo "backed up the previous ~/.bash_profile to ~/.bash_profile.pre-trimixxx"
    fi
    install -m 0644 /tmp/bash_profile ~/.bash_profile
    rm -f /tmp/bash_profile

    if grep -qs startx ~/.profile; then
        echo
        echo "WARNING: ~/.profile contains a startx line. ~/.bash_profile sources"
        echo "         ~/.profile, so X would start from there BEFORE the boot mode"
        echo "         is read -- and holding PLAY at boot would do nothing."
        echo "         Remove it: starting X is the job of ~/.bash_profile now."
        grep -n startx ~/.profile || true
        echo
    fi

    # The rescue console (hold CUE at boot): a shell on the touchscreen, in a
    # terminal beside an on-screen keyboard, or on the bare console when X
    # will not start. See trimixxx-debug and rescue-session.
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends \
        xterm matchbox-keyboard >/dev/null
    sudo install -m 0755 /tmp/trimixxx-debug /usr/local/bin/trimixxx-debug
    sudo install -d -m 0755 /usr/local/lib/trimixxx
    sudo install -m 0755 /tmp/rescue-session /usr/local/lib/trimixxx/rescue-session
    # The layout under the DEFAULT name: this matchbox-keyboard crashes when
    # given a layout by name (see rescue-session).
    install -d -m 0755 ~/.matchbox
    install -m 0644 /tmp/rescue-keyboard.xml ~/.matchbox/keyboard.xml
    rm -f /tmp/trimixxx-debug /tmp/rescue-session /tmp/rescue-keyboard.xml
'

# ---- clean Mixxx shutdown on `systemctl stop getty@tty1` ---------------------
# Mixxx handles the termination signal itself, but X lives in the same session
# scope and dies in the same instant, aborting the shutdown part-way. This
# drop-in stops Mixxx first and waits for it. See the file's own comment.
scp "$HERE/getty-tty1-stop-mixxx.conf" "$HOST":/tmp/
ssh "$HOST" '
    set -eux
    sudo mkdir -p /etc/systemd/system/getty@tty1.service.d
    sudo install -m 0644 /tmp/getty-tty1-stop-mixxx.conf \
        /etc/systemd/system/getty@tty1.service.d/10-trimixxx-stop-mixxx.conf
    rm -f /tmp/getty-tty1-stop-mixxx.conf
    sudo systemctl daemon-reload
    # Deliberately NOT restarting getty@tty1: that would kill the running Mixxx,
    # which is rarely what someone deploying config wants. The drop-in applies
    # to the next stop either way.
    systemctl show -p ExecStop --value getty@tty1.service | head -2
'

# ---- boot splash -------------------------------------------------------------
# The deck's logo on the panel for the first seconds of boot, then the boot log
# as usual -- the splash sits on a spare VT rather than hiding the console, so
# nothing is suppressed. Self-contained installer: it reads the panel's
# framebuffer geometry off the Pi and renders the SVG to match, here, so the
# deck needs no image tooling. Deliberately not shown now (`--test` does that on
# demand) -- taking the screen away from a running Mixxx mid-deploy is rude.
HOST="$HOST" "$HERE/splash-install.sh"

# ---- eth0 for the Pro DJ Link network ----------------------------------------
# A Pro DJ Link network has no DHCP server, so eth0 needs an IPv4 link-local
# address or the CDJs' broadcasts to 169.254.255.255 are discarded at the IP
# layer and Mixxx sees nothing. Self-contained and eth0-only; it verifies wlan0
# and the default route are unchanged before returning. See the script's header.
HOST="$HOST" "$HERE/prolink-eth0.sh"

# ---- Wi-Fi fallback ----------------------------------------------------------
# The deck's own hotspot, named after its hostname, when no Wi-Fi has been
# joined 45 s into a boot -- the way in at a venue. Self-contained and inert
# until the next boot; it verifies wlan0 and the default route are unchanged.
# See wifi-fallback/README.md.
HOST="$HOST" "$HERE/wifi-fallback/install.sh"

# ---- DJ USB auto-mount -------------------------------------------------------
# Self-contained installer (its own scp + sudo dance, incl. udev reload/trigger).
# Pass HOST through explicitly -- it is a local var here, not exported.
HOST="$HOST" "$HERE/dj-usb/install.sh"

echo "Pi system config uploaded."
