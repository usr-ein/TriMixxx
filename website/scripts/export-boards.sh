#!/bin/sh
# Exports the boards for the site, from their KiCad sources in ../boards:
#   renders     a 3D view of each PCB (WebP, in src/assets/boards/);
#   schematics  every schematic sheet (SVG, in public/schematics/) and a WebP
#               preview of the root sheet (in src/assets/schematics/), drawn in
#               KiCad's Vampire colour theme on the site's near-black;
#   models      each board in 3D, for the site's viewer: GLB, its board's own
#               colours decoded (kicad-glb-colors.mjs), compressed by gltfpack,
#               in the media clone's trimixxx/models/, to commit to
#               usr-ein/static-resources. The never-built main board comes from
#               the last commit that had it, with the libraries it used then.
#
#   sh scripts/export-boards.sh [renders] [schematics] [models]
#
# With no argument it does all three. Run it on a Mac with KiCad 10 after a
# board changes, then commit what it wrote. Needs kicad-cli (KiCad.app),
# rsvg-convert and cwebp (brew install librsvg webp), the Vampire colour theme
# (KiCad's Plugin and Content Manager, Colour themes), and for models a media
# clone (npm run media) and npx, which fetches gltfpack.
set -eu

KICAD_CLI=${KICAD_CLI:-/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli}
KICAD_MODELS=${KICAD_MODELS:-$(dirname "$KICAD_CLI")/../SharedSupport/3dmodels}
KICAD9_THIRD_PARTY=${KICAD9_THIRD_PARTY:-$HOME/Documents/KiCad/9.0/3rdparty}
THEME=${THEME:-vampire}
BACKGROUND='#0b0c0b'
site=$(cd "$(dirname "$0")/.." && pwd)
repo=$(cd "$site/.." && pwd)
boards="$repo/boards"
models="$site/media/trimixxx/models"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

parts=${*:-renders schematics models}
want() {
  case " $parts " in *" $1 "*) return 0 ;; *) return 1 ;; esac
}
if want models; then
  [ -d "$site/media/.git" ] || { echo "no media clone: run npm run media first" >&2; exit 1; }
  mkdir -p "$models"
fi

gltfpack() {
  npx --yes gltfpack@1.3.0 "$@"
}

# render SLUG PCB SIDE ROTATION ZOOM
render() {
  "$KICAD_CLI" pcb render --side "$3" --quality high --perspective --floor \
    --rotate "$4" --zoom "$5" --background transparent \
    --width 2000 --height 1300 --output "$work/$1.png" "$2" >/dev/null
  cwebp -quiet -q 88 -alpha_q 100 "$work/$1.png" -o "$site/src/assets/boards/$1.webp"
}

# schematics SLUG SCH STEM
schematics() {
  out="$site/public/schematics/$1"
  rm -rf "$out"
  mkdir -p "$out"
  "$KICAD_CLI" sch export svg --theme "$THEME" --exclude-drawing-sheet --no-background-color \
    --output "$out" "$2" >/dev/null 2>&1
  # A browser shows an SVG opened on its own on white: give every sheet the
  # site's background, as a rectangle under the drawing and on the root
  # element, whose background paints the whole window around the sheet.
  for svg in "$out"/*.svg; do
    perl -0pi -e "s/<svg\\b([^>]*)>/<svg style=\"background-color: $BACKGROUND\"\$1>\\n<rect width=\"100%\" height=\"100%\" fill=\"$BACKGROUND\"\\/>/" "$svg"
  done
  rsvg-convert --width 2400 --background-color "$BACKGROUND" "$out/$3.svg" --output "$work/$1-sch.png"
  cwebp -quiet -q 85 "$work/$1-sch.png" -o "$site/src/assets/schematics/$1.webp"
}

# model SLUG PCB [KICAD-CLI OPTION...]
model() {
  slug=$1
  pcb=$2
  shift 2
  "$KICAD_CLI" pcb export glb --subst-models --include-tracks --include-pads --include-zones \
    --include-silkscreen --include-soldermask --force "$@" --output "$work/$slug.glb" "$pcb" \
    >/dev/null 2>&1
  node "$site/scripts/kicad-glb-colors.mjs" "$work/$slug.glb" >/dev/null
  gltfpack -cc -i "$work/$slug.glb" -o "$models/$slug.glb" >/dev/null
}

# slug  project-directory  file-stem  side  rotation (x,y,z degrees)  zoom
while read -r slug dir stem side rotate zoom; do
  echo "== $slug"
  pcb="$boards/$dir/$stem.kicad_pcb"
  if want renders; then render "$slug" "$pcb" "$side" "$rotate" "$zoom"; fi
  if want schematics; then schematics "$slug" "$boards/$dir/$stem.kicad_sch" "$stem"; fi
  if want models; then model "$slug" "$pcb"; fi
done <<'BOARDS'
midi-s3-mini midi_s3_mini midi_s3_mini top -38,0,-18 0.85
one-button one_button OneButton top -40,0,25 0.75
play-cue play_cue_btn play_cue_btn top -40,0,-15 0.8
loop loop_btn loop_btn bottom -40,0,-15 1
midi-laser midi-laser-pcb midi-laser-pcb top -38,0,-18 0.85
BOARDS

if want models; then
  echo "== mainboard"
  old="$work/mainboard"
  mkdir -p "$old"
  git -C "$repo" archive 02997e681e52c54710ede9e3cca202f4118eaf40 \
    CDJ-MainBoard JLC2KiCad_lib kicad-thirdparty-footprints | tar -x -C "$old"
  # Its footprints name models relative to the project, and one by its path on
  # the Mac it was drawn on; the rest by KiCad 8 and 9's library variables.
  # JLC2KiCad gave four SOT-26 models an offset of about a metre, which would
  # park them far off the board: offsets over 100 mm go back to zero.
  ln -s ../JLC2KiCad_lib/footprint/packages3d "$old/CDJ-MainBoard/packages3d"
  for step in "$old"/kicad-thirdparty-footprints/*.step; do
    ln -s "$step" "$old/CDJ-MainBoard/"
  done
  perl -pi -e "s|\"/Users/[^/\"]+/Documents/CustomDJ/|\"$old/|g" "$old/CDJ-MainBoard/CDJ-MainBoard.kicad_pcb"
  perl -0pi -e 's{(\(offset\s*\(xyz )(\S+) (\S+) (\S+)\)}{abs($2) > 100 || abs($3) > 100 ? "${1}0 0 0)" : "$1$2 $3 $4)"}ge' \
    "$old/CDJ-MainBoard/CDJ-MainBoard.kicad_pcb"
  model mainboard "$old/CDJ-MainBoard/CDJ-MainBoard.kicad_pcb" \
    --define-var "KICAD9_3DMODEL_DIR=$KICAD_MODELS" \
    --define-var "KICAD8_3RD_PARTY=$KICAD9_THIRD_PARTY"
fi

if want renders; then ls -la "$site/src/assets/boards"; fi
if want schematics; then du -sh "$site/public/schematics"/* "$site/src/assets/schematics"; fi
if want models; then ls -la "$models"; fi
