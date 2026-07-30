#!/usr/bin/env bash
# Install (and optionally try out) the deck's boot splash.
#
# Three pieces go to the Pi: the logo pre-rendered into the panel's own
# framebuffer format, the script that blits it, and the unit that runs that
# script early in boot. See trimixxx-splash.sh for how the splash and the boot
# log share the screen, and splash-render.py for the rendering.
#
# The render happens HERE, not on the deck: the Pi ends up with a file it can
# `cat` at /dev/fb0 and needs no SVG renderer, image library or plymouth stack
# installed. The framebuffer layout that file has to match is read off the
# running Pi rather than hard-coded, so a panel swap or a config.txt change is
# picked up by the next deploy instead of turning the splash into static.
#
# Idempotent: re-running just re-renders and re-installs.
#
#   ./splash-install.sh              # install, do not disturb the screen
#   ./splash-install.sh --test       # install, then show it on the deck now
#   ./splash-install.sh --preview    # render and open the PNG here; no Pi needed
#   HOST=other ./splash-install.sh
#
# The boot hold is SPLASH_HOLD in trimixxx-splash.service (8s); --hold only
# changes how long --test keeps the screen.
set -euo pipefail

HOST="${HOST:-trimixxx-pi}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SVG="$HERE/trimixxx_logo_crt.svg"

TEST=0
PREVIEW=0
HOLD=5
while [ $# -gt 0 ]; do
    case "$1" in
        --test)    TEST=1 ;;
        --preview) PREVIEW=1 ;;
        --hold)    HOLD="$2"; shift ;;
        *)         echo "usage: $0 [--test] [--preview] [--hold SECONDS]" >&2; exit 2 ;;
    esac
    shift
done

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# ---- preview: render locally and look at it ----------------------------------
# The deck is across the room and its panel is the only place this is ever
# displayed, so being able to check a change to the artwork without deploying
# (or without a Pi at all) is worth the four lines.
if [ "$PREVIEW" = 1 ]; then
    uv run "$HERE/splash-render.py" "$SVG" -o "$TMP/splash.raw" --preview "$TMP/splash.png"
    cp "$TMP/splash.png" "${TMPDIR:-/tmp}/trimixxx-splash-preview.png"
    open "${TMPDIR:-/tmp}/trimixxx-splash-preview.png" 2>/dev/null ||
        echo "preview at ${TMPDIR:-/tmp}/trimixxx-splash-preview.png"
    exit 0
fi

# ---- what the deck's framebuffer actually looks like -------------------------
# virtual_size is "1024,600"; stride is bytes per row, which is NOT always
# width*bpp/8 (drivers pad), and getting it wrong shears the image diagonally.
echo "reading framebuffer geometry from $HOST"
# The trailing `echo` is not decoration: tr turns every newline into a space, so
# without it the stream ends at EOF with no delimiter, `read` returns non-zero
# and `set -e` ends the script here having printed nothing about why.
read -r FBW FBH FBBPP FBSTRIDE < <(
    ssh "$HOST" 'cd /sys/class/graphics/fb0 && cat virtual_size bits_per_pixel stride' |
        tr ',\n' ' '
    echo
)
echo "  ${FBW}x${FBH}, ${FBBPP}bpp, stride ${FBSTRIDE}"

# ---- render ------------------------------------------------------------------
uv run "$HERE/splash-render.py" "$SVG" \
    -o "$TMP/splash.raw" \
    --width "$FBW" --height "$FBH" --bpp "$FBBPP" --stride "$FBSTRIDE"

# ---- install -----------------------------------------------------------------
# The geometry travels with the image. At boot there are two framebuffers in
# succession -- the firmware's simplefb, then vc4's -- and the script uses this
# to wait for the one the image was actually packed for instead of painting a
# sheared, wrongly-coloured mess into the other. See trimixxx-splash.sh.
echo "$FBW $FBH $FBBPP $FBSTRIDE" >"$TMP/splash.geom"

scp -q "$TMP/splash.raw" "$TMP/splash.geom" \
    "$HERE/trimixxx-splash.sh" "$HERE/trimixxx-splash.service" "$HOST":/tmp/
ssh "$HOST" "bash -seu -- '$FBSTRIDE' '$FBH'" <<'REMOTE'
stride="$1"; height="$2"

sudo install -d -m 0755 /usr/local/share/trimixxx
sudo install -m 0644 /tmp/splash.raw            /usr/local/share/trimixxx/splash.raw
sudo install -m 0644 /tmp/splash.geom           /usr/local/share/trimixxx/splash.geom
sudo install -m 0755 /tmp/trimixxx-splash.sh    /usr/local/bin/trimixxx-splash
sudo install -m 0644 /tmp/trimixxx-splash.service /etc/systemd/system/trimixxx-splash.service
rm -f /tmp/splash.raw /tmp/splash.geom /tmp/trimixxx-splash.sh /tmp/trimixxx-splash.service

sudo systemctl daemon-reload
# Enabled, not started: starting it now would take the screen away from Mixxx
# for no reason. `--test` is the way to see it on demand.
sudo systemctl enable trimixxx-splash.service >/dev/null

# The one failure this can have that nothing else would report: an image whose
# size does not match the framebuffer. Too short and the bottom of the screen
# keeps whatever was there; too long and the write is truncated mid-row.
want=$((stride * height))
got=$(stat -c %s /usr/local/share/trimixxx/splash.raw)
if [ "$got" != "$want" ]; then
    echo "ERROR: splash.raw is $got bytes, framebuffer needs $want" >&2
    exit 1
fi
echo "splash.raw: $got bytes, matches the framebuffer"
systemctl is-enabled trimixxx-splash.service
REMOTE

# ---- optional live test ------------------------------------------------------
# Runs the real script, not a stand-in, so this exercises the VT switch and the
# blit exactly as boot will. Mixxx keeps running throughout -- Xorg is on tty1
# and simply loses the foreground while the logo is up, then redraws.
if [ "$TEST" = 1 ]; then
    echo "showing the splash on $HOST for ${HOLD}s (the deck's screen will leave Mixxx and come back)"
    ssh "$HOST" "sudo /usr/local/bin/trimixxx-splash '$HOLD'"
    echo "splash shown and screen handed back"
fi

echo "Boot splash installed."
