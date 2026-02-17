# /// script
# requires-python = ">=3.10"
# dependencies = ["sexpdata"]
# ///
"""Parse KiCad netlist (.net) and output per-module component summary as Markdown."""

import json
import sys
from collections import defaultdict
from pathlib import Path

import sexpdata


def sym_str(val):
    """Convert sexpdata Symbol or string to plain string."""
    if isinstance(val, sexpdata.Symbol):
        return val.value()
    return str(val)


def find_entries(sexpr, key):
    """Find all sub-lists whose first element matches key."""
    results = []
    if isinstance(sexpr, list):
        if len(sexpr) > 0 and sym_str(sexpr[0]) == key:
            results.append(sexpr)
        for item in sexpr:
            results.extend(find_entries(item, key))
    return results


def find_entry(sexpr, key):
    """Find first sub-list whose first element matches key."""
    entries = find_entries(sexpr, key)
    return entries[0] if entries else None


def get_value(sexpr, key):
    """Get the string value of (key value) from a list."""
    entry = find_entry(sexpr, key)
    if entry and len(entry) > 1:
        return sym_str(entry[1])
    return ""


def get_property(comp, prop_name):
    """Get a named property value from a component."""
    for item in comp:
        if isinstance(item, list) and len(item) >= 3:
            if sym_str(item[0]) == "property":
                name_entry = find_entry(item, "name")
                val_entry = find_entry(item, "value")
                if name_entry and val_entry:
                    if sym_str(name_entry[1]) == prop_name:
                        return sym_str(val_entry[1])
    return ""


def parse_netlist(net_path: Path):
    with open(net_path) as f:
        data = sexpdata.loads(f.read())

    # Parse sheet names from the design section
    design = find_entry(data, "design")
    sheets = find_entries(design, "sheet")
    sheet_map = {}  # tstamp path -> sheet name
    for sheet in sheets:
        name = get_value(sheet, "name")
        sheet_map[name] = name

    # Parse components
    components_section = find_entry(data, "components")
    comps = find_entries(components_section, "comp")

    modules = defaultdict(list)
    for comp in comps:
        ref = get_value(comp, "ref")
        value = get_value(comp, "value")
        description = get_value(comp, "description")
        footprint = get_value(comp, "footprint")

        # Get sheet path name
        sheetpath = find_entry(comp, "sheetpath")
        sheet_name = get_value(sheetpath, "names") if sheetpath else "/"

        # Get libsource part name
        libsource = find_entry(comp, "libsource")
        part = get_value(libsource, "part") if libsource else ""

        modules[sheet_name].append({
            "ref": ref,
            "value": value,
            "part": part,
            "description": description,
            "footprint": footprint,
        })

    return modules


SHEET_DESCRIPTIONS = {
    "/": "Root — Raspberry Pi CM5 compute module, status LEDs, fan connector, RTC battery, and top-level glue logic",
    "/Audio Outputs/": "Audio Outputs — I2S DAC (PCM5242), RCA stereo pair, 6.35mm and 3.5mm headphone jacks, and analog output filtering",
    "/Test Points/": "Test Points — Debug and measurement test points for key signals",
    "/USB DJ Ports/": "USB DJ Ports — USB-A 3.0 (Rekordbox sticks), USB-A power-only, USB-C 2.0, orientation mux (HD3SS3220), signal switch (HD3SS3212), and ESD protection",
    "/HDMI and Ethernet/": "HDMI and Ethernet — Dual micro-HDMI outputs (touchscreen + debug), gigabit Ethernet via RJ45",
    "/Power Delivery/": "Power Delivery — USB-C PD input (CH224K negotiation), 20V-to-5V buck (SY8368AQQC), 3.3V LDO (AP2112K), USB power switches (AP2553W)",
    "/Arduino MIDI/": "Arduino MIDI — ATmega32U4 USB MIDI controller, 16MHz crystal, JST connectors for buttons/encoders/jog wheel",
}


def format_markdown(modules: dict) -> str:
    """Format parsed modules as a Markdown section."""
    lines = []
    lines.append("## Bill of Materials by Module")
    lines.append("")
    lines.append(f"*Auto-generated from the KiCad netlist — {sum(len(v) for v in modules.values())} components total.*")
    lines.append("")

    # Sort: Root first, then alphabetical
    def sort_key(name):
        if name == "/":
            return (0, "")
        return (1, name)

    for sheet_name in sorted(modules.keys(), key=sort_key):
        comps = modules[sheet_name]
        desc = SHEET_DESCRIPTIONS.get(sheet_name, sheet_name.strip("/"))
        lines.append(f"### {desc}")
        lines.append("")

        # Group by reference prefix (letter part) for a summary
        by_prefix = defaultdict(list)
        for c in comps:
            prefix = "".join(ch for ch in c["ref"] if ch.isalpha())
            by_prefix[prefix].append(c)

        # Filter out purely passive groups from detailed listing
        # but still show notable ICs/connectors
        notable = []
        passives = defaultdict(int)

        for c in sorted(comps, key=lambda x: x["ref"]):
            prefix = "".join(ch for ch in c["ref"] if ch.isalpha())
            if prefix in ("R", "C", "L"):
                passives[prefix] += 1
            else:
                notable.append(c)

        passive_summary = []
        prefix_names = {"R": "resistors", "C": "capacitors", "L": "inductors"}
        for p in ("C", "R", "L"):
            if passives[p]:
                passive_summary.append(f"{passives[p]} {prefix_names[p]}")

        if notable:
            lines.append("| Ref | Value | Description |")
            lines.append("|-----|-------|-------------|")
            for c in notable:
                desc_text = c["description"] or c["part"]
                # Truncate very long descriptions
                if len(desc_text) > 80:
                    desc_text = desc_text[:77] + "..."
                lines.append(f"| {c['ref']} | {c['value']} | {desc_text} |")
            lines.append("")

        if passive_summary:
            lines.append(f"Plus {', '.join(passive_summary)}.")
            lines.append("")

    return "\n".join(lines)


def main():
    net_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("CDJ-MainBoard_2026-02-17.net")
    modules = parse_netlist(net_path)
    print(format_markdown(modules))


if __name__ == "__main__":
    main()
