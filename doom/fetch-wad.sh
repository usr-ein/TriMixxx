#!/usr/bin/env bash
# Fetch DOOM1.WAD -- the shareware episode, i.e. the Doom that "can it run Doom?"
# means -- into ./wad/, and prove it is the right file.
#
# The WAD is NOT committed: it is 4 MB of someone else's game. The shareware
# episode has been freely redistributable since 1993 (that was the whole point
# of shareware), so downloading it is fine; the full doom.wad is not, and if you
# own it, drop it in yourself:
#
#     ./fetch-wad.sh --from ~/games/doom.wad
#
# Anything Doom-shaped works, since Chocolate Doom takes any IWAD.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DEST_DIR="$HERE/wad"
DEST="$DEST_DIR/doom1.wad"

# DOOM1.WAD v1.9, the last shareware release and the one everybody has.
# Both hashes verified against a fresh download; the md5 is the value published
# in the Doom Wiki's IWAD table.
WANT_SIZE=4196020
WANT_MD5="f0cefca49926d00903cf57551d901abe"
WANT_SHA256="1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"

# Mirrors, tried in order. id's own idgames zip is deliberately NOT here: it
# ships the WAD as DEICE-compressed DOS installer chunks (DOOMS_19.1/.2), which
# would need a DOS unpacker to get at.
MIRRORS=(
    "https://github.com/Akbar30Bill/DOOM_wads/raw/master/doom1.wad"
    "https://archive.org/download/theultimatedoom_doom2_doom_doom64/doom1.wad"
)

usage() { echo "usage: $0 [--from <path-to-a-wad>]" >&2; exit 2; }

FROM=""
while [ $# -gt 0 ]; do
    case "$1" in
        --from) FROM="${2:-}"; [ -n "$FROM" ] || usage; shift 2 ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done

mkdir -p "$DEST_DIR"

# --- is it actually a WAD? ---------------------------------------------------
# The first four bytes of any Doom IWAD are the ASCII "IWAD". Checking this
# rather than only the hash is what makes a 404 page saved as doom1.wad fail
# here, loudly, instead of failing as "Doom won't start" on the deck.
check_iwad() {
    local f="$1" magic
    magic="$(head -c 4 "$f" | tr -d '\0')"
    if [ "$magic" != "IWAD" ] && [ "$magic" != "PWAD" ]; then
        echo "ERROR: $f does not start with IWAD/PWAD (got '${magic}') -- not a Doom WAD" >&2
        return 1
    fi
}

hash_of() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
    else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

if [ -n "$FROM" ]; then
    [ -f "$FROM" ] || { echo "ERROR: no such file: $FROM" >&2; exit 1; }
    check_iwad "$FROM"
    cp "$FROM" "$DEST"
    echo "copied $FROM -> $DEST"
    echo "sha256 $(hash_of "$DEST")  ($(wc -c < "$DEST" | tr -d ' ') bytes)"
    echo "(a WAD you supplied yourself; no hash check)"
    exit 0
fi

if [ -f "$DEST" ] && [ "$(hash_of "$DEST")" = "$WANT_SHA256" ]; then
    echo "$DEST is already the right file; nothing to do"
    exit 0
fi

tmp="$DEST_DIR/.doom1.wad.part"
trap 'rm -f "$tmp"' EXIT

got=""
for url in "${MIRRORS[@]}"; do
    echo "fetching $url"
    if curl -fsSL --max-time 180 -o "$tmp" "$url"; then got="$url"; break; fi
    echo "  failed, trying the next mirror"
done
[ -n "$got" ] || { echo "ERROR: could not download doom1.wad from any mirror" >&2; exit 1; }

check_iwad "$tmp"

size="$(wc -c < "$tmp" | tr -d ' ')"
md5="$( (command -v md5sum >/dev/null 2>&1 && md5sum "$tmp" | cut -d' ' -f1) || md5 -q "$tmp")"
sha="$(hash_of "$tmp")"

echo "size   $size"
echo "md5    $md5"
echo "sha256 $sha"

# A hash mismatch is a WARNING, not an error: there are six shareware releases
# (v1.0 through v1.9) and any of them plays. What must not pass is a file that
# is not a WAD at all, which check_iwad above has already settled.
if [ "$sha" != "$WANT_SHA256" ]; then
    echo
    echo "NOTE: this is not the v1.9 shareware WAD this script expects"
    echo "      (size $WANT_SIZE, md5 $WANT_MD5)."
    echo "      It is a valid WAD, so it will play -- just a different release."
fi

mv "$tmp" "$DEST"
trap - EXIT
echo
echo "-> $DEST"
