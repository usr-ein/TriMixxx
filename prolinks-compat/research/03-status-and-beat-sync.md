# ProLink / DJ Link — Status, Beat, Sync, On-Air, Tempo (realtime player state)

> **⚠ Pre-hardware document.** Written from published reverse-engineering
> literature before any capture from real CDJs existed. Much of it has since
> been confirmed, and a good deal corrected, by testing against two
> CDJ-2000NXS. **`docs/PROTOCOL.md` is the current specification; where this
> document disagrees with it, this document is wrong.** `docs/FINDINGS.md`
> records each correction with its evidence.

Realtime player-state side of the Pioneer Pro DJ Link protocol: the **beat packet**
(UDP **50001**) and the **detailed status packet** (UDP **50002**). Covers structure,
byte offsets, sync/master handoff, mixer integration, and what a virtual CDJ must send.

Sources:
- `dysentery/doc/modules/ROOT/pages/beats.adoc` (beats)
- `dysentery/doc/modules/ROOT/pages/sync.adoc` (sync/master)
- `dysentery/doc/modules/ROOT/pages/mixer_integration.adoc` (fader start / on-air)
- `dysentery/doc/modules/ROOT/pages/vcdj.adoc` (CDJ + mixer + rekordbox status packets)
- `dysentery/doc/modules/ROOT/examples/status_shared.edn` (shared packet header)
- `python-prodj-link/prodj/network/packets.py` (`BeatPacket`, `StatusPacket` constructs)
- `prolink-connect/src/status/{index,types,utils}.ts` (parser, flags, port constants)

Confidence tags: **(confirmed)** = stated as known/verified in dysentery or matched by two
independent parsers; **(inferred)** = guess/partial knowledge per dysentery, or only one
parser implements it.

---

## 0. Shared packet header (all DJ-Link packets)

All packets on 50000/50001/50002 begin with the same 11-byte preamble
(`status_shared.edn` `draw-packet-header`, and python `UdpMagic`):

| Offset | Len | Field | Value | Notes |
|--------|-----|-------|-------|-------|
| `0x00`–`0x09` | 10 | magic | `51 73 70 74 31 57 6d 4a 4f 4c` = ASCII `Qspt1WmJOL` | **(confirmed)** every packet |
| `0x0a` | 1 | packet **type** | e.g. `0a` CDJ status, `29` mixer status, `28` beat, `03` on-air | **(confirmed)** |
| `0x0b`–`0x1f` | 21 | **Device Name** | ASCII, null-padded | **(confirmed)** name byte0 at `0x0b` (was `00` in keep-alive) |
> **Corrected on hardware — C14.** The name is **20** bytes, `0x0b`-`0x1e`. Byte `0x1f` is a structural constant `0x01` in all 1503 captured packets, mirroring the keep-alive on 50000. An emitter following this row writes a 21st name byte over it.
| `0x20` | 1 | **subtype** | `00` (mixer/beat/control), `03` (CDJ status), `01`/`02` (rekordbox/keepalive) | **(confirmed)** |
| `0x21` | 1 | **D** = device/player number | 1–6 players, `0x21` mixer, `0x11` rekordbox | **(confirmed)** |
| `0x22`–`0x23` | 2 | **len** | for subtype `00`/`03`: `len_r` = bytes *after* this field. For rekordbox subtype `01`: `len_p` = whole-packet length | **(confirmed)** vcdj.adoc:56 |

`prolink-connect` `PROLINK_HEADER` = the 11 bytes `0x00`–`0x0a`. Note: the type byte at
`0x0a` is part of `PROLINK_HEADER`. Ports: `BEAT_PORT=50001`, `STATUS_PORT=50002`,
`ANNOUNCE_PORT=50000` (`constants.ts`).

---

## 1. CDJ Status packet — UDP 50002, type `0x0a`

Sent roughly **every 200 ms** by each player (more often during jog-wheel activity on
newer players) (vcdj.adoc:20,90). Subtype byte at `0x20` = `0x03` for CDJs (values `03`–`06`
seen, likely a protocol/packet version) (vcdj.adoc:213-214).

### 1.1 Packet lengths across generations (confirmed, vcdj.adoc:84-88)

| Length | Hardware |
|--------|----------|
| `0xd0` (208) | Pre-nexus (older). No beat number; `F` flag byte is always `00` |
| `0xd4` (212) | Nexus |
| `0x11b` (283) | XDJ-1000 |
| `0x11c` (284) / `0x124` (292) | Newer firmware / Nexus 2 |
| `0x200` (512) | CDJ-3000 (`len_r` = `0x438` in python construct — note that is the python sentinel) |

`prolink-connect` ignores any packet `< 0xc8` bytes (short rekordbox status) (`utils.ts:15`).
python `StatusPacket` uses `extra.remaining_bytes` to branch CDJ-3000 (`0x438`) via `StopIf`.

### 1.2 Key field offset table (CDJ status, type `0x0a`)

Offsets are absolute from packet start. Cross-checked against `prolink-connect/utils.ts`
(PC), `python-prodj-link` `StatusPacket.content.cdj` (PY), and `vcdj.adoc`.

| Offset | Len | Sym | Field | Notes / values | Conf |
|--------|-----|-----|-------|----------------|------|
| `0x21` | 1 | D | device number | player # as displayed; PC `packet[0x21]` | confirmed |
| `0x24` | 1 | D | device number (dup) | repeated | confirmed |
| `0x27` | 1 | A | activity | `00` idle, `01` playing/searching/loading | confirmed |
| `0x28` | 1 | D_r | **source player #** | device track loaded from; `00` none; =D if local | confirmed (PC `0x28`) |
| `0x29` | 1 | S_r | **source slot** | `00` none, `01` CD, `02` SD, `03` USB, `04` rekordbox, `05` unknown, `06` streaming-direct, `07` USB2 (XDJ-AZ), `09` Beatport LINK | confirmed (PC `0x29`) |
| `0x2a` | 1 | T_r | track type | `00` none, `01` rekordbox, `02` unanalyzed, `05` audio CD, `06` streaming | confirmed (PC `0x2a`) |
| `0x2c`–`0x2f` | 4 | rekordbox | **track ID** | rekordbox DB id / CD track # / streaming index | confirmed (PC `0x2c` u32) |
| `0x32`–`0x33` | 2 | Track | track number (position in list) | | confirmed |
| `0x35` | 1 | t_srt | track sort mode | `00` default, `01` title, `02` artist, `03` album, `04` BPM, `05` rating, `0c` key | confirmed |
| `0x37` | 1 | t_src | track source menu | `04` track, `05` playlist, `11` folder/CD, `16` history, etc. | confirmed |
| `0x38`–`0x3a` | (4) | t_cat1 | menu category 1 id | | inferred |
| `0x3b`–`0x3f` | (4) | t_cat2 | menu category 2 id | | inferred |
| `0x46`–`0x47` | 2 | d_n | tracks in disc/playlist/menu | | confirmed |
| `0x6a` | 1 | U_a | USB activity | toggles `04`/`06` | confirmed |
| `0x6b` | 1 | S_a | SD activity | toggles `04`/`06` | confirmed |
| `0x6f` | 1 | U_l | USB local state | `04` none, `00` loaded, `02`/`03` unmounting | confirmed |
| `0x73` | 1 | S_l | SD local state | `04` none, `00` loaded, `02`/`03` unmounting | confirmed |
| `0x75` | 1 | L | Link available | `01` if any media present on network | confirmed |
| `0x7b` | 1 | **P_1** | **play state** | `00` none, `02` loading, `03` play, `04` loop, `05` paused, `06` cued, `07` cue-play, `08` cue-scratch, `09` search, `0e` CD spundown, `11` end, `12` emergency loop | confirmed (PC `0x7b`) |
| `0x7c`–`0x7f` | 4 | Firmware | ASCII firmware version | | confirmed |
| `0x84`–`0x87` | 4 | **Sync_n** | sync counter | bumped to (max+1) when player gives up master | confirmed |
| `0x89` | 1 | **F** | **status flag bits** | see §1.3 (only nexus+; older=`00`) | confirmed (PC `0x89`) |
| `0x8b` | 1 | P_2 | play-state-2 bitfield | nexus `7a` play/`7e` stop; nxs2 `fa`/`fe`; XDJ-XZ `9a`/`9e`; pre-nexus `6a`/`6e` | confirmed |
| `0x8c`–`0x8f` | 4 | **Pitch_1** | effective pitch | `00100000`=0%, `00000000`=−100%, `00200000`=+100%. PC reads 3 bytes at `0x8d` | confirmed |
| `0x90`–`0x91` | 2 | M_v | master/BPM-valid | `7fff` no track, `8000` rekordbox track (tempo accepted as master), `0000` non-rekordbox | confirmed |
| `0x92`–`0x93` | 2 | **BPM** | track BPM ×100 | `ffff` = no track. PC `0x92` u16 / 100 | confirmed |
| `0x94`–`0x95` | 2 | M_slip | slip master-valid | XDJ-1000/nxs2 only; else `7fff` | inferred |
| `0x96`–`0x97` | 2 | BPM_slip | slip BPM ×100 | else `ffff` | inferred |
| `0x98`–`0x9b` | 4 | Pitch_2 | slider pitch (settling) | PC reads "effectivePitch" at `0x99` (3 bytes) | confirmed |
| `0x9d` | 1 | P_3 | play mode detail | `01` paused/reverse, `09` vinyl fwd, `0b` slipping, `0d` CDJ fwd | confirmed |
| `0x9e` | 1 | **M_m** | master-meaningful | `00` not master, `01` master playing rekordbox, `02` master but non-rekordbox (no tempo) | confirmed |
| `0x9f` | 1 | **M_h** | master handoff | normally `ff`; during handoff holds # of incoming master | confirmed |
| `0xa0`–`0xa3` | 4 | **Beat** | absolute beat counter (1..end) | `0` at track start (paused), `ffffffff` if no rekordbox track / pre-nexus. PC `0xa0` | confirmed |
| `0xa4`–`0xa5` | 2 | Cue | beats-to-next-cue | `01ff` none, `0100` at 64 bars, counts down to `0000`. PC treats `0x1ff` (MAX_INT9) as null | confirmed |
| `0xa6` | 1 | **B_b** | beat in bar (1–4) | `0` if no rekordbox track. PC `0xa6` | confirmed |
| `0xb3` | 1 | u_g | update-grid | `ff` for one packet when beat grid edited | inferred |
| `0xb7` | 1 | M_p | media presence (CDJ-3000) | bit0 SD, bit1 USB | inferred |
| `0xb8` | 1 | U_e | USB unsafely ejected | `1`/`0` | confirmed |
| `0xb9` | 1 | S_e | SD unsafely ejected | `1`/`0` | confirmed |
| `0xba` | 1 | el | emergency loop active | `1` active. PC `isEmergencyMode = Boolean(packet[0xba])` | confirmed |
| `0xc0`–`0xc3` | 4 | Pitch_3 | effective pitch (copy of P1) | | confirmed |
| `0xc4`–`0xc7` | 4 | Pitch_4 | slider pitch (copy of P2) | | confirmed |
| `0xc8`–`0xcb` | 4 | Packet | packet counter | increments per packet; CDJ-3000 fixed `00000000`. PC `0xc8` | confirmed |
| `0xcc` | 1 | nx | hardware class | `0f` nexus, `1f` XDJ-XZ/CDJ-3000, `05` older | confirmed |
| `0xcd` | 1 | t | touch-audio support | bit5 set if supported | inferred |
| `0xd0`–`0xef` | 32 | — | settings block 1 (CDJ-2000+) | starts `12 34 56 78`; waveform color/position | inferred |
| `0xff`–`0x10f` | — | — | settings block 2 (CDJ-3000) | | inferred |
| `0x113` | 1 | P_4 | playback bitmask | meaning unknown | inferred |
| `0x116`–`0x117` | 2 | T_b | bar time-steps | hi-res phase (only when paused+master/sub-beat loop) | inferred |
| `0x11a`–`0x11b` | 2 | T_pos | position-in-bar | | inferred |
| `0x158` | 1 | M_t | master tempo engaged | `00`/`01` (CDJ-3000) | inferred |
| `0x15c`–`0x15e` | 3 | Key | track key | note/major-minor/accidental | inferred |
| `0x164`–`0x16b` | 8 | KeyShift | key shift (cents, i64) | ±100/semitone | inferred |
| `0x1b6`–`0x1b9` | 4 | Loop_s | loop start (CDJ-3000) | ms = value×65536/1000 | inferred |
| `0x1be`–`0x1c1` | 4 | Loop_e | loop end | same encoding | inferred |
| `0x1c8`–`0x1c9` | 2 | Loop_b | loop whole beats | | inferred |

### 1.3 Status flag byte F (offset `0x89`) — bit field (confirmed)

vcdj.adoc:413-433, `prolink-connect/types.ts` `StatusFlag`, python `StateMask`.

```
bit7 bit6 bit5 bit4 bit3 bit2 bit1 bit0
  1  Play Mast Sync OnAir 1  BPM   0      (typical mask)
```

| Bit | Mask | Meaning |
|-----|------|---------|
| 6 | `0x40` | **Play** — 1 when playing |
| 5 | `0x20` | **Master** — this device is tempo master |
| 4 | `0x10` | **Sync** — sync mode on |
| 3 | `0x08` | **On-Air** — believes its output audible (needs mixer cooperation; platter goes red) |
| 1 | `0x02` | **BPM** — degraded to BPM-sync (jog-nudged; tracks tempo but not beat-aligned) |
| 7, 2 | `0x84` | always 1 (python `StateMaskAdapter` ORs in `0x84`) |

PC: `isOnAir = (F & 0x08)`, `isSync = (F & 0x10)`, `isMaster = (F & 0x20)`. Pre-nexus
players always send `F=00`, so play/master/sync/on-air cannot be inferred from them
(vcdj.adoc:411).

### 1.4 BPM / pitch math (confirmed)

- Track BPM (2 dp) = `(byte[0x92]*256 + byte[0x93]) / 100`.
- Pitch % = `((byte[0x8d]*0x10000 + byte[0x8e]*0x100 + byte[0x8f]) − 0x100000) / 0x100000 * 100`.
- Effective (displayed) BPM = `(byte[0x92]*0x100 + byte[0x93]) * (byte[0x8d]*0x10000 + byte[0x8e]*0x100 + byte[0x8f]) / 0x100000`.
- `Pitch_1`/`Pitch_3` = effective pitch in effect (fader or synced master); `Pitch_2`/`Pitch_4`
  = local fader position (drifts during brake/release ramps) (vcdj.adoc:453-462).

---

## 2. Mixer status packet — UDP 50002, type `0x29`

Length **`0x38` (56) bytes**, subtype `00`, `len_r = 0x14` (vcdj.adoc:28,51).
DJM device number `D = 0x21` at bytes `0x21` and `0x24`. python `StatusPacket.content.djm`.

| Offset | Sym | Field | Notes | Conf |
|--------|-----|-------|-------|------|
| `0x21`,`0x24` | D | device number | `0x21` for the DJM | confirmed |
| `0x27` | F | status flag | only `f0` (master) or `d0` (not master) seen — mixer always "playing"+"synced", never on-air | confirmed |
| `0x28`–`0x2b` | Pitch | pitch | always `00100000` (+0%) | confirmed |
| `0x2e`–`0x2f` | BPM | BPM ×100 | only valid for rekordbox source; mixer passes master's BPM | confirmed |
| `0x36` | M_h | master handoff | `00` no master → `ff` once master exists; player# during handoff | confirmed |
| `0x37` | B_b | beat in bar (1–4) | NOT synced to master, not useful — use beat packets instead | confirmed |

Mixer BPM = `(byte[0x2e]*256 + byte[0x2f]) / 100`. Rekordbox sends a near-identical
"mixer status" packet but with subtype `01` (so `len_p` = whole length) and `F = c0`
(vcdj.adoc:56,737-744); `B_b` always `0` from rekordbox.

---

## 3. Beat packet — UDP 50001, type `0x28`

**`0x60` (96) bytes**, subtype `00` (byte `0x20`). python `BeatPacket.type_beat`,
`stype_beat = 0x3c`. The header here has the name beginning at `0x0b` and `len_r = 0x3c`
at `0x22`–`0x23` (beats.adoc:43-48).

### 3.1 What triggers it / cadence (confirmed)

- Sent on **each beat** — arrival = the player is starting a new beat (beats.adoc:17).
- CDJs send beat packets **only while playing AND only for rekordbox-analyzed tracks**
  (CDJ-3000 can self-analyze on first play) (beats.adoc:18).
- The **mixer sends them all the time**, acting as a backup metronome when no other device
  counts beats (beats.adoc:19-20).
- Timing values are reported **as if at 0% pitch** — scale by current pitch yourself
  (beats.adoc:54-60).

### 3.2 Beat packet offset table (confirmed)

| Offset | Len | Sym | Field | Notes |
|--------|-----|-----|-------|-------|
| `0x0b`–`0x1f` | 21 | name | device name | |
| `0x20` | 1 | subtype | `00` | |
| `0x21` | 1 | D | device number | player #, `21` mixer |
| `0x22`–`0x23` | 2 | len_r | = `003c` | |
| `0x24`–`0x27` | 4 | **nextBeat** | ms to next beat | `ffffffff` if track ends first |
| `0x28`–`0x2b` | 4 | **2ndBeat** | ms to beat after next | |
| `0x2c`–`0x2f` | 4 | **nextBar** | ms to next measure (1–4 beats away) | |
| `0x30`–`0x33` | 4 | **4thBeat** | ms to 4th upcoming beat | |
| `0x34`–`0x37` | 4 | **2ndBar** | ms to 2nd measure (5–8 beats) | |
| `0x38`–`0x3b` | 4 | **8thBeat** | ms to 8th upcoming beat | |
| `0x3c`–`0x53` | 24 | — | filler `0xff` ×24 | python `Padding(24)` |
| `0x54`–`0x57` | 4 | Pitch | pitch (`00100000`=0%) | |
| `0x58`–`0x59` | 2 | — | `00 00` (`ff` when scratching) | |
| `0x5a`–`0x5b` | 2 | BPM | track BPM ×100 | mixer passes master's BPM |
| `0x5c` | 1 | **B_b** | beat in bar (1→2→3→4) | downbeat=1 when from master |
| `0x5d`–`0x5e` | 2 | — | `00 00` (`ff` scratching) | |
| `0x5f` | 1 | D | device number (redundant copy) | subtype-`00` quirk |

Effective BPM combining: `(byte[0x5a]*100 + byte[0x5b]) * (byte[0x55]*0x10000 +
byte[0x56]*0x100 + byte[0x57]) / 0x6400000` (beats.adoc:91-96).

### 3.3 Absolute Position packet — UDP 50001, type `0x0b` (CDJ-3000 only, confirmed)

Sent to all devices **every 30 ms** while a track is loaded, even when not playing
(beats.adoc:107-109). Subtype `02` (`draw-packet-header 0x0b 2`). Fields: `D`(`0x21`),
`len_r`(`0x22`), `TrackLength` u32 seconds (`0x24`), `Playhead` u32 ms (`0x28`),
`Pitch` i32 = slider×100 (`0x2c`), 8×`00`, `BPM` u32 = effective BPM×10 (`ffffffff` unknown).
This is the reliable way to track position with scratch/loop/reverse on CDJ-3000.

---

## 4. Mixer integration (50001 control packets)

### 4.1 Channels on-air — type `0x03` (confirmed, mixer_integration.adoc:33-79)

Mixer **broadcasts** to 50001. Four-channel form: subtype `00`, `len_r = 0x0009`, **9 bytes**.
`F1`–`F4` at `0x24`–`0x27` = `01` on-air / `00` off (off = silenced by crossfader, channel
fader, trim, filter, source switch, or master level), then 5×`00`.

Six-channel form (CDJ-3000 / DJM-V10): **subtype `03`**, `len_r = 0x0011` (17 bytes):
`F1`–`F4` at `0x24`–`0x27`, 5×`00`, then `F5`,`F6`, then 6×`00`. python parses only the
4-channel `type_mixer` (`ch_on_air` = `Array(4)`).

A CDJ only reports On-Air (`F` bit3) because the mixer told it to. With no DJM present a
virtual CDJ can synthesize this packet, but a real DJM/XDJ-XZ will quickly reassert.

### 4.2 Fader start — type `0x02` (confirmed, mixer_integration.adoc:8-31)

Mixer (or anyone) sends to 50001. subtype `00`, `len_r = 0x0004`, **4 bytes** `C1`–`C4` at
`0x24`–`0x27`: `00` start (if at cue), `01` stop+return to cue, `02` leave as-is. Can be
broadcast. Not supported by XDJ-XZ or CDJ-3000.

### 4.3 Mixer as tempo master

The DJM reports master via `F` byte (`f0`=master, `d0`=not) in its `0x29` status packet,
and participates in handoff via `M_h` at `0x36`. It always reports +0% pitch and passes
through the master player's BPM. Its BPM is only valid when a rekordbox-analyzed source is
playing — for self-detected BPM from analog/unanalyzed audio it does NOT transmit the value
even though it shows it on screen (vcdj.adoc:71-72).

---

## 5. Sync & tempo master (control packets on 50001 + status fields)

### 5.1 Sync control — type `0x2a` (confirmed, sync.adoc:8-29)

Send to target's port 50001. subtype `00`, `len_r = 0x0008`, 8 bytes after len. `D` at
`0x24` = the player # you are impersonating; `S` at the last byte (`0x2b`): `0x10` = turn
Sync on, `0x20` = Sync off, **`0x01` = become tempo master** (acts as pressing Master).
Targets either a CDJ or DJM.

### 5.2 Tempo master handoff handshake (confirmed, sync.adoc:31-88)

The "Baroque" dance:

1. **No current master:** device just becomes master and sets `F` bit5 (`0x20`) + `M_m`
   in its status packets. Done.
2. **Existing master, coordinated takeover:**
   - Challenger sends **takeover request type `0x26`** to current master's 50001: subtype
     `00`, `len_r = 0x0004`, `D` at `0x24` = challenger's #.
   - Master replies **takeover response type `0x27`** to challenger's 50001: subtype `00`,
     `len_r = 0x0008`, `D` at `0x24` = master's own #, and a trailing `... 00 00 00 01`.
   - Outgoing master keeps asserting master (`F` bit5 + `M_m`) but sets **`M_h`** (CDJ `0x9f`,
     mixer `0x36`) = challenger's device number, announcing the handoff.
   - When the challenger sees its own # in the outgoing master's `M_h`, it starts asserting
     master in its own status (`F` bit5, `M_m`).
   - When the outgoing master sees the new master assert the role, it clears its own master
     flags, resets `M_h` back to `ff`, and bumps its **`Sync_n`** (`0x84`) to one greater
     than any other player's `Sync_n`. (Mixers don't report `Sync_n`.)

### 5.3 Unsolicited handoff (confirmed, sync.adoc:90-94)

If the current master is **stopped** (not playing) and sees another device that is both
**synced and playing**, it sets `M_h` to that device's number, telling it to become master.
That device should then take over as in step 2's tail even though it never asked.

### 5.4 Beat-grid alignment / downbeat

Master is identified from CDJ status `F` bit5. The **downbeat** is read from the master
player's **beat packets** `B_b` (`0x5c`), counting 1→2→3→4 — *not* from status-packet
`B_b`, which is not beat-aligned and arrives off-cadence (vcdj.adoc:519-521, mixer
`B_b` explicitly "not useful", vcdj.adoc:74). Status `Beat` (`0x a0`, absolute beat number)
plus a downloaded beat grid lets you translate beats↔track-time; between (rare) beat
packets you interpolate using pitch from status packets (beats.adoc:103-105).

---

## 6. What a Virtual CDJ MUST send vs optional

From `vcdj.adoc` "Creating a Virtual CDJ" (lines 8-22):

**MUST (to be alive / receive direct status):**
- Bind a UDP server socket to **port 50002**.
- Send **keep-alive packets to port 50000** broadcast, ~every **1.5 s**, copying a CDJ
  keep-alive (type `0x06`) using the **real MAC + IP** of the receiving interface so devices
  can reach you (vcdj.adoc:11-15). Once you do this, other players/mixers start unicasting
  their status packets to your 50002 socket. (Keep-alive details are in `startup.adoc`, not
  this file's scope, but it is the required liveness signal.)

**SHOULD / required for specific features:**
- **Device number:** use `07` to avoid clashing with real players (1–4, or 1–6 for CDJ-3000)
  (vcdj.adoc:14). BUT a non-standard number **breaks dbserver metadata requests**; for
  metadata you must take a real slot number (1–6, ≤6) (vcdj.adoc:17, 264). So if you want
  metadata/streaming-track queries to work you must masquerade as a real low player number.
- To **participate in sync/master** you must send CDJ **status packets** (type `0x0a`) on
  50002 with correct `F`/`M_m`/`M_h`/`Sync_n` — that is how the handshake (§5.2) observes
  you. dysentery/prolink note that you must send status packets for metadata features and to
  be treated as a legit player in the master dance.
- To drive **on-air / fader-start** without a DJM you may broadcast the `0x03`/`0x02`
  packets (§4), but these are optional and a real DJM overrides them.

**OPTIONAL / receive-only:**
- Beat packets (`0x28`) and Absolute Position (`0x0b`) need only be *received* to track
  tempo/position; a virtual CDJ that only observes does not have to emit them. It must emit
  beat packets only if it wants to act as a playing master others sync to.

`prolink-connect`'s `StatusEmitter` is purely a **listener** on the status socket — it parses
incoming `0x0a` status and `0x06` media-slot packets and never synthesizes status; the
liveness/keep-alive is handled elsewhere in that library.

---

## Summary

The realtime layer has two workhorse packets sharing the `Qspt1WmJOL` header: the
~96-byte **beat packet** (UDP 50001, type `0x28`) emitted on every beat by playing CDJs
(rekordbox tracks only) and continuously by the mixer, carrying ms distances to the next
1st/2nd/4th/8th beat and next/2nd bar plus pitch, BPM×100 and the in-bar beat counter; and
the variable-length **status packet** (UDP 50002, type `0x0a` CDJ / `0x29` mixer, 208–512
bytes by generation) carrying device/source-player/source-slot, track id & type, play state
(`0x7b`), the all-important flag byte `F` (`0x89`: play `0x40`/master `0x20`/sync `0x10`/
on-air `0x08`/bpm-sync `0x02`), BPM (`0x92`), beat number (`0xa0`), in-bar beat (`0xa6`),
and the master-handoff fields `M_m`(`0x9e`)/`M_h`(`0x9f`)/`Sync_n`(`0x84`). Master is
announced via `F` bit5 + `M_m` and changed through a multi-step handshake (request `0x26`,
response `0x27`, `M_h` announcement, `Sync_n` bump), plus an unsolicited variant when the
master is stopped. The DJM reports channels on-air (`0x03`) and fader-start (`0x02`) on
50001 and acts as a fallback metronome/master. A virtual CDJ MUST bind 50002 and broadcast
keep-alives to 50000 every ~1.5 s to receive direct status; to do metadata or join the
sync/master dance it must additionally take a real low device number and emit valid status
packets.

### Gaps / cautions
- Most CDJ-3000-specific high-offset fields (settings blocks, key, key-shift, loop,
  T_b/T_pos, M_p, P_4) are dysentery-**inferred** and not parsed by prolink-connect.
- python `StatusPacket` uses `extra.remaining_bytes` sentinels (`0xb0`, `0xf8`, `0x438`)
  that differ from the dysentery whole-packet lengths (`0xd4`, `0x11c`, `0x200`) — they are
  `len_r` values, not total length; reconcile before hardcoding.
- prolink-connect's `mediaSlotFromPacket` has visibly buggy slice arithmetic
  (`packet.slice(0x2c, 0x0c + 40)`); treat its media-slot offsets with suspicion (out of
  scope here but flagged).
- Exact byte layout between `0x46` and `0x7b`, the four pitch copies' precise differences,
  and `P_2`/`P_3`/`P_4`/`u9`/`u10` semantics are only partially understood.
- Beat-packet behavior for the CDJ-3000 self-analyzed tracks and the six-channel on-air
  subtype `03` rest on limited captures.
