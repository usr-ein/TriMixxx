#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "lxml>=5.0",
#     "typer>=0.12",
#     "rich>=13.0",
# ]
# ///
"""
Turn a KiCad copper-layer SVG plot into a laser-ready plain SVG.

What it does
------------
KiCad's `pcbnew` SVG plot of a copper layer renders the board area as a
black rectangle with the copper geometry painted in white on top (filled
pads and text, plus thick white-stroked traces). This script flips that
into the representation a laser engraver wants: a single solid region
covering everything that should be ablated, with the copper geometry
punched out as transparent holes. The final fill is recolored to
`#046307` (see `OUTPUT_FILL`) — the boolean ops still run on black/white,
the color swap is a post-process so the laser software sees green.

Pipeline
--------
1. Parse the input with lxml. For each geometric element (rect, path,
   circle, etc.) resolve its effective rendered color: fill if present,
   otherwise the stroke (this matters because KiCad draws routed tracks
   as thick white-stroked paths inside `fill:none; stroke:#FFFFFF` groups).
   Assign a stable ID to each black/white element so Inkscape can address
   it later. The input file is never modified — work happens on a copy
   in a tempdir.
2. Inkscape pass 1 on the annotated copy:
   - `object-to-path`         convert rects/circles into paths
   - `object-stroke-to-path`  bake stroked traces into filled paths
   - `selection-ungroup` x3   flatten KiCad's nested groups so the
                              boolean ops aren't fooled by hierarchy
   - `select-by-id:<whites>`  select every white element
   - `path-union`             true boolean union (NOT `path-combine` —
                              combine leaves `fill-rule:evenodd` on the
                              merged path, which turns pad/track/teardrop
                              overlaps into wedge-shaped holes)
   - export plain SVG to an intermediate file
3. After path-union, exactly one white-filled path remains. Read the
   intermediate back to find its actual ID (Inkscape picks which ID
   survives, and it isn't the first selected one).
4. Inkscape pass 2 on the intermediate:
   - `select-by-id:<bg>,<combined>`   exactly two paths
   - `path-difference`                bottom minus top = bg with the
                                      copper geometry cut out
   - `select-by-id:<clip>`            add the injected viewBox rect to the
                                      selection left by path-difference
   - `path-intersection`              clip the sheet to the page (see below)
   - export plain SVG as `{stem}-bambu-ready.svg` next to the original
5. Post-process the exported file with lxml: rewrite every black fill
   (style declaration or `fill=` attribute) to `OUTPUT_FILL` (#046307).

Why the page clip (step 4)
--------------------------
KiCad's background rect is not the board outline: it's inflated ~10mm
beyond the page on every side, and it overhangs the SVG viewBox unevenly.
Worse, the B.Cu plot is mirrored, so that rect is shifted horizontally
relative to F.Cu — the left/right overhangs swap between the two layers.
A spec-compliant viewer clips everything to the viewBox, so the overhang
is invisible and the two layers look aligned. Bambu Suite's SVG importer
does NOT clip to the viewBox; it takes the real geometry bounding box (the
inflated rect) as the object extent. That overhang becomes an uneven,
layer-dependent border, and because it differs between F.Cu and B.Cu their
top-left corners no longer coincide — destroying double-sided alignment.

The fix is to make the output's geometry bbox equal the viewBox. We do the
actual clipping with Inkscape (`path-intersection` against a viewBox-sized
rectangle), not by editing coordinates in XML. The one thing Inkscape's
action API cannot do is synthesize a rectangle at given coordinates, so
`annotate()` injects a single `<rect>` matching the viewBox (id `bambu_clip`,
filled a neutral red so it's never mistaken for the black bg or white copper)
and the intersection consumes it. The copper geometry sits comfortably inside
the viewBox, so the holes are untouched by the clip.

The two-pass design is deliberate: `path-difference` in Inkscape only
gives the intended bottom-minus-top result on a 2-object selection;
extra paths in the selection are skipped silently. We also can't predict
the combined path's ID ahead of time, hence the intermediate read.

Drill-hole markers (small black `<circle>` elements inside `fill:#000000`
groups) are passed through unchanged. They land inside the now-transparent
pad regions and the laser will ablate them — harmless since those points
get physically drilled later.

Developed against
-----------------
Inkscape 1.4.3 (0d15f75, 2025-12-25). Action names and behaviors
(`select-by-id` accepting a comma-separated list, `path-union` working
with hundreds of paths, `export-overwrite` being a separate verb)
match this version; earlier 1.x releases may need tweaks.

Usage
-----
    ./make_bambu_ready.py midi-laser-pcb-F_Cu.svg
    ./make_bambu_ready.py *.svg
    ./make_bambu_ready.py --rotate 90 *.svg   # rotate outputs 90° clockwise
"""
from __future__ import annotations

import shutil
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Annotated

import typer
from lxml import etree
from rich.console import Console
from rich.panel import Panel
from rich.table import Table

SVG_NS = "http://www.w3.org/2000/svg"
GEOM_TAGS = {"rect", "path", "circle", "ellipse", "polygon", "polyline", "line"}

_WHITE = {"#fff", "#ffffff", "white"}
_BLACK = {"#000", "#000000", "black"}

OUTPUT_FILL = "#046307"
CLIP_ID = "bambu_clip"

console = Console(stderr=False, highlight=False)


# ---------- color resolution ----------


def _classify_color(value: str) -> str | None:
    v = value.strip().lower()
    if v in _WHITE:
        return "white"
    if v in _BLACK:
        return "black"
    if v == "none":
        return "none"
    return None


def _parse_style(style: str) -> dict[str, str]:
    if not style:
        return {}
    out: dict[str, str] = {}
    for chunk in style.split(";"):
        key, sep, value = chunk.partition(":")
        if sep:
            out[key.strip().lower()] = value.strip()
    return out


def _resolve_property(elem: etree._Element, prop: str) -> str | None:
    """Walk ancestors to find an effective value for a style/attribute property."""
    node: etree._Element | None = elem
    while node is not None:
        style_val = _parse_style(node.get("style", "")).get(prop)
        if style_val:
            return style_val
        attr_val = node.get(prop)
        if attr_val:
            return attr_val
        node = node.getparent()
    return None


def _resolve_render_color(elem: etree._Element) -> str | None:
    """Effective rendered color after stroke-to-path: fill if filled, else stroke."""
    fill = _resolve_property(elem, "fill")
    fill_class = _classify_color(fill) if fill else None
    if fill_class in {"white", "black"}:
        return fill_class
    if fill_class == "none" or fill_class is None:
        stroke = _resolve_property(elem, "stroke")
        if stroke:
            stroke_class = _classify_color(stroke)
            if stroke_class in {"white", "black"}:
                return stroke_class
    return None


# ---------- annotation ----------


def annotate(input_path: Path, working_path: Path) -> tuple[str, list[str], str | None]:
    """Assign IDs to geometric elements, returning (black_id, white_ids, clip_id).

    Also injects a viewBox-sized rectangle (`clip_id`) used later to clip the
    sheet to the page — see the module docstring for why. `clip_id` is None
    when the SVG has no viewBox, in which case the clip step is skipped.
    """
    tree = etree.parse(str(input_path))
    root = tree.getroot()

    black_id: str | None = None
    white_ids: list[str] = []

    for index, elem in enumerate(root.iter()):
        if etree.QName(elem.tag).localname not in GEOM_TAGS:
            continue
        color = _resolve_render_color(elem)
        if color is None:
            continue
        new_id = f"bambu_{index:05d}"
        elem.set("id", new_id)
        if color == "black" and black_id is None:
            black_id = new_id
        elif color == "white":
            white_ids.append(new_id)

    if black_id is None:
        raise typer.Exit(code=1)
    if not white_ids:
        raise typer.Exit(code=1)

    clip_id = _inject_viewbox_clip(root)

    tree.write(str(working_path), xml_declaration=True, encoding="UTF-8")
    return black_id, white_ids, clip_id


def _inject_viewbox_clip(root: etree._Element) -> str | None:
    """Append a viewBox-sized rect to use as the page-clip shape.

    Inkscape's action API has no verb to create a rectangle at given
    coordinates, so this single element is built in XML; the actual clipping
    is done by Inkscape (`path-intersection`). The rect is filled neutral red
    so `_resolve_render_color` never confuses it with the black background or
    the white copper, and it is added after the annotation loop so it never
    becomes `black_id`.
    """
    viewbox = root.get("viewBox")
    if not viewbox:
        return None
    try:
        minx, miny, width, height = (float(v) for v in viewbox.split())
    except ValueError:
        return None
    clip = etree.SubElement(root, f"{{{SVG_NS}}}rect")
    clip.set("id", CLIP_ID)
    clip.set("x", f"{minx:.6f}")
    clip.set("y", f"{miny:.6f}")
    clip.set("width", f"{width:.6f}")
    clip.set("height", f"{height:.6f}")
    clip.set("style", "fill:#FF0000;stroke:none")
    return CLIP_ID


# ---------- inkscape orchestration ----------


def _run_inkscape(working_svg: Path, actions: list[str]) -> None:
    cmd = ["inkscape", str(working_svg), f"--actions={';'.join(actions)}"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        console.print(f"[red]Inkscape failed (exit {result.returncode})[/red]")
        if result.stdout:
            console.print(result.stdout)
        if result.stderr:
            console.print(result.stderr)
        raise typer.Exit(code=result.returncode)


def _find_unique_white_path_id(svg_path: Path) -> str:
    """After path-union there is exactly one white-filled path left."""
    tree = etree.parse(str(svg_path))
    found: list[str] = []
    for p in tree.iter(f"{{{SVG_NS}}}path"):
        style = (p.get("style") or "").lower()
        if "fill:#ffffff" in style or "fill:#fff;" in style or "fill:white" in style:
            pid = p.get("id")
            if pid:
                found.append(pid)
    if len(found) != 1:
        console.print(
            f"[red]Expected exactly 1 white path after union, found {len(found)}[/red]"
        )
        raise typer.Exit(code=1)
    return found[0]


def recolor_output(svg_path: Path, new_fill: str = OUTPUT_FILL) -> None:
    """Replace every black fill (style or attribute, on any element) with new_fill.

    Runs as a post-process on the final Inkscape output, so the boolean
    pipeline still operates on the black/white source while the saved file
    uses the visual color we actually want for the laser job.
    """
    tree = etree.parse(str(svg_path))
    for elem in tree.iter():
        style = elem.get("style")
        if style:
            parts = []
            changed = False
            for chunk in style.split(";"):
                key, sep, value = chunk.partition(":")
                if sep and key.strip().lower() == "fill":
                    if _classify_color(value) == "black":
                        chunk = f"{key}:{new_fill}"
                        changed = True
                parts.append(chunk)
            if changed:
                elem.set("style", ";".join(parts))
        fill_attr = elem.get("fill")
        if fill_attr and _classify_color(fill_attr) == "black":
            elem.set("fill", new_fill)
    tree.write(str(svg_path), xml_declaration=True, encoding="UTF-8")


def rotate_output(svg_path: Path, degrees: int) -> None:
    """Rotate the finished sheet clockwise by 90/180/270°, as the final step.

    Runs dead last — after recolor, on the saved file — so nothing downstream
    can undo it. Inkscape rotates the geometry and `page-fit-to-selection`
    refits the page, so the output bbox still equals the viewBox (width/height
    swap for 90/270, keeping double-sided alignment intact). `page-fit` emits a
    unitless width/height, so we re-assert them in mm from the new viewBox
    afterwards (1 user unit == 1 mm, the KiCad convention). 0 is a no-op.
    """
    if degrees == 0:
        return
    rotations = {
        90: ["object-rotate-90-cw"],
        180: ["object-rotate-90-cw", "object-rotate-90-cw"],
        270: ["object-rotate-90-ccw"],
    }
    actions = [
        "select-all:all",
        *rotations[degrees],
        "page-fit-to-selection",
        "export-plain-svg",
        f"export-filename:{svg_path}",
        "export-overwrite", "export-do",
    ]
    _run_inkscape(svg_path, actions)
    _restore_mm_dimensions(svg_path)


def _restore_mm_dimensions(svg_path: Path) -> None:
    """Re-assert width/height in mm from the viewBox (page-fit emits unitless)."""
    tree = etree.parse(str(svg_path))
    root = tree.getroot()
    viewbox = root.get("viewBox")
    if not viewbox:
        return
    try:
        _, _, width, height = (float(v) for v in viewbox.split())
    except ValueError:
        return
    root.set("width", f"{width:.6f}mm")
    root.set("height", f"{height:.6f}mm")
    tree.write(str(svg_path), xml_declaration=True, encoding="UTF-8")


def run_inkscape(
    working_svg: Path,
    black_id: str,
    white_ids: list[str],
    clip_id: str | None,
    output: Path,
) -> None:
    intermediate = working_svg.with_name("step1.svg")

    pass1 = [
        "select-all:all", "object-to-path",
        "select-all:all", "object-stroke-to-path",
        "select-all:all", "selection-ungroup",
        "select-all:all", "selection-ungroup",
        "select-all:all", "selection-ungroup",
        "select-clear",
        f"select-by-id:{','.join(white_ids)}",
        "path-union",
        "export-plain-svg",
        f"export-filename:{intermediate}",
        "export-overwrite", "export-do",
    ]
    _run_inkscape(working_svg, pass1)

    combined_id = _find_unique_white_path_id(intermediate)

    pass2 = [
        "select-clear",
        f"select-by-id:{black_id},{combined_id}",
        "path-difference",
    ]
    # path-difference leaves its result selected; add the viewBox rect to that
    # selection and intersect to clip the sheet to the page (see docstring).
    if clip_id:
        pass2 += [f"select-by-id:{clip_id}", "path-intersection"]
    pass2 += [
        "export-plain-svg",
        f"export-filename:{output}",
        "export-overwrite", "export-do",
    ]
    _run_inkscape(intermediate, pass2)


# ---------- main processing ----------


def _humanize_bytes(n: int) -> str:
    for unit in ("B", "KiB", "MiB"):
        if n < 1024:
            return f"{n:.1f} {unit}"
        n /= 1024
    return f"{n:.1f} GiB"


def process(input_path: Path, rotate: int = 0) -> tuple[Path, int, int, float]:
    if not input_path.is_file():
        console.print(f"[red]Not a file:[/red] {input_path}")
        raise typer.Exit(code=1)
    output = input_path.with_name(f"{input_path.stem}-bambu-ready.svg")

    t0 = time.monotonic()
    with tempfile.TemporaryDirectory() as tmpdir:
        working = Path(tmpdir) / input_path.name
        black_id, white_ids, clip_id = annotate(input_path, working)
        run_inkscape(working, black_id, white_ids, clip_id, output)
    recolor_output(output)
    rotate_output(output, rotate)
    elapsed = time.monotonic() - t0

    return output, 1, len(white_ids), elapsed


def _inkscape_version() -> str:
    try:
        out = subprocess.run(
            ["inkscape", "--version"], capture_output=True, text=True, timeout=5
        )
        return out.stdout.strip().splitlines()[0] if out.stdout else "unknown"
    except Exception:
        return "unknown"


app = typer.Typer(
    add_completion=False,
    help="Convert KiCad copper-layer SVGs into laser-ready plain SVGs.",
    rich_markup_mode="rich",
)


@app.command()
def main(
    inputs: Annotated[
        list[Path],
        typer.Argument(
            help="KiCad SVG plot(s) to convert. Output goes next to each input "
            "as '{stem}-bambu-ready.svg'.",
            exists=True,
            file_okay=True,
            dir_okay=False,
            readable=True,
            resolve_path=True,
        ),
    ],
    rotate: Annotated[
        int,
        typer.Option(
            "--rotate",
            "-r",
            help="Rotate each output clockwise by 0, 90, 180, or 270 degrees. "
            "Applied as the very last step, after the page clip and recolor.",
        ),
    ] = 0,
) -> None:
    if rotate not in (0, 90, 180, 270):
        console.print("[red]--rotate must be one of: 0, 90, 180, 270[/red]")
        raise typer.Exit(code=1)
    if shutil.which("inkscape") is None:
        console.print("[red]inkscape not found on PATH[/red]")
        raise typer.Exit(code=1)

    console.print(
        Panel.fit(
            f"[bold]Bambu-ready SVG builder[/bold]\n"
            f"[dim]Using:[/dim] {_inkscape_version()}",
            border_style="cyan",
        )
    )

    table = Table(show_header=True, header_style="bold cyan")
    table.add_column("Input", style="white")
    table.add_column("Whites", justify="right", style="green")
    table.add_column("Output size", justify="right", style="magenta")
    table.add_column("Time", justify="right", style="yellow")
    table.add_column("Output", style="white")

    for path in inputs:
        with console.status(f"[cyan]Processing[/cyan] {path.name}…"):
            output, _blacks, whites, elapsed = process(path, rotate)
        table.add_row(
            path.name,
            str(whites),
            _humanize_bytes(output.stat().st_size),
            f"{elapsed:.2f}s",
            output.name,
        )

    console.print(table)
    console.print("[green]Done.[/green]")


if __name__ == "__main__":
    app()
