#!/bin/sh
# Adds a short clip to the media clone (./media/trimixxx/video), ready to
# commit to usr-ein/static-resources:
#   - <id>.mp4: H.264, at most 1280 px on its long side, silent unless --keep-audio, cut to
#     --from/--to if given, with ALL metadata dropped (a phone's video carries
#     its GPS position in it);
#   - <id>.jpg: its poster frame, from the clip's first second;
#   - <id>.yaml: to fill in: the label (required), a caption, the tags.
#
#   sh scripts/add-video.sh [--from 00:00:03] [--to 00:00:12] [--name boot] [--keep-audio] FILE
#
# A phone's HDR video (HLG or PQ) is tone-mapped to ordinary range, or it would
# look grey and flat in a browser. --name suffixes the id, for two clips cut
# from one file.
#
# Needs a working ffmpeg (FFMPEG=/path/to/ffmpeg if the one on PATH isn't),
# exiftool and ImageMagick.
set -eu

from=""
to=""
suffix=""
audio=no
while [ $# -gt 1 ]; do
  case "$1" in
    --from) from=$2; shift 2 ;;
    --to) to=$2; shift 2 ;;
    --name) suffix=-$2; shift 2 ;;
    --keep-audio) audio=yes; shift ;;
    *) break ;;
  esac
done
[ $# -eq 1 ] || { echo "usage: $0 [--from T] [--to T] [--name NAME] [--keep-audio] FILE" >&2; exit 2; }
src=$1

ffmpeg=${FFMPEG:-ffmpeg}
"$ffmpeg" -hide_banner -version >/dev/null 2>&1 \
  || { echo "ffmpeg doesn't run; set FFMPEG to a working one" >&2; exit 1; }

site=$(cd "$(dirname "$0")/.." && pwd)
out="$site/media/trimixxx/video"
[ -d "$site/media/.git" ] || { echo "no media clone: run npm run media first" >&2; exit 1; }
mkdir -p "$out"

name=$(basename "$src")
stem=$(printf '%s' "${name%.*}" | tr '[:upper:]' '[:lower:]' | tr -c 'a-z0-9\n' '-' | sed 's/--*/-/g; s/^-//; s/-$//')
taken=$(exiftool -s3 -d '%Y-%m-%d' -CreationDate -CreateDate "$src" | head -n 1)
case "$taken" in
  "" | 0000*) taken=$(date -r "$src" '+%Y-%m-%d') ;;
esac
id="$taken-$stem$suffix"

# At most 1280 px on the long side, landscape or portrait.
scale="scale=w='if(gte(iw,ih),min(1280,iw),-2)':h='if(gte(iw,ih),-2,min(1280,ih))':flags=lanczos"
filters="$scale,format=yuv420p"
if "$ffmpeg" -hide_banner -i "$src" 2>&1 | grep -q -E 'arib-std-b67|smpte2084'; then
  filters="zscale=t=linear:npl=100,format=gbrpf32le,zscale=p=bt709,tonemap=tonemap=hable:desat=0,zscale=t=bt709:m=bt709:r=tv,$scale,format=yuv420p"
fi

set -- -hide_banner -loglevel error -y
[ -n "$from" ] && set -- "$@" -ss "$from"
[ -n "$to" ] && set -- "$@" -to "$to"
set -- "$@" -i "$src" -map 0:v:0
[ "$audio" = yes ] && set -- "$@" -map 0:a:0? -c:a aac -b:a 128k
[ "$audio" = no ] && set -- "$@" -an
"$ffmpeg" "$@" -map_metadata -1 -map_chapters -1 \
  -vf "$filters" -colorspace bt709 -color_primaries bt709 -color_trc bt709 \
  -c:v libx264 -preset slow -crf 29 -movflags +faststart "$out/$id.mp4"

"$ffmpeg" -hide_banner -loglevel error -y -ss 0.5 -i "$out/$id.mp4" -frames:v 1 "$out/$id.png"
magick "$out/$id.png" -strip -quality 85 "$out/$id.jpg"
rm -f "$out/$id.png"

if [ -n "$(exiftool -s3 -GPS:all -Keys:GPSCoordinates -UserData:GPSCoordinates "$out/$id.mp4")" ]; then
  rm -f "$out/$id.mp4" "$out/$id.jpg"
  echo "$name: location survived; not added" >&2
  exit 1
fi

if [ ! -e "$out/$id.yaml" ]; then
  cat >"$out/$id.yaml" <<YAML
video: ./$id.mp4
poster: ./$id.jpg
# What the clip shows, for screen readers. Required.
label: ""
caption: ""
date: $taken
tags: [deck]
# credit: only when someone else took it
YAML
fi
echo "$id"
