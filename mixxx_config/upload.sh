#!/usr/bin/env bash

set -eux

ssh bedwolf 'rm -rf ~/.mixxx/skins/TriMixxx'

scp -r TriMixxx_skin bedwolf:~/.mixxx/skins/
ssh bedwolf 'mv ~/.mixxx/skins/TriMixxx_skin ~/.mixxx/skins/TriMixxx'
scp mixxx.cfg bedwolf:~/.mixxx/mixxx.cfg
scp TriMixxx.midi.xml TriMixxx.scripts.js bedwolf:~/.mixxx/controllers/
ssh bedwolf 'sudo systemctl restart getty@tty1.service'
echo Upload done
