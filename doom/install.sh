#!/usr/bin/env bash
# One-shot install of Doom onto the deck's Pi: the engine, the WAD, the launcher
# script and the two config files. Needs sudo on the far side; re-running it is
# safe.
#
# This does NOT install the controls -- those come from `trimixxx-deckkeys`,
# which ships with the launch manager (../trimixxx-launcher). Install that first
# or the deck will be a very expensive keyboard-less Doom.
set -euo pipefail

HOST="${HOST:-trimixxx-pi}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WAD="$HERE/wad/doom1.wad"

# The engine. Chocolate Doom is vanilla-accurate, SDL2, and packaged -- so the
# deck runs the same Doom everybody else means by "can it run Doom", and there
# is no engine fork in this repo to maintain. crispy-doom takes the same
# arguments and the same config, if you want the widescreen one instead.
DOOM_PKG="${DOOM_PKG:-chocolate-doom}"

if [ ! -f "$WAD" ]; then
    echo "no WAD yet -- fetching it"
    "$HERE/fetch-wad.sh"
fi

echo "==> installing $DOOM_PKG on $HOST"
ssh "$HOST" "
    set -eu
    if ! command -v $DOOM_PKG >/dev/null 2>&1; then
        sudo apt-get update -qq
        sudo apt-get install -y $DOOM_PKG
    fi
    $DOOM_PKG --version 2>/dev/null | head -1 || true
"

echo "==> copying the WAD, the launcher and the configs"
scp -q "$WAD" "$HERE/trimixxx-doom" "$HERE/default.cfg" "$HERE/chocolate-doom.cfg" "$HOST":/tmp/

ssh "$HOST" '
    set -eu
    sudo install -d -m 0755 /usr/local/share/games/doom
    sudo install -m 0644 /tmp/doom1.wad     /usr/local/share/games/doom/doom1.wad
    sudo install -m 0755 /tmp/trimixxx-doom /usr/local/bin/trimixxx-doom

    # The configs live in the user home and stay user-writable: Chocolate Doom
    # REWRITES both of them on exit, so anything root-owned here would either
    # fail or silently discard whatever you changed in-game.
    install -d -m 0755 "$HOME/.local/share/chocolate-doom"
    for f in default.cfg chocolate-doom.cfg; do
        if [ -f "$HOME/.local/share/chocolate-doom/$f" ]; then
            cp "$HOME/.local/share/chocolate-doom/$f" "$HOME/.local/share/chocolate-doom/$f.bak"
        fi
        install -m 0644 "/tmp/$f" "$HOME/.local/share/chocolate-doom/$f"
    done
    rm -f /tmp/doom1.wad /tmp/trimixxx-doom /tmp/default.cfg /tmp/chocolate-doom.cfg
'

cat <<EOF

Doom installed on $HOST.

  boot into it   hold PLAY from power-on, let go when the play LED blinks
  from Mixxx     the skin's DOOM button (SysEx F0 7D 21 44 4F 4F 4D F7)
  by hand        ssh $HOST 'echo doom | sudo tee /run/trimixxx/mode'
  right now      ssh $HOST 'DISPLAY=:0 trimixxx-doom'   (no deck controls)

  get out        Esc -> Quit Game -> y, on the ring pads
  panic          hold LOOP IN + LOOP OUT together for 2 s

  controls       trimixxx-deckkeys --map doom --print-map
  music          it ships silent; chocolate-doom-setup -> Sound -> OPL (Adlib)
EOF
