#!/usr/bin/env bash
# Deploy step 005 (lib.sh has the contract): Mixxx's configuration, from
# mixxx_config/ -- the skin, the mapping and its scripts, mixxx.cfg, the sound
# config, the UI font -- on this deck's wiring. User space only (~/.mixxx, and
# the font in ~/.local/share/fonts), plus every deck's rendering for a release
# card. Mixxx restarts with it (exit 12).
. "$(dirname "$0")/lib.sh"
cd mixxx_config

# ---- every XML checked before anything leaves the Mac -----------------------
# A malformed skin is not a loud failure on the deck: Qt rejects the file and
# Mixxx quietly boots its default skin, with nothing in the log to say why.
# Parsed here, where a traceback is unmissable, and before the skin on the deck
# is deleted below. The trap: XML forbids a double hyphen INSIDE a comment, so
# a command-line flag written in a skin comment is enough.
python3 - <<'PY' || { echo "XML validation failed: nothing was uploaded" >&2; exit 1; }
import glob, os, sys, xml.dom.minidom
files = sorted(set(glob.glob("TriMixxx_skin/**/*.xml", recursive=True)
                   + ["TriMixxx.midi.xml", "PiMidiDaemon.midi.xml", "soundconfig.xml"]))
bad = 0
for f in files:
    if not os.path.exists(f):
        print(f"  MISSING  {f}", file=sys.stderr); bad += 1; continue
    try:
        xml.dom.minidom.parse(f)
    except Exception as e:
        print(f"  INVALID  {f}: {e}", file=sys.stderr); bad += 1
sys.exit(1 if bad else 0)
PY
echo "XML: all well-formed"

# ---- this deck's wiring -------------------------------------------------------------
# Ring pads are numbered by their place in the daisy chain and PLAY/CUE by
# their GPIO, so a deck wired in another order sends the right MIDI for the
# wrong button. Its unit file says where each control landed, and the
# mapping and script are rewritten for it on the way out (units/apply.py); the
# files here stay canonical. The skin is staged too: a unit file may give the
# panel no bezel, or the deck an accent colour of its own. Keyed on the deck's
# hostname. Done before anything on the deck is touched.
stage="$(mktemp -d)"
decks="$(mktemp -d)"
trap 'rm -rf "$stage" "$decks"' EXIT
cp -R TriMixxx_skin "$stage/"
if [ -f "$UNIT_JSON" ]; then
    python3 units/apply.py "$UNIT_JSON" . "$stage"
else
    echo "==> $DECK: standard wiring"
    cp TriMixxx.midi.xml TriMixxx.scripts.js "$stage/"
fi

# ---- every deck's skin and mapping, for a release card --------------------------------
# A release card runs on any deck (pi-qemu/PLAN.md Part 1): at each start its
# identity unit links the deck's own files from /usr/share/trimixxx/decks/<deck>
# into ~/.mixxx, or .../standard for a deck wired as the canonical files say.
# So every unit file is rendered here, whatever deck this is. Nothing reads
# them on a dev deck.
for u in units/*.json; do
    d="$decks/$(basename "$u" .json)"
    mkdir -p "$d"
    cp -R TriMixxx_skin "$d/"
    python3 units/apply.py "$u" . "$d" >/dev/null
done
mkdir -p "$decks/standard"
cp -R TriMixxx_skin "$decks/standard/"
cp TriMixxx.midi.xml TriMixxx.scripts.js "$decks/standard/"
tar -C "$decks" -cf - . | ssh deck '
    sudo rm -rf /usr/share/trimixxx/decks.new
    sudo mkdir -p /usr/share/trimixxx/decks.new
    sudo tar -C /usr/share/trimixxx/decks.new --no-same-owner --warning=no-timestamp -xf -
    sudo chmod -R u=rwX,go=rX /usr/share/trimixxx/decks.new
    sudo rm -rf /usr/share/trimixxx/decks
    sudo mv /usr/share/trimixxx/decks.new /usr/share/trimixxx/decks
    echo "release renderings: $(ls /usr/share/trimixxx/decks | tr "\n" " ")"'

# ---- the UI font ---------------------------------------------------------------------
# MesloLGL Nerd Font is no stock Pi OS font, and both mixxx.cfg and the skin's
# stylesheet name it; Qt resolves families by name, so a missing font falls
# back silently. The per-user fontconfig directory: no sudo. The check that the
# deck RESOLVES the family would catch a renamed file or a stale cache. Before
# the skin is deleted below, like the XML check.
ssh deck 'mkdir -p ~/.local/share/fonts'
scp fonts/*.ttf deck:.local/share/fonts/
ssh deck 'fc-cache -f ~/.local/share/fonts >/dev/null 2>&1; fc-list : family | grep -q "MesloLGL Nerd Font"' \
    || { echo "the deck cannot resolve 'MesloLGL Nerd Font' after the install" >&2; exit 1; }

# ---- the skin, the mapping, mixxx.cfg and the sound config ------------------------------
# A deck that never ran Mixxx has no ~/.mixxx: made first, since scp into a
# missing directory fails.
ssh deck 'mkdir -p ~/.mixxx/skins ~/.mixxx/controllers && rm -rf ~/.mixxx/skins/TriMixxx'
scp -r "$stage/TriMixxx_skin" deck:.mixxx/skins/
ssh deck 'mv ~/.mixxx/skins/TriMixxx_skin ~/.mixxx/skins/TriMixxx'
# soundconfig.xml is the audio device and buffer, kept apart from mixxx.cfg.
# Its `latency` is not milliseconds but an index into a power-of-2 ladder:
# frames = bit_ceil(samplerate_khz) << (latency - 1); at 44.1 kHz, 3 = 256
# frames (5.8 ms), 4 = 512 (11.6 ms), 5 = 1024 (23.2 ms). It matters beyond
# latency: Mixxx filters the jog's pitch bend through a 25-tap moving average
# whose taps are buffers (ratecontrol.cpp), so the bend's smear is 25 buffers.
# Mixxx rewrites this file at its start, so no comments in it, and pull it
# again after changing the sound prefs on the deck.
# mixxx.cfg is never written by the fork (CoreServices::finalize()); stock Mixxx
# rewrites it at exit, so Mixxx is stopped first, then the files copied.
ssh deck 'sudo systemctl stop getty@tty1.service' || true
scp mixxx.cfg soundconfig.xml deck:.mixxx/
scp "$stage/TriMixxx.midi.xml" "$stage/TriMixxx.scripts.js" \
    PiMidiDaemon.midi.xml PiMidiDaemon.scripts.js deck:.mixxx/controllers/
ssh deck 'sudo systemctl restart getty@tty1.service'
echo "config: done; Mixxx restarted"
exit $SESSION_RESTARTED
