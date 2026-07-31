#!/usr/bin/env bash
# Tear down the tap bridge and put this Mac on the DJ network as a participant.
#
# The other half of tools/setup-bridge.sh. That one makes the machine a wire
# between two decks; this one makes it a node beside them, with an address the
# decks can reach, so the Python proof of concept can announce itself, browse a
# deck's media, and serve its own.
#
#   sudo tools/setup-cdj-node.sh                 # undo the bridge, then pick a port
#   sudo tools/setup-cdj-node.sh en9             # use this port
#   sudo tools/setup-cdj-node.sh en9 169.254.99.100
#
# ---------------------------------------------------------------------------
# Why a link-local address and not DHCP.
#
# There is no DHCP server on a DJ rig. A CDJ tries DHCP about three times, gives
# up after roughly nine seconds and self-assigns a 169.254.0.0/16 address
# (docs/FINDINGS.md F8) -- so the whole network is link-local by default, and
# the way to join it is to pick an address in that range rather than to wait for
# a lease that will never arrive.
#
# Why the check at the end is a packet capture and not a ping.
#
# **CDJs do not answer ping.** A silent ping is the expected result even when
# everything is working, so it tells you nothing. What does tell you something
# is their keep-alive: every player broadcasts one on UDP 50000 about every two
# seconds, whether or not anything has talked to it. Seeing one is proof the
# cable, the port and the address are all right.
# ---------------------------------------------------------------------------
set -euo pipefail

STATE="$HOME/.prolink-tap.state"

# A free address in the link-local range the decks use. .99.100 is what this
# Mac has historically announced from, and it is outside the range the two decks
# have self-assigned, so it does not contend with them.
DEFAULT_IP="169.254.99.100"
# /16, because that is the size of the link-local block every player is in. A
# narrower mask makes the decks look off-subnet and every RPC times out.
NETMASK="255.255.0.0"

die() { printf '%s\n' "$*" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || die "this script is macOS-only"

port_name_of() {
    networksetup -listallhardwareports | awk -v dev="$1" '
        /^Hardware Port: / { port = substr($0, 16) }
        /^Device: / && $2 == dev { print port; exit }'
}
service_of() {
    networksetup -listnetworkserviceorder | awk -v dev="$1" '
        /^\([0-9*]+\)/ { name = substr($0, index($0, ")") + 2) }
        $0 ~ ("Device: " dev "\\)$") { print name; exit }'
}
wifi_device() {
    networksetup -listallhardwareports | awk '
        /^Hardware Port: Wi-Fi$/ { getline; print $2; exit }'
}
status_of() { ifconfig "$1" 2>/dev/null | awk '/status:/ { print $2; exit }'; }

sudo -v

# -- 1. undo the bridge -----------------------------------------------------

BRIDGE=""; MEMBER_A=""; MEMBER_B=""
if [ -f "$STATE" ]; then
    # shellcheck disable=SC1090
    . "$STATE"
fi

if [ -n "$BRIDGE" ] && ifconfig "$BRIDGE" >/dev/null 2>&1; then
    [ "$BRIDGE" = "bridge0" ] && die "refusing to destroy bridge0 -- that is macOS's Thunderbolt Bridge"
    echo "Removing $BRIDGE ($MEMBER_A + $MEMBER_B)..."
    for m in $(ifconfig "$BRIDGE" | awk '/member:/ { print $2 }'); do
        sudo ifconfig "$BRIDGE" deletem "$m" || true
    done
    sudo ifconfig "$BRIDGE" down || true
    sudo ifconfig "$BRIDGE" destroy
    echo "  $BRIDGE destroyed."
else
    echo "No tap bridge of ours is up (nothing recorded in $STATE, or it is already gone)."
fi

# Give the ports their network service back. setup-bridge.sh turned IPv4 and
# IPv6 off to keep the Mac silent on the wire; leaving them off would make the
# port look dead in System Settings later, with no clue why.
for dev in "$MEMBER_A" "$MEMBER_B"; do
    [ -n "$dev" ] || continue
    eval "svc=\${SERVICE_$dev:-}"
    [ -n "${svc:-}" ] || continue
    echo "Re-enabling network service '$svc' ($dev)"
    sudo networksetup -setnetworkserviceenabled "$svc" on || true
done
rm -f "$STATE"

# -- 2. choose the port to join the rig on ----------------------------------

WIFI="$(wifi_device || true)"

candidates=()
for dev in $(ifconfig -l | tr ' ' '\n' | grep '^en' || true); do
    [ "$dev" = "$WIFI" ] && continue
    # Skip anything still in a bridge -- on a stock Mac that is bridge0's
    # Thunderbolt ports, which are not where a CDJ is plugged in.
    in_bridge=""
    for b in $(ifconfig -l | tr ' ' '\n' | grep '^bridge' || true); do
        ifconfig "$b" 2>/dev/null | awk '/member:/ { print $2 }' | grep -qx "$dev" && in_bridge=1
    done
    [ -n "$in_bridge" ] && continue
    candidates+=("$dev")
done

IFACE="${1:-}"
IP="${2:-$DEFAULT_IP}"

if [ -z "$IFACE" ]; then
    printf '\n%-6s %-10s %-28s %s\n' DEVICE STATUS "HARDWARE PORT" MEDIA
    for dev in "${candidates[@]}"; do
        printf '%-6s %-10s %-28s %s\n' "$dev" "$(status_of "$dev")" \
            "$(port_name_of "$dev")" \
            "$(ifconfig "$dev" 2>/dev/null | sed -n 's/^[[:space:]]*media: //p' | head -1)"
    done
    printf '\n'

    active=()
    for dev in "${candidates[@]}"; do
        [ "$(status_of "$dev")" = "active" ] && active+=("$dev")
    done
    if [ "${#active[@]}" -eq 1 ]; then
        IFACE="${active[0]}"
        echo "Only $IFACE has a live link; using it."
    else
        read -r -p "Which interface faces the CDJs? " IFACE
    fi
fi

ifconfig "$IFACE" >/dev/null 2>&1 || die "no such interface: $IFACE"
[ "$IFACE" = "$WIFI" ] && die "$IFACE is Wi-Fi -- refusing to reconfigure it"

case "$IP" in
    169.254.*) ;;
    *) echo "warning: $IP is outside 169.254.0.0/16, which is where every CDJ puts itself (F8)." >&2 ;;
esac

# -- 3. give it an address --------------------------------------------------

SVC="$(service_of "$IFACE" || true)"
echo
echo "Configuring $IFACE ($(port_name_of "$IFACE")) as $IP/$NETMASK"

# The service has to be on for IPConfiguration to manage the port at all -- the
# bridge teardown above may have left it disabled.
if [ -n "$SVC" ]; then
    sudo networksetup -setnetworkserviceenabled "$SVC" on || true
fi
sudo ifconfig "$IFACE" up

# **ipconfig set, not ifconfig inet.** They are not interchangeable for a
# link-local address, and the difference is invisible until nothing works:
# `ifconfig` writes the address straight into the kernel, while `ipconfig` goes
# through IPConfiguration, which performs the RFC 3927 duplicate-address probe
# and announcement that makes a 169.254 address usable. After `ifconfig` the
# address is there, the route looks right, and every ARP for a peer stays
# (incomplete) so every unicast fails. docs/CAPTURE-PLAN.md §0.
sudo ipconfig set "$IFACE" MANUAL "$IP" "$NETMASK"

sleep 1
echo
ipconfig getsummary "$IFACE" 2>/dev/null | grep -E 'Active|ManualAddress|LastFailureStatus' || true
echo
ifconfig "$IFACE" | grep -E 'flags|status|inet ' || true

# A configured address that has not been applied is the normal state with no
# deck plugged in, and it looks like a failure if you do not expect it:
# IPConfiguration stores the configuration and refuses to apply it while the
# interface has no carrier ("Active: FALSE, LastFailureStatus: media inactive").
# It goes live on its own the moment a powered CDJ is plugged in.
if [ "$(status_of "$IFACE")" != "active" ]; then
    echo
    echo "note: $IFACE has no carrier, so the address is stored but not applied yet."
    echo "      That is expected with the decks off; it applies itself when one powers on."
fi

# -- 4. prove it works ------------------------------------------------------

echo
echo "Listening for a CDJ keep-alive on UDP 50000 for 10 seconds..."
echo "(a CDJ takes ~10 s after power-on to say anything, and never answers ping)"

# The watchdog runs *inside* the sudo, not around it. tcpdump has to be root,
# and a shell running as the user cannot signal a root process -- so a
# `sudo timeout tcpdump` from out here would hang forever on a quiet network
# with no way to stop it. Doing both under one `sudo sh` keeps the killer at the
# same privilege as the thing it kills, and drops the dependency on `timeout`,
# which is Homebrew-only on macOS and may not be on root's PATH.
listen="$(mktemp -t prolink-listen)"
sudo sh <<SH || true
tcpdump -i "$IFACE" -n -l -c 3 'udp port 50000' > "$listen" 2>/dev/null &
pid=\$!
( sleep 10; kill \$pid 2>/dev/null ) &
watchdog=\$!
wait \$pid 2>/dev/null
kill \$watchdog 2>/dev/null
SH

if [ -s "$listen" ]; then
    cat "$listen"
    rm -f "$listen"
    cat <<EOF

Heard the rig. $IFACE is on the DJ network as $IP.

Next:
  uv run prolinks devices --iface $IFACE
  uv run prolinks serve   --iface $IFACE --volume /Volumes/YOUR_STICK --claim
EOF
else
    rm -f "$listen"
    cat <<EOF

Nothing on UDP 50000 in 10 seconds. That is not necessarily this script's doing:

  * a CDJ needs ~10 s after power-on before it announces at all (F8)
  * check the cable is in $IFACE and the deck is on
  * if you meant to tap between two decks instead, run tools/setup-bridge.sh

The address is configured either way -- re-run the listen by hand with:
  sudo tcpdump -i $IFACE -n 'udp port 50000'
EOF
fi
