---
name: trimixxx0
description: Run your own emulated TriMixxx deck (trimixxx0, a Raspberry Pi 4 under QEMU) to test deck software without the hardware - up in ~4 s from a snapshot, screenshots of its screen (headless), buttons/jog/encoder/fader over the virtual S3, LEDs, ssh, Mixxx's logs, and deploying your own Mixxx build, Mixxx config/skin/mapping, pi_config or launcher changes onto it. Use whenever a change to mixxx/, mixxx_config/, pi_config/, trimixxx-launcher/, ttymidi or the S3 MIDI contract should be checked on a running deck. Many agents can each run their own at once.
---

# trimixxx0: your own emulated deck

`pi-qemu/instance.sh` gives you a deck of your own: its own copy of the card,
ssh port, ssh alias and control socket. It is headless and silent. **It
starts in about 4 s** from a snapshot, already booted with Mixxx running.
A cold boot takes about 5 s, Mixxx about 1 s more. Other agents may be
running theirs at the same time, and the person at the laptop may be running
their own deck (port 2222, with windows). Yours touches neither.

`instance.sh` is a thin layer over plain commands; its header documents the
command behind each subcommand. For anything not covered here (running a
deck by hand, snapshots, the image build, USB sticks, troubleshooting), read
`pi-qemu/README.md`. Where the README shows a bare `pi-qemu COMMAND`, you use
`pi-qemu/instance.sh ctl NAME COMMAND`.

Run everything from the root of your checkout (or worktree), with
**that checkout's** `pi-qemu/instance.sh`. It deploys the code next to it,
and finds the built QEMU and cards in the main checkout by itself.

## Rules

1. **Pick one NAME and only ever touch that instance.** Make it unique to
   your task, e.g. `fix-sync-meter`.
   - Never `pkill` qemu, pi-qemu or docker.
   - Never `instance.sh rm|kill|stop|down` a NAME you did not create.
   - Never run `instance.sh golden` or `pi-qemu/image/build.sh` unless asked:
     they replace the shared golden card.
2. **Never reach a deck except through `instance.sh`.**
   - No bare `pi-qemu COMMAND`: without `PI_QEMU_CONTROL` it drives the
     person's own deck.
   - No upload script run directly (`mixxx/upload.sh`, `mixxx_config/upload.sh`,
     `pi_config/upload.sh` and the rest): without `HOST` they default to
     `trimixxx-pi`, a **real deck**. Your shell's state does not carry
     between tool calls, and in zsh `$HOST` is the Mac itself. Use
     `instance.sh deploy` or `instance.sh run`, which set everything inside
     one process.
3. **No sound on the Mac, ever.** Never pass `--audio speakers`. To check audio,
   record it to a WAV file and analyse the file (see below).
4. **Reuse, do not restart.** `instance.sh up NAME` on a running instance only
   reports it, so call it at the start of every step. To change how it runs
   (`--fresh`, `--boot`, `-- --audio ...`), `rm` it first: on a running
   instance `up` refuses options rather than ignore them.
5. **Clean up, or say what is left.** See "Before you finish". Not optional.

## Start (or reuse) your deck

```sh
pi-qemu/instance.sh list                     # "no instances", or what runs (yours may already)
pi-qemu/instance.sh up NAME                  # ~4 s from the golden snapshot; reports it if running
```

The instance lives in `~/.pi-qemu/instances/NAME/`. Each one needs 2 GB of
RAM; if `list` already shows about 4 running, ask before starting another.

- `up NAME --fresh`: start again from the golden snapshot; your card's
  changes are lost.
- `up NAME --boot`: boot instead of restoring (~5 s), e.g. to test the boot
  itself. Then `ready NAME`.
- `stop NAME`: suspend (~9 s; leaves a ~1 GB snapshot until the next `up`,
  which resumes it in ~3 s on a new ssh port; `instance.sh ssh` follows it).
- `up NAME --window`: also shows its screen and panel on the Mac, but only
  when the person wants to watch.

## Look at it

```sh
pi-qemu/instance.sh shot NAME "$TMPDIR/NAME-1.png"   # the 1280x800 screen, headless; Read the PNG
pi-qemu/instance.sh ctl NAME leds            # the S3's LEDs as JSON: play, cue, loopIn/Out, ringA/ringB pads
pi-qemu/instance.sh ctl NAME status          # Pi running, S3 link, ssh port, run dir
pi-qemu/instance.sh ssh NAME 'tail -50 /tmp/mixxx/mixxx.log'   # Mixxx (also /tmp/mixxx/stderr.log; .1 = previous run)
pi-qemu/instance.sh ssh NAME 'journalctl -b -u trimixxx-launchd -n 50'
```

Put screenshots in a temp or scratch directory, not in the repo. Take one
after anything you expect to change the screen, and look at it before you
conclude.

The deck's clock is UK time (Europe/London), which may differ from the Mac's;
mind the offset when matching the deck's logs with output on the Mac.

## Poke it like a DJ

These go through the virtual S3 as MIDI on the Pi's UART, exactly as the real
board sends them, on trimixxx0's wiring (`mixxx_config/units/trimixxx0.json`):

```sh
pi-qemu/instance.sh ctl NAME press play               # press+release, 80 ms; `press cue 600` holds
pi-qemu/instance.sh ctl NAME down cue                 # ... `up cue` releases
pi-qemu/instance.sh ctl NAME jog 3240                 # +clockwise, 12960 ticks per turn
pi-qemu/instance.sh ctl NAME touch on                 # platter touch (scratch); `touch off`
pi-qemu/instance.sh ctl NAME browse -2                # the track encoder, + is up
pi-qemu/instance.sh ctl NAME press push               # encoder push: opens the library, then activates the selection
pi-qemu/instance.sh ctl NAME tempo 8192               # fader 0..16383, 8192 = centre
pi-qemu/instance.sh ctl NAME midi 90 3C 7F            # raw bytes, for anything else
pi-qemu/instance.sh ctl NAME stick insert SAM3        # a rekordbox USB stick image (read-only); `stick list`
```

Controls: `tempo-range keylock loop8 loop4 loop-double loop-halve back hotcue1
hotcue2 hotcue3 hotcue4 slip sort play cue loop-in loop-out reloop push
jog-touch`. The S3's MIDI contract is `firmwares/trimixxx-midi/lib/PiLink/MidiMap.hpp`.

- **Load a track:** `stick insert SAM3`, `press push`, then browse and push
  until it loads, with a screenshot between steps. With no track loaded, most
  presses change nothing visible.
- **Check that bytes reached the Pi:**
  `instance.sh ssh NAME 'sudo grep ^0: /proc/tty/driver/ttyAMA'`.
  - `rx:` counts what the S3 sent. It grows by 6 per `press` (note on and
    note off), 3 per `down`, `up`, `touch` or `browse` step, 6 per `tempo`,
    and 3 per 63 ticks of `jog`. `jog` answers before its bytes are sent.
  - `tx:` is what Mixxx sent back to the S3 (the LEDs). It growing after a
    press shows Mixxx handled it.

## Deploy your own code onto it

```sh
pi-qemu/instance.sh deploy NAME config       # mixxx_config: mapping, scripts, skin, fonts, mixxx.cfg (~10 s)
pi-qemu/instance.sh deploy NAME mixxx        # the Mixxx fork: Docker arm64 build, swaps /usr/bin/mixxx
pi-qemu/instance.sh deploy NAME system       # pi_config/upload.sh: units, session, splash, eth0, ...
pi-qemu/instance.sh deploy NAME launcher     # trimixxx-launcher
pi-qemu/instance.sh deploy NAME ttymidi      # the serial<->MIDI bridge
pi-qemu/instance.sh run NAME -- some/script.sh   # anything else that uses HOST + plain ssh/scp
```

`deploy` runs your checkout's `pi-qemu/deploy.sh` against the instance (the
same steps a real deck gets). Where a step restarts Mixxx, or the MIDI bridge
(after which `deploy.sh` restarts the session, since Mixxx only opens the
S3's port at start), it waits until Mixxx is ready and prints
`NAME: Mixxx is ready (sound open, the S3 connected; pid N)`. That means
Mixxx is running with its sound output and the deck's controller open, not
that a track plays. A new pid is the proof Mixxx restarted, and a
screenshot is meaningful straight away. `pi-qemu/instance.sh ready NAME`
checks the same on its own.

Typical times: `config` ~10 s, `system` ~20 s, `mixxx` a Docker build first
(minutes when the cache is cold).

In a fresh worktree, `mixxx/` needs `git submodule update --init mixxx` before
`deploy NAME mixxx`.

`mixxx`, `launcher`, `ttymidi` and `doom` build in Docker. `deploy` stops
early if Docker's disk has less than 4 GB free. Freeing it is the person's
call: tell them, do not prune.

Mixxx builds compile incrementally in Docker cache mounts that every session
shares:
- Grep `deploy`'s output for each `.cpp` you changed being compiled. A stale
  object can ship without your change, and nothing errors.
- To force a rebuild, touch a stamp file in `mixxx/dist/`.

## When ssh does not answer

```sh
pi-qemu/instance.sh console NAME 'systemctl --failed' 'ip -br a' 'dmesg | tail -30'   # serial console login
```

`~/.pi-qemu/instances/NAME/card.run/console.log` has the boot from the first
kernel line after `up --boot`. After a restore it only has what came after
the restore.

If `ready` says Mixxx started before the MIDI bridge restarted, the S3 no
longer reaches it. Do what the message says: restart the session
(`sudo systemctl restart getty@tty1`), then `ready` again.

## Check audio without sound

```sh
pi-qemu/instance.sh up NAME -- --audio "wav:$TMPDIR/NAME.wav"   # a new instance (rm an old one first)
# ... make the deck play, then analyse the file; it is valid at any moment
```

The WAV is 16-bit stereo at 44.1 kHz, recorded from the start, silence
included. Python's `wave` module reads it. Measure the peak, the length of
the loud part, and the frequency from zero crossings.

A tone without Mixxx:
`instance.sh ssh NAME 'sudo systemctl stop getty@tty1; timeout 3 speaker-test -D plughw:0,0 -c 1 -r 44100 -t sine -f 440'`.
- `-c 1`: one tone on both channels at once (`-c 2` plays left, then right).
- Exit code 124 is `timeout` ending it, which is normal.
- Stopping the session takes ~5 s. Afterwards bring Mixxx back with
  `instance.sh ssh NAME 'sudo systemctl start getty@tty1'`, then
  `instance.sh ready NAME`.

## Before you finish

Every time you end your work, whether it succeeded, failed or stopped part way:

1. Run `pi-qemu/instance.sh list`.
2. For each instance **you** started:
   - **Remove it** (`pi-qemu/instance.sh rm NAME`) when nobody needs to look
     at it.
   - **Or keep it, and say so in your final message:** its name, whether it
     is still running or suspended, and the exact command to remove it.
     `instance.sh stop NAME` suspends one you will come back to; that costs
     disk, not RAM, and `up` resumes it in ~3 s.
3. Your final message always ends with a line like
   `Emulated decks: removed fix-sync-meter.` or
   `Emulated decks: fix-sync-meter still running - pi-qemu/instance.sh rm fix-sync-meter when done.`
