# pi-midi-daemon

**Does the system-level things on the Pi that Mixxx cannot do itself.**

Mixxx has no way to touch the operating system. Its controller scripts run in a
bare `QJSEngine` whose entire API is control values, timers and MIDI I/O — there
is no file, process, shell or network access (no `XMLHttpRequest`, no `fetch`) —
and skins are only XML/QSS. **MIDI is the one channel out of Mixxx**, and SysEx is
the only MIDI message that carries an arbitrary payload.

So: this daemon opens a virtual MIDI port, Mixxx enables it as an ordinary
controller, and Mixxx asks for system actions by sending SysEx to it. Today that
means "shut the deck down from a button on the touchscreen"; the action table is
the place to add anything else Mixxx needs from the OS later.

```
Mixxx  ──SysEx──>  [pi-midi-daemon MIDI in ]  ──>  daemon  ──>  systemctl poweroff
Mixxx  <──SysEx──  [pi-midi-daemon MIDI out]  <──  daemon  <──  a DJ stick was (un)mounted
```

This is separate from `trimixxx-bridge.service` (ttymidi), which carries the real
deck's MIDI to/from the ESP32. Two different ports, two different jobs.

## Wire format

```
F0 7D <opcode> F7
```

- `7D` is the SysEx manufacturer ID the MIDI spec **reserves for non-commercial /
  educational use**, so it can never collide with a real vendor's messages.
- Messages with any other manufacturer ID are ignored, as are unknown opcodes.
- The `F0`/`F7` framing is optional — the daemon accepts the body either way,
  since drivers differ on whether they hand it over.

Opcodes are split by direction so a MIDI dump reads unambiguously.

**Commands — Mixxx → daemon** (the daemon does the thing):

| Opcode | Action     | Runs                  |
| ------ | ---------- | --------------------- |
| `0x00` | `ping`     | nothing (logs only)   |
| `0x01` | `shutdown` | `systemctl poweroff`  |
| `0x02` | `reboot`   | `systemctl reboot`    |

`ping` exists to prove the whole Mixxx→daemon path works without powering the
deck off.

**Events — daemon → Mixxx** (the daemon reports something happened):

| Opcode | Event          | Meaning                                    |
| ------ | -------------- | ------------------------------------------ |
| `0x10` | `usb-mounted`  | a DJ stick appeared at `/media/DJ_USB_*`   |
| `0x11` | `usb-unmounted`| one went away (unmounted, or yanked)       |

Events carry **no payload** — which slot or device it was is not something Mixxx
needs; it just rescans. Both must match `PiMidiDaemon.scripts.js`.

## Watching for DJ sticks

`../mixxx_config/dj-usb` mounts sticks read-only at `/media/DJ_USB_1` /
`DJ_USB_2`. The daemon polls that glob (`--usb-glob`, `--usb-poll`) and reports
each mountpoint appearing and disappearing exactly once.

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

## Security

The daemon runs as **root**, because performing privileged system actions is its
entire purpose; doing it directly avoids a `sudoers` rule.

What keeps that safe is that **the opcode only selects an entry from a fixed table
in `main.go`**. No byte from the MIDI stream is ever interpolated into a command,
and nothing is passed through a shell (`exec.Command`, not `sh -c`). There is
deliberately no "run this string" opcode, and there must never be one.

**The trust boundary is the MIDI port itself**: anything that can send SysEx to
`pi-midi-daemon` can power the machine off. On a single-user appliance that is
acceptable — it is equivalent to having a power button.

## Build

**Ship builds are fully static** — the same trick as the `ttymidi` fork: Alpine +
musl inside Docker, linked static (musl + ALSA + libstdc++), so the binary runs
on Raspberry Pi OS with **no runtime dependencies at all**. Nothing to install on
the device, no `libasound2`, no glibc coupling.

```bash
make docker-arm      # -> dist/pi-midi-daemon
```

```
dist/pi-midi-daemon: ELF 64-bit LSB executable, ARM aarch64, statically linked, stripped
```

Three things this build has to get right:

- **Alpine ships no static `libasound.a`** (same trap ttymidi hit), so alsa-lib
  is compiled from source with `--enable-static`.
- **cgo must be told to link statically** (`-linkmode external -extldflags
  "-static"`); a plain `go build` with cgo is dynamic.
- **rtmidi is C++** — it's vendored inside gomidi, so `librtmidi-dev` is never
  needed, but the image does need `g++` and a static `libstdc++`.

The Dockerfile asserts `readelf -d` finds no `NEEDED` entries, so a build that
would need a runtime loader fails rather than shipping.

Target arch defaults to `linux/arm64`; for a 32-bit Pi OS use
`make docker-arm ARM_PLATFORM=linux/arm/v7`.

For hacking on this machine, `make all` does a native **dynamic** build, which
does need cgo + ALSA headers (`libasound2-dev`, build-time only). `make test`
runs the protocol tests and needs neither.

## Install on the device

```bash
make install-remote          # docker-arm + scp + enable/restart the service
```

or by hand:

```bash
sudo install -m 0755 dist/pi-midi-daemon /usr/local/bin/
sudo install -m 0644 pi-midi-daemon.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now pi-midi-daemon.service
systemctl status pi-midi-daemon.service
```

The unit is ordered `Before=getty@tty1.service` and only reports "started" once
the ALSA port really exists — the same gate `trimixxx-bridge.service` uses, and
for the same reason: **Mixxx enumerates MIDI devices once at startup**, so if the
port appears late, Mixxx never lists it.

## Mixxx side

In **Preferences → Controllers**, `pi-midi-daemon` appears as a MIDI device.
Enable it and load its mapping. That mapping is what turns a Mixxx control into a
SysEx command — an `<output>` can only emit 3-byte messages, so the SysEx is sent
from a small script via `midi.sendSysexMsg()`, which is legal there because the
script is bound to *this* controller's port. (A controller script can only send
to its own device's port, which is exactly why this daemon gets its own mapping
rather than being driven from `TriMixxx.scripts.js`.)

## Testing

Run it without touching the machine:

```bash
./pi-midi-daemon --dry-run        # logs the action instead of running it
./pi-midi-daemon --port some-name # override the virtual port name
./pi-midi-daemon --usb-glob '/tmp/usbtest/DJ_USB_*' --usb-poll 500ms
```

The last one is how to exercise the USB events without a stick: point the glob at
a scratch directory and mount anything there (`hdiutil attach` on macOS, `mount
-o loop` on Linux). Creating the directory alone must *not* fire an event — only
a real mount does.

Then send it a command by hand (alsa-utils):

```bash
aseqsend -p pi-midi-daemon F0 7D 00 F7   # ping
aseqsend -p pi-midi-daemon F0 7D 01 F7   # shutdown
```

`journalctl -u pi-midi-daemon -f` shows what it received and did.
