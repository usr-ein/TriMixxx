#!/usr/bin/env bash
# Build a format-coverage matrix from one source track, for testing which
# audio formats a CDJ accepts over ProLink and what the dbserver metadata
# reports for each.
#
# Every file's *title tag and filename carry its format*, so a track can be
# identified from the deck's display alone -- which is the point: we are trying
# to attribute two unknown metadata fields (docs/FINDINGS.md F31, F32), and the
# only way to read them is to know which file produced them.
#
# Note on MPEG-2: Layer III at 16-160 kbps exists only at 16/22.05/24 kHz. The
# 44.1/48 kHz rates are MPEG-1 by definition, so the MPEG-2 rows use the rates
# the standard actually allows.
set -euo pipefail

src=${1:-"test_tracks/6 SENSE - Mechanical Mania.mp3"}
out=${2:-"test_tracks/format-matrix"}
mkdir -p "$out"
n=0

emit() {  # emit <title> <extension> <ffmpeg args...>
    local title=$1 ext=$2; shift 2
    n=$((n + 1))
    local name; name=$(printf '%02d %s' "$n" "$title")
    ffmpeg -hide_banner -loglevel error -y -i "$src" -vn -map_metadata 0 \
        -metadata title="$title" \
        -metadata artist="FORMAT TEST" \
        -metadata track="$n" \
        "$@" "$out/$name.$ext"
    printf '  %-34s %8s KiB\n' "$name.$ext" "$(( $(stat -f%z "$out/$name.$ext") / 1024 ))"
}

echo "MP3 -- MPEG-1 (32-320 kbps, 44.1/48 kHz)"
for rate in 44100 48000; do
    label=$([ "$rate" = 44100 ] && echo 44k1 || echo 48k)
    for kbps in 32 64 128 192 256 320; do
        emit "MP3 MPEG1 ${kbps}k $label" mp3 \
            -c:a libmp3lame -b:a "${kbps}k" -ar "$rate" -ac 2
    done
done

echo "MP3 -- MPEG-2 (16-160 kbps, 22.05/24 kHz)"
for rate in 22050 24000; do
    label=$([ "$rate" = 22050 ] && echo 22k05 || echo 24k)
    for kbps in 16 32 64 96 128 160; do
        emit "MP3 MPEG2 ${kbps}k $label" mp3 \
            -c:a libmp3lame -b:a "${kbps}k" -ar "$rate" -ac 2
    done
done

echo "AAC (16-320 kbps, mono/stereo, 8-44.1 kHz)"
emit "AAC 16k 8k mono"     m4a -c:a aac -b:a 16k  -ar 8000  -ac 1
emit "AAC 32k 16k mono"    m4a -c:a aac -b:a 32k  -ar 16000 -ac 1
emit "AAC 64k 22k05 st"    m4a -c:a aac -b:a 64k  -ar 22050 -ac 2
emit "AAC 96k 32k st"      m4a -c:a aac -b:a 96k  -ar 32000 -ac 2
emit "AAC 128k 44k1 st"    m4a -c:a aac -b:a 128k -ar 44100 -ac 2
emit "AAC 192k 44k1 st"    m4a -c:a aac -b:a 192k -ar 44100 -ac 2
emit "AAC 256k 44k1 st"    m4a -c:a aac -b:a 256k -ar 44100 -ac 2
emit "AAC 320k 44k1 st"    m4a -c:a aac -b:a 320k -ar 44100 -ac 2

echo "WAV (16/24-bit, 44.1/48 kHz)"
emit "WAV 16b 44k1" wav -c:a pcm_s16le -ar 44100 -ac 2
emit "WAV 16b 48k"  wav -c:a pcm_s16le -ar 48000 -ac 2
emit "WAV 24b 44k1" wav -c:a pcm_s24le -ar 44100 -ac 2
emit "WAV 24b 48k"  wav -c:a pcm_s24le -ar 48000 -ac 2

# -write_id3v2 is required here: without it ffmpeg writes only AIFF's native
# NAME/ANNO chunks, which carry the title but silently drop artist, BPM and
# key -- so the files would analyse differently from every other row purely
# because of the container.
echo "AIFF (16/24-bit, 44.1/48 kHz)"
emit "AIFF 16b 44k1" aiff -c:a pcm_s16be -ar 44100 -ac 2 -write_id3v2 1
emit "AIFF 16b 48k"  aiff -c:a pcm_s16be -ar 48000 -ac 2 -write_id3v2 1
emit "AIFF 24b 44k1" aiff -c:a pcm_s24be -ar 44100 -ac 2 -write_id3v2 1
emit "AIFF 24b 48k"  aiff -c:a pcm_s24be -ar 48000 -ac 2 -write_id3v2 1

echo
echo "$n files, $(du -sh "$out" | cut -f1) total, in $out"
