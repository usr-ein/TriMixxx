#!/usr/bin/env bash

set -eux

# ssh alias for the deck's Pi. Override for a one-off: HOST=other ./upload.sh
HOST="${HOST:-trimixxx-pi}"

ssh "$HOST" 'rm -rf ~/.mixxx/skins/TriMixxx'

scp -r TriMixxx_skin "$HOST":~/.mixxx/skins/
ssh "$HOST" 'mv ~/.mixxx/skins/TriMixxx_skin ~/.mixxx/skins/TriMixxx'
scp mixxx.cfg "$HOST":~/.mixxx/mixxx.cfg
scp TriMixxx.midi.xml TriMixxx.scripts.js \
    PiMidiDaemon.midi.xml PiMidiDaemon.scripts.js "$HOST":~/.mixxx/controllers/
ssh "$HOST" 'sudo systemctl restart getty@tty1.service'
echo Upload done
