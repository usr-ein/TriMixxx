# The Bluetooth receiver

"Bluetooth receiver", in the browser's home list, makes the deck a Bluetooth
speaker for a device, a phone or a Mac: while its page is open, the device
plays straight to the DAC and the screen shows what it plays (the cover,
title, artist, album, where in the track), with its previous, play/pause and
next; the deck's PLAY, loop halve and loop double drive it too. The radio is
up only while the page is open (Sam, 2026-10-17).

## What runs when

| | Menu closed | Menu open |
|---|---|---|
| Mixxx | owns the DAC (`hw:CARD=CODEC`) | has let go of it; the deck paused |
| radio | down, rfkill-blocked | up |
| `trimixxx-bluetoothd` | stopped | bluetoothd, with `bluetooth-main.conf` |
| `trimixxx-bluealsa` | stopped | BlueALSA, A2DP sink only |
| `trimixxx-bluealsa-aplay` | stopped | the device's stream to `plughw:CARD=CODEC` |
| `trimixxx-bluetooth-guard` | none | watches Mixxx's PID |

Mixxx (the fork's `BluetoothReceiver`, `docs/fork-map.md`) opens it in this
order: the deck paused and held paused, the sound card let go of
(`SoundManager::releaseDevices()`), then `sudo -n trimixxx-bluetooth up PID`.
It closes it the other way: `trimixxx-bluetooth down`, which stops the player
first and frees the DAC, then `SoundManager::setupDevices()`, tried again
every second while the card is busy, with a toast after three tries (the
deck is never left silent without a word).

Nothing starts at boot. The units here have no `[Install]`, and the packages'
own (`bluetooth`, `bluealsa`, `bluealsa-aplay`) are masked by the base step
(`pi-qemu/deploy/000_base.pi.sh`), so neither a first start's presets nor a
D-Bus call activating `org.bluez` can bring the radio up behind the menu's
back.

**A crash** leaves nothing up. `up` starts a transient unit,
`trimixxx-bluetooth-guard`, that looks at Mixxx's PID every second and runs
`down` when it is gone, before the session's restart loop starts the next
Mixxx (2 s); and Mixxx takes down anything still up as it starts, before it
opens the sound card. A screen left off by a crash is lit again then too.

## The pieces

| File | Installed as | What |
|---|---|---|
| `trimixxx-bluetooth` | `/usr/local/sbin/trimixxx-bluetooth` | `up PID`, `down`, `star ADDRESS\|none`, `status`; `guard PID` for the guard unit |
| `trimixxx-bluetooth.sudoers` | `/etc/sudoers.d/trimixxx-bluetooth` | Mixxx may run `up`, `down` and `star` as root, nothing else |
| `trimixxx-bluetooth.target` | `/etc/systemd/system/` | the three services below |
| `trimixxx-bluetoothd.service` | `/etc/systemd/system/` | bluez's unit, without `[Install]` or its D-Bus alias |
| `trimixxx-bluealsa.service` | `/etc/systemd/system/` | bluez-alsa's, sink only |
| `trimixxx-bluealsa-aplay.service` | `/etc/systemd/system/` | bluez-alsa's player, to the DAC |
| `bluetooth-main.conf` | `/usr/local/share/trimixxx/bluetooth-main.conf` | bluetoothd's `-f`: a loudspeaker, visible 2 min after +, experimental (for covers) |
| `bluez-standin.py` | never | a stand-in for bluetoothd on an emulated deck (below) |
| `obexd-standin.py` | never | a stand-in for obexd's cover client, with it (below) |

The deploy step is `002_system` (`pi-qemu deck deploy TARGET system`), and the
packages come with the base step: `bluez` (on Raspberry Pi OS already),
`bluez-alsa-utils`, and `bluez-obexd` for covers (with what it pulls in,
evolution-data-server's libraries mostly, ~21 MB installed).

## Pairing, picking, the star, and the devices it remembers

The deck is visible only after + (in the device list's corner), for
`DiscoverableTimeout` (120 s), or until a device has paired. Mixxx is BlueZ's
agent (`BluetoothAgent`, DisplayYesNo): a device pairing shows six digits, the
deck shows the same with Confirm / Cancel, and both are confirmed.

The page lists the paired devices, and **nothing connects until the DJ picks
one** (Sam: "I don't want it jumping clients without being told"). The device
picked is the only one BlueZ trusts: any other paired device trying to
connect or take over goes to the agent, which refuses it until it is picked.
Picking another switches at once, the one playing disconnected. A device just
paired is picked. Each row's bin asks to confirm, then unpairs the device
(so does a long press on the row): BlueZ deletes its keys.

**The star** (one device at most, Sam): the starred device is the deck's own
pick when the menu opens, and whenever nothing is picked while it is open.
Mixxx asks it to connect (`Device1.Connect`, which pages it for up to BlueZ's
5.12 s page timeout) and, while it is not in (out of range, its Bluetooth
off), again every 10 s, until it is in, the DJ picks another (which stands
until the menu closes), or the menu closes. Never outside the menu: the radio
is off then. It stays trusted between openings, so BlueZ writes nothing for it
at each opening, and it may come in by itself the moment the radio is up.

BlueZ keeps the pairings in `/var/lib/bluetooth`. A release card's root is
read-only (RAM over squashfs) and would forget them at every start, so
`trimixxx-bluetoothd` binds `/var/lib/rauc/trimixxx-bluetooth`, on the state
partition (p8), over it, and `up` makes that folder the first time. The star
is a file beside them, `trimixxx-starred` (one address), which `star` writes
whole (a new file renamed over the old) and `up` reads back (`starred
ADDRESS` on its output). They are written when a device pairs, is picked (its
trust), is starred or unstarred, or forgotten, never at a normal start (but
once: experimental mode's admin-policy driver writes its empty allowlist,
`admin_policy_settings`, the first time bluetoothd starts with it). On a
dev card there is no p8, and `/var/lib/bluetooth` on the root keeps them.

## What plays: the track, its position, its cover, and the controls

BlueZ's `MediaPlayer1` for the device connected (AVRCP) gives the track
(title, artist, album, duration), its status and its position; the page
counts the position on by itself between BlueZ's reports, and each report
replaces it, backwards as much as forwards. BlueZ reports a position when the
device says it changed (a track change, a pause or a play, and a seek if the
device says so; BlueZ asks for nothing in between).

PLAY and the page's play/pause ask the device's player to `Play` or `Pause`
(from what it says it is doing), loop halve and the page's previous
`Previous`, loop double and the page's next `Next`: AVRCP's own keys, as a
car's buttons. PLAY's light is the device's playing.

**Covers** come over AVRCP's imaging (BIP), through obexd on the deck user's
session bus: the device says where it serves them (its SDP record, which
BlueZ 5.82 shows as `MediaPlayer1.ObexPort` only in experimental mode, hence
`Experimental = true` in `bluetooth-main.conf`), and gives a track's handle
only while a cover session to it is open (AVRCP 1.6). So Mixxx opens one as
soon as the device is in, or takes the one Pi OS's `mpris-proxy` opens for
every such device, and fetches each new track's thumbnail (a 200 px JPEG) by
its handle. The handles come with a track's attributes, which BlueZ reads when
the track changes: a session opened after the first read brings its cover
from the next track on.

## The radio

The deck's own, the Pi 4's CYW43455, on UART0 (the PL011, at 3 Mbaud with flow
control), as on a stock Pi: the S3 moved to UART3 to give it back
(`pi_config/fresh-install.md` 1.3). The kernel attaches it by itself (krnbt,
`hci_uart_bcm`); bluetoothd powers it while the menu is open, and `down`
blocks it with rfkill. A Pi with no radio there (an emulated deck) shows "No
Bluetooth radio on this deck".

## On a deck

```sh
trimixxx-bluetooth status                          # up or down
journalctl -b -u trimixxx-bluetoothd -u trimixxx-bluealsa -u trimixxx-bluealsa-aplay
bluetoothctl show; bluetoothctl devices Paired     # while the menu is open
grep -E 'BluetoothReceiver|BluetoothAgent' /tmp/mixxx/stderr.log
```

## On an emulated deck: the stand-in

QEMU has no radio. `bluez-standin.py` plays enough of `org.bluez` for the
page: an adapter, and devices that pair with a code, connect, play, pause,
seek, skip at the deck's asking, or are out of range. With the menu open (so
the radio side is up):

```sh
pi-qemu deck ssh NAME 'sudo systemctl stop trimixxx-bluetoothd'
scp pi_config/bluetooth/bluez-standin.py NAME:/tmp/      # eval "$(pi-qemu deck env NAME)" first
pi-qemu deck ssh NAME 'sudo nohup python3 /tmp/bluez-standin.py >/tmp/standin.log 2>&1 &'
# then, + pressed on the deck:
pi-qemu deck ssh NAME 'busctl call org.bluez /org/bluez org.trimixxx.StandIn Pair s "Pixel 9"'
pi-qemu deck ssh NAME 'busctl call org.bluez /org/bluez org.trimixxx.StandIn Connect s "Pixel 9"'
pi-qemu deck ssh NAME 'busctl call org.bluez /org/bluez org.trimixxx.StandIn Play sssuu "Title" "Artist" "Album" 245000 61000'
pi-qemu deck ssh NAME 'busctl call org.bluez /org/bluez org.trimixxx.StandIn Seek u 0'      # back to the start
pi-qemu deck ssh NAME 'busctl call org.bluez /org/bluez org.trimixxx.StandIn Away s "Pixel 9"'   # out of range
```

Its header lists the rest; its log (`/tmp/standin.log`) says what the deck
asked (`the deck asked: Next`). Covers come through obexd, on the deck user's
session bus: `obexd-standin.py` stands in for it, and tells the BlueZ stand-in
when a cover session opens, as an iPhone gives a track's cover only while one
is open. `Cover s FILE` stages FILE as the cover of the tracks to come (from
the next track change on). Like Android's cover server, the stand-in serves
one client at all, and Pi OS's own `mpris-proxy` (running in every session)
opens a session for any device that offers covers, so Mixxx takes that one
(stop it, `systemctl --user stop mpris-proxy`, to have Mixxx open its own):

```sh
scp pi_config/bluetooth/obexd-standin.py cover.png NAME:/tmp/
pi-qemu deck ssh NAME 'systemctl --user stop obex.service; nohup python3 /tmp/obexd-standin.py >/tmp/obex-standin.log 2>&1 &'
pi-qemu deck ssh NAME 'busctl call org.bluez /org/bluez org.trimixxx.StandIn Cover s /tmp/cover.png'
```

The audio switch can be checked on an emulated deck
without it: with the deck started as `deck up NAME -- --audio wav:FILE`, play
a tone to `plughw:CARD=CODEC,DEV=0` while the menu is open, and Mixxx's sound
either side of it lands in the same file.
