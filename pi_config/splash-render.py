#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow"]
# ///
"""Render the deck's logo SVG into a raw framebuffer blob for the boot splash.

The splash is shown at boot by `cat`ing a file straight onto /dev/fb0 (see
trimixxx-splash.sh), so the whole point of this script is to do all the work
HERE, on the machine deploying, and leave the Pi with a file whose bytes are
already in its framebuffer's exact layout: no image library, no PNG decoder, no
SVG renderer, nothing to install on the deck at all.

That layout is not a guess -- splash-install.sh reads it off the running Pi
(/sys/class/graphics/fb0/{virtual_size,bits_per_pixel,stride}) and passes it in,
so a panel or config.txt change is picked up on the next deploy instead of
silently producing a screen of colourful noise. The deck today reports
1024x600, 16 bpp, stride 2048, and `fbset -i` spells the channels out as
"rgba 5/11,6/5,5/0" -- i.e. RGB565, little endian, no padding per row.

SVG rasterising is rsvg-convert's job (librsvg is what the artwork was drawn
against; the filters, masks and clip paths all land where they should). Pillow
only composites and packs.

Usage (geometry defaults to the deck's panel):
    ./splash-render.py logo.svg -o splash.raw
    ./splash-render.py logo.svg -o splash.raw --preview /tmp/splash.png
"""

import argparse
import shutil
import subprocess
import sys
from io import BytesIO
from pathlib import Path

from PIL import Image

# The artwork's own background, so the logo card bleeds into the surrounding
# screen instead of sitting in a letterbox. This is the SVG's inner CRT panel
# fill (#07190f) rather than the outer card (#04110a): the card carries a hard
# black offset shadow down and to the right, and against the darker outer fill
# that shadow is invisible.
DEFAULT_BG = "#07190f"

# Fraction of the screen the artwork spans. The rest is margin -- a splash that
# runs edge to edge on a 7" panel reads as a glitch, not as a logo.
DEFAULT_FILL = 0.92


def rasterise(svg: Path, width: int) -> Image.Image:
    """SVG -> RGBA bitmap `width` px wide, aspect preserved."""
    rsvg = shutil.which("rsvg-convert")
    if not rsvg:
        sys.exit("splash-render: rsvg-convert not found -- `brew install librsvg`")
    png = subprocess.run(
        [rsvg, "--width", str(width), "--keep-aspect-ratio", str(svg)],
        check=True,
        capture_output=True,
    ).stdout
    return Image.open(BytesIO(png)).convert("RGBA")


def compose(svg: Path, w: int, h: int, bg: str, fill: float) -> Image.Image:
    """The full screen: artwork scaled to `fill` of it, centred on `bg`."""
    art = rasterise(svg, round(w * fill))
    # Rasterise, then re-rasterise if the height overflowed. Cheaper in code
    # than parsing the SVG's viewBox to predict the aspect ratio, and it stays
    # correct if the artwork is ever redrawn at different proportions.
    if art.height > h * fill:
        art = rasterise(svg, round(art.width * (h * fill) / art.height))

    screen = Image.new("RGBA", (w, h), bg)
    screen.alpha_composite(art, ((w - art.width) // 2, (h - art.height) // 2))
    return screen.convert("RGB")


def pack(img: Image.Image, bpp: int, stride: int) -> bytes:
    """RGB888 -> the framebuffer's own byte layout, one `stride` per row.

    Written out rather than handed to Pillow's raw-mode packer because the
    packing IS the interface with the kernel here: if it is wrong the deck shows
    static, and there is no error anywhere to tell you why.
    """
    w, h = img.size
    src = img.tobytes()
    out = bytearray(stride * h)
    for y in range(h):
        i = y * w * 3
        o = y * stride
        if bpp == 16:  # RGB565 little endian: rrrrrggg gggbbbbb
            for _ in range(w):
                v = ((src[i] & 0xF8) << 8) | ((src[i + 1] & 0xFC) << 3) | (src[i + 2] >> 3)
                out[o] = v & 0xFF
                out[o + 1] = v >> 8
                i += 3
                o += 2
        else:  # XRGB8888 little endian: B, G, R, unused
            for _ in range(w):
                out[o] = src[i + 2]
                out[o + 1] = src[i + 1]
                out[o + 2] = src[i]
                i += 3
                o += 4
    return bytes(out)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("svg", type=Path)
    ap.add_argument("-o", "--out", type=Path, required=True, help="raw blob to write")
    ap.add_argument("--width", type=int, default=1024)
    ap.add_argument("--height", type=int, default=600)
    ap.add_argument("--bpp", type=int, default=16, choices=(16, 32))
    ap.add_argument("--stride", type=int, default=0, help="row bytes (default: width*bpp/8)")
    ap.add_argument("--bg", default=DEFAULT_BG)
    ap.add_argument("--fill", type=float, default=DEFAULT_FILL)
    ap.add_argument("--preview", type=Path, help="also write the composed image as a PNG")
    args = ap.parse_args()

    stride = args.stride or args.width * args.bpp // 8
    if stride < args.width * args.bpp // 8:
        sys.exit(f"splash-render: stride {stride} too small for {args.width}px at {args.bpp}bpp")

    img = compose(args.svg, args.width, args.height, args.bg, args.fill)
    if args.preview:
        img.save(args.preview)
    args.out.write_bytes(pack(img, args.bpp, stride))
    print(
        f"splash-render: {args.out} "
        f"({args.width}x{args.height}, {args.bpp}bpp, stride {stride}, "
        f"{args.out.stat().st_size} bytes)"
    )


if __name__ == "__main__":
    main()
