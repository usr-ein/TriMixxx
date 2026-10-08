#!/usr/bin/env python3
"""Each deck's own config.txt lines, for the one release card every deck runs.

    deck-sections.py UNITS_DIR >> config.txt

For every mixxx_config/units/<deck>.json with a "serial", a section only that
board reads (Raspberry Pi's [0x<serial>] filter, pi-qemu/PLAN.md §4.2):

    # trimixxx2
    [0x1234abcd]
    dtoverlay=<its "panelOverlay">
    <its "configTxt" lines, if any>

and a closing [all]. "serial" is the board's own, the last 8 hex digits of
/proc/device-tree/serial-number (OTP row 28: `vcgencmd otp_dump | grep ^28:`),
written 0x1234abcd. A unit without one gets no section: on the release, such a
deck starts as a bare Pi 4 would, with no panel overlay. A malformed serial, or
two decks with the same one, stops the release. Standard library only.
"""

import json
import re
import sys
from pathlib import Path

SERIAL = re.compile(r"0x[0-9a-f]{8}")


def main(units_dir: str) -> int:
    sections, seen = [], {}
    for path in sorted(Path(units_dir).glob("*.json")):
        unit = json.loads(path.read_text())
        serial = unit.get("serial")
        if serial is None:
            continue
        deck = unit.get("deck", path.stem)
        if not isinstance(serial, str) or not SERIAL.fullmatch(serial.lower()):
            print(f"deck-sections.py: {path.name}: serial {serial!r} is not 0x + 8 hex digits", file=sys.stderr)
            return 1
        serial = serial.lower()
        if serial in seen:
            print(f"deck-sections.py: {path.name}: serial {serial} is {seen[serial]}'s too", file=sys.stderr)
            return 1
        seen[serial] = deck
        lines = []
        if unit.get("panelOverlay"):
            lines.append(f"dtoverlay={unit['panelOverlay']}")
        for line in unit.get("configTxt", []):
            if not isinstance(line, str) or "\n" in line or line.lstrip().startswith("["):
                print(f"deck-sections.py: {path.name}: configTxt line {line!r} is not one config.txt line", file=sys.stderr)
                return 1
            lines.append(line)
        sections.append("\n".join([f"# {deck}", f"[{serial}]", *lines]))
    if sections:
        print("\n# Each deck's own lines, under its board's serial (pi-qemu/release/boot/deck-sections.py)")
        print("\n".join(sections))
        print("[all]")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
