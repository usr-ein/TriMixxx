#!/usr/bin/env bash
# Deploy step 007 (lib.sh has the contract): Doom on the deck -- the engine,
# the WAD, the launcher script and its configs. Its controls come with the
# launcher (003: trimixxx-deckkeys).
#
# Chocolate Doom: vanilla-accurate, SDL2 and packaged, so the deck runs the
# Doom "can it run Doom" means, and there is no engine fork here to keep.
. "$(dirname "$0")/lib.sh"

wad=doom/wad/doom1.wad
[ -f "$wad" ] || { echo "no WAD yet: fetching it"; doom/fetch-wad.sh; }

ssh deck '
    set -eu
    if ! command -v chocolate-doom >/dev/null 2>&1; then
        sudo apt-get update -qq
        sudo apt-get install -y chocolate-doom
    fi
    chocolate-doom --version 2>/dev/null | head -1 || true'
scp "$wad" doom/trimixxx-doom doom/default.cfg doom/chocolate-doom.cfg deck:/tmp/
ssh deck '
    set -eu
    sudo install -d -m 0755 /usr/local/share/games/doom
    sudo install -m 0644 /tmp/doom1.wad     /usr/local/share/games/doom/doom1.wad
    sudo install -m 0755 /tmp/trimixxx-doom /usr/local/bin/trimixxx-doom
    # In the home directory and writable: Chocolate Doom REWRITES both configs
    # at exit, so a root-owned one would fail or lose what was changed in-game.
    install -d -m 0755 "$HOME/.local/share/chocolate-doom"
    for f in default.cfg chocolate-doom.cfg; do
        if [ -f "$HOME/.local/share/chocolate-doom/$f" ]; then
            cp "$HOME/.local/share/chocolate-doom/$f" "$HOME/.local/share/chocolate-doom/$f.bak"
        fi
        install -m 0644 "/tmp/$f" "$HOME/.local/share/chocolate-doom/$f"
    done
    rm -f /tmp/doom1.wad /tmp/trimixxx-doom /tmp/default.cfg /tmp/chocolate-doom.cfg'

cat <<'EOF'
Doom is on the deck.
  boot into it   hold PLAY from power-on, let go when the play LED blinks
  from Mixxx     the skin's DOOM button (SysEx F0 7D 21 44 4F 4F 4D F7)
  by hand        echo doom | sudo tee /run/trimixxx/mode
  get out        Esc -> Quit Game -> y, on the ring pads
  panic          hold LOOP IN + LOOP OUT together for 2 s
  controls       trimixxx-deckkeys --map doom --print-map
EOF
exit $DONE
