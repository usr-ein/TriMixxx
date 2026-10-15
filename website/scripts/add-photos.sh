#!/bin/sh
# Adds photographs to the media clone (./media/trimixxx/photos), ready to commit
# to usr-ein/static-resources. Each original (HEIC, JPEG or PNG, from a phone
# or a camera) becomes:
#   - a JPEG at most 2560 px on its long side, turned upright, quality 85,
#     with ALL metadata stripped: no GPS position, no camera serial, no dates;
#   - a YAML file beside it, to fill in: the alt text (required), a caption,
#     the tags, and the date, taken from the original's EXIF before stripping.
#
#   sh scripts/add-photos.sh [--tag TAG] [--crop L,T,R,B] [--rotate DEG] [--name ID] FILE...
#
# --tag is the gallery section: deck, event, build, boards or detail; screen
# keeps a screenshot out of the gallery.
# --crop keeps a rectangle given in percent of the upright photo, left, top,
# right and bottom edges: --crop 0,32,100,74 keeps the middle band. --rotate
# turns the result (90, -90, 180), for a photo taken sideways. --name replaces
# the id, which is otherwise the capture date and the file's name:
# 2026-10-04-img-8410. Existing YAML files are never overwritten.
#
# Needs exiftool and ImageMagick (brew install exiftool imagemagick); HEIC
# goes through macOS's sips.
set -eu

tag=deck
crop=""
rotate=0
name=""
while [ $# -gt 0 ]; do
  case "$1" in
    --tag) tag=$2; shift 2 ;;
    --crop) crop=$2; shift 2 ;;
    --rotate) rotate=$2; shift 2 ;;
    --name) name=$2; shift 2 ;;
    *) break ;;
  esac
done
[ $# -gt 0 ] || { echo "usage: $0 [--tag TAG] [--crop L,T,R,B] [--rotate DEG] [--name ID] FILE..." >&2; exit 2; }

site=$(cd "$(dirname "$0")/.." && pwd)
out="$site/media/trimixxx/photos"
[ -d "$site/media/.git" ] || { echo "no media clone: run npm run media first" >&2; exit 1; }
mkdir -p "$out"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# "L,T,R,B" in percent of a W x H image, as an ImageMagick crop geometry.
geometry() {
  echo "$1" | awk -F, -v w="$2" -v h="$3" \
    '{ printf "%dx%d+%d+%d", w * ($3 - $1) / 100, h * ($4 - $2) / 100, w * $1 / 100, h * $2 / 100 }'
}

# convert INPUT OUTPUT: upright, cropped, turned, resized, stripped.
convert() {
  set -- "$1" "$2" -auto-orient
  if [ -n "$crop" ]; then
    size=$(magick "$1" -auto-orient -format '%w %h' info:)
    set -- "$@" -crop "$(geometry "$crop" "${size% *}" "${size#* }")" +repage
  fi
  [ "$rotate" = 0 ] || set -- "$@" -rotate "$rotate"
  in=$1
  dest=$2
  shift 2
  magick "$in" "$@" -resize '2560x2560>' -strip \
    -sampling-factor 4:2:0 -interlace JPEG -quality 85 "$dest"
}

for src in "$@"; do
  file=$(basename "$src")
  stem=$(printf '%s' "${file%.*}" | tr '[:upper:]' '[:lower:]' | tr -c 'a-z0-9\n' '-' | sed 's/--*/-/g; s/^-//; s/-$//')
  taken=$(exiftool -s3 -d '%Y-%m-%d' -DateTimeOriginal -CreateDate "$src" | head -n 1)
  case "$taken" in
    "" | 0000*) taken=$(date -r "$src" '+%Y-%m-%d') ;;
  esac
  id=${name:-"$taken-$stem"}

  case "$file" in
    *.heic | *.HEIC | *.heif | *.HEIF)
      sips -s format jpeg "$src" --out "$work/$id.jpg" >/dev/null
      input="$work/$id.jpg"
      ;;
    *) input=$src ;;
  esac

  convert "$input" "$out/$id.jpg"

  # -strip should leave nothing; check, and refuse to keep a photo that still
  # says where it was taken.
  if [ -n "$(exiftool -s3 -GPS:all "$out/$id.jpg")" ]; then
    rm -f "$out/$id.jpg"
    echo "$file: GPS survived stripping; not added" >&2
    exit 1
  fi

  if [ ! -e "$out/$id.yaml" ]; then
    cat >"$out/$id.yaml" <<YAML
image: ./$id.jpg
# What someone who can't see the photo needs to know. Required.
alt: ""
caption: ""
date: $taken
# One or more of: deck, event, build, boards, detail, screen. The first is its gallery section (none for screen).
tags: [$tag]
# credit: only when someone else took it
featured: false
YAML
  fi
  echo "$id"
done
