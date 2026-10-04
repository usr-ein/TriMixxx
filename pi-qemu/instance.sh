#!/usr/bin/env bash
# Emulated decks (trimixxx0) side by side, one per agent or test: each its own
# copy of the card, ssh port, ssh alias and control socket. A thin layer --
# every command below says what it runs, and all of it can be done by hand.
#
#   instance.sh up NAME [--window] [--fresh] [--boot] [--from CARD] [-- RUN_ARGS...]
#   instance.sh ssh NAME [COMMAND...]          ssh -F <dir>/ssh_config NAME
#   instance.sh ctl NAME PI_QEMU_COMMAND...    pi-qemu with PI_QEMU_CONTROL set
#   instance.sh shot NAME FILE.png             the Pi's screen, window or not
#   instance.sh deploy NAME [STEP...]          this checkout's deploy.sh onto it, then
#                                                waits for the new Mixxx (steps: base ttymidi
#                                                system launcher mixxx config library doom)
#   instance.sh run NAME -- COMMAND...         COMMAND with HOST=NAME and ssh/scp reaching
#                                                the instance, e.g. run NAME -- pi_config/upload.sh
#   instance.sh ready NAME [SECONDS]           wait until Mixxx runs, its sound open and the
#                                                S3 connected (default 120 s)
#   instance.sh env NAME                       the same exports, for one shell of your own
#   instance.sh console NAME COMMAND...        over the serial console, no ssh
#   instance.sh stop NAME                      suspend: save the machine, then off
#   instance.sh down NAME                      clean poweroff
#   instance.sh kill NAME                      pull the plug
#   instance.sh rm NAME                        stop it and delete its card
#   instance.sh list
#   instance.sh golden [CARD]                  make the golden card + snapshot
#
# Starting is a restore, not a boot: a new instance gets an APFS clone of the
# golden card *and* of the snapshot saved with it (.cache/golden/trimixxx0.img
# and .state, made by `golden`), and pi-qemu resumes that machine -- booted,
# Mixxx running -- in seconds. `stop` saves an instance the same way, so its
# next `up` is seconds too. A restore needs the card it was saved with, so the
# snapshot is used once and then deleted; the card goes on from there. --boot
# boots the card instead; --fresh starts over from the golden pair.
#
# An instance lives in ~/.pi-qemu/instances/NAME/: card.img, state (while
# suspended), card.run/ (pi-qemu's run dir: control.sock, ssh.port, qemu.log,
# console.log, ...), ssh_config, bin/ssh and bin/scp (the alias NAME for
# scripts that call plain ssh/scp), pid, command (the pi-qemu command line)
# and pi-qemu.log. `up` on a running instance only reports it: call it freely.
#
# Headless (no window, silent) unless --window. RUN_ARGS go to `pi-qemu run`
# as they are, e.g. -- --audio wav:/abs/path/out.wav. Never --audio speakers
# unless the person at the laptop asked for sound.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# The built tools and the cards are in the main checkout, also when this runs
# from a git worktree (which has neither).
MAIN="$(dirname "$(git -C "$HERE" rev-parse --path-format=absolute --git-common-dir)")"
PIQ="${PI_QEMU_HOME:-$MAIN/pi-qemu}"
# A checkout that changes pi-qemu itself, or the S3's MIDI table compiled into
# it, builds its own (pi-qemu/app/build.sh) and points this at it; QEMU, the
# golden card and the stick images still come from $PIQ.
BIN="${PI_QEMU_BIN:-$PIQ/app/build/pi-qemu}"
# Short paths: the run dir holds unix sockets, and macOS caps their paths at
# 104 bytes. Same disk as the repo, so the card copies stay APFS clones.
INSTANCES="${PI_QEMU_INSTANCES:-$HOME/.pi-qemu/instances}"
GOLDEN="$PIQ/.cache/golden/trimixxx0"
SSH_KEY="${SSH_KEY:-$HOME/.ssh/no_pass/rsa_sam}"

die() { echo "instance.sh: $*" >&2; exit 1; }
usage() { sed -n '/^#   instance.sh/s/^# *//p' "$0" >&2; exit 2; }
clone() { rm -f "$2"; cp -c "$1" "$2" 2>/dev/null || cp "$1" "$2"; }

# ---- one instance's files: $D, $NAME ------------------------------------------------
running() { local pid; pid="$(cat "$D/pid" 2>/dev/null)" && kill -0 "$pid" 2>/dev/null; }
need_running() { running || die "$NAME is not running (instance.sh up $NAME)"; }
ssh_n() { ssh -F "$D/ssh_config" "$NAME" "$@"; }
# Pull the plug and wait until pi-qemu (and its QEMU) are gone.
stop_now() {
    running || return 0
    kill "$(cat "$D/pid")" 2>/dev/null || true
    for _ in $(seq 1 50); do running || return 0; sleep 0.2; done
    kill -9 "$(cat "$D/pid")" 2>/dev/null || true
}
# Start pi-qemu on $D/card.img with the given extra args; write ssh_config.
# --private: this deck leaves ~/.pi-qemu/current (and port 2222) to the
# person's own, and pi-qemu picks a free ssh port, written to ssh.port.
launch() {
    mkdir -p "$D/bin"
    rm -f "$D/card.run/ssh.port"
    local args=(run --private --deck trimixxx0 --tools "$PIQ/qemu/.build/bin" --sticks "$PIQ/.cache/sticks"
                "$@" "$D/card.img")
    echo "$BIN ${args[*]}" > "$D/command"
    nohup "$BIN" "${args[@]}" > "$D/pi-qemu.log" 2>&1 &
    echo $! > "$D/pid"
    for _ in $(seq 1 50); do [ -s "$D/card.run/ssh.port" ] && break; sleep 0.2; done
    [ -s "$D/card.run/ssh.port" ] || die "pi-qemu did not start: $D/pi-qemu.log"
    local port; port="$(cat "$D/card.run/ssh.port")"
    # The alias NAME, for this instance only: ~/.ssh/config is not touched.
    cat > "$D/ssh_config" <<EOF
Host $NAME
  HostName 127.0.0.1
  Port $port
  User sam1902
  IdentityFile $SSH_KEY
  IdentitiesOnly yes
  StrictHostKeyChecking no
  UserKnownHostsFile /dev/null
  LogLevel ERROR
  ConnectTimeout 10
  ServerAliveInterval 15
  SetEnv LC_ALL=C.UTF-8
EOF
    for tool in ssh scp; do
        printf '#!/bin/sh\nexec /usr/bin/%s -F "%s" "$@"\n' "$tool" "$D/ssh_config" > "$D/bin/$tool"
        chmod +x "$D/bin/$tool"
    done
}
wait_ssh() { # $1: seconds
    local end=$(( $(date +%s) + $1 ))
    until ssh_n -o ConnectTimeout=2 true 2>/dev/null; do
        running || die "pi-qemu exited: $D/pi-qemu.log, $D/card.run/qemu.log"
        [ "$(date +%s)" -lt "$end" ] || die "no ssh after $1 s: $D/card.run/console.log"
        sleep 0.2
    done
}
# Mixxx is ready: running, holding /tmp/mixxx/mixxx.log open -- its own log,
# not the previous run's, which it renames as it starts -- and that log says
# the sound stream started and the deck's controller (the S3's MIDI) is open;
# the skin is loaded before either. And the MIDI bridge (ttymidi) is older
# than this Mixxx: Mixxx opens the bridge's port once, at start, so a bridge
# restarted since has left it deaf to the S3 (exit 2).
mixxx_ready() {
    ssh_n -o ConnectTimeout=2 'p=$(pgrep -xo mixxx) || exit 1
        [ "$(readlink /proc/$p/fd/* 2>/dev/null | grep -cx /tmp/mixxx/mixxx.log)" -gt 0 ] || exit 1
        grep -q "Started stream successfully" /tmp/mixxx/mixxx.log || exit 1
        grep -q "Opening controller: \"TriMixxx\"" /tmp/mixxx/mixxx.log || exit 1
        b=$(pgrep -xo ttymidi) || exit 1
        [ "$(ps -o etimes= -p $b)" -ge "$(ps -o etimes= -p $p)" ] || exit 2' 2>/dev/null
}
wait_ready() { # $1: seconds
    local end=$(( $(date +%s) + $1 )) rc
    while :; do
        rc=0; mixxx_ready || rc=$?
        [ "$rc" = 0 ] && return 0
        [ "$rc" = 2 ] && die "Mixxx started before the MIDI bridge restarted, so the S3 no longer reaches it: $0 ssh $NAME 'sudo systemctl restart getty@tty1', then $0 ready $NAME"
        running || die "pi-qemu exited: $D/pi-qemu.log, $D/card.run/qemu.log"
        [ "$(date +%s)" -lt "$end" ] || die "Mixxx not ready after $1 s: $0 ssh $NAME 'tail -30 /tmp/mixxx/stderr.log'"
        sleep 0.5
    done
}
# Save the machine to $D/state and wait for pi-qemu to be gone. Headless
# pi-qemu exits with the Pi; with windows it stays open, so stop it.
suspend_now() {
    PI_QEMU_CONTROL="$D/card.run/control.sock" "$BIN" save "$D/state"
    for _ in $(seq 1 50); do running || return 0; sleep 0.2; done
    stop_now
}

[ $# -ge 1 ] || usage
cmd="$1"; shift
mkdir -p "$INSTANCES"
case "$cmd" in
list)
    ls -d "$INSTANCES"/*/ >/dev/null 2>&1 || { echo "no instances"; exit 0; }
    for d in "$INSTANCES"/*/; do
        [ -d "$d" ] || continue
        n="$(basename "$d")"; pid="$(cat "$d/pid" 2>/dev/null || true)"
        state=stopped port=-; [ -f "$d/state" ] && state=suspended
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            state="running (pid $pid)"; port="$(cat "$d/card.run/ssh.port" 2>/dev/null || echo -)"
        fi
        printf '%-20s ssh port %-5s %s\n' "$n" "$port" "$state"
    done
    exit 0
    ;;
golden)
    # The golden pair: a card, booted until Mixxx plays, saved with its
    # machine. Every new instance starts from a clone of both.
    card="${1:-$PIQ/.cache/build/trimixxx0.img}"
    [ -f "$card" ] || die "no card at $card"
    if lsof "$card" >/dev/null 2>&1; then die "$card is in use (a running deck?): power it off first"; fi
    NAME=golden-make D="$INSTANCES/.golden"
    stop_now; rm -rf "$D"; mkdir -p "$D"
    clone "$card" "$D/card.img"
    echo "booting $card (headless, silent) to save it..."
    launch --no-controls --display none
    wait_ssh 300
    # Saved once Mixxx has its sound device open and has settled: that is the
    # deck every instance wakes up as.
    end=$(( $(date +%s) + 300 ))
    until ssh_n "grep -q 'Started stream successfully' /tmp/mixxx/mixxx.log" 2>/dev/null; do
        [ "$(date +%s)" -lt "$end" ] || die "Mixxx did not open its sound device: instance.sh golden on $card"
        sleep 2
    done
    sleep 10
    suspend_now
    mkdir -p "$(dirname "$GOLDEN")"
    mv -f "$D/state" "$GOLDEN.state.new"; mv -f "$D/card.img" "$GOLDEN.img.new"
    mv -f "$GOLDEN.state.new" "$GOLDEN.state"; mv -f "$GOLDEN.img.new" "$GOLDEN.img"
    rm -rf "$D"
    echo "golden: $GOLDEN.img + $GOLDEN.state ($(du -h "$GOLDEN.state" | cut -f1) of machine)"
    exit 0
    ;;
esac

[ $# -ge 1 ] || usage
NAME="$1"; shift
[[ "$NAME" =~ ^[a-zA-Z0-9][a-zA-Z0-9_-]*$ ]] || die "NAME: letters, digits, - and _"
D="$INSTANCES/$NAME"
CONTROL="$D/card.run/control.sock"

case "$cmd" in
up)
    window=0 fresh=0 boot=0 from=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --window) window=1 ;;
            --fresh) fresh=1 ;;
            --boot) boot=1 ;;
            --from) from="$2"; shift ;;
            --) shift; break ;;
            *) die "up: unknown option $1" ;;
        esac
        shift
    done
    if running; then # reuse it: one start serves every later call
        # ... but not silently with other settings than asked for
        [ "$window$fresh$boot" = 000 ] && [ -z "$from" ] && [ $# -eq 0 ] ||
            die "$NAME is already running: its options cannot change now. $0 rm $NAME (or stop $NAME), then up again"
        echo "$NAME is already up: ssh port $(cat "$D/card.run/ssh.port"), pid $(cat "$D/pid")"
        echo "  ssh -F $D/ssh_config $NAME"
        echo "  when done: $0 rm $NAME   (or stop $NAME to keep it, suspended)"
        exit 0
    fi
    [ -x "$BIN" ] || die "no $BIN: run $PIQ/app/build.sh"
    mkdir -p "$D"
    if [ -n "$from" ]; then # a card of your own: booted, no snapshot
        [ -f "$from" ] || die "no card at $from"
        rm -f "$D/state"; clone "$from" "$D/card.img"
    elif [ "$fresh" = 1 ] || [ ! -f "$D/card.img" ]; then
        [ -f "$GOLDEN.img" ] || die "no golden card: instance.sh golden [CARD]"
        rm -f "$D/state"; clone "$GOLDEN.img" "$D/card.img"
        [ -f "$GOLDEN.state" ] && clone "$GOLDEN.state" "$D/state"
    fi
    restore=()
    if [ "$boot" = 0 ] && [ -f "$D/state" ]; then restore=(--restore "$D/state"); fi
    [ "$boot" = 1 ] && rm -f "$D/state" # booting moves the card on: the snapshot no longer fits
    view=(--no-controls --display none); [ "$window" = 1 ] && view=()
    t0=$(date +%s)
    launch ${view[@]+"${view[@]}"} ${restore[@]+"${restore[@]}"} "$@"
    wait_ssh 300
    how=booted
    if [ ${#restore[@]} -gt 0 ]; then
        how=restored
        rm -f "$D/state" # used: the card goes on from here
        # The machine wakes with the clock it was saved with; give it now.
        ssh_n "sudo date -u -s @$(date -u +%s) >/dev/null" || true
    fi
    cat <<EOF
$NAME is up ($how in $(( $(date +%s) - t0 )) s): ssh port $(cat "$D/card.run/ssh.port"), pid $(cat "$D/pid")$([ "$window" = 1 ] && echo ", with windows")
  ssh -F $D/ssh_config $NAME
  PI_QEMU_CONTROL=$CONTROL $BIN status
  console log: $D/card.run/console.log
  when done: $0 rm $NAME   (or stop $NAME to keep it, suspended)
EOF
    ;;
ssh) need_running; ssh_n "$@" ;;
deploy)
    need_running
    # The code next to this script -- your checkout or worktree -- onto the
    # instance, by the same deploy.sh a real deck gets. The tools and cards
    # still come from the main checkout.
    case " ${*:-all} " in
        *" all "*|*" ttymidi "*|*" launcher "*|*" mixxx "*|*" doom "*)
            # These build in Docker, whose VM disk fills up: a full one fails
            # minutes in. Freeing it is the person's call.
            free_gb="$(docker run --rm --pull missing debian:trixie df -BG --output=avail / 2>/dev/null | tail -1 | tr -dc 0-9 || true)"
            [ -n "$free_gb" ] || die "Docker is not running: these steps build in it"
            [ "$free_gb" -ge 4 ] || die "Docker's disk has ${free_gb} GB free, a build needs ~4: ask the person to free some (docker system df)"
            ;;
    esac
    # A git worktree starts without its submodules: the ones these steps build from.
    case " ${*:-all} " in *" all "*|*" mixxx "*|*" ttymidi "*) "$HERE/worktree.sh" prepare ;; esac
    PATH="$D/bin:$PATH" HOST="$NAME" "$HERE/deploy.sh" "$NAME" "$@"
    # These restart the deck's session, so Mixxx: wait for the new one.
    case " ${*:-all} " in
        *" all "*|*" system "*|*" launcher "*|*" mixxx "*|*" config "*|*" library "*)
            wait_ready 180
            echo "$NAME: Mixxx is ready (sound open, the S3 connected; pid $(ssh_n 'pgrep -xo mixxx'))" ;;
    esac
    ;;
run)
    need_running
    [ "${1:-}" = "--" ] && shift
    [ $# -ge 1 ] || die "run NAME -- COMMAND..."
    PATH="$D/bin:$PATH" HOST="$NAME" exec "$@"
    ;;
ready)
    need_running
    wait_ready "${1:-120}"
    echo "$NAME: Mixxx is ready (sound open, the S3 connected; pid $(ssh_n 'pgrep -xo mixxx'))"
    ;;
ctl) need_running; PI_QEMU_CONTROL="$CONTROL" exec "$BIN" "$@" ;;
shot)
    need_running
    [ $# -eq 1 ] || die "shot NAME FILE.png"
    PI_QEMU_CONTROL="$CONTROL" "$BIN" screenshot "$1"
    ;;
env)
    [ -f "$D/ssh_config" ] || die "no instance $NAME"
    printf 'export PATH=%q:"$PATH" HOST=%q PI_QEMU_CONTROL=%q\n' "$D/bin" "$NAME" "$CONTROL"
    ;;
console)
    need_running
    exec python3 "$PIQ/console.py" "$D/card.run/console.sock" "$@"
    ;;
stop)
    running || { echo "$NAME is not running"; exit 0; }
    suspend_now
    echo "$NAME suspended: instance.sh up $NAME resumes it"
    ;;
down)
    running || { echo "$NAME is not running"; exit 0; }
    rm -f "$D/state"
    ssh_n 'sudo systemctl poweroff' 2>/dev/null || true
    for _ in $(seq 1 45); do
        running || break
        if ! pgrep -P "$(cat "$D/pid")" -f qemu-system >/dev/null; then kill "$(cat "$D/pid")" 2>/dev/null || true; fi
        sleep 2
    done
    running && { echo "$NAME did not power off: pulling the plug" >&2; stop_now; }
    echo "$NAME is off"
    ;;
kill) rm -f "$D/state"; stop_now; echo "$NAME: plug pulled" ;;
rm)
    stop_now
    rm -rf "$D"
    echo "$NAME removed"
    ;;
*) usage ;;
esac
