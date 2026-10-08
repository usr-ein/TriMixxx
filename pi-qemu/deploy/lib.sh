# The deploy steps' contract. Each NNN_name.sh sources this first; the
# NNN_name.pi.sh ones run on the deck and keep the same rules themselves.
#
#   pi-qemu deck deploy TARGET [STEP...]    every step in number order, or the
#                                           named ones (mixxx, 004 or 004_mixxx)
#
# A step is a file here, NNN_name.sh or NNN_name.pi.sh, and the number is its
# place: adding a step is adding the next number.
#
# Where it runs. NNN_name.sh runs on the Mac, in the repo ($REPO), and
# reaches the deck as `ssh deck ...` and `scp FILE deck:PATH`: pi-qemu puts
# ssh and scp for the target first in PATH. NNN_name.pi.sh is piped into
# `sudo bash -s` on the deck.
#
# Its environment:
#   DECK           the deck's hostname, which names its unit file
#   REPO           the checkout being deployed (a worktree's own code)
#   UNIT_JSON      mixxx_config/units/$DECK.json; none: the standard wiring
#   PANEL_OVERLAY  the unit file's panelOverlay (for .pi.sh steps)
#   PIQEMU_STEP=1  set by pi-qemu: a step run by hand refuses, as there is no
#                  default deck to reach
#
# Its exit status:
#   0   done
#   10  done, and the boot flags changed: the deck reboots before the next step
#   11  done, and the session must restart once the steps are over: Mixxx opens
#       the MIDI bridge's and the launcher's ports only at its start
#   12  done, and it restarted the session itself
#   anything else: failed, and the run stops there
#
# Run again, a step brings the deck back to what the repo says and changes
# nothing where nothing changed. A step that builds in Docker says so in its
# header with a line `# needs: docker`: pi-qemu checks Docker's free disk first.

set -euo pipefail
if [ "${PIQEMU_STEP:-}" != 1 ]; then
    echo "$(basename "$0") is a deploy step: pi-qemu deck deploy TARGET $(basename "$0" .sh | cut -d_ -f2-)" >&2
    exit 2
fi
cd "$REPO"

DONE=0 REBOOT=10 RESTART_SESSION=11 SESSION_RESTARTED=12

# A field of this deck's unit file; empty if there is no file or no field.
unit() {
    [ -f "$UNIT_JSON" ] || return 0
    python3 -c 'import json, sys; print(json.load(open(sys.argv[1])).get(sys.argv[2], ""))' "$UNIT_JSON" "$1"
}
