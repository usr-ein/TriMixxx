# The Pro DJ Link protocol, as we have observed it

**This is the current, authoritative specification.** It describes the protocol
as it actually behaves on CDJ-2000NXS hardware running firmware 1.44, and it is
what to implement from.

Three documents, three jobs:

| | |
|---|---|
| **`docs/PROTOCOL.md`** (this) | What is true now. Implement from this. |
| `docs/FINDINGS.md` | *How we know* — every finding with its evidence, in the order it was learned, including the ones that turned out wrong. Cited here as F*n*. |
| `research/00`–`09` | The pre-hardware literature review. Useful for context and for claims we have not tested; **where it disagrees with this document, it is wrong.** |

Everything below is marked:

- **confirmed** — observed on the wire from real hardware, usually with our own
  implementation reproducing it byte-for-byte;
- **inferred** — consistent with observation but not directly demonstrated;
- **unknown** — we reproduce bytes we do not understand.

A note on how to read the "unknown" entries: several fields here are values we
copy without knowing their meaning. That is deliberate. The alternative —
sending a plausible zero — has broken playback twice (F33, F35).

---

## 1. Network model

| Port | Transport | Purpose |
|---|---|---|
| 50000 | UDP, broadcast | Discovery, device-number claiming, keep-alive |
| 50001 | UDP | Beat / sync (not implemented here) |
| 50002 | UDP, **unicast** | Status, media query, device settings |
| 12523 | TCP | "Which port is your dbserver on?" |
| 1051 | TCP | dbserver — the metadata protocol LINK drives |
| 111 | UDP | ONC RPC portmapper → mountd, nfsd |
| 2049 | UDP | nfsd (standard port) |
| 48276 | UDP | mountd (Pioneer's port; discover it via portmap anyway) |

Addressing is link-local (`169.254.0.0/16`), self-assigned. A CDJ tries DHCP
about three times first and takes ~9 s to send its first packet after power-on —
worth knowing when timing a capture (F8).

**A CDJ does not answer ICMP.** Ping is useless as a reachability test.

Every UDP packet begins with the 10-byte magic `Qspt1WmJOL`, then a one-byte
type. **The header differs by port**: on 50000 a subtype byte follows the type
and the name starts at `0x0c`; on 50002 the name starts at `0x0b` and byte
`0x1f` is a structural `0x01` (C14). Reusing one decoder for both yields
plausible nonsense rather than an error.

---

## 2. Discovery and device numbering — UDP 50000

### 2.1 Packet types *(confirmed)*

| Type | Name | Length | |
|---|---|---|---|
| `0x0a` | HELLO | `0x25` | "I am here" |
| `0x00` | CLAIM_MAC | `0x2c` | stage 1 |
| `0x02` | CLAIM_IP | `0x32` | stage 2 |
| `0x04` | CLAIM_NUMBER | `0x26` | stage 3 |
| `0x05` | NUMBER_IN_USE | `0x26` | "the number I hold is N" |
| `0x06` | KEEP_ALIVE | `0x36` | every 2.0 s |
| `0x08` | NUMBER_CONFLICT | `0x29` | "that number is mine" |

The subtype byte equals the total length for every type. `research/02` §0.1
gives type `0x04` a length of `0x2a`; it is `0x26` (C2).

### 2.2 The handshake *(confirmed)*

```
3× HELLO → 3× CLAIM_MAC → 3× CLAIM_IP → N× CLAIM_NUMBER → KEEP_ALIVE forever
```
~300 ms apart, all broadcast. **N is 3 when the device boots into an empty
network and 1 when it joins one that already has peers** (C13). It is *not*
governed by the auto/manual setting, as `research/02` §1.0 has it — three
controlled boots with the assignment mode held constant settle that.

### 2.3 Automatic vs manual numbering *(confirmed)*

Byte `0x31` of CLAIM_IP: **`0x01` automatic, `0x02` a specific number** (F36).
Every capture before F36 had both decks manually numbered, so only `0x02` had
ever been seen — `research/02` marked this "confirmed" on documentation alone.

An auto-numbered deck alone on an empty network **picked 2, not 1**, with 1
free. So auto assignment is not "lowest free number"; the deck's previous manual
setting was 2, which suggests it is remembered *(inferred)*.

**Type `0x05` is not only a mixer packet.** `research/02` §1.7 files it under
mixer channel assignment. In the same instant a joining deck sent its stage-3
claim, an auto-numbered deck **unicast** a type `0x05` back carrying its own
number — same 38-byte layout as CLAIM_NUMBER, differing only in the type byte.
Absent from every capture with two manually-numbered decks. Reading it as "this
number is taken" fits what an auto-assigning device must publish *(inferred,
n=1)*.

### 2.4 Keep-alive fields *(confirmed)*

- **Interval 2.0026 s**, a tight hardware timer — not the 1.5 s `research/02`
  gives, which traces back to what reference *tools* chose (C12). The 10 s
  device timeout is therefore 5 missed keep-alives, not 6–7.
- **Byte `0x25` is "was I first on this network?"** — `0x02` if the device was
  first, `0x01` if peers were already present. Latched at boot and never
  re-evaluated: a deck held `0x02` while its peer count went 1→2 (F9). Not a
  CDJ/mixer role byte as documented, and not the peer count.
- **Byte `0x35` is `00`** on nexus hardware, not `01` (C3). `0x64` is required
  for CDJ-3000 coexistence.
- Byte `0x30` of CLAIM_IP is a **CDJ/mixer role**, not a constant: a
  DJM-2000nexus sends `02`, a CDJ `01` (C1).
- The device name is 20 bytes, NUL-padded, and `CDJ-2000nexus` is the exact
  casing (F1).

---

## 3. Status, media and settings — UDP 50002

### 3.1 Status is unicast to announced peers only *(confirmed)*

This decides the whole consume-side design. Every one of 1507 status packets in
one session went deck-to-deck; **not one reached a host that had not announced
itself**, though it was on the network with an address the whole time (F21).

| Mode | Media state | dbserver | Risk |
|---|---|---|---|
| Passive (no announcement) | poll MOUNT `EXPORT` | unavailable | none |
| Announced (virtual CDJ) | pushed, ~200 ms | available if number ≤ 4 | contends for a number |

**A number in 1–4 is required to be *browsable*, not merely preferred** (F45).
At device 5 a deck accepts the announcement completely — it puts us in its
device table and unicasts 900+ status packets to us — and then never sends a
single media query, so it never offers us as a source. The check precedes the
whole browse path, and the failure is silent. Serving is impossible when 1–4
are all taken; degrade to the observer number 7 with serving off.

### 3.2 CDJ status, type `0x0a` *(confirmed)*

284 bytes on firmware 1.44. **Length does not identify the generation** —
`research/03` maps `0x11c` to "Nexus 2", and a plain CDJ-2000nexus sends it
(F22). Sent every ~200 ms.

Media presence lives at offset `0x6f` (USB) and `0x73` (SD), and **nowhere
else** (F20). A device that does not emit status is a device with empty slots
however loudly it announces.

Across 749 consecutive packets from an idle deck only **six** bytes ever
changed. So our emitter starts from a captured skeleton and substitutes only
understood fields; the ~270 unknown bytes are reproduced exactly. That is the
difference between plausible and indistinguishable (F23).

*Observed but not imitated:* a real deck sends each status packet from a
different, incrementing source port (6688, 6689, …).

### 3.3 Media query `0x05` / response `0x06` *(confirmed)*

**The step no reference implementation performs, because none of them serve.** A
deck asks what a slot actually contains, and will not offer a medium it believes
is empty — announcing and emitting status are not enough (F24).

Query carries the requester, its IP, the target device and the slot. Response
(192 bytes):

| Offset | Field |
|---|---|
| `0x24` | device number |
| `0x28` | slot |
| `0x2c`–`0x6b` | media name, UTF-16 **big**-endian, 64 bytes |
| `0x6c`… | creation date, e.g. `2025-06-24` |
| `0xa4` | **track count** |
| `0xac` | **playlist count** |
| `0xb4` / `0xbc` | total / free bytes |

One query per slot, issued when a deck first browses it, not repeated (F37).
Answer with the true counts: a deck told there are no tracks has no reason to
offer the medium.

### 3.4 Device settings `0x35` / `0x36` *(confirmed)*

"LOAD SETTINGS from that device's medium" — and **it is not a file read** (F38).
The requesting deck mounts the NFS export, reads nothing from it, and asks here
instead. A server implementing only NFS sees a mount, concludes nothing was
wanted, and never learns a request was made.

```
0x35  40B unicast   0x24 requester, 0x25 slot
0x36  80B unicast   same, then 0x28 magic 0x12345678, one word, 32 settings bytes
```

The bytes come from `PIONEER/MYSETTING.DAT` — see §7.3.

---

## 4. File access — ONC RPC / NFSv2 over UDP

### 4.1 A CDJ-2000NXS serves NFS *(confirmed)*

The go/no-go gate for the whole transport, and it passes (F10):

```
100003  2  udp   2049  nfs
100005  1  udp  48276  mountd
100000  2  udp    111  portmapper
```

Three independent observations across three devices give the same numbers (F6),
so 48276 looks like a Pioneer constant rather than a per-boot allocation — but
portmap discovery is still the right way to find it.

### 4.2 Passive access works *(confirmed)*

A CDJ serves files to a host that has **never announced itself** (F11). The
mechanism: the export's access list is the whole link-local subnet
(`169.254.0.0/255.255.0.0`), so an unannounced host is inside the permitted set
by default (F12).

> **Caveat.** A device in dysentery's capture exported to two **per-host**
> entries instead. A device scoping its export that way would presumably refuse
> an unannounced client, making passive access firmware- or model-dependent. So
> treat `NFSERR_ACCES` on MNT as "try announcing first", not as fatal.

### 4.3 Exports *(confirmed)*

| Slot | Export |
|---|---|
| SD | `/B/` |
| USB | `/C/` |
| rekordbox collection | `/` |

Both confirmed on hardware — USB in F12, SD in F37. But `/C/EXPORT` has also
been seen for USB on other hardware, so **match on the prefix** rather than the
whole string (C6).

**A real deck never calls `EXPORT`.** Not once in any session: it goes straight
to `MNT` with the documented path (F37). Enumerating is still the more robust
*client* behaviour, but a server answering only `MNT` satisfies real hardware.

Real players *do* call `UMNT`, once per slot, following the physical eject
(C9, F37) — `research/06` lists it as unused.

**Pioneer's UTF-16LE convention is not applied uniformly within one structure.**
In an `EXPORT` reply the path is UTF-16LE and the group names are plain ASCII
(C7). Decoding the groups as UTF-16LE turns `169.254.244.181/255.255.255.255`
into CJK mojibake — this was flagged in our code as an explicit assumption
*before* the capture, and the capture falsified it.

### 4.4 Credentials *(confirmed)*

`AUTH_UNIX` with `machine_name=""`, `uid=0`, `gid=0`, no gids. The stamp is a
**per-call nonce**, not the magic constant `research/06` describes: every call
in the reference capture carries a different one (C8).

### 4.5 Filehandles: a CDJ breaks the spec *(confirmed)*

RFC 1094 says a filehandle is 32 **opaque** bytes echoed back verbatim. A
CDJ-2000NXS keeps only the leading **12** and overwrites the rest with its own
file reference (F28):

```
served:   8a5edab282632443219e051e 4ade2d1d5bbc671c781051bf1437897cbdfea0f1
returned: 8a5edab282632443219e051e 03012d0000001b58000000000303010000000162
          |____ first 12 kept ____| |______ replaced by the player _______|
```

A server that trusts the spec works perfectly for browsing and fails at exactly
the moment a DJ loads a track. **Key the handle table on the first 12 bytes.**

*Consequence for multi-slot serving:* a handle is normally a hash of the path,
so two media sharing a root mint identical handles for the same relative path —
the root most obviously — and after truncation nothing distinguishes them. Put
each medium in its own subtree.

### 4.6 Reads *(confirmed)*

Audio travels over NFS, **streamed rather than downloaded** (F18): a deck reads
progressively and seeks on demand, touching ~38% of a file during load plus 30 s
of playback plus cue juggling.

Real CDJs use **8192-byte** reads, the NFSv2 maximum, relying on IP
fragmentation (F19). Our client defaults to 1280 to stay under the MTU — safe,
measured at 1459 KiB/s, but 6.4× more round trips than the hardware uses.

Serving must answer **random-access reads with low latency during playback**; a
stall is an audio dropout on someone's deck. Measured working: 75 MB lossless
files read across their whole length, scrubbed without delay (F39).

### 4.7 Name matching *(confirmed)*

A rekordbox medium is **FAT32** — case-insensitive — and `export.pdb` does not
necessarily record a directory entry's case: the database says `Gesaffelstein`
where the directory is `GESAFFELSTEIN`. The pdb also stores **NFC** where the
filesystem reports **NFD** (`カガミ`), because rekordbox wrote them through
different APIs.

A server comparing bytes answers `NFSERR_NOENT` for files that are plainly
there. Match exactly first, then fall back to `NFC(name).casefold()`, and always
return the handle for the name **as stored** (O6).

---

## 5. dbserver — TCP 1051

What the LINK button actually drives. Port discovery on 12523: a fixed 19-byte
query, a 2-byte reply. Both directions then exchange a 5-byte preamble
(`11 00 00 00 01`) before any message.

### 5.1 Wire format *(confirmed)*

Three things are easy to get wrong, and 208 real messages round-trip
byte-exactly once they are right (F7):

- **Two independent type numberings.** Every value is a tagged field
  (`0f`/`10`/`11`/`14`/`26`) and the header *also* carries a 12-byte blob of
  *argument* tags (`02`/`03`/`06`) describing the same arguments with different
  numbers. Both must agree.
- **Strings count characters, not bytes** — the length is the UTF-16 character
  count *including a trailing NUL*, so the bytes are twice that. The text is
  UTF-16 **big**-endian, the opposite of the NFS layer.
- **A zero-length binary argument is omitted entirely.** Not sent as an empty
  blob: absent. Reading one blindly desynchronises the stream.

Transaction ids start around `0x03800001`, not at 1 (C10).

### 5.2 Session shape *(confirmed)*

```
INTRODUCE → SUCCESS[0, our device number]
0x3e03    → 0x4b02[0x3e03, 0, our number, ""]     only from a foreign device
MENU_*    → SUCCESS[type, item count]
RENDER    → MENU_HEADER, n× MENU_ITEM, MENU_FOOTER
```

Three request types appear in ordinary browsing that `research/04` does not
document at all (C11): `0x3e03`, `0x3100` and `0x3d03`. All three must be
answered.

**Erroring on an unknown request is not free.** `0x3e03` answered with `0x4003`
made a deck fetch the root menu and then disconnect without opening anything
(F25). Two other undocumented requests must also be acknowledged: `0x3100` with
a bare `SUCCESS`, and `0x3d03` likewise *(the latter inferred — no capture shows
a real reply)*.

`0x0001` (MENU_CLOSE) draws **no reply at all** and must not discard state: a
deck sends it while still scrolling the list it is supposedly finished with
(F16, F27).

### 5.3 Concurrent menus *(confirmed)*

A deck does not browse one menu at a time. It dips into a metadata menu and
resumes a 692-item list at the next offset **without re-issuing the menu
request**, so a server must hold several result sets at once.

**Key them on `(descriptor, item count)`.** The count alone is not enough: F27
used it because the two menus in that capture had different sizes, and it broke
the moment metadata became 13 items and collided with a 13-track album (F41).
The descriptor's menu-target byte separates the list being scrolled (`M=1`) from
the transient menu dipped into (`M=2`), and it appears in both the menu request
and the render.

### 5.4 The descriptor *(confirmed)*

Argument 0 of nearly every request: `D << 24 | M << 16 | Sr << 8 | Tr`.

- **D** requesting device number
- **M** menu target (1 main / 2 transient)
- **Sr** slot: **SD 2, USB 3** (F37) — the discriminator when one connection
  carries two media
- **Tr** track type: 1 rekordbox, 2 unanalysed files

### 5.5 Root menu *(confirmed — all twelve)*

| id | type | | id | type | |
|---|---|---|---|---|---|
| `01` | `80` | GENRE | `11` | `90` | FOLDER |
| `02` | `81` | ARTIST | `12` | `91` | SEARCH |
| `03` | `82` | ALBUM | `14` | `93` | BITRATE |
| `04` | `83` | TRACK | `16` | `95` | HISTORY |
| `05` | `84` | PLAYLIST | `1b` | `8c` | **DATE ADDED** |
| `0a` | `89` | LABEL | `0c` | `8b` | KEY |

Labels are wrapped in **U+FFFA … U+FFFB** — presumably "this is a known
category, substitute your localised string". A bare label renders correctly and
is *not treated as openable* (F26). Flags are 0 on category items.

**Do not derive the id.** F26 computed it from the menu *request* type's low
byte and gave KEY the id BITRATE uses, so a deck opening KEY asked for bitrates.
F40 replaced that with `item type − 0x7f`, right for eleven of twelve and wrong
for DATE ADDED (`0x1b`/`0x8c`, a difference of `0x71`). Two derivations, two
exceptions — list them (F43).

### 5.6 Drilling in is a grid *(confirmed)*

One systematic request type: **`0x1000 | depth << 8 | category`**, where
*category* is the **menu request** type's low byte. All thirteen types seen in
one session are generated by that formula (F42):

```
0x1101 0x1102 0x1103 0x110a 0x1111 0x1112 0x1114   depth 1
0x1201 0x1202        0x120a               0x1214   depth 2
0x1301               0x130a                        depth 3
```

`research/04`'s three named messages — ARTISTS_FOR_GENRE, ALBUMS_FOR_ARTIST,
TRACKS_FOR_ALBUM — are three points in that grid.

> **Two numbering schemes coexist and disagree.** The category byte here is the
> *request* numbering (KEY `0x14`); the root-item id is a different numbering
> (KEY `0x0c`). This is exactly how F40's bug happened.

Arguments are `[descriptor, sort, filter…]` — one filter id per level. Chains
differ per category: GENRE narrows to an artist, then an album, then tracks;
ARTIST skips straight to albums; ALBUM straight to tracks.

**`ALL` entries** head a filtered list — id `0xffffffff`, type `0xa0`, label
`ALL` wrapped like a category — **but only when there is more than one entry**.
Choosing it sends `0xffffffff` as that level's filter, meaning "do not narrow".

**KEY has an extra level: harmonic tolerance** (F44). Choosing a key does not
list its tracks; it offers three widening matches, all with the same key id,
differing only in argument 0:

```
arg0=0   'Abm'                  1A
arg0=1   'Abm, B'               1A + 1B  (relative)
arg0=2   'Abm, B, Dbm, Ebm'     + the adjacent wheel positions
```

`0x1214` then takes `(key id, tolerance)` and returns tracks.

### 5.7 Sorting *(confirmed)*

The sort id is **argument 1** of `MENU_TRACK`, `MENU_PLAYLIST`, the drill types
and `MENU_SEARCH`. `0x1400` asks for the available orders; argument 2 names the
menu, and the reply is the same twelve regardless:

| id | type | | id | type | |
|---|---|---|---|---|---|
| 0 | `a1` | DEFAULT | 6 | `80` | GENRE |
| 1 | `a2` | ALPHABET | `0a` | `89` | LABEL |
| 2 | `81` | ARTIST | `0c` | `8b` | KEY |
| 3 | `82` | ALBUM | `0d` | `93` | BITRATE |
| 4 | `85` | BPM | `10` | `97` | DJ PLAY COUNT |
| 5 | `86` | RATING | `11` | `8c` | DATE ADDED |

**The sort selects the item's second column** — the feature that makes it
useful. The item type is **`(column field type << 8) | 0x04`**, so the familiar
`0x0704` is not "title and artist" as `research/04` names it, but *a track whose
second column is the ARTIST field*:

| sort | item type | label 2 | argument 0 |
|---|---|---|---|
| DEFAULT / ALPHABET / ARTIST | `0704` | artist | artist id |
| ALBUM | `0204` | album | album id |
| BPM | `0d04` | *(empty)* | `0x3390` = 132.00 |
| RATING | `0a04` | *(empty)* | rating |
| GENRE | `0604` | genre | genre id |
| LABEL | `0e04` | label | label id |
| KEY | `0f04` | `6A` | key id |
| BITRATE | `1004` | *(empty)* | `0x140` = 320 |
| DJ PLAY COUNT | `2a04` | *(empty)* | play count |
| DATE ADDED | `2e04` | `2025-11-13` | track id |

Numeric columns send an **empty** label and put the raw number in **argument
0** — the same slot that carries the file size in a track-info path item. The
deck formats it.

`DEFAULT` inside a playlist must keep the curated order.

### 5.8 Search *(confirmed)*

`[descriptor, sort, byte length, text, 0]`. Argument 2 is the term's UTF-16 size
*including its NUL*; **argument 3 is the text** (F44). A deck searches as you
type — one request per keystroke.

### 5.9 Metadata: thirteen items *(confirmed)*

`GET_METADATA` answers with **13** (F32), in this order: title, artist, album,
duration, tempo, comment, key, rating, colour, genre, date added, bitrate,
label.

- Each item carries the id of the **row it references** — artist 122, album 86 —
  not the track's own. That is what lets a player offer "more by this artist".
  Putting the track id everywhere renders identically and means something else.
- The **title item carries the artwork id**; without it a player never requests
  the image, so INFO shows no cover.
- Items are emitted unconditionally, including empty ones.
- Argument 0 is `1` on eight of them and `0` on the rest; the split matches no
  rule we can name and is reproduced as observed *(unknown)*.

### 5.10 Track info: six items *(confirmed)*

`GET_TRACK_INFO` answers with **6**, and returning only the path is enough to
render a track and walk it over NFS but **not enough to load it**:

| # | type | value |
|---|---|---|
| 1 | `04` | the **container** — `FileType` from pdb `0x5a` |
| 2 | `0b` | duration |
| 3 | `0d` | tempo ×100 |
| 4 | `23` | comment |
| 5 | `00` | path — **argument 0 is the file size** |
| 6 | `2f` | constant `1` *(unknown)* |

Two traps here, and we fell into both. **Argument 0 of the path item is the file
size** — zero on every other menu item ever captured, so it reads as structural
padding, and it is the one thing a load needs that browsing does not (F31).
And **the same type byte means different things in different replies**: `0x04`
is the title in `GET_METADATA` and the container here (F35).

### 5.11 Analysis data is **transformed**, not forwarded *(confirmed)*

A server cannot hand a player the bytes rekordbox wrote. Every blob is
converted: the file is big-endian, the wire little-endian, and three of five
change layout too (F30).

Every binary reply shares one envelope:

```
[request type, 0, byte length, blob, *trailing]
```

Argument 0 echoes the **request's message type**, not the track id.

| Request | Reply | Wire form |
|---|---|---|
| `0x2504` VBR index | `0x4502` | `PVBR` payload, every 32-bit word byte-swapped |
| `GET_BEAT_GRID` | `0x4602` | 20-byte LE prefix, then 16-byte entries: the file's 8-byte `(beat, tempo, time)` byte-swapped, padded with eight `0xff` |
| `GET_WAVEFORM_PREVIEW` | `0x4402` | each packed `PWAV` byte split into `(height = b & 0x1f, whiteness = b >> 5)`, then the 100-byte `PWV2` appended — 900 bytes, not 800 |
| `GET_WAVEFORM_DETAIL` | `0x4a02` | 20-byte LE prefix, then `PWV3` payload verbatim |
| `GET_CUE_POINTS` | `0x4702` | two blobs: 36-byte records `[order, hot cue, 0, 0, frame]` and `(time, loop_time)` pairs, **sorted by time** |

**`0x2504` is the MP3 VBR seek index and gates playback.** Without a
time-to-byte-offset table a player cannot seek, so it never issues a single
READ — a load that resolves the path perfectly and then does nothing.

Cue positions travel as a **frame index at 150 fps**, truncated not rounded
(271 ms → 40).

`GET_WAVEFORM_PREVIEW` carries the track id at **argument 2**, not 1 like its
siblings.

**The one field we cannot derive** *(unknown)*: the fifth prefix word of the
beat grid and detail waveform. The two observed values are for the same track in
the same load, so it is per-reply, monotonic, ~40,000/second — a counter or
allocator address. **It must be non-zero**: with zero the main waveform does not
draw (F33). We emit a counter of the same shape.

---

## 6. What a device must do to be browsable

The complete list, learned by getting each one wrong in turn:

1. **Announce** on 50000 — keep-alive at least, claim chain to hold a number,
   and that number **must be in 1–4** (F45). Outside it, every later step still
   works and none of them is ever reached.
2. **Emit status** on 50002, unicast per peer at 200 ms, with the slot state set
   (F20/F21). Media presence is advertised here and nowhere else.
3. **Answer media queries** (`0x05`) with true track and playlist counts (F24).
4. **Serve NFS** — and this comes **before** dbserver, which is not the order
   we originally assumed (F46). A portmapper on **UDP/111 is mandatory**: with
   nothing there a deck retries `GETPORT` once a second indefinitely, never
   falls back to the well-known 48276/2049, and never reaches step 5, so it
   does not list us at all. Key filehandles on their first 12 bytes (F28).
5. **Answer the port query** on 12523.
6. **Serve dbserver** on the advertised port, and **never** answer an unknown
   request with `0x4003` (F25).
7. Optionally **answer `0x35`** for LOAD SETTINGS (F38).

The observed sequence, from one load (F46):

```
media query 0x05  ──►  portmap GETPORT ──►  MNT  ──►  12523  ──►  dbserver  ──►  READ
      (t=7.6s)              (t=44.09s)                 (t=44.11s)              (t=52s)
```

An error and an empty folder are indistinguishable on a CDJ's screen, so the set
of menu types you implement is a **user-visible surface**, not an internal
detail (F40).

### 6.1 Two media at once *(confirmed)*

A player browsing two media on one peer opens **one** dbserver connection and
distinguishes them purely by the descriptor's slot byte (F37). So serving two
media is one server with a library per slot — resolved **per message**, since
both travel over the same connection — not a server per slot.

---

## 7. rekordbox file formats

### 7.1 `export.pdb` *(confirmed)*

4096-byte pages, a reverse index of doubly-reversed row slots, a presence
bitmask, and "strange" chain-head pages that contribute no rows.

**PioString**, the variable-length string, has three forms. The UTF-16 form
(`0x90`) is **little-endian and starts at `offset + 4`** — there is a padding
byte, exactly as in the long-ASCII form (O6).

> `research/05` says big-endian from `offset + 3`, and so did we. The two errors
> **cancel exactly for ASCII**, so encoder and decoder agreed perfectly, a
> 692-track library parsed cleanly, and only non-ASCII names came out as
> mojibake. Round-trip tests cannot catch this class of bug.

Two fields matter that the schema does not name:

- **Row offset `0x5a` is the container** (F34): `.mp3` 1, `.m4a` 4, `.flac` 5,
  `.wav` 11, `.aiff` 12 — 651 rows, no exceptions. dysentery calls it
  `unknown6`. Announce the wrong one and a deck fetches the file, tries to
  decode it as an MP3, and says it cannot.
- The header's `sequence` counter at `0x14` **changes as the deck writes its own
  bookkeeping** — a play count, a history entry. Two pulls of the same 1 MB
  database differed in exactly two header bytes. Any cache keyed on a whole-file
  hash will invalidate spuriously; zero `0x10`–`0x18` before hashing (F13).

### 7.2 ANLZ — `ANLZ####.DAT` / `.EXT` *(confirmed)*

A `PMAI` header then a flat sequence of tags, **big-endian** — unlike the
little-endian pdb that references them. `.DAT` carries `PPTH PVBR PQTZ PWAV
PWV2 PCOB`; `.EXT` adds `PWV3 PCO2 PQT2 PWV5 PWV4 PSSI`.

To *serve* these you must transform them (§5.11). To *consume* them you need the
interpretation instead.

### 7.3 `PIONEER/*SETTING*.DAT` *(confirmed)*

Uniform container, **little-endian**:

```
0x00  u32       header length, always 96
0x04  char[32]  brand / creator / version
0x64  u32       payload length
0x68  payload   0x12345678, one word, then the settings
      u16       checksum, two pad bytes
```

`MYSETTING.DAT` holds 32 settings bytes at `0x70` and is what the `0x36` reply
carries — with the two leading words byte-swapped to big-endian.

Four variants exist and one is understood. `MYSETTING2.DAT` lacks the payload
magic, so its first eight bytes are settings data rather than a header;
`DEVSETTING.DAT` (24 bytes) and `DJMMYSETTING.DAT` (44) carry the magic but have
never been requested over the wire. Nothing in the `0x35` request obviously
selects between them *(unknown)*.

The settings bytes themselves are not interpreted. They look like `0x80`-based
enumerations but nothing maps them to the options on the deck's screen — and
serving needs only to hand over what the medium holds.

---

## 8. Where we deliberately differ from the hardware

One place, and it is a considered choice rather than an oversight (F43).

A CDJ sorts key names as text, so a library in Camelot notation comes out
`1A 1B 10A 10B 11A 11B 12A 12B 2A 2B` — the wheel positions interleave and two
harmonically adjacent keys land eleven screens apart. **We sort by
`(position, letter)`.** The sort happens entirely on the server and the deck
renders whatever order it is handed, so there is no interoperability cost.

Everywhere else the goal is to be indistinguishable from a real deck. Here being
indistinguishable would mean being wrong.

---

## 9. Known unknowns

| | |
|---|---|
| The fifth prefix word (§5.11) | Must be non-zero; meaning unknown |
| `GET_TRACK_INFO` item 6 | Constant `1` on all four containers |
| Metadata argument 0 | `1` on eight items, `0` on five; no rule found |
| `0x3d03` | We acknowledge it; no capture shows a real reply |
| `0x3b03`, `0x3903`, `0x3001`, `0x3401` | Appear around a loaded track; undecoded |
| The green key indicator | Does not light for compatible keys; no capture shows a deck displaying it over LINK |
| `FOLDER` | Browses unanalysed files by directory, track type 2; not served |
| Auto number selection | Not "lowest free"; possibly remembered |
| `0x1400` argument 2 | Names a menu but the reply is the same regardless |

---

## 10. Method notes worth carrying forward

Five bugs came from **deriving** a value that looked derivable, and each
derivation was a proxy that happened to be unique in the evidence then
available: root ids from the request type (F26), then from the item type (F40);
menus keyed on item count (F27/F41); two track-info fields guessed in opposite
directions so they cancelled for the only format ever captured (F31/F34/F35);
analysis bytes assumed to pass through unchanged (F29/F30).

What worked instead was **constructing evidence so each variable moves alone** —
a stick holding one track in all 40 supported formats settled two fields that
three sessions of inference got wrong, and one browse session driven through
every category and sort settled the entire menu surface.

And a negative result is only as good as the instrument's ability to have shown
a positive one. Two findings were contaminated by capturing on a bridge
interface, which floods broadcast but forwards learned unicast directly between
members — so the capture looked healthy while missing everything of interest
(F15, F17). **Verify a tap with unicast, not broadcast.**
