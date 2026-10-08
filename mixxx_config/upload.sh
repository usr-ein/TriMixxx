#!/usr/bin/env bash

set -eux

# ssh alias for the deck's Pi. Override for a one-off: HOST=other ./upload.sh
HOST="${HOST:-trimixxx-pi}"

# ---- Validate every XML BEFORE anything leaves this machine ----------------
# A malformed skin is not a loud failure on the deck: Qt rejects the whole file
# and Mixxx quietly boots its default skin instead, so the deck comes back up
# looking wrong with nothing in the log to explain it, and the obvious reading
# is "the upload did nothing". Parse it here, where a traceback is unmissable.
#
# The trap worth knowing: XML forbids a double hyphen INSIDE a comment. Writing
# a CLI flag in a skin comment is enough to reject the file.
#
# This runs first, before the `rm -rf` below, so a bad file cannot leave the
# deck with its skin deleted and no replacement.
echo "Validating XML before upload..."
python3 - <<'PY' || { echo "ABORT: XML validation failed, nothing was uploaded." >&2; exit 1; }
import glob, os, sys, xml.dom.minidom

files = sorted(set(
    glob.glob("TriMixxx_skin/**/*.xml", recursive=True)
    + ["TriMixxx.midi.xml", "PiMidiDaemon.midi.xml", "soundconfig.xml"]
))
bad = 0
for f in files:
    if not os.path.exists(f):
        print(f"  MISSING  {f}", file=sys.stderr)
        bad += 1
        continue
    try:
        xml.dom.minidom.parse(f)
    except Exception as e:
        print(f"  INVALID  {f}: {e}", file=sys.stderr)
        bad += 1
    else:
        print(f"  ok       {f}")
sys.exit(1 if bad else 0)
PY

# ---- This deck's wiring ----------------------------------------------------
# Ring pads are numbered by their place in the daisy chain and PLAY/CUE by the
# GPIO they land on, so a deck wired in a different order sends the right MIDI
# for the wrong button. Such a deck has a units/<hostname>.json saying where
# each control actually landed, and the mapping and script are rewritten for it
# on the way out (units/apply.py); the files in this folder stay canonical. A
# deck with no file -- the standard wiring -- gets them untouched.
#
# Keyed on the deck's hostname, not on $HOST, which is only this machine's ssh
# alias for it. Done before anything on the deck is touched, so a bad wiring
# file stops the upload here.
DECK="$(ssh "$HOST" hostname)"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
# The skin is staged too: a unit file can say the panel has no bezel, or give
# the deck an accent colour of its own, and both are written into the skin.
cp -R TriMixxx_skin "$STAGE/"
if [ -f "units/$DECK.json" ]; then
    python3 units/apply.py "units/$DECK.json" . "$STAGE"
else
    echo "==> $DECK: standard wiring"
    cp TriMixxx.midi.xml TriMixxx.scripts.js "$STAGE/"
fi

# ---- Every deck's skin and mapping, for a release card --------------------------
# A release card runs on any deck (pi-qemu/PLAN.md Part 1): at each start its
# identity unit links the deck's own files from /usr/share/trimixxx/decks/<deck>
# into ~/.mixxx, or from .../standard for a deck wired as the canonical files
# say. So every unit file is rendered here, whatever deck this is. Nothing
# reads them on a dev deck.
DECKS="$(mktemp -d)"
trap 'rm -rf "$STAGE" "$DECKS"' EXIT
for unit in units/*.json; do
    d="$DECKS/$(basename "$unit" .json)"
    mkdir -p "$d"
    cp -R TriMixxx_skin "$d/"
    python3 units/apply.py "$unit" . "$d" >/dev/null
done
mkdir -p "$DECKS/standard"
cp -R TriMixxx_skin "$DECKS/standard/"
cp TriMixxx.midi.xml TriMixxx.scripts.js "$DECKS/standard/"
tar -C "$DECKS" -cf - . | ssh "$HOST" '
    sudo rm -rf /usr/share/trimixxx/decks.new
    sudo mkdir -p /usr/share/trimixxx/decks.new
    sudo tar -C /usr/share/trimixxx/decks.new --no-same-owner -xf -
    sudo chmod -R u=rwX,go=rX /usr/share/trimixxx/decks.new
    sudo rm -rf /usr/share/trimixxx/decks
    sudo mv /usr/share/trimixxx/decks.new /usr/share/trimixxx/decks
    ls /usr/share/trimixxx/decks'

# ---- Fonts -----------------------------------------------------------------
# MesloLGL Nerd Font is not a stock Raspberry Pi OS font, and both
# mixxx.cfg ([Library] Font) and the skin's stylesheet name it. Qt resolves font
# families through fontconfig BY NAME, so an uninstalled font is not an error
# anywhere: it silently falls back, and the deck comes up looking subtly wrong.
# Ship it rather than leaving it as Pi-local state a re-image would not restore.
#
# ~/.local/share/fonts is the per-user fontconfig directory, so no sudo and no
# system font path is touched. This is the ONE thing upload.sh puts outside
# ~/.mixxx.
#
# Runs before the rm -rf below for the same reason the XML check does: a failure
# here must not leave the deck with its skin deleted.
echo "Installing fonts..."
ssh "$HOST" 'mkdir -p ~/.local/share/fonts'
scp fonts/*.ttf "$HOST":~/.local/share/fonts/
# Verify the deck can actually RESOLVE the family, rather than trusting that
# copying files was enough. This is the check that would have caught a renamed
# font file or a stale fontconfig cache.
ssh "$HOST" 'fc-cache -f ~/.local/share/fonts >/dev/null 2>&1;
             fc-list : family | grep -q "MesloLGL Nerd Font"' \
    || { echo "ABORT: the deck cannot resolve 'MesloLGL Nerd Font' after install." >&2; exit 1; }

# A deck that has never run Mixxx has no ~/.mixxx at all, and scp into a missing
# directory fails -- so a fresh unit needs these before the first copy.
ssh "$HOST" 'mkdir -p ~/.mixxx/skins ~/.mixxx/controllers && rm -rf ~/.mixxx/skins/TriMixxx'

scp -r "$STAGE/TriMixxx_skin" "$HOST":~/.mixxx/skins/
ssh "$HOST" 'mv ~/.mixxx/skins/TriMixxx_skin ~/.mixxx/skins/TriMixxx'
# soundconfig.xml is the audio device + buffer config, and it is SEPARATE from
# mixxx.cfg -- Mixxx keeps sound hardware in its own file, so before this it was
# Pi-local state that no re-image would reproduce.
#
# Its `latency` attribute is NOT milliseconds: it is an index into a power-of-2
# ladder, frames = bit_ceil(samplerate_khz) << (latency - 1) (soundmanagerconfig
# .cpp). At 44100 that is 64 << (latency-1), so each step DOUBLES the buffer:
#   3 = 256 fr = 5.8ms    4 = 512 fr = 11.6ms    5 = 1024 fr = 23.2ms
# It is worth caring about beyond plain output latency: Mixxx filters the `jog`
# pitch-bend control through a 25-tap moving average whose taps are BUFFERS, not
# ms (ratecontrol.cpp), so bend smear = 25 * buffer. At 5 that was ~580ms of
# lag-in on the jog; 4 halves it to ~290ms. Drop it further only as far as the
# USB DAC tolerates -- too low and you get xruns mid-set. Scratch does not go
# through that filter and barely notices this.
#
# Mixxx REWRITES this file at startup once it has set up devices, so do not put
# comments in it (they will not survive) and re-pull it after changing sound
# prefs on the deck itself. Nothing writes it on shutdown, so scp-then-restart
# below is safe: the dying instance will not clobber what we just pushed.
# mixxx.cfg is never written by the fork (CoreServices::finalize()), so what is
# copied here is exactly what the deck runs with, until the next upload. Stock
# Mixxx does rewrite it from memory when it exits, which would clobber a copy
# made while it ran; stopping first, then copying, keeps that safe as well, and
# the new instance reads what we sent.
ssh "$HOST" 'sudo systemctl stop getty@tty1.service' || true
scp mixxx.cfg soundconfig.xml "$HOST":~/.mixxx/
scp "$STAGE/TriMixxx.midi.xml" "$STAGE/TriMixxx.scripts.js" \
    PiMidiDaemon.midi.xml PiMidiDaemon.scripts.js "$HOST":~/.mixxx/controllers/
ssh "$HOST" 'sudo systemctl restart getty@tty1.service'
echo Upload done
