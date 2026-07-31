#!/usr/bin/env bash
# Put this Mac in the middle of two Pro DJ Link cables, as a transparent tap.
#
# The rig is: deck A -- [dongle 1] Mac [dongle 2] -- deck B. A BSD bridge
# forwards between the two ports, so the decks see each other exactly as if the
# Mac were a switch, and every frame between them crosses our interfaces where
# tcpdump can see it.
#
#   sudo tools/setup-bridge.sh              # detect, confirm, bridge
#   sudo tools/setup-bridge.sh en5 en9      # skip detection, use these two
#
# Undo it with tools/setup-cdj-node.sh, which also turns one port back into a
# working link-local interface.
#
# ---------------------------------------------------------------------------
# Two things about this that are not obvious and cost us captures to learn.
#
# **Capture the bridge MEMBERS, not the bridge.** A BSD bridge floods broadcast
# traffic up to the bridge interface but forwards learned unicast directly
# between members. So a capture on bridge1 shows the keep-alives -- which are
# broadcast -- and misses every dbserver and NFS packet between the decks, which
# are unicast. That looks like "the decks are not talking to each other" rather
# than like a capture mistake. Use pktap across both members; this script prints
# the exact command.
#
# **bridge0 belongs to macOS.** It is the Thunderbolt Bridge service, with the
# Thunderbolt ports as members. Creating our tap there, or adding members to it,
# fights configd and breaks Thunderbolt networking. We take the next free
# number, and refuse to touch bridge0 at all.
# ---------------------------------------------------------------------------
set -euo pipefail

STATE="$HOME/.prolink-tap.state"

die() { printf '%s\n' "$*" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || die "this script is macOS-only (it uses ifconfig bridging and networksetup)"

# -- interface inventory ----------------------------------------------------

# device -> human name, e.g. "en9" -> "USB 10/100/1000 LAN".
port_name_of() {
    networksetup -listallhardwareports | awk -v dev="$1" '
        /^Hardware Port: / { port = substr($0, 16) }
        /^Device: / && $2 == dev { print port; exit }'
}

# The network *service* a device belongs to, if any. Needed to stop macOS
# configuring the port while we are tapping with it -- see below.
service_of() {
    networksetup -listnetworkserviceorder | awk -v dev="$1" '
        /^\([0-9*]+\)/ { name = substr($0, index($0, ")") + 2) }
        $0 ~ ("Device: " dev "\\)$") { print name; exit }'
}

wifi_device() {
    networksetup -listallhardwareports | awk '
        /^Hardware Port: Wi-Fi$/ { getline; print $2; exit }'
}

bridge_members() {
    local b
    for b in $(ifconfig -l | tr ' ' '\n' | grep '^bridge' || true); do
        ifconfig "$b" 2>/dev/null | awk '/member:/ { print $2 }'
    done
}

status_of() { ifconfig "$1" 2>/dev/null | awk '/status:/ { print $2; exit }'; }
media_of()  { ifconfig "$1" 2>/dev/null | sed -n 's/^[[:space:]]*media: //p' | head -1; }
mac_of()    { ifconfig "$1" 2>/dev/null | awk '/ether/ { print $2; exit }'; }
ipv4_of()   { ifconfig "$1" 2>/dev/null | awk '/inet /{ print $2; exit }'; }

WIFI="$(wifi_device || true)"
TAKEN="$(bridge_members || true)"

# Candidates: real Ethernet ports we are allowed to touch.
#
# Excluded, each for its own reason: the Wi-Fi device, because taking it down
# would cut the machine off; anything already a member of a bridge, which on a
# stock Mac means bridge0's Thunderbolt ports; and every non-Ethernet pseudo
# interface (awdl, llw, utun, ap) that would never carry a CDJ frame.
candidates=()
for dev in $(ifconfig -l | tr ' ' '\n' | grep '^en' || true); do
    [ "$dev" = "$WIFI" ] && continue
    printf '%s\n' "$TAKEN" | grep -qx "$dev" && continue
    candidates+=("$dev")
done

[ "${#candidates[@]}" -ge 2 ] || die "found ${#candidates[@]} usable Ethernet port(s); a tap needs two.
Plug in a second USB Ethernet adapter and run this again."

show_table() {
    printf '\n%-6s %-12s %-10s %-28s %s\n' DEVICE STATUS ADDRESS "HARDWARE PORT" MEDIA
    for dev in "${candidates[@]}"; do
        printf '%-6s %-12s %-10s %-28s %s\n' \
            "$dev" "$(status_of "$dev")" "$(ipv4_of "$dev")" \
            "$(port_name_of "$dev")" "$(media_of "$dev")"
    done
    printf '\n'
}

# -- choose the two ports ---------------------------------------------------

if [ "$#" -eq 2 ]; then
    A="$1"; B="$2"
elif [ "$#" -ne 0 ]; then
    die "usage: $0 [<interface-a> <interface-b>]"
else
    show_table
    active=()
    for dev in "${candidates[@]}"; do
        [ "$(status_of "$dev")" = "active" ] && active+=("$dev")
    done
    if [ "${#active[@]}" -eq 2 ]; then
        A="${active[0]}"; B="${active[1]}"
        echo "Two ports have a live link: $A and $B."
    else
        # Deliberately not guessing. "Inactive" here usually means the cable is
        # not in yet, and bridging the wrong pair silently produces a tap that
        # captures nothing -- which is indistinguishable from decks that are not
        # talking.
        echo "${#active[@]} port(s) have a live link, so I will not guess which pair you mean."
        read -r -p "Interface A: " A
        read -r -p "Interface B: " B
    fi
fi

for dev in "$A" "$B"; do
    ifconfig "$dev" >/dev/null 2>&1 || die "no such interface: $dev"
    [ "$dev" = "$WIFI" ] && die "$dev is Wi-Fi -- refusing to bridge it"
    printf '%s\n' "$TAKEN" | grep -qx "$dev" && die "$dev is already a bridge member (bridge0 is macOS's Thunderbolt Bridge)"
done
[ "$A" != "$B" ] || die "A and B are the same interface"

cat <<EOF

About to bridge:
  A: $A  ($(port_name_of "$A"), link $(status_of "$A"))
  B: $B  ($(port_name_of "$B"), link $(status_of "$B"))

This will:
  * create a new bridge and make $A and $B its members
  * DISABLE both ports' network services, so configd stops re-applying DHCP to
    them -- left enabled it fights the bridge, and its solicitations pollute
    the capture
  * leave Wi-Fi ($WIFI) and bridge0 (Thunderbolt) untouched

EOF
read -r -p "Type 'yes' to continue: " reply
[ "$reply" = "yes" ] || die "aborted; nothing changed"

# -- build it ---------------------------------------------------------------

sudo -v

# Never bridge0: that is the Thunderbolt Bridge service and configd owns it.
BRIDGE=""
for n in $(seq 1 20); do
    if ! ifconfig "bridge$n" >/dev/null 2>&1; then BRIDGE="bridge$n"; break; fi
done
[ -n "$BRIDGE" ] || die "no free bridge device between bridge1 and bridge20"

# Record what we change before changing it, so the teardown script can put the
# machine back even if this shell is long gone.
#
# Values go through %q because a service name is a hardware port name and those
# have spaces in them -- "USB 10/100/1000 LAN". Written bare, the teardown
# script's `. "$STATE"` would try to run `10/100/1000` as a command.
: > "$STATE"
printf 'BRIDGE=%q\nMEMBER_A=%q\nMEMBER_B=%q\n' "$BRIDGE" "$A" "$B" >> "$STATE"

for dev in "$A" "$B"; do
    svc="$(service_of "$dev" || true)"
    if [ -n "$svc" ]; then
        printf 'SERVICE_%s=%q\n' "$dev" "$svc" >> "$STATE"
        # Disable the whole service, not just IPv4. Left enabled, configd keeps
        # re-applying DHCP to a bridge member and fights the bridge -- and the
        # solicitations land in the capture, attributed to a device that is
        # supposed to be a wire. docs/CAPTURE-PLAN.md §0.
        sudo networksetup -setnetworkserviceenabled "$svc" off || true
    fi
    sudo ipconfig set "$dev" NONE || true
    sudo ifconfig "$dev" up
done

sudo ifconfig "$BRIDGE" create
sudo ifconfig "$BRIDGE" addm "$A" addm "$B"
sudo ifconfig "$BRIDGE" up

echo
ifconfig "$BRIDGE"
echo
echo "Bridge $BRIDGE is up: $A <-> $B. State written to $STATE"

cat <<EOF

Capture with pktap across BOTH members -- not the bridge:

  sudo tcpdump -i pktap,$A,$B -s 0 -n -w capture.pcap

Or, with the repo's helper, which also writes a NOTES skeleton beside it:

  tools/capture.sh S99-my-scenario pktap,$A,$B "what I am testing"

Capturing '$BRIDGE' instead would show you the broadcast keep-alives and none of
the dbserver or NFS traffic, because a bridge forwards learned unicast straight
between members without copying it up.

Undo, and turn $A or $B back into a usable link-local interface:

  tools/setup-cdj-node.sh
EOF
