#!/usr/bin/env python3
"""Rewrite the TriMixxx mapping for one deck's wiring.

    apply.py UNIT_JSON SRC_DIR OUT_DIR

Reads SRC_DIR/TriMixxx.midi.xml and SRC_DIR/TriMixxx.scripts.js and writes the
copies for the deck UNIT_JSON describes into OUT_DIR:

  * every note binding (a <control> with status 0x8n/0x9n) moves to the note
    that deck's button actually sends -- `buttons`, canonical -> physical;
  * every note <output> moves to the note its light actually answers to --
    `lights`;
  * the script's `// @unit-wiring` line gets both maps, for the ring LEDs (sent
    as SysEx by node, which the XML cannot reach) and for the handlers that read
    meaning off the note number.

Only <midino> text changes in the XML, so diffing the result against the
canonical file shows the remap and nothing else. CC bindings keep their numbers:
the remap is for buttons. Run by ../upload.sh; standard library only.
"""

import json
import re
import sys
import xml.dom.minidom
from pathlib import Path

BLOCK = re.compile(r"<(control|output)>(.*?)</\1>", re.S)
STATUS = re.compile(r"<status>\s*0x([0-9A-Fa-f]{2})\s*</status>")
MIDINO = re.compile(r"(<midino>\s*)0x([0-9A-Fa-f]{2})(\s*</midino>)")
MARKER = re.compile(r"^TriMixxx\.UNIT = .*// @unit-wiring.*$", re.M)


def note(text):
    value = int(text, 16)
    if not 0 <= value <= 0x7F:
        sys.exit(f"apply.py: {text} is not a MIDI note")
    return value


def note_map(unit, key):
    table = {note(k): note(v) for k, v in unit.get(key, {}).items()}
    if len(set(table.values())) != len(table):
        sys.exit(f"apply.py: two controls share one note in `{key}`")
    return table


def remap_xml(text, buttons, lights):
    # (kind, status, new note) -> the canonical note that landed there. Two
    # different canonical notes landing on one physical note would make one
    # control unreachable, silently -- refuse instead.
    landed = {}

    def rewrite(block):
        kind, body = block.group(1), block.group(2)
        status, midino = STATUS.search(body), MIDINO.search(body)
        if not status or not midino or int(status.group(1), 16) >> 4 not in (0x8, 0x9):
            return block.group(0)
        old = int(midino.group(2), 16)
        new = (buttons if kind == "control" else lights).get(old, old)
        key = (kind, status.group(1).upper(), new)
        if landed.setdefault(key, old) != old:
            sys.exit(f"apply.py: <{kind}> notes 0x{landed[key]:02X} and 0x{old:02X} "
                     f"would both land on 0x{new:02X}")
        body = MIDINO.sub(lambda m: f"{m.group(1)}0x{new:02X}{m.group(3)}", body, count=1)
        return f"<{kind}>{body}</{kind}>"

    return BLOCK.sub(rewrite, text)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    unit_path, src, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    unit = json.loads(unit_path.read_text())
    buttons, lights = note_map(unit, "buttons"), note_map(unit, "lights")

    mapping = remap_xml((src / "TriMixxx.midi.xml").read_text(), buttons, lights)
    xml.dom.minidom.parseString(mapping)  # still well-formed, or this raises

    script = (src / "TriMixxx.scripts.js").read_text()
    if len(MARKER.findall(script)) != 1:
        sys.exit("apply.py: TriMixxx.scripts.js needs exactly one `// @unit-wiring` line")
    wiring = json.dumps({
        "deck": unit["deck"],
        "buttons": {str(k): v for k, v in sorted(buttons.items())},
        "lights": {str(k): v for k, v in sorted(lights.items())},
    })
    script = MARKER.sub(lambda _: f"TriMixxx.UNIT = {wiring}; // @unit-wiring, from "
                                  f"units/{unit_path.name}", script)

    out.mkdir(parents=True, exist_ok=True)
    (out / "TriMixxx.midi.xml").write_text(mapping)
    (out / "TriMixxx.scripts.js").write_text(script)
    moved = sum(1 for a, b in zip(
        re.findall(r"<midino>[^<]*</midino>", (src / "TriMixxx.midi.xml").read_text()),
        re.findall(r"<midino>[^<]*</midino>", mapping)) if a != b)
    print(f"apply.py: {unit['deck']}: {moved} bindings renumbered, "
          f"{len(lights)} lights remapped")


if __name__ == "__main__":
    main()
