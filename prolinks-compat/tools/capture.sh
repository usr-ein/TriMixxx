#!/usr/bin/env bash
# Capture one scenario to captures/<name>/, with a notes skeleton.
#
# Usage: tools/capture.sh S05-link-browse en5 "deck A browses deck B's USB"
#
# The flags that matter: -s 0 keeps whole packets (a snaplen silently clips the
# dbserver and NFS payloads we are here for), -n avoids DNS lookups that would
# pollute the capture, and there is deliberately no BPF filter -- a filter that
# looks obviously right is how you find out later that the interesting packet
# was on a port nobody thought to include.
set -euo pipefail

name="${1:?scenario name, e.g. S05-link-browse}"
iface="${2:?capture interface, e.g. en5}"
shift 2
description="${*:-}"

dir="captures/$name"
mkdir -p "$dir"

if [ -e "$dir/run.pcap" ]; then
    echo "refusing to overwrite $dir/run.pcap -- pick another name" >&2
    exit 1
fi

{
    echo "# $name"
    echo
    echo "- started: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "- interface: $iface"
    echo "- description: $description"
    echo
    echo "## Hardware state"
    echo "- deck A: ip=?  firmware=?  slot=?  media=?"
    echo "- deck B: ip=?  firmware=?  slot=?  media=?"
    echo "- bridge: ?"
    echo
    echo "## Timeline"
    echo "- 0:00 capture started"
    echo "- "
} > "$dir/NOTES.md"

echo "sudo tcpdump -i $iface -s 0 -n -w $dir/run.pcap" > "$dir/cmd.txt"

echo "capturing $name on $iface -> $dir/run.pcap"
echo "fill in $dir/NOTES.md as you go. Ctrl-C to stop."
echo
sudo tcpdump -i "$iface" -s 0 -n -w "$dir/run.pcap"
