#!/bin/sh
# Boot splash for the deck: show the TriMixxx logo on the panel for a few
# seconds, then hand the screen back to the boot log. Installed as
# /usr/local/bin/trimixxx-splash and started by trimixxx-splash.service.
#
# The image is a raw framebuffer blob (see splash-render.py), so displaying it
# is one `cat` and needs nothing installed on the deck -- no plymouth, no fbi,
# no initramfs hook.
#
# The whole trick is the VT switch. Writing the logo onto tty1 would work for
# about a tenth of a second, until the next systemd line was printed over it;
# and hiding the log with `quiet splash` on the kernel command line is the usual
# fix, but the log is wanted here -- watching the deck's units come up is how
# you see the MIDI bridge or a USB mount fail at a gig. So the splash goes on an
# otherwise unused VT and takes the foreground. The console keeps printing to
# tty1 the whole time; a background VT's text just accumulates in its screen
# buffer instead of being drawn. Switching back at the end redraws it, so the
# boot log is not suppressed, only deferred -- it appears the moment the logo
# leaves, and boot carries on into X and Mixxx normally.
#
# The other half of the job is waiting for the right framebuffer to exist --
# there are two of them during a boot, and the image only fits one. That is the
# long comment further down.
#
# Nothing here delays boot: the service is Type=simple, so systemd considers it
# started as soon as this forks, and the wait and hold below run alongside the
# rest of the boot rather than in front of it.
set -eu

IMG="${SPLASH_IMAGE:-/usr/local/share/trimixxx/splash.raw}"
# "WIDTH HEIGHT BPP STRIDE" the image was rendered for, written by
# splash-install.sh. Checked rather than assumed -- see the wait below.
GEOM="${SPLASH_GEOM:-/usr/local/share/trimixxx/splash.geom}"
FB="${SPLASH_FB:-/dev/fb0}"
SYSFB="${SPLASH_SYSFB:-/sys/class/graphics/fb0}"
# Seconds to wait for that framebuffer to exist. It is not there when this unit
# starts; on the deck vc4 binds at about 6s.
WAIT="${SPLASH_WAIT:-20}"
# tty1 is the deck's console (and the VT Xorg runs on, `Xorg ... vt1 -keeptty`),
# tty2-6 are systemd's on-demand gettys. 7 is free.
VT="${SPLASH_VT:-7}"
HOLD="${1:-${SPLASH_HOLD:-8}}"

[ -r "$IMG" ] || { echo "no splash image at $IMG, skipping splash" >&2; exit 0; }
[ -r "$GEOM" ] || { echo "no geometry file at $GEOM, skipping splash" >&2; exit 0; }
want="$(cat "$GEOM")"

# What /dev/fb0 is RIGHT NOW: "WIDTH HEIGHT BPP STRIDE". virtual_size is
# comma-separated ("1024,600"); the rest are one number each.
current_geom() {
    [ -r "$SYSFB/virtual_size" ] || return 1
    size="$(tr ',' ' ' <"$SYSFB/virtual_size")"
    echo "$size $(cat "$SYSFB/bits_per_pixel") $(cat "$SYSFB/stride")"
}

# WAIT FOR THE RIGHT FRAMEBUFFER. There are two of them, one after the other,
# and blitting into the wrong one is the whole failure mode this guards against.
#
# The firmware hands the kernel a simplefb at 0.6s -- on the deck 720x576, 32bpp,
# 2880 bytes per row -- and that is what the early kernel log is printed on. vc4
# only binds around 6s, at which point fb0 is destroyed and re-created as
# 1024x600, 16bpp, 2048 bytes per row, and the console moves across.
#
# This unit is deliberately early, so it starts during the simplefb window. The
# image is packed for one exact layout, so writing it into the other lays
# 2048-byte rows into 2880-byte ones: every row starts further left than the
# last (a picture sheared diagonally) in colours that are the wrong format
# entirely. It looks like a corrupt boot, which is the opposite of the point.
#
# So: poll until the geometry is the one the image was rendered for, and if it
# never is, say so and show nothing. Never paint garbage.
i=0
while [ "$i" -lt "$((WAIT * 10))" ]; do
    now="$(current_geom || true)"
    [ "$now" = "$want" ] && break
    sleep 0.1
    i=$((i + 1))
done

if [ "${now:-}" != "$want" ]; then
    echo "framebuffer is [${now:-none}] after ${WAIT}s but the splash was" \
         "rendered for [$want]; skipping. Re-run splash-install.sh." >&2
    exit 0
fi

# fb0 registering and fbcon finishing its move onto it are not the same instant,
# and a redraw landing after the blit would wipe it.
sleep 0.3

# Back to the console whatever happens next -- an error, a shutdown mid-splash,
# or systemd stopping the unit. Leaving the deck on a blank spare VT would look
# exactly like a hung boot.
trap 'chvt 1 2>/dev/null || true' EXIT INT TERM

chvt "$VT"
# fbcon parks a blinking cursor in the top-left of the VT it just switched to,
# which sits on top of the logo. setterm writes the escape sequence for whatever
# TERM says, and this runs from systemd with no TERM at all.
TERM=linux setterm --cursor off >"/dev/tty$VT" 2>/dev/null || true
# After the switch, not before: fbcon clears a VT as it brings it forward.
cat "$IMG" >"$FB"

# One line in the journal saying what it drew and where. The previous version of
# this script had no such line, and "it looks wrong on the panel" was the only
# evidence there was that it had picked the wrong framebuffer.
echo "splash up on vt$VT, framebuffer [$now], holding ${HOLD}s"

sleep "$HOLD"
