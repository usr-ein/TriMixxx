#!/usr/bin/env bash
# Deploy step 006 (lib.sh has the contract): Mixxx's music library directory.
# Mixxx asks "Choose music library directory" at every start until its
# database names one -- database state, not mixxx.cfg (fresh-install.md 3.4) --
# so ~/Music goes in once, and a deck that has one keeps it. The database is
# made at Mixxx's first start (005_config's restart, on a new deck). Mixxx is
# stopped for the write and started again (exit 12); with nothing to write,
# it is left running.
. "$(dirname "$0")/lib.sh"

outcome="$(ssh deck 'set -e
    mkdir -p ~/Music
    db=~/.mixxx/mixxxdb.sqlite
    for i in $(seq 1 60); do [ -s $db ] && break; sleep 2; done
    [ -s $db ] || { echo "no Mixxx database yet: start Mixxx once, then deploy this step again" >&2; exit 1; }
    if [ "$(sqlite3 $db "SELECT COUNT(*) FROM directories")" = 0 ]; then
        sudo systemctl stop getty@tty1
        sqlite3 $db "INSERT INTO directories (directory) VALUES ('"'"'/home/sam1902/Music'"'"')"
        sudo systemctl start getty@tty1
        echo written
    fi
    echo "library: $(sqlite3 $db "SELECT directory FROM directories")" >&2')"
[ "$outcome" = written ] && exit $SESSION_RESTARTED
exit $DONE
