# ProLink / DJ Link — Protocol Overview and Network Architecture

> **⚠ Pre-hardware document.** Written from published reverse-engineering
> literature before any capture from real CDJs existed. Much of it has since
> been confirmed, and a good deal corrected, by testing against two
> CDJ-2000NXS. **`docs/PROTOCOL.md` is the current specification; where this
> document disagrees with it, this document is wrong.** `docs/FINDINGS.md`
> records each correction with its evidence.

Research notes for **prolinks-compat**. This document covers the high-level
protocol structure, network topology, port map, packet families, the magic
header, and device-number conventions. It is intended as the foundation for a
real implementation.

Facts are tagged **(confirmed)** when verified against real Pioneer hardware
per the dysentery packet analysis, or **(inferred)** when deduced from
behaviour, code, or partial captures. Inline citations point at the source:
`(dysentery vcdj.adoc)`, `(dysentery startup.adoc)`, `(dysentery packets.adoc)`,
`(prolink-connect)`, `(python-prodj-link)`, `(libcdj)`.

> **Terminology note.** Pioneer's marketing names are "Pro DJ Link" and
> "DJ Link"; the network protocol is informally called "ProLink". rekordbox
> internal strings and the libcdj project use "ProLink"/"ProDJLink". These all
> refer to the same protocol (libcdj README; prolink-connect README).

---

## 1. What ProLink / DJ Link is, and the device ecosystem

Pro DJ Link is a proprietary, **undocumented** Ethernet/UDP protocol used by
Pioneer (now AlphaTheta) professional DJ gear to coordinate performances:
share tempo/beat-grid for sync, exchange detailed per-player status, allow
players to load tracks from each other's media over the network, and serve
track metadata and analysis (waveforms, beat grids, cues) to other devices and
to rekordbox. None of the projects referenced here are sanctioned by Pioneer;
everything was reverse-engineered from packet captures (dysentery README).

**Device ecosystem (confirmed devices appearing on the network):**

- **CDJ players** — the multiplayers. Generations matter because packet sizes
  and feature sets differ (dysentery vcdj.adoc):
  - Pre-nexus CDJs: status packets `0xd0` bytes, no beat counter, status flag
    byte `F` always `00` (limited inferable state).
  - Nexus (nxs) CDJs (e.g. CDJ-2000nexus): status `0xd4` bytes.
  - Newer firmware / Nexus 2 (nxs2): `0x11c` or `0x124` byte status packets.
  - **XDJ-1000**: `0x11b` byte status packets; sends no settings block.
  - **CDJ-3000**: `0x200` byte status packets; introduces device numbers 5 and
    6, two settings blocks, key/key-shift fields, loop fields, and precise
    position packets.
- **DJM mixers** — e.g. DJM-2000nexus. Acts as tempo/network hub; sends
  much smaller (`0x38`-byte) status packets and assigns device numbers to
  players plugged into channel-specific ports (dysentery vcdj.adoc,
  startup.adoc).
- **rekordbox** (desktop) — joins the network, sends mixer-style status
  packets, and runs a remote-database server that other devices can query
  (dysentery vcdj.adoc).
- **rekordbox mobile** — also joins; picks its own device number (dysentery
  vcdj.adoc).
- **All-in-one units** with partial implementations:
  - **XDJ-XZ**: embeds two players + a mixer behind a single IP; exposes only
    one slot pair to the network; does not send the "assignment finished"
    packet; does not broadcast media-slot info; quirky/broken device-number
    assignment on the laptop port (dysentery startup.adoc).
  - **XDJ-AZ**: full Pro DJ Link, but only 2 of 4 decks are network-exposed;
    in four-deck mode uses a new slot value `0x07` (dysentery startup.adoc,
    vcdj.adoc).
  - **Opus Quad**: cannot truly join a DJ Link network; exposes IDs 1, 2, 33
    but only exchanges lighting-related packets with rekordbox (dysentery
    packets.adoc, startup.adoc).
- **XDJ-RX**: does **not** implement the protocol at all — its LINK port only
  talks to rekordbox; dysentery has been reported to crash it. Do not probe it
  (dysentery README).

---

## 2. Network topology

**Physical / link layer.** Standard wired **Ethernet** (often via a gigabit
switch, or directly via a DJM's built-in switch ports). Each device has a real
MAC address; the protocol carries the device's own **MAC (6 bytes)** and
**IPv4 address (4 bytes)** inside many packets (in raw byte form, not the usual
INET string representation) so peers can reach it directly
(dysentery startup.adoc; libcdj README).

**IP assignment — two modes:**

1. **DHCP** — CDJs *do* accept DHCP if a server is present; you can run
   `dnsmasq`/`dhcpd` and assign addresses yourself (libcdj autoip.md)
   **(confirmed)**.
2. **Link-local auto-IP (RFC 3927)** — if no DHCP server answers, each device
   self-assigns an address from **`169.254.0.0/16`** (link-local), broadcast
   `169.254.255.255`. On Linux this is `avahi-autoipd` (libcdj autoip.md)
   **(confirmed)**. In dysentery's own example capture the network used a
   manually/DHCP-assigned `172.16.42.x/24` range, showing the protocol does not
   require link-local addressing (dysentery README).

**Startup sequence at the IP layer:** a device first tries to obtain an IP
(DHCP), and only after giving up self-assigns a link-local address; *then* it
begins broadcasting DJ Link announcement packets to UDP 50000 (dysentery
startup.adoc).

**Role of the mixer (DJM) in addressing.** The DJM does *not* assign IP
addresses — that's DHCP/auto-IP. What it assigns is the **device/player
number** for players plugged into its **channel-specific Ethernet ports**. When
a CDJ is plugged into a channel port, the mixer overrides the player's own
choice and tells it which number to use (matching the physical channel), via a
short handshake on UDP 50000 (assignment-intention `0x01` → device-number
request → channel-assignment `0x03` → assignment-finished `0x05`). On a generic
(non-channel) network port, the player self-selects its number via the
broadcast claim sequence instead (dysentery startup.adoc) **(confirmed on
DJM-2000nexus)**.

**To participate as a "Virtual CDJ"** you must know your interface's real local
IP, MAC, and the subnet broadcast address, and put your real IP/MAC into the
keep-alive packets so devices can reach you (dysentery vcdj.adoc). dysentery,
python-prodj-link (`guess_own_iface`), and prolink-connect
(`autoconfigFromPeers` / `getMatchingInterface`) all derive the correct
interface by matching against the IP of the first observed peer.

---

## 3. Port map

All multi-byte integers in DJ Link packets are **big-endian** (inferred from
all captures and parser code). Discovery, beat, and status traffic are **UDP**;
metadata and file transfer use **TCP** and **RPC/NFS** respectively.

| Proto | Port | Bind/dest | Purpose | Source |
|-------|------|-----------|---------|--------|
| UDP | **50000** | broadcast (recv on `0.0.0.0`) | Announcement / discovery / device-number (channel) negotiation / keep-alive | dysentery packets.adoc, startup.adoc; constants.ts `ANNOUNCE_PORT`; prodj.py `keepalive_port` (confirmed) |
| UDP | **50001** | broadcast | Beat sync, Fader Start, Channels On Air, absolute position, master handoff, sync control | dysentery packets.adoc; constants.ts `BEAT_PORT`; prodj.py `beat_port` (confirmed) |
| UDP | **50002** | unicast + recv on `0.0.0.0` | Detailed device status (CDJ/Mixer status), media query/response, load-track command, load settings | dysentery packets.adoc, vcdj.adoc; constants.ts `STATUS_PORT`; prodj.py `status_port` (confirmed) |
| UDP | **50004** | unicast | "Touch Audio" data/handover/timing between supported players & mixers; also carries an incrementing packet counter | dysentery packets.adoc, vcdj.adoc (confirmed) |
| TCP | **12523** | unicast | **`remotedb`/`dbserver` port-discovery service.** Connect and send the literal "RemoteDBServer" query to learn the *actual* TCP port the database server is listening on | prolink-connect `REMOTEDB_SERVER_QUERY_PORT`; python-prodj-link `DBServerQueryPort = 12523` (confirmed) |
| TCP | *(dynamic)* | unicast | The real **remote database (dbserver) server** for track metadata, waveforms, beat grids, cues. Port number is returned by the 12523 query (varies per device/firmware) | prolink-connect remotedb/index.ts (confirmed) |
| UDP | **111** | unicast | **SUN RPC portmapper** (`PortmapPort=111`, `PortmapVersion=2`) — used to look up the dynamic `mount` and `nfs` ports on a player | python-prodj-link packets_nfs.py / nfsclient.py (confirmed) |
| UDP | *(dynamic)* | unicast | **`mount` daemon** (`MountVersion=1`) — port discovered via portmapper; mounts the player's media export | python-prodj-link nfsclient.py (confirmed) |
| UDP | *(dynamic)* | unicast | **NFS** (`NfsVersion=2`) — port discovered via portmapper; reads rekordbox DB files and track files directly off mounted USB/SD media | python-prodj-link nfsclient.py (confirmed) |

### dbserver port discovery (TCP 12523)

The query packet sent to TCP 12523 is (prolink-connect remotedb/index.ts,
**confirmed**):

```
00 00 00 0f                      ; 4-byte length/marker = 0x0f = 15
"RemoteDBServer"                 ; 14 ASCII bytes
00                               ; NUL terminator
```

The device replies with **2 bytes (big-endian uint16)** = the TCP port to
connect to for the actual database protocol. This indirection exists because
the dbserver listens on a firmware-dependent port.

### Two metadata strategies

- **remotedb (TCP)** — query the player's/rekordbox's database server directly.
  Requires the virtual CDJ to use a device number **1–6** (see §5).
- **NFS (RPC/portmap/mount/nfs over UDP)** — mount the player's media and read
  the rekordbox `.pdb`/analysis files and audio directly; works regardless of
  virtual device number, and works through the XDJ-XZ USB-to-host interface
  (python-prodj-link; dysentery startup.adoc).

---

## 4. Packet families and the magic header

**Every DJ Link UDP packet begins with the same fixed 10-byte magic header**
(dysentery packets.adoc; constants.ts `PROLINK_HEADER` — **confirmed**):

```
51 73 70 74 31 57 6d 4a 4f 4c
```

As ASCII this reads **`Qspt1WmJOL`** (`0x51='Q' 0x73='s' 0x70='p' 0x74='t'
0x31='1' 0x57='W' 0x6d='m' 0x4a='J' 0x4f='O' 0x4c='L'`). Note the task prompt's
example byte list `0x51,0x73,0x70,0x74,0x49,0x6e,0x65,0x44` is *not* correct —
the verified sequence is the 10 bytes above (the 5th byte is `0x31`/`'1'`, not
`0x49`).

**Immediately after the 10-byte magic comes one byte at offset `0x0a` that
identifies the packet kind** (its meaning is interpreted together with the UDP
port it arrived on) (dysentery packets.adoc). After the kind byte comes the
device name (a fixed-width ASCII field), then a `01` separator byte at offset
`0x20`, then a per-family structure byte and length.

### Port 50000 — announcement / device-number negotiation

| Kind | Purpose |
|------|---------|
| `0x00` | First-stage channel-number claim (mixer & CDJ) |
| `0x01` | Mixer assignment-intention (to a channel-specific port) |
| `0x02` | Second-stage channel-number claim |
| `0x03` | Mixer channel assignment (tells CDJ its number) |
| `0x04` | Final-stage channel-number claim |
| `0x05` | Mixer assignment finished |
| `0x06` | Device keep-alive (presence + ownership of device number) |
| `0x08` | Channel conflict (defends an already-claimed number) |
| `0x0a` | Initial device announcement |

(dysentery packets.adoc, startup.adoc — confirmed)

### Port 50001 — beat / sync / mixer features

| Kind | Purpose |
|------|---------|
| `0x02` | Fader Start |
| `0x03` | Channels On Air |
| `0x0b` | Absolute Position (CDJ-3000+) |
| `0x26` | Master Handoff Request |
| `0x27` | Master Handoff Response |
| `0x28` | Beat |
| `0x2a` | Sync Control |

(dysentery packets.adoc — confirmed)

### Port 50002 — status / media / control

| Kind | Purpose |
|------|---------|
| `0x05` | Media Query |
| `0x06` | Media Response |
| `0x0a` | CDJ Status |
| `0x19` | Load Track Command |
| `0x1a` | Load Track Acknowledgment |
| `0x29` | Mixer Status |
| `0x34` | Load Settings Command |

(dysentery packets.adoc — confirmed)

### Port 50004 — touch audio

| Kind | Purpose |
|------|---------|
| `0x1e` | Audio Data |
| `0x1f` | Audio Handover |
| `0x20` | Audio Timing |

(dysentery packets.adoc — confirmed)

### Length-field subtlety

There are two length conventions, distinguished by the "structure" byte at
offset `0x20`:

- Subtype `0x00`/`0x03` packets carry **`len_r`** = bytes *remaining* after the
  length field (e.g. a mixer status packet reports `0x0e` = 14 remaining).
- Subtype `0x01`/`0x02` packets carry **`len_p`** = length of the *entire*
  packet including the header (e.g. mixer initial announcement reports
  `0x0025`). rekordbox's mixer-style status uses the `len_p` convention
  (dysentery startup.adoc, vcdj.adoc — confirmed).

Status packets additionally have a packet-version/subtype byte at `0x20`: `0x00`
for mixer status, `0x03` for CDJ status, with `0x03`–`0x06` observed (likely
protocol/packet version) (dysentery vcdj.adoc).

---

## 5. Device numbers / player numbers

The **device number** `D` is the player number shown on the CDJ. It appears in
keep-alive and status packets (e.g. CDJ status at bytes `0x21` and `0x24`)
(dysentery vcdj.adoc, startup.adoc).

| Number | Device | Notes |
|--------|--------|-------|
| `0x01`–`0x04` (1–4) | Real CDJ players | Standard four-deck range; channel matches DJM port (confirmed) |
| `0x05`–`0x06` (5–6) | CDJ-3000 players | CDJ-3000 extends the range to 6; requires CDJ-3000-compatible startup/keep-alive packet variants or you can kick a 3000 off the network (dysentery startup.adoc) |
| `0x21` (33) | DJM mixer | DJM-2000nexus uses `0x21`; Opus Quad / all-in-ones use 33 for their mixer section (dysentery vcdj.adoc, startup.adoc) |
| `0x11` (17) | rekordbox (desktop) | Default; uses conflict resolution if multiple copies run (dysentery vcdj.adoc) |
| `0x29` (41) | rekordbox mobile | First instance; increments from there (dysentery vcdj.adoc) |
| `0x00` | "no device" / pre-assignment | Used transiently during channel assignment (dysentery startup.adoc) |

**Virtual CDJ device-number recommendations (important, somewhat conflicting
between projects):**

- **Use a number outside 1–6 (e.g. 7) to avoid taking a real player's slot.**
  dysentery suggests `0x07`; prolink-connect's `DEFAULT_VCDJ_ID = 0x07` and
  `autoconfigFromPeers` defaults to 7 (constants.ts, network.ts). python-prodj-
  link's `Vcdj` defaults to player number **5**, and prolink-connect's README
  *recommends 5*. The tension: a number in 1–6 occupies a slot (so you can have
  at most 5 real CDJs) but a number **>6 cannot make dbserver/remotedb metadata
  queries** — CDJs only answer metadata requests from device numbers 1–6
  (dysentery vcdj.adoc §streaming; network.ts comments; prolink-connect README).
- **Auto-assign (`a`/`auto-id`) vs fixed:** a virtual CDJ broadcasts keep-alives
  copied from the CDJ keep-alive template; byte `0x31` in the second-stage claim
  is `0x01` for auto-assign, `0x02` for a specific number (dysentery
  startup.adoc; libcdj `VDJ_FLAG_AUTO_ID`).
- **Streaming metadata** (CDJ-3000 Beatport LINK / Streaming Direct Play)
  requires the virtual device number to be **6 or lower** or you get no response
  (dysentery vcdj.adoc).

**Channel conflict:** if you claim a number already in use, the incumbent sends
a `0x08` conflict packet on UDP 50000 and you must back off (dysentery
startup.adoc). The XDJ-XZ notably does *not* defend its numbers, which can
corrupt naive implementations (dysentery startup.adoc).

---

## 6. How it fits together (discovery → status → metadata)

A typical client/virtual-CDJ lifecycle:

1. **Bind three UDP sockets** to `0.0.0.0` on ports 50000, 50001, 50002 (with
   `SO_BROADCAST` enabled on the ones you'll transmit broadcasts from)
   (prodj.py; network.ts).
2. **Discovery.** Listen on 50000 for keep-alive (`0x06`) and announcement
   (`0x0a`) packets. Each carries the peer's device number, name, IP and MAC.
   Build a device list ("backline" in libcdj terms). Use the first peer's IP to
   pick the correct local interface, IP, MAC, and broadcast address (dysentery
   README; python-prodj-link `guess_own_iface`; prolink-connect
   `autoconfigFromPeers`).
3. **Become a Virtual CDJ.** Choose a device number (see §5), then broadcast
   CDJ-style keep-alive packets to **50000** roughly every **1.5 s** (dysentery
   vcdj.adoc; `ANNOUNCE_INTERVAL = 1500`; python-prodj-link
   `packet_interval = 1.5`). This is the trick (discovered by Diogo Santos) that
   makes other devices start unicasting **detailed status** directly to your
   port 50002 (dysentery vcdj.adoc, README).
4. **Status.** Receive CDJ status (`0x0a`) and mixer status (`0x29`) on 50002,
   roughly every **200 ms** per device, to track loaded track, tempo/BPM, pitch,
   beat/bar position, play state, on-air, sync/master, media slots, etc. Track
   tempo/beat precisely via beat packets (`0x28`) on **50001** from the master
   player (dysentery vcdj.adoc, packets.adoc). The status packets tell you
   *which* device/slot/track-id is loaded — the key inputs for a metadata query.
5. **Metadata / library access.** Using `(device number, slot, track id)` from
   the status packet, fetch metadata one of two ways:
   - **remotedb (TCP):** connect to **12523**, send the `RemoteDBServer` query
     to learn the real dbserver port, connect there, and run menu/metadata/
     waveform/beat-grid/cue queries. Requires your virtual device number to be
     **1–6** (dysentery vcdj.adoc; prolink-connect remotedb).
   - **NFS (UDP):** hit the player's **portmapper (111)** to find the mount and
     nfs ports, `mnt` the media export, then read the rekordbox database and
     track files directly. Works with any virtual device number and through the
     XDJ-XZ USB interface (python-prodj-link nfsclient.py; dysentery
     startup.adoc).
6. **Control / sync (optional).** Send Load Track (`0x19`) on 50002, Fader Start
   (`0x02`)/Channels On Air (`0x03`)/Sync Control (`0x2a`)/Master Handoff
   (`0x26`/`0x27`) on 50001 (dysentery packets.adoc).

---

## Important gaps & cautions noticed

- **Magic-header byte list in the task prompt is wrong.** The verified header
  is `51 73 70 74 31 57 6d 4a 4f 4c` ("Qspt1WmJOL"), 10 bytes — not the
  `...49 6e 65 44` variant. Use the confirmed sequence.
- **dbserver and NFS ports are dynamic**, never hard-coded — always discover via
  TCP 12523 (dbserver) and UDP 111 portmap (mount/nfs). Only 12523 and 111 are
  fixed.
- **Device-number recommendation conflicts** (5 vs 7) between projects, driven
  by the slot-occupancy-vs-metadata-access trade-off. For a build that needs
  remotedb metadata, you *must* be 1–6; if you only need passive status/beat
  monitoring or NFS metadata, prefer >6 to avoid occupying a slot.
- **CDJ-3000 needs distinct startup/keep-alive packet templates** (one byte
  longer initial announcement, different byte `0x35` in keep-alive). Getting
  this wrong can repeatedly kick a 3000 (set to player 5/6) off the network
  (dysentery startup.adoc).
- **All-in-one quirks**: XDJ-XZ/Opus Quad don't broadcast media-slot info and
  the XDJ-XZ has broken device-number assignment on the laptop port; XDJ-RX
  does not implement the protocol and may crash when probed.
- This document covers topology and framing only. The **detailed byte layouts**
  of keep-alive, beat, status, media, and metadata packets (offsets, length
  conventions, field semantics) are in dysentery's `startup.adoc`, `vcdj.adoc`,
  `beats.adoc`, `media.adoc`, and `track_metadata.adoc` — they should be
  captured in follow-up research docs before implementing senders.
- **Endianness** is assumed big-endian throughout from parser code and captures;
  confirm per-field when implementing.
