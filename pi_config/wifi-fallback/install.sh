#!/usr/bin/env bash
# Install the Wi-Fi fallback on a deck: the script, its unit, and the hotspot
# profile the script brings up. Usually run by ../upload.sh; stands alone too.
#
# INERT BY DESIGN. Nothing here changes what wlan0 is doing now: the profile is
# created with autoconnect=no, the unit is enabled for the NEXT boot and not
# started, and wlan0's address and the default route are checked before and
# after, the same proof prolink-eth0.sh gives for eth0.
#
# The profile is static per deck, written here once: its SSID is the deck's
# hostname, which does not change after a deck is set up, and its channel is
# the deck's own from the table below. Re-running writes the same values again,
# so editing the table or the password and re-running is how to change either.
set -euo pipefail

HOST="${HOST:-trimixxx-pi}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The hotspot's password. In the repo on purpose: it guards a debug network that
# only exists when the deck has no other, and whoever is standing at the deck
# needs it without the repo to hand (the Diagnostics page shows it). Anyone who
# reads this can join that network, so ssh on a deck with a hotspot should not
# accept passwords -- see ../fresh-install.md, "Access".
PASSWORD='trimixxx-debug-Kmj3Df'

# One 2.4 GHz channel per deck, so two decks at the same venue never share one.
# Hardcoded on purpose: count up for each new deck. Anything not listed is
# Trimixxx1.
DECK="$(ssh "$HOST" hostname)"
case "$DECK" in
    trimixxx2) CHANNEL=7 ;;
    *)         CHANNEL=6 ;;
esac
echo "==> $DECK: hotspot \"$DECK\" on 2.4 GHz channel $CHANNEL"

scp -q "$HERE/trimixxx-wifi-fallback" "$HERE/trimixxx-wifi-fallback.service" "$HOST":/tmp/
ssh "$HOST" "bash -seu -- '$DECK' '$CHANNEL' '$PASSWORD'" <<'REMOTE'
ssid="$1"; channel="$2"; psk="$3"
profile=trimixxx-hotspot

# --- what wlan0 looks like now, so we can prove we did not disturb it --------
wlan_before="$(ip -4 -o addr show wlan0 2>/dev/null | awk '{print $4}' || true)"
route_before="$(ip -4 route show default 2>/dev/null || true)"
echo "wlan0 before : ${wlan_before:-<none>}"

sudo install -m 0755 /tmp/trimixxx-wifi-fallback /usr/local/bin/trimixxx-wifi-fallback
sudo install -m 0644 /tmp/trimixxx-wifi-fallback.service \
    /etc/systemd/system/trimixxx-wifi-fallback.service
rm -f /tmp/trimixxx-wifi-fallback /tmp/trimixxx-wifi-fallback.service
# Where the one-off test flag goes; see the script's header.
sudo install -d -m 0755 /var/lib/trimixxx

# --- the hotspot profile ------------------------------------------------------
# autoconnect=no is the load-bearing setting: NetworkManager must never choose
# this over home on its own. Only the fallback script activates it.
#
# WPA2-PSK with CCMP, which is what every phone and laptop joins; `shared`
# makes NetworkManager hand out 10.42.0.x addresses, with the deck at 10.42.0.1.
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
    sudo nmcli connection add type wifi ifname wlan0 con-name "$profile" ssid "$ssid" \
        "${settings[@]}" >/dev/null
    echo "profile      : $profile created"
fi
nmcli -f connection.autoconnect,802-11-wireless.ssid,802-11-wireless.mode,802-11-wireless.channel,ipv4.method,ipv4.addresses \
    connection show "$profile" | sed 's/^/  /'

# --- the unit: enabled for the next boot, not started now ---------------------
sudo systemctl daemon-reload
sudo systemctl enable trimixxx-wifi-fallback.service 2>/dev/null
echo "unit         : $(systemctl is-enabled trimixxx-wifi-fallback.service), not started"

# --- prove wlan0 is untouched ---------------------------------------------------
wlan_after="$(ip -4 -o addr show wlan0 2>/dev/null | awk '{print $4}' || true)"
route_after="$(ip -4 route show default 2>/dev/null || true)"
echo "wlan0 after  : ${wlan_after:-<none>}"
if [ "$wlan_before" != "$wlan_after" ] || [ "$route_before" != "$route_after" ]; then
    echo "ERROR: wlan0 or the default route changed during the install" >&2
    exit 1
fi
echo "wlan0 and the default route are unchanged"
REMOTE

echo "Wi-Fi fallback installed on $DECK; it acts from the next boot."
