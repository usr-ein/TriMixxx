#!/usr/bin/env bash
# Configure the deck's eth0 for a Pro DJ Link network: IPv4 link-local, no DHCP.
#
# WHY. A Pro DJ Link network has no DHCP server. CDJs self-assign into
# 169.254.0.0/16 and broadcast their keep-alives to 169.254.255.255 -- a
# *directed subnet broadcast*. A host with no address in that subnet receives
# those frames at the NIC and then discards them at the IP layer as not-for-us,
# so Mixxx binds UDP 50000 successfully and hears absolutely nothing. The deck's
# RX counter climbing while the sidebar says "no players found" is exactly that.
#
# eth0 shipped as netplan `dhcp4: true`, which NetworkManager renders as
# ipv4.method=auto with ipv4.link-local at its default of 0 ("respect the global
# default", i.e. off). DHCP therefore times out and NM does *not* fall back to a
# link-local address -- the device just sits `disconnected` with no IPv4 at all.
#
# ipv4.method=link-local makes NM do proper RFC 3927 IPv4LL, with address
# conflict detection, which is precisely what a CDJ does. It is also instant,
# where `auto` costs a DHCP timeout on every boot.
#
# TRADE-OFF: eth0 can no longer take a DHCP lease. That is the right call for a
# port whose only job is the CDJ network, but it does mean plugging eth0 into an
# ordinary LAN will not get an address. wlan0 remains the deck's route to the
# world and is deliberately not touched by this script.
#
# SAFETY. wlan0 carries ssh and is critical, so this script:
#   * names eth0's connection profile explicitly and never operates on all
#     devices (no `netplan apply`, no `nmcli networking off`, no daemon restart);
#   * records wlan0's address and default route before and after, and fails
#     loudly if either moved.
#
# Idempotent: re-running when eth0 is already link-local changes nothing.
set -euo pipefail

HOST="${HOST:-trimixxx-pi}"

ssh "$HOST" 'bash -seu' <<'REMOTE'
# --- what wlan0 looks like now, so we can prove we did not disturb it --------
wlan_before="$(ip -4 -o addr show wlan0 2>/dev/null | awk '{print $4}' || true)"
route_before="$(ip -4 route show default 2>/dev/null || true)"
echo "wlan0 before : ${wlan_before:-<none>}"

# --- find eth0's NetworkManager profile -------------------------------------
# By device and type rather than by name or UUID: the profile is called
# "netplan-eth0" with a generated UUID today, and neither survives a re-image.
profile="$(nmcli -t -f NAME,TYPE,DEVICE connection show |
    awk -F: '$2=="802-3-ethernet" && ($3=="eth0" || $3=="") {print $1; exit}')"

if [ -z "$profile" ]; then
    echo "ERROR: no ethernet connection profile found; nothing to configure" >&2
    exit 1
fi
echo "eth0 profile : $profile"

current="$(nmcli -g ipv4.method connection show "$profile")"
if [ "$current" = "link-local" ]; then
    echo "already link-local; nothing to change"
else
    echo "ipv4.method   : $current -> link-local"
    sudo nmcli connection modify "$profile" ipv4.method link-local
    # Bring up this profile on this device only. Deliberately not
    # `nmcli networking` or a NetworkManager restart, either of which would
    # bounce wlan0 and drop this ssh session.
    sudo nmcli connection up "$profile" ifname eth0 >/dev/null
fi

# --- wait for IPv4LL to settle ----------------------------------------------
# RFC 3927 probes for conflicts before committing, so the address does not
# appear instantly.
for _ in $(seq 1 20); do
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

# --- prove wlan0 is untouched ------------------------------------------------
wlan_after="$(ip -4 -o addr show wlan0 2>/dev/null | awk '{print $4}' || true)"
route_after="$(ip -4 route show default 2>/dev/null || true)"
echo "wlan0 after  : ${wlan_after:-<none>}"

if [ "$wlan_before" != "$wlan_after" ]; then
    echo "ERROR: wlan0 address changed ($wlan_before -> $wlan_after)" >&2
    exit 1
fi
if [ "$route_before" != "$route_after" ]; then
    echo "ERROR: default route changed:" >&2
    printf '  before: %s\n  after : %s\n' "$route_before" "$route_after" >&2
    exit 1
fi
echo "wlan0 and the default route are unchanged"
REMOTE

echo "eth0 configured for Pro DJ Link."
