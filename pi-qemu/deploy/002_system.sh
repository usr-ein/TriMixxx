#!/usr/bin/env bash
# Deploy step 002 (lib.sh has the contract): the deck's system, from
# pi_config/ -- what needs sudo on the deck. 005_config is its user-space
# counterpart (~/.mixxx), so a mapping tweak never touches the system and a
# system change never goes through Mixxx's restart.
#
#   cpu-governor.service        cores pinned to `performance` (low-latency audio)
#   trimixxx-bridge.service     the ttymidi bridge, which gates Mixxx at boot
#   trimixxx-lights-off.*       every button light off at shutdown
#   swap sizes, panel drivers in the initramfs: boot speed
#   99-prolink-ports.conf       Mixxx may bind UDP/111 (Pro DJ Link serving)
#   font fallback               Noto behind the deck's terminal font
#   ~/.xinitrc, ~/.bash_profile the X session and what starts it; the rescue console
#   getty-tty1-stop-mixxx.conf  Mixxx quits before the session (and X) go
#   a release card's pieces     identity, health check, RAUC's backend (inert here)
#   the boot splash             the logo on the panel for the first seconds
#   eth0                        IPv4 link-local, for the CDJs' network
#   the Wi-Fi fallback          the deck's own hotspot when it finds no Wi-Fi
#   dj-usb                      USB sticks mounted as DJ_USB_1/2
#
# The session restarts at the end of the run (exit 11): the bridge restarted
# here, and the session files take effect from the next session.
. "$(dirname "$0")/lib.sh"

P=pi_config

# ---- preflight -------------------------------------------------------------------
# The bridge is restarted below, but its binary comes from 001_ttymidi: without
# it the restart fails half-way through, with some pieces installed and the
# rest not. Checked before anything changes.
ssh deck 'test -x /usr/local/bin/ttymidi' || {
    echo "the deck has no /usr/local/bin/ttymidi: deploy step 001_ttymidi first" >&2
    exit 1
}
# A malformed fontconfig rule is ignored with a warning nothing on the deck
# shows; a malformed keyboard layout crashes the rescue console's keyboard.
for f in $P/60-trimixxx-fonts.conf $P/rescue-keyboard.xml; do
    python3 -c "import sys, xml.dom.minidom; xml.dom.minidom.parse(sys.argv[1])" "$f" \
        || { echo "$f is not well-formed XML" >&2; exit 1; }
done

# ---- systemd units: the CPU governor and the bridge -----------------------------
# The governor is a oneshot, so enable --now installs and applies it. The bridge
# gates getty@tty1 (hence Mixxx) at boot; restarted so an edited unit takes
# effect now. That drops and recreates its MIDI port under a running Mixxx,
# hence this step's exit 11.
scp $P/cpu-governor.service $P/trimixxx-bridge.service deck:/tmp/
ssh deck '
    set -eux
    sudo install -m 0644 /tmp/cpu-governor.service    /etc/systemd/system/cpu-governor.service
    sudo install -m 0644 /tmp/trimixxx-bridge.service /etc/systemd/system/trimixxx-bridge.service
    rm -f /tmp/cpu-governor.service /tmp/trimixxx-bridge.service
    sudo systemctl daemon-reload
    sudo systemctl enable --now cpu-governor.service
    # "performance"; an emulated deck has no cpufreq, and the unit stands down there.
    g=/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
    if [ -e "$g" ]; then cat "$g"; else echo "no cpufreq here: governor left alone"; fi
    sudo systemctl enable trimixxx-bridge.service
    sudo systemctl restart trimixxx-bridge.service
'

# ---- button lights off at shutdown --------------------------------------------
# The S3 stays powered after a halt, so a deck shut down mid-track would keep
# its lights on. The unit does its work when it STOPS (its header has the
# ordering), so it is started but never restarted here: a restart would black
# the deck out under a running Mixxx. The script is read at shutdown, so an
# edited one needs no restart anyway.
scp $P/trimixxx-lights-off $P/trimixxx-lights-off.service deck:/tmp/
ssh deck '
    set -eux
    sudo install -m 0755 /tmp/trimixxx-lights-off         /usr/local/bin/trimixxx-lights-off
    sudo install -m 0644 /tmp/trimixxx-lights-off.service /etc/systemd/system/trimixxx-lights-off.service
    rm -f /tmp/trimixxx-lights-off /tmp/trimixxx-lights-off.service
    sudo systemctl daemon-reload
    sudo systemctl enable trimixxx-lights-off.service
    sudo systemctl start trimixxx-lights-off.service
'

# ---- boot speed: swap sizes, panel drivers in the initramfs -------------------
# fresh-install.md 1.4. The swap drop-in pins what rpi-swap computes anyway, so
# its generator stops running Perl at every boot. The DSI panel's driver and
# its I2C mux go into the initramfs, which carries vc4 already: without them
# the panel came up at ~6 s instead of ~1.7 s. Only on a deck with this panel
# that boots through an initramfs; rebuilt only when the list changed.
scp $P/trimixxx-swap-sizes.conf deck:/tmp/
ssh deck '
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
        if [ "$changed" = 1 ]; then sudo update-initramfs -u -k "$(uname -r)"; fi
    fi
'

# ---- the unprivileged port floor ---------------------------------------------
# Lets Mixxx bind UDP/111 (the RPC portmapper) without root, which Pro DJ Link
# serving needs: a CDJ asks the portmapper before it lists us as a source. The
# file explains why this rather than setcap. Applied now as well. /proc rather
# than `sysctl -n`: sysctl is not on a non-login ssh shell's PATH.
scp $P/99-prolink-ports.conf deck:/tmp/
ssh deck '
    set -eux
    sudo install -m 0644 /tmp/99-prolink-ports.conf /etc/sysctl.d/99-prolink-ports.conf
    rm -f /tmp/99-prolink-ports.conf
    sudo sysctl --system >/dev/null
    cat /proc/sys/net/ipv4/ip_unprivileged_port_start
'

# ---- font fallback --------------------------------------------------------------
# The UI font (MesloLGL Nerd Font, from 005_config) has no CJK, Arabic, Hebrew,
# Indic or emoji, and Mixxx takes one family name: everything else in a track
# title falls back through fontconfig, onto nearly nothing on stock Pi OS. The
# Noto families to fall back to, and the rule saying to. Four families, not
# fonts-noto, which pulls every script there is (fonts-noto-extra: a few
# hundred MB more, if the rare scripts are wanted). fc-match proves the chain.
scp $P/60-trimixxx-fonts.conf deck:/tmp/
ssh deck '
    set -eux
    sudo apt-get update -qq
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        fonts-noto-core fonts-noto-cjk fonts-noto-color-emoji fonts-dejavu-core
    sudo install -m 0644 /tmp/60-trimixxx-fonts.conf /etc/fonts/conf.d/60-trimixxx-fonts.conf
    rm -f /tmp/60-trimixxx-fonts.conf
    sudo fc-cache -f >/dev/null
    fc-match "MesloLGL Nerd Font" >/dev/null
    fc-match -s "MesloLGL Nerd Font" | head -8
'

# ---- the X session ------------------------------------------------------------------
# What startx runs on tty1: no blanking, a minimal window manager, the Mixxx
# restart loop. The login user's, so no sudo. startx execs it, so the exec bit
# is load-bearing: without it X comes up grey, with no Mixxx.
scp $P/xinitrc deck:.xinitrc
ssh deck 'chmod 0755 ~/.xinitrc'

# ---- the session's entry point, and the rescue console -------------------------
# ~/.bash_profile is what the autologin shell runs: it reads /run/trimixxx/mode
# and starts X (Mixxx or Doom) or the debug console (its header; and
# trimixxx-launcher/README.md). Bash reads it INSTEAD of ~/.profile, and it
# sources ~/.profile itself, so a startx left in ~/.profile would run before
# the mode is read: the boot gesture would do nothing. The rescue console
# (hold CUE at boot): a shell in a terminal beside an on-screen keyboard, or
# on the bare console when X will not start.
scp $P/bash_profile deck:/tmp/bash_profile
scp $P/trimixxx-debug $P/rescue-session $P/rescue-keyboard.xml deck:/tmp/
ssh deck '
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

# ---- Mixxx quits before the session goes ----------------------------------------
# X lives in the session's scope and dies in the same instant as Mixxx, which
# aborts Mixxx's shutdown part-way: the drop-in stops Mixxx first and waits.
# Not restarting getty@tty1 for it: the drop-in applies to the next stop.
scp $P/getty-tty1-stop-mixxx.conf deck:/tmp/
ssh deck '
    set -eux
    sudo mkdir -p /etc/systemd/system/getty@tty1.service.d
    sudo install -m 0644 /tmp/getty-tty1-stop-mixxx.conf \
        /etc/systemd/system/getty@tty1.service.d/10-trimixxx-stop-mixxx.conf
    rm -f /tmp/getty-tty1-stop-mixxx.conf
    sudo systemctl daemon-reload
    systemctl show -p ExecStop --value getty@tty1.service | head -2
'

# ---- a release card's pieces, inert on a dev deck --------------------------------
# A release card (pi-qemu/PLAN.md Part 1) runs this same system read-only, its
# deck's identity applied from /data at every start, and RAUC updating it. The
# files go onto every deck, so the dev decks run what the release runs; only a
# release enables them, with the /data mount they need, and only a release has
# /etc/rauc/system.conf. The bootloader's own update service is masked on every
# deck: it would stage an EEPROM update at each start without asking, and on an
# A/B card where the boot ROM never looks.
scp $P/trimixxx-identity $P/trimixxx-identity.service \
    $P/trimixxx-health $P/trimixxx-health.service $P/rauc/rpi-tryboot deck:/tmp/
ssh deck '
    set -eux
    sudo install -d -m 0755 /usr/local/lib/trimixxx /usr/lib/rauc
    sudo install -m 0755 /tmp/trimixxx-identity /usr/local/lib/trimixxx/identity
    sudo install -m 0755 /tmp/trimixxx-health /usr/local/lib/trimixxx/health
    sudo install -m 0755 /tmp/rpi-tryboot /usr/lib/rauc/rpi-tryboot
    sudo install -m 0644 /tmp/trimixxx-identity.service /etc/systemd/system/trimixxx-identity.service
    sudo install -m 0644 /tmp/trimixxx-health.service /etc/systemd/system/trimixxx-health.service
    rm -f /tmp/trimixxx-identity /tmp/trimixxx-identity.service /tmp/trimixxx-health \
        /tmp/trimixxx-health.service /tmp/rpi-tryboot
    sudo systemctl mask --quiet rpi-eeprom-update.service
    sudo systemctl daemon-reload
'

# ---- the boot splash ------------------------------------------------------------------
# The logo on the panel for the first seconds of boot, on a spare VT so the
# boot log is never hidden (trimixxx-splash.sh). Rendered here, into the
# panel's own framebuffer format read off the deck -- a panel swap is picked up
# by the next deploy -- so the deck needs no image tooling. Enabled, not shown:
# taking the screen from a running Mixxx mid-deploy would be rude. To look at it:
#   uv run pi_config/splash-render.py pi_config/trimixxx_logo_crt.svg -o /tmp/s.raw --preview /tmp/s.png
#   pi-qemu deck ssh TARGET sudo /usr/local/bin/trimixxx-splash 5      (on the deck)
# virtual_size is "1024,600"; the stride is bytes per row, which drivers pad:
# a wrong one shears the image.
read -r FBW FBH FBBPP FBSTRIDE < <(
    ssh deck 'cd /sys/class/graphics/fb0 && cat virtual_size bits_per_pixel stride' | tr ',\n' ' '
    echo # read wants its delimiter: without it, set -e ends this here
)
echo "framebuffer: ${FBW}x${FBH}, ${FBBPP} bpp, stride ${FBSTRIDE}"
splash="$(mktemp -d)"
trap 'rm -rf "$splash"' EXIT
uv run $P/splash-render.py $P/trimixxx_logo_crt.svg -o "$splash/splash.raw" \
    --width "$FBW" --height "$FBH" --bpp "$FBBPP" --stride "$FBSTRIDE"
# The geometry travels with the image: at boot the firmware's framebuffer comes
# first, then vc4's, and the script waits for the one the image was packed for.
echo "$FBW $FBH $FBBPP $FBSTRIDE" > "$splash/splash.geom"
scp "$splash/splash.raw" "$splash/splash.geom" $P/trimixxx-splash.sh $P/trimixxx-splash.service deck:/tmp/
ssh deck "bash -seu -- '$FBSTRIDE' '$FBH'" <<'REMOTE'
stride="$1"; height="$2"
sudo install -d -m 0755 /usr/local/share/trimixxx
sudo install -m 0644 /tmp/splash.raw              /usr/local/share/trimixxx/splash.raw
sudo install -m 0644 /tmp/splash.geom             /usr/local/share/trimixxx/splash.geom
sudo install -m 0755 /tmp/trimixxx-splash.sh      /usr/local/bin/trimixxx-splash
sudo install -m 0644 /tmp/trimixxx-splash.service /etc/systemd/system/trimixxx-splash.service
rm -f /tmp/splash.raw /tmp/splash.geom /tmp/trimixxx-splash.sh /tmp/trimixxx-splash.service
sudo systemctl daemon-reload
sudo systemctl enable trimixxx-splash.service >/dev/null
# The one failure nothing else would report: an image of the wrong size.
want=$((stride * height))
got=$(stat -c %s /usr/local/share/trimixxx/splash.raw)
[ "$got" = "$want" ] || { echo "splash.raw is $got bytes, the framebuffer needs $want" >&2; exit 1; }
echo "splash.raw: $got bytes, matches the framebuffer; $(systemctl is-enabled trimixxx-splash.service)"
REMOTE

# ---- the network: what wlan0 is now, to prove it was left alone -------------------
# eth0's profile and the hotspot's never touch wlan0, which carries ssh: no
# `netplan apply`, no `nmcli networking`, no NetworkManager restart. Its
# address and the default route are compared before and after.
wlan_state() { ssh deck 'echo "$(ip -4 -o addr show wlan0 2>/dev/null | awk "{print \$4}") $(ip -4 route show default 2>/dev/null)"'; }
before="$(wlan_state)"

# ---- eth0, for the Pro DJ Link network ------------------------------------------------
# A Pro DJ Link network has no DHCP: CDJs take 169.254.0.0/16 addresses and
# broadcast to 169.254.255.255, which a host with no address there drops at
# the IP layer -- Mixxx binds UDP 50000 and hears nothing. NetworkManager's
# link-local method is RFC 3927 IPv4LL, as the CDJs do, and instant where a
# DHCP attempt would time out at every boot. The profile is
# eth0-link-local.nmconnection, bound to eth0, the same file every release
# card carries; netplan's old eth0 profile is deleted only where it sits alone
# in its own file (cloud-init's can also hold the deck's Wi-Fi). Plugging eth0
# into an ordinary LAN then gets no address: wlan0 is the way to the world.
scp $P/eth0-link-local.nmconnection deck:/tmp/
ssh deck 'bash -seu' <<'REMOTE'
kf=/etc/NetworkManager/system-connections/eth0-link-local.nmconnection
if sudo cmp -s /tmp/eth0-link-local.nmconnection "$kf"; then
    echo "eth0 profile : eth0-link-local, already installed"
else
    # NetworkManager ignores a keyfile that isn't root's alone; reload touches no device.
    sudo install -m 0600 -o root -g root /tmp/eth0-link-local.nmconnection "$kf"
    sudo nmcli connection reload
    echo "eth0 profile : eth0-link-local installed"
fi
rm -f /tmp/eth0-link-local.nmconnection
nmcli -t -f UUID,TYPE,NAME connection show |
while IFS=: read -r uuid type name; do
    [ "$type" = "802-3-ethernet" ] && [ "$name" != eth0-link-local ] || continue
    if sudo grep -qs '^    eth0:' "/etc/netplan/90-NM-$uuid.yaml"; then
        sudo nmcli connection delete uuid "$uuid" >/dev/null
        echo "old profile  : $name ($uuid) deleted"
    fi
done
# With no cable there is nothing to activate: the profile brings eth0 up by
# itself when a CDJ or a switch is plugged in.
carrier="$(cat /sys/class/net/eth0/carrier 2>/dev/null || echo 0)"
if [ "$carrier" = 1 ]; then
    if [ "$(nmcli -g GENERAL.CONNECTION device show eth0 2>/dev/null)" != eth0-link-local ]; then
        sudo nmcli connection up eth0-link-local ifname eth0 >/dev/null
    fi
    for _ in $(seq 1 20); do # IPv4LL probes for conflicts before it commits
        addr="$(ip -4 -o addr show eth0 | awk '{print $4}')"
        [ -n "$addr" ] && break
        sleep 0.5
    done
    echo "eth0 address : ${addr:-<none>}"
    case "${addr:-}" in
        169.254.*) echo "OK: eth0 is on the link-local subnet the CDJs use" ;;
        "")        echo "ERROR: eth0 still has no IPv4 address" >&2; exit 1 ;;
        *)         echo "WARNING: eth0 has $addr, not a 169.254/16 link-local address" >&2 ;;
    esac
else
    echo "eth0 address : <no link> -- the profile is link-local, and comes up when plugged in"
fi
REMOTE

# ---- the Wi-Fi fallback: the deck's own hotspot -----------------------------------------
# When no Wi-Fi is joined 45 s into a boot, the deck becomes one: named after
# its hostname, on its own 2.4 GHz channel (its unit file's hotspotChannel, 6
# without one; one per deck, so two decks at a venue never share one), with
# the password in pi_config/wifi-fallback/hotspot.env. Inert until the next
# boot: the profile has autoconnect=no (NetworkManager must never choose it on
# its own), and the unit is enabled, not started. See wifi-fallback/README.md.
. $P/wifi-fallback/hotspot.env
channel="$(unit hotspotChannel)"
channel="${channel:-6}"
echo "hotspot      : \"$DECK\" on 2.4 GHz channel $channel"
scp $P/wifi-fallback/trimixxx-wifi-fallback $P/wifi-fallback/trimixxx-wifi-fallback.service deck:/tmp/
ssh deck "bash -seu -- '$DECK' '$channel' '$HOTSPOT_PASSWORD'" <<'REMOTE'
ssid="$1"; channel="$2"; psk="$3"
profile=trimixxx-hotspot
sudo install -m 0755 /tmp/trimixxx-wifi-fallback /usr/local/bin/trimixxx-wifi-fallback
sudo install -m 0644 /tmp/trimixxx-wifi-fallback.service /etc/systemd/system/trimixxx-wifi-fallback.service
rm -f /tmp/trimixxx-wifi-fallback /tmp/trimixxx-wifi-fallback.service
sudo install -d -m 0755 /var/lib/trimixxx # the one-off test flag's place (the script's header)
# WPA2-PSK with CCMP, what every phone and laptop joins; `shared` hands out
# 10.42.0.x addresses, the deck at 10.42.0.1.
settings=(
    connection.autoconnect no
    802-11-wireless.mode ap
    802-11-wireless.band bg
    802-11-wireless.channel "$channel"
    802-11-wireless-security.key-mgmt wpa-psk
    802-11-wireless-security.proto rsn
    802-11-wireless-security.pairwise ccmp
    802-11-wireless-security.group ccmp
    802-11-wireless-security.psk "$psk"
    ipv4.method shared
    ipv4.addresses 10.42.0.1/24
    ipv6.method disabled
)
if nmcli -t -f NAME connection show | grep -qx "$profile"; then
    sudo nmcli connection modify "$profile" 802-11-wireless.ssid "$ssid" "${settings[@]}"
    echo "profile      : $profile updated"
else
    sudo nmcli connection add type wifi ifname wlan0 con-name "$profile" ssid "$ssid" "${settings[@]}" >/dev/null
    echo "profile      : $profile created"
fi
sudo systemctl daemon-reload
sudo systemctl enable trimixxx-wifi-fallback.service 2>/dev/null
echo "unit         : $(systemctl is-enabled trimixxx-wifi-fallback.service), not started"
REMOTE

after="$(wlan_state)"
if [ "$before" != "$after" ]; then
    echo "ERROR: wlan0's address or the default route changed: was '$before', now '$after'" >&2
    exit 1
fi
echo "wlan0 and the default route are unchanged"

# ---- DJ USB sticks, mounted -------------------------------------------------------------
# udev hands each stick to dj-usb@.service, which mounts it read-only as
# DJ_USB_1/2. The trigger picks up a stick already plugged in.
scp $P/dj-usb/dj-usb $P/dj-usb/99-dj-usb.rules $P/dj-usb/dj-usb@.service deck:/tmp/
ssh deck '
    set -eux
    sudo install -m 0755 /tmp/dj-usb            /usr/local/bin/dj-usb
    sudo install -m 0644 /tmp/99-dj-usb.rules   /etc/udev/rules.d/99-dj-usb.rules
    sudo install -m 0644 /tmp/dj-usb@.service   /etc/systemd/system/dj-usb@.service
    rm -f /tmp/dj-usb /tmp/99-dj-usb.rules /tmp/dj-usb@.service
    sudo systemctl daemon-reload
    sudo udevadm control --reload-rules
    sudo udevadm trigger --subsystem-match=block --action=add
'

echo "system: done; the session restarts once the steps are over"
exit $RESTART_SESSION
