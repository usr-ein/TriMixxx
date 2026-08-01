# trimixxx-launcher

**Decides what the deck is at boot, and does the system-level things Mixxx
cannot do itself.**

```
boot ─▶ ttymidi ─▶ trimixxx-launchd ─▶ /run/trimixxx/mode ─▶ getty@tty1 ─┬─▶ Mixxx
             │            │                                             ├─▶ Doom
        deck MIDI ────────┤                                             └─▶ debug console
                          │
                          └─▶ trimixxx-deckkeys ─▶ /dev/uinput ─▶ a keyboard + a mouse
```

Two binaries:

| | |
| --- | --- |
| **`trimixxx-launchd`** | the launch manager and the SysEx daemon. Root, systemd, one instance. |
| **`trimixxx-deckkeys`** | turns the deck's MIDI into a virtual keyboard and mouse. Started by `launchd`, only for the modes that need it. |

> Previously `pi-midi-daemon`. The binary and the unit were renamed; the
> **virtual MIDI port is still called `pi-midi-daemon`**, because Mixxx keys its
> controller bindings off the device name (`mixxx_config/mixxx.cfg`) and
> renaming it would silently orphan the mapping.

## Why one daemon does both

Mixxx has no way to touch the operating system. Its controller scripts run in a
bare `QJSEngine` whose entire API is control values, timers and MIDI I/O — no
file, process, shell or network access — and skins are only XML/QSS. **MIDI is
the one channel out of Mixxx**, and SysEx is the only MIDI message that carries
an arbitrary payload. So this daemon opens a virtual MIDI port, Mixxx enables it
as an ordinary controller, and asks for system actions by sending SysEx to it.

The launch-manager half arrived almost for free. Choosing the boot mode means
*reading a button on the deck*, which is MIDI; acting on it means starting things
as root; and this unit's readiness gate is already what releases `getty@tty1`,
hence the session, hence the app. It was one step away from being the launch
manager, so it is.

## Modes

| Mode | Boot gesture | What runs |
| --- | --- | --- |
| `mixxx` | *nothing* — the default | X → Mixxx, as always |
| `doom` | **hold PLAY** from power-on | X → `trimixxx-doom` ([`../doom`](../doom)) |
| `debug` | **hold CUE** from power-on | a text console on tty1, **no X** |

**Hold the button from power-on and let go when the play/cue LEDs start
blinking.** The blinking is the window opening — on a unit whose screen is still
black, it is the only feedback there is. The chosen button then flashes to
acknowledge.

### Why it watches for the *release*

The obvious implementation — "is the button down?" — cannot work, and the reason
is worth writing down.

The firmware sends a Note-On when a button becomes held, *including* one already
held when the S3 boots (its debounced level starts `false`, so a button held at
power-on reads as a fresh press). But the S3 is up about half a second after
power-on and this daemon about twenty seconds after that. **That Note-On is long
gone**: ttymidi did not exist to carry it, nothing retransmits, and the firmware
has no "report current button state" SysEx to ask with.

What survives is the **release**. A button held through the whole boot sends its
Note-Off the moment the finger lifts — and a Note-Off with no Note-On before it
means exactly one thing: *this was already down when I started listening*. So
either edge selects: a lone Note-Off is the hold, a Note-On is someone pressing
during the window. Nobody touches the deck at boot otherwise, so nothing is
ambiguous.

Cost: `--select-window` (3 s) on every boot. Adding a state-report command to the
firmware would remove it — that is the upgrade path, and it needs a reflash.

### `/run/trimixxx/mode`

One file, one meaning: *the mode the deck should be in*. It is the state **and**
the request channel, for anything that can write a file:

```sh
ssh trimixxx-pi 'echo doom  | sudo tee /run/trimixxx/mode'   # switch now
ssh trimixxx-pi 'echo mixxx | sudo tee /run/trimixxx/mode'   # and back
```

`launchd` polls it once a second and makes reality match: it starts or stops
`deckkeys`, and restarts `getty@tty1` **only if something is definitely running
the wrong app**. "Nothing is running" means a session is already in flight — the
normal case when Doom has just exited and handed the mode back — and restarting
on top of that would kill the session that is already doing the right thing.

It lives on **tmpfs on purpose**: a wedged Doom, a debug screen someone walked
away from, a botched experiment — none of them survive a power cycle. There is no
persistent default mode, and there should not be one.

## Ways out, in order of increasing desperation

The deck has no keyboard. Every one of these works from the deck itself, and each
is independent of the ones above it.

1. **Quit the app normally.** Doom: Esc → Quit Game → y, all on the ring pads.
2. **Panic chord — hold LOOP IN + LOOP OUT together for 2 s.** Strafe left and
   strafe right at once: the one combination nobody holds by accident, because
   they cancel out. `launchd` sees it straight off the MIDI, whatever is on
   screen, and puts the deck back into Mixxx. Ignored in Mixxx mode, where those
   are real loop buttons.
3. **ssh**, and write the mode file. Which is why —
4. **Power cycle.** tmpfs. You get Mixxx.

### ssh and wifi come up first, always

`trimixxx-launchd.service` is ordered `After=` `NetworkManager.service`,
`network-online.target` and `ssh.service`. By the time anything in the launcher
can go wrong, **the deck is already reachable**, and it stays reachable whatever
the launcher then does.

Ordering *only* — deliberately no `Requires=`/`BindsTo=` and no
`Wants=network-online.target`. At a gig there is no wifi, and a deck that will
not boot without one is a deck that does not boot. Nothing blocks on the network
either: measured on the deck, `NetworkManager-wait-online` reaches
`network-online.target` in ~8 s and `ssh.service` is listening in ~213 ms, both
well before this unit's own dependencies (ttymidi, sound) are done — so the
ordering is free and waiting on top of it would buy nothing.

The other half of the same promise is the **failsafe default**: if `launchd`
fails outright, its readiness gates give up (bounded at 30 s and 25 s) and
`getty@tty1` is released anyway; with no mode file, `~/.bash_profile` and
`~/.xinitrc` both fall through to Mixxx. A launch manager that cannot answer must
not be able to stop the deck from being a deck.

## Wire format

```
F0 7D <opcode> [args] F7
```

- `7D` is the SysEx manufacturer ID the MIDI spec **reserves for non-commercial /
  educational use**, so it can never collide with a real vendor's messages.
- Messages with any other manufacturer ID are ignored, as are unknown opcodes.
- The `F0`/`F7` framing is optional — the daemon accepts the body either way,
  since drivers differ on whether they hand it over.

Opcodes are blocked by direction and by kind, so a MIDI dump reads unambiguously.

**`0x0x` — commands, Mixxx → daemon** (do this to the machine):

| Opcode | Action | Runs |
| ------ | ---------- | --------------------- |
| `0x00` | `ping` | nothing (logs only) |
| `0x01` | `shutdown` | `systemctl poweroff` |
| `0x02` | `reboot` | `systemctl reboot` |

`ping` exists to prove the whole Mixxx→daemon path works without powering the
deck off.

**`0x1x` — events, daemon → Mixxx** (this happened):

| Opcode | Event | Meaning |
| ------ | -------------- | ------------------------------------------ |
| `0x10` | `usb-mounted` | a DJ stick appeared at `/media/DJ_USB_*` |
| `0x11` | `usb-unmounted`| one went away (unmounted, or yanked) |

Events carry **no payload** — which slot it was is not something Mixxx needs; it
just rescans. Both must match `PiMidiDaemon.scripts.js`.

**`0x2x` — mode commands, Mixxx → daemon** (be something else):

| Message | Mode |
| --- | --- |
| `F0 7D 20 4D 49 58 F7` (`"MIX"`) | back to Mixxx |
| `F0 7D 21 44 4F 4F 4D F7` (`"DOOM"`) | Doom |
| `F0 7D 22 44 42 47 F7` (`"DBG"`) | the debug console |

Each carries **magic**, following the firmware's own precedent for its reset
command (`F0 7D 02 52 53 54 F7`, `"RST"`): a stray or corrupt SysEx must not be
able to drop a live set into Doom. A bare `F0 7D 21 F7` does nothing.

### Security

The daemon runs as **root**, because performing privileged system actions is its
entire purpose; doing it directly avoids a `sudoers` rule.

What keeps that safe is that **the opcode only selects an entry from a fixed
table in `sysex.go`**. No byte from the MIDI stream is ever interpolated into a
command, and nothing is passed through a shell (`exec.Command`, not `sh -c`);
argument bytes are only ever compared against a constant. There is deliberately
no "run this string" opcode, and there must never be one.

**The trust boundary is the MIDI port itself**: anything that can send SysEx to
`pi-midi-daemon` can power the machine off. On a single-user appliance that is
acceptable — it is equivalent to having a power button.

## trimixxx-deckkeys

Reads the deck's MIDI and replays it into the kernel through `/dev/uinput` as a
**virtual USB keyboard and mouse**. Everything above the kernel — X, the console,
SDL — then sees ordinary input, which is what lets an **unmodified** Doom be
played on a DJ deck. See [`../doom`](../doom) for the full story and the control
chart.

```sh
trimixxx-deckkeys --map doom --print-map      # the chart, from the real table
trimixxx-deckkeys --map doom --dry-run        # log the keys instead of typing
```

| Flag | |
| --- | --- |
| `--map doom\|console` | which table to run |
| `--jog-scale 1.8` | mouse pixels per jog tick — the turn speed (tuned on the deck, 6 → 3.6 → 1.8) |
| `--jog-invert` | on by default: the wheel's quadrature counts the opposite way round from mouse X, so without it left turns right |
| `--fader-deadzone 0.15` | fader travel around centre that means "stand still" |
| `--fader-run 0.60` | travel past which walking becomes running |
| `--fader-invert` | swap which end of the fader walks forward |
| `--lights` | on by default: colour the deck's buttons to show what they do |
| `--dry-run` | no `/dev/uinput`, no root; prints what it would press |

`--dry-run` is also the way to see what the deck is sending on a *live* deck: it
creates no input devices, so a second instance can be run alongside the real one
as a passive probe without touching the game.

`launchd` starts exactly one instance, only for `doom` and `debug`, and stops it
with SIGTERM so it can release every held key on the way out — a stuck Ctrl would
outlive the process and there is no keyboard on the deck to undo it with.
**Never in Mixxx mode**: Mixxx speaks MIDI itself, and a virtual keyboard typing
Doom keys at a live set would be a disaster.

The mapping tables live in `internal/keymap/` and have tests: every bound note
must be a control the firmware actually sends, and every emittable key must be
declared to the uinput device (the kernel silently drops undeclared codes, which
looks exactly like a broken mapping).

## Watching for DJ sticks

`../pi_config/dj-usb` mounts sticks read-only at `/media/DJ_USB_1` / `DJ_USB_2`.
The daemon polls that glob (`--usb-glob`, `--usb-poll`) and reports each
mountpoint appearing and disappearing exactly once.

**Why polling and not inotify/udev**, which looks like the obvious answer:
`dj-usb` does `mkdir` and only *then* `mount`, so inotify's `IN_CREATE` fires on
an empty, not-yet-mounted directory — Mixxx would rescan nothing and miss the
stick. inotify cannot see the mount itself at all. So instead of watching the
directory, the daemon tests whether the path is *really a mounted filesystem*
(comparing its `st_dev` against its parent's) on a timer. That is true regardless
of how the mount got there, needs no extra dependency, and a couple of seconds of
latency is nothing for a library refresh.

Mounts that already exist at startup are treated as the baseline and reported to
nobody: this unit starts before Mixxx, so an event fired then would go nowhere.

## Layout

```
cmd/launchd/      the daemon: mode selection, mode switching, SysEx, USB events
cmd/deckkeys/     the MIDI -> uinput bridge
internal/midimap  transcription of the firmware's MidiMap.hpp -- the addresses
internal/deck     the link to ttymidi's ALSA port: events out, LEDs in
internal/uinput   virtual input devices; raw ioctls, no cgo
internal/keymap   what each control does in each mode (doom.go, console.go)
```

Listening to the deck *alongside Mixxx* works because ttymidi creates its ports
`SND_SEQ_PORT_CAP_SUBS_READ`/`SUBS_WRITE` — subscribable, not exclusive. Neither
side knows the other is there.

## Build

**Ship builds are fully static** — the same trick as the `ttymidi` fork: Alpine +
musl inside Docker, linked static (musl + ALSA + libstdc++), so the binaries run
on Raspberry Pi OS with **no runtime dependencies at all**.

```bash
make docker-arm      # -> dist/trimixxx-launchd, dist/trimixxx-deckkeys
```

```
dist/trimixxx-launchd:  ELF 64-bit LSB executable, ARM aarch64, statically linked, stripped
dist/trimixxx-deckkeys: ELF 64-bit LSB executable, ARM aarch64, statically linked, stripped
```

Three things this build has to get right:

- **Alpine ships no static `libasound.a`** (same trap ttymidi hit), so alsa-lib
  is compiled from source with `--enable-static`.
- **cgo must be told to link statically** (`-linkmode external -extldflags
  "-static"`); a plain `go build` with cgo is dynamic.
- **rtmidi is C++** — it's vendored inside gomidi, so `librtmidi-dev` is never
  needed, but the image does need `g++` and a static `libstdc++`.

`uinput` adds nothing to any of that: it is raw ioctls against `/dev/uinput`, so
the bridge needs no library at all — which is why the syscall numbers are
precomputed in `internal/uinput` rather than pulled in through cgo.

The Dockerfile asserts `readelf -d` finds no `NEEDED` entries in either binary,
so a build that would need a runtime loader fails rather than shipping.

Target arch defaults to `linux/arm64`; for a 32-bit Pi OS use
`make docker-arm ARM_PLATFORM=linux/arm/v7`.

For hacking on this machine, `make all` does native **dynamic** builds, which do
need cgo + ALSA headers (`libasound2-dev`, build-time only). `make test` runs the
protocol, mapping and gesture tests and needs neither. `make chart` prints both
control charts.

## Install

```bash
make install-remote          # docker-arm + scp + enable/restart the service
```

This also **removes the old `pi-midi-daemon` service** if it is still installed —
two daemons fighting over the same virtual port name would be a confusing way to
spend an evening.

The session side (`~/.bash_profile`, `~/.xinitrc`, the debug console stub) is
installed by [`../pi_config/upload.sh`](../pi_config); Doom itself by
[`../doom/install.sh`](../doom).

The unit is ordered `Before=getty@tty1.service` and holds it back through two
gates: the ALSA port really existing, and the mode having been chosen. The first
is the same gate `trimixxx-bridge.service` uses, for the same reason — **Mixxx
enumerates MIDI devices once at startup**, so a port that appears late is one
Mixxx never lists. The second is what makes the boot gesture work at all.

## Mixxx side

In **Preferences → Controllers**, `pi-midi-daemon` appears as a MIDI device.
Enable it and load its mapping. That mapping is what turns a Mixxx control into a
SysEx command — an `<output>` can only emit 3-byte messages, so the SysEx is sent
from a small script via `midi.sendSysexMsg()`, which is legal there because the
script is bound to *this* controller's port. (A controller script can only send
to its own device's port, which is exactly why this daemon gets its own mapping
rather than being driven from `TriMixxx.scripts.js`.)

Adding a **DOOM button** to the skin is the same shape as the existing power
menu: a skin control, a `makeConnection`, and `midi.sendSysexMsg([0xF0, 0x7D,
0x21, 0x44, 0x4F, 0x4F, 0x4D, 0xF7])`. Not wired up — the boot gesture and the
mode file are enough to use it.

## Testing

Run it without touching the machine:

```bash
./trimixxx-launchd --dry-run             # logs actions instead of running them
./trimixxx-launchd --mode doom           # skip the gesture, force a mode
./trimixxx-launchd --select-window 10s   # a longer window to fumble in
./trimixxx-launchd --usb-glob '/tmp/usbtest/DJ_USB_*' --usb-poll 500ms
```

The USB one is how to exercise the events without a stick: point the glob at a
scratch directory and mount anything there (`hdiutil attach` on macOS, `mount -o
loop` on Linux). Creating the directory alone must *not* fire an event — only a
real mount does.

Then send it a command by hand (alsa-utils):

```bash
aseqsend -p pi-midi-daemon F0 7D 00 F7                # ping
aseqsend -p pi-midi-daemon F0 7D 01 F7                # shutdown
aseqsend -p pi-midi-daemon F0 7D 21 44 4F 4F 4D F7    # go to Doom
```

And to watch what the deck itself is sending — which is what the boot gesture
reads:

```bash
aseqdump -p TriMixxx
```

`journalctl -u trimixxx-launchd -f` shows the decision, the mode, and everything
`deckkeys` logs.
