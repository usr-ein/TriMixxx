#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# ///
"""Render trimixxx.svg into trimixxx.ico, the icon compiled into pi-qemu (see
../CMakeLists.txt). Run it after changing the SVG, and commit both.

Each size is drawn from the SVG at that size rather than scaled down from the
largest, so the small ones stay sharp: the T sits on a 16-unit grid and lands
on whole pixels at 16, 32, 48 and 64. The Dock shows 256 on a Retina screen.

rsvg-convert does the drawing (librsvg, as for the splash: pi_config/
splash-render.py). The .ico is its PNGs as they are, behind the format's table
of sizes -- an icon entry may be a whole PNG file, which Qt, macOS and Windows
since Vista all read -- so the file stays small, and the same SVG always makes
the same bytes.

Needs: brew install librsvg
"""

import shutil
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SIZES = [16, 24, 32, 48, 64, 128, 256]

rsvg = shutil.which("rsvg-convert")
if not rsvg:
    sys.exit("make-ico: rsvg-convert not found -- `brew install librsvg`")
pngs = [
    subprocess.run(
        [rsvg, "--width", str(s), "--height", str(s), str(HERE / "trimixxx.svg")],
        check=True,
        capture_output=True,
    ).stdout
    for s in SIZES
]

# ICONDIR (reserved, type 1 = icon, count), one 16-byte ICONDIRENTRY per image
# (width and height in a byte each, 0 meaning 256; no palette; 1 plane; 32 bpp;
# its length and offset), then the images.
ico = struct.pack("<HHH", 0, 1, len(SIZES))
offset = len(ico) + 16 * len(SIZES)
for size, png in zip(SIZES, pngs):
    ico += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(png), offset)
    offset += len(png)
(HERE / "trimixxx.ico").write_bytes(ico + b"".join(pngs))
print(f"==> {HERE / 'trimixxx.ico'}")
