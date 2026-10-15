#!/bin/sh
# Renders every board in ../boards for the site: a 3D view of the PCB (WebP,
# in src/assets/boards/) and its schematic sheets (SVG, in public/schematics/,
# plus a WebP preview of the root sheet in src/assets/schematics/).
#
# Run it on a Mac with KiCad 10 after a board changes, then commit what it wrote:
#   sh scripts/export-boards.sh
# Needs kicad-cli (KiCad.app), rsvg-convert and cwebp (brew install librsvg webp).
set -eu

KICAD_CLI=${KICAD_CLI:-/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli}
site=$(cd "$(dirname "$0")/.." && pwd)
boards=$(cd "$site/../boards" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# slug  project-directory  file-stem  side  rotation (x,y,z degrees)  zoom
while read -r slug dir stem side rotate zoom; do
  echo "== $slug"
  pcb="$boards/$dir/$stem.kicad_pcb"
  sch="$boards/$dir/$stem.kicad_sch"

  "$KICAD_CLI" pcb render --side "$side" --quality high --perspective --floor \
    --rotate "$rotate" --zoom "$zoom" --background transparent \
    --width 2000 --height 1300 --output "$work/$slug.png" "$pcb" >/dev/null
  cwebp -quiet -q 88 -alpha_q 100 "$work/$slug.png" -o "$site/src/assets/boards/$slug.webp"

  rm -rf "${site:?}/public/schematics/$slug"
  mkdir -p "$site/public/schematics/$slug"
  "$KICAD_CLI" sch export svg --exclude-drawing-sheet --no-background-color \
    --output "$site/public/schematics/$slug" "$sch" >/dev/null 2>&1
  rsvg-convert --width 2400 --background-color '#f5f4ef' \
    "$site/public/schematics/$slug/$stem.svg" --output "$work/$slug-sch.png"
  cwebp -quiet -q 85 "$work/$slug-sch.png" -o "$site/src/assets/schematics/$slug.webp"
done <<'BOARDS'
midi-s3-mini midi_s3_mini midi_s3_mini top -38,0,-18 0.85
one-button one_button OneButton top -40,0,25 0.75
play-cue play_cue_btn play_cue_btn top -40,0,-15 0.8
loop loop_btn loop_btn bottom -40,0,-15 1
midi-laser midi-laser-pcb midi-laser-pcb top -38,0,-18 0.85
BOARDS

ls -la "$site/src/assets/boards" "$site/src/assets/schematics"
du -sh "$site/public/schematics"/*
