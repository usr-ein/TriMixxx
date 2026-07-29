# Findings

Corrections and confirmations produced by the PoC, with the evidence for each.
Every entry is reproducible by running the test suite against the reference
captures in `research/ref-repos/` (git-ignored; see `research/00-references.md`
for the clone commands).

Status vocabulary matches the research docs: **confirmed** = observed on the
wire, **inferred** = deduced, **open** = needs hardware.

---

## Index

Areas: **DISC** discovery/keep-alive (UDP 50000) · **STAT** status + media query
(UDP 50002) · **DB** dbserver (TCP 1051) · **NFS** RPC/NFSv2 · **PDB** database
and analysis files · **METH** capture methodology.

| | Area | Finding |
|---|---|---|
| [F1](#f1) | DISC | `CDJ-2000nexus` is the exact name string — casing confirmed |
| [F2](#f2) | DISC | The whole UDP-50000 packet family round-trips byte-exactly |
| [F3](#f3) | DISC | Device numbers match the documented ranges |
| [F4](#f4) | DISC | Mixer-assignment types are real and the documented sizes |
| [F5](#f5) | NFS | CDJ-class hardware really does use NFS |
| [F6](#f6) | NFS | Ports: mountd **48276**, nfsd **2049** — three devices, same numbers |
| [F7](#f7) | DB | The dbserver wire format round-trips byte-exactly |
| [F8](#f8) | DISC | First capture from the target hardware is clean |
| [F9](#f9) | DISC | Keep-alive byte `0x25` = "was I first on this network?", latched at boot |
| [F10](#f10) | NFS | **A CDJ-2000NXS serves NFS.** E4 passed — the go/no-go gate |
| [F11](#f11) | NFS | **Passive NFS works**: no announcement needed. E1 confirmed |
| [F12](#f12) | NFS | Exports `/C/` to the whole link-local **subnet** — the mechanism behind F11 |
| [F13](#f13) | PDB | Anchor test passes. Cache key must use `stable_digest`, not raw sha1 |
| [F14](#f14) | PDB | The pdb parser works on a real 692-track library |
| [~~F15~~](#f15) | METH | **RETRACTED** — the evidence was a tap artefact |
| [F16](#f16) | DB | A real LINK browse decoded end to end; `0x0001` is fire-and-forget |
| [F17](#f17) | METH | `bridge1` is the wrong tap: it never sees deck-to-deck unicast |
| [F18](#f18) | NFS | **Audio travels over NFS**, streamed not downloaded |
| [F19](#f19) | NFS | Real CDJs use **8192-byte** reads and rely on IP fragmentation |
| [F20](#f20) | STAT | Media state **is** advertised, at offsets `0x6f`/`0x73` of 50002 status |
| [F21](#f21) | STAT | **Status is unicast to announced peers only** — decides the Mixxx design |
| [F22](#f22) | STAT | A nexus on fw 1.44 sends **284-byte** status; length ≠ generation |
| [F23](#f23) | STAT | We can emit a status packet **indistinguishable** from a real deck's |
| [F24](#f24) | STAT | Why a CDJ won't list us: the **media query** (`0x05`) goes unanswered |
| [F25](#f25) | DB | **A real CDJ browsed our Mac.** `0x3e03` must be answered, not errored |
| [F26](#f26) | DB | Root-menu items need a category id, `U+FFFA` label wrapper, and zero flags |
| [F27](#f27) | DB | **A CDJ browsed our categories.** Menus are concurrent; `MENU_CLOSE` is not a release |
| [F28](#f28) | NFS | **A CDJ does not treat the filehandle as opaque** — only 12 bytes survive |
| [F29](#f29) | NFS | The filehandle fix let the deck walk the whole path; no READ follows |
| [F30](#f30) | DB | The load sequence decoded: `0x2504` is the **VBR seek index**; analysis is transformed, not forwarded |
| [F31](#f31) | DB | `GET_TRACK_INFO` is **six** items, and argument 0 of the path item is the **file size** |
| [F32](#f32) | DB | **Playback works.** `GET_METADATA` is **13** items carrying *referenced* row ids |
| [F33](#f33) | DB | **Serve side complete.** The opaque prefix word must be non-zero |
| [F34](#f34) | PDB | Row offset `0x5a` is the **container**; the disc-number half was **wrong** |
| [F35](#f35) | DB | `GET_TRACK_INFO` item **1** is the container; item 6 is a constant `1` |
| [F36](#f36) | DISC | **Auto numbering**: byte `31` = `01`, and a type-`05` "number in use" reply |
| [F37](#f37) | NFS | SD is `/B/`; **one** dbserver connection multiplexes both slots by descriptor byte |
| [F38](#f38) | STAT | **LOAD SETTINGS** is a UDP `0x35`/`0x36` exchange reading `PIONEER/MYSETTING.DAT` |
| [F39](#f39) | — | **All four containers play and settings load.** Serve objective complete |
| [F40](#f40) | DB | Root ids derive from the **item type**; `0x14` is BITRATE, not KEY. Drill-downs added |
| [F41](#f41) | DB | Result sets key on `(descriptor, count)` — count alone collided at 13 items |
| [F42](#f42) | DB | Drill-downs are a **grid** `0x1000\|depth<<8\|category`; ALL entries; the SORT menu |
| [F43](#f43) | DB | Sorting picks the item's **second column**; all 12 root categories listed |

**Corrections to `research/`** — C1 stage-2 byte `30` is a role · C2 stage-3 is
38 bytes · C3 nexus keep-alive byte `35` is `00` · C4 byte `25` is not a role
(→F9) · C5 reference-repo licences · C6 USB export is not always `/C/` · C7
`EXPORT` paths are UTF-16LE but groups ASCII · C8 the `AUTH_UNIX` stamp is a
nonce · C9 players do call `UMNT` · C10 transaction ids start ~`0x03800001` ·
C11 three undocumented message types · C12 keep-alives are **2.0 s** · C13
stage-3 repeat count follows peer-presence-at-boot · C14 the status name field
is 20 bytes.

**Open** — O4 what is `0x3e03`? (`0x3100` answered, F30) · O5 the deck
issues no READ after a successful `LOOKUP` — *resolved, F31*. O7 is resolved (F33) O1–O3 and O6 are resolved (F18/F19, F10, F9, and
three path bugs respectively).

---

## Confirmations

<a id="f1"></a>

### F1 — `CDJ-2000nexus` is the exact device-name string  *(confirmed)*

`research/02` §4.1 and the README's open-question list both flagged the casing
as **inferred**: no published capture contained a literal CDJ-2000 name field,
only `XDJ-1000` and `VDJ-1000`. It does now.

```
43 44 4a 2d 32 30 30 30 6e 65 78 75 73 00 00 00 00 00 00 00
C  D  J  -  2  0  0  0  n  e  x  u  s
```

165 keep-alives across the dysentery captures. A DJM-2000nexus mixer likewise
reports `DJM-2000nexus`. The 20-byte field is NUL-padded as documented.

*Evidence:* `tests/test_captures.py::test_cdj_2000nexus_name_casing_is_confirmed`.
*Action:* `research/02` §4.1's "inferred for exact casing" caveat can be dropped,
and README open question 3 is closed for the name half.

<a id="f2"></a>

### F2 — The whole UDP-50000 packet family round-trips byte-exactly  *(confirmed)*

272 packets from a real CDJ-2000nexus, DJM-2000nexus and a virtual CDJ decode
and re-encode to identical bytes across all five packet types we model. Every
field is preserved, including the ones whose meaning is still unknown.

*Evidence:* `tests/test_captures.py::test_every_captured_packet_round_trips_byte_exactly`.

<a id="f3"></a>

### F3 — Device numbers match the documented ranges  *(confirmed)*

Players at 2 and 3; the DJM at `0x21` (33). Matches `research/02` §3.1.

<a id="f4"></a>

### F4 — The mixer-assignment types are real and have the documented sizes  *(confirmed)*

Types `01`/`03`/`05` appear at 47/39/38 bytes (`0x2f`/`0x27`/`0x26`), matching
the sizes `research/02` §1.7 marks as *inferred* from `startup.adoc`'s prose.
They are still not field-decoded here (they arrive as `UnknownPacket`), which
is fine for the passive path but will need doing for mixer-attached operation.

<a id="f5"></a>

### F5 — CDJ-class hardware really does use NFS  *(confirmed)*

`research/06` §1 marks "standalone CDJs export NFS" as confirmed, but its
evidence is libcdj's `rpcinfo` against an **XDJ**. `LinkInfo.pcapng` shows a
player at `169.254.192.112` running the full RPC sequence against *two* peers
during a LINK session: portmap `GETPORT` for mountd and nfsd, then `EXPORT`,
then `MNT`, then later `UMNT`.

This does not by itself settle experiment E4 for the **CDJ-2000NXS** — the
capture's device models are not pinned down — but it moves "do players speak
NFS to each other" from inference to observation.

*Evidence:* `test_real_players_use_the_nfs_stack`.

<a id="f6"></a>

### F6 — Observed RPC ports: mountd 48276, nfsd 2049  *(confirmed)*

Matching libcdj's `rpcinfo` numbers exactly. nfsd sits on the standard NFS
port; mountd does not, so portmap discovery remains mandatory.

*Evidence:* `test_observed_mountd_and_nfsd_ports`.


<a id="f7"></a>

### F7 — The dbserver wire format round-trips byte-exactly  *(confirmed)*

208 messages from the `LinkInfo` captures — both directions, including the
port-discovery handshake, `Introduce`, menu requests, `0x3000` renders, menu
headers/items/footers and artwork blobs — decode and re-encode to identical
bytes. Every stream is consumed end to end with nothing left over, which also
validates the omitted-empty-blob rule, since a single mis-step there would
desynchronise the remainder of the stream.

Details confirmed in passing:

- the 19-byte port query and its 2-byte reply are exactly as documented, and
  both captures answer **1051**;
- the 5-byte preamble (`11 00 00 00 01`) heads the byte stream in *both*
  directions before any message;
- `Introduce`'s reply carries the **server's own player number** in argument 2,
  the one `0x4000` whose second argument is not an item count;
- menu-item label lengths are `(characters + 1) * 2` bytes — `Above & Beyond`
  is 14 characters and announces `0x1e`.

*Evidence:* `tests/test_dbserver.py::test_every_captured_dbserver_message_round_trips`.


<a id="f8"></a>

### F8 — First capture from the target hardware is clean  *(confirmed)*

`S01-cold-boot-a`, a CDJ-2000NXS cold boot on the author's own rig: 42 DJ-Link
packets, **42/42 round-trip byte-exact**, and the handshake exactly as
documented — 3× hello, 3× stage-1, 3× stage-2, 3× stage-3, at 300.1 ms
intervals (doc says ~300 ms), then keep-alives. `CDJ-2000nexus` and the
`0x00` trailing byte (C3) are now confirmed on the **actual target hardware**,
not merely on dysentery's units of unknown provenance.

The unit tried DHCP three times before falling back to link-local, ~9 s before
its first DJ-Link packet — worth knowing when timing a capture: start it before
powering on, or the discovery phase is already over.

A real keep-alive from that capture is committed as a golden vector in
`tests/test_djl.py::NXS_KEEPALIVE`. Unlike the dysentery captures it is ours,
so it can live in the repository and is available wherever the tests run.


<a id="f9"></a>

### F9 — Keep-alive byte `25` means "was I first on this network?"  *(confirmed)*

A previously unexplained field, now pinned down. `research/02` §2 reads byte
`25` as "`01` CDJ / `02` mixer"; python-prodj-link guesses "sometimes other
player's id". Both are wrong.

**It is latched at boot and held for the session: `02` if the device was the
first on the network, `01` if peers were already present.**

Six device-boots whose starting conditions we control, no exceptions:

| Capture | Deck | Peer present at boot? | byte `25` | stage-3 packets |
|---|---|---|---|---|
| S01 | A (D=1) | no | `02` | 3 |
| S1b | B (D=2) | no | `02` | 3 |
| S02 | B (D=2) | **yes** | `01` | 1 |
| S2c | A (D=1) | **yes** | `01` | 1 |
| S2c | B (D=2) | no | `02` | — |
| S02 | A (D=1) | no | `02` | — |

The S2c capture was made as an explicit prediction: deck A had come up `02` in
every prior capture, and booting it into a network where deck B was already
running was predicted to flip it to `01`. It did, and deck A simultaneously
dropped from three stage-3 packets to one.

**The same latch drives both behaviours** (see C13). A device evidently records
"am I the first here?" once at boot, then expresses it in two places: how many
times it asserts its device number, and byte `25` of every subsequent
keep-alive. It never re-evaluates — deck A held `02` across its peer count
going 1→2 mid-session.

Three earlier readings are ruled out by this data: not a CDJ/mixer role byte (a
DJM sends both values), not the peer count (held constant across a change), and
not "the other player's number" (dysentery's D=33 sends `01` with only D=2
alive).

*Implemented:* `VirtualCdj` latches `first_on_network` at `start()` and never
recomputes it, selecting both the stage-3 repeat count and byte `25` from it.
Both variants are held as golden vectors and our announcer reproduces each
byte-for-byte.

*Evidence:* `captures/S01-cold-boot-a`, `S1b-cold-boot-b-alone`,
`S02-deck-b-joins`, `S2c-deck-a-joins`;
`test_keepalive_byte_25_follows_first_on_network`,
`test_our_announcer_reproduces_the_joining_handshake_byte_for_byte`.


<a id="f10"></a>

### F10 — **A CDJ-2000NXS serves NFS.** Experiment E4 passed  *(confirmed)*

The go/no-go gate for the entire chosen transport, and it passes. With a USB
stick inserted and the deck idle:

```
program   v  prot   port  name
 100003   2  udp    2049  nfs
 100005   1  udp   48276  mountd
 100000   2  udp     111  portmapper
```

Why this mattered: `research/06` §1 marks "standalone CDJs export NFS" as
*confirmed*, but its evidence is libcdj's `rpcinfo` against an **XDJ**, and the
players in dysentery's `LinkInfo` capture are not identified. The CDJ-2000NXS is
a 2012 unit and nothing established that it had an RPC stack at all. Had this
come back empty, the NFS path would have been dead for this hardware and the
project would have had to pivot to dbserver.

The ports match libcdj's XDJ observation and dysentery's capture **exactly** —
nfsd on the standard 2049, mountd on the distinctly non-standard 48276. Three
independent observations, three different devices, same numbers. Portmap
discovery is still required, but 48276 looks like a Pioneer constant rather than
a per-boot allocation.

<a id="f11"></a>

### F11 — **Passive NFS access works.** Experiment E1 confirmed, guard-enforced

Re-run under `--assert-passive`, which arms a guard that raises *before* the
`sendto` syscall on any DJ-Link port and then verifies the capture journal:

```
VERDICT: NFS transport AVAILABLE on 169.254.103.172
passivity verified: 0 datagrams sent on DJ-Link ports [50000, 50001, 50002, 50004]
(4 sent in total, all on ephemeral RPC ports)
```

So a CDJ serves files to a host that has **never announced itself** — no
keep-alive, no device number, no claim handshake, nothing on 50000/50001/50002
at all. This is the strongest form of the claim available: not "we did not
transmit" but "we could not have".

*Why it matters more than it looks.* It means the Mixxx feature can read a live
rig's libraries without participating in the DJ-Link network at all — no device
number to contend for, no risk of knocking a deck off mid-set, and no need for
the virtual-CDJ announcer on the consume path. dysentery's `startup.adoc:468`
asserts this; it is now demonstrated on the target hardware. It also means the
announcer is only needed for the *serve* side and for dbserver queries, which
substantially de-risks the whole consume objective.

*Evidence:* `captures/S04-media-insert`, deck A at 169.254.103.172.


<a id="f12"></a>

### F12 — Export names confirmed on an NXS, and the access list is a **subnet**

`prolinks exports` against deck A with a USB stick inserted:

```
'/C/'  raw=2f0043002f00  groups=['169.254.0.0/255.255.0.0']
```

Three things settled at once:

**E3 resolved.** USB is `/C/` on a CDJ-2000NXS, matching `research/06` §3 — which
was previously confirmed only against XDJ-class hardware. The raw bytes
`2f 00 43 00 2f 00` also confirm the **UTF-16LE** path encoding directly:
three characters, six bytes.

**C7 confirmed on target hardware.** The path is UTF-16LE but the group is plain
ASCII, in the same reply. The convention genuinely is not applied uniformly, and
decoding the group as UTF-16LE would have produced mojibake here too.

**The access list is a whole subnet, and that is why F11 works.** This deck
exports `/C/` to `169.254.0.0/255.255.0.0` — the entire link-local range, not a
list of known peers. So an unannounced host is inside the permitted set by
default, which is the mechanism behind passive NFS access.

> **Caveat, and it matters for the Mixxx feature.** dysentery's capture shows a
> device exporting `/C/EXPORT` to two **per-host** entries
> (`169.254.244.181/255.255.255.255` and `169.254.192.112/255.255.255.255`) —
> the two peers it had discovered. A device that scopes its export that way
> would presumably refuse an unannounced client, which would make F11
> firmware- or model-dependent rather than universal.
>
> Consequence: the consume path should treat `NFSERR_ACCES` on MNT as "try
> announcing first", not as a hard failure. It is also a plausible explanation
> for the `NFSERR_ACCES` libcdj hit (experiment E2) that has nothing to do with
> the credential flavour.

**Only the populated slot is listed.** No `/B/` appears, and deck A has no SD
card in it. If that holds up, `EXPORT` is a direct way to discover which slots
have media — cheaper and more reliable than probing each slot with `MNT`, and it
would settle the E4 decision-tree branch about gating on media state.
*Untested:* insert an SD card and re-run to confirm `/B/` appears.

*Evidence:* `captures/S04-media-insert`, deck A.


<a id="f13"></a>

### F13 — Anchor test passes: NFS transfer is byte-exact. Plus a real cache bug caught

Pulled the 1,077,248-byte `export.pdb` off deck A over NFS — 842 READs, **zero
short reads**, no retries, 1459 KiB/s — then ejected the stick and read the same
file directly on the Mac. The SHA-256s differ.

They differ in **exactly two bytes**, both in the file header, and nowhere else
in a megabyte:

| Offset | Field (`research/05` §2.1) | Over NFS | On the stick |
|---|---|---|---|
| `0x10` | `unknown1` | 4 | 5 |
| `0x14` | `sequence` (global write counter) | 20585 | 20586 |

Both are documented write bookkeeping, and the counter advanced by exactly one.
**The deck wrote to its own database between the two reads.** The library
content is bit-identical: 692 tracks, 329 artists, 275 albums, 35 playlists,
same records in the same order from both files.

So the transport is verified. The mismatch is the *deck* changing the file, not
us mis-reading it.

**The part that matters for Mixxx.** `research/10` specifies the media cache key
as `mediaKey = sha1(export.pdb)[0:16]`, content-addressing the whole file. That
is now known to be wrong: the hash changes whenever the player writes its own
bookkeeping — a play count, a history entry — which would invalidate the cache
and force a full re-download and re-parse of a library that has not changed by
one track. On a busy deck that could happen repeatedly in a set.

Fixed with :func:`prolinks_poc.proto.pdb.stable_digest`, which zeroes the
volatile window `0x10..0x18` before hashing. Verified against the two real
files: raw digests differ, stable digests match. The Mixxx cache key should use
the same rule.

*Evidence:* `/tmp/deckA.pdb` versus `/Volumes/SAM2/PIONEER/rekordbox/export.pdb`;
`test_stable_digest_ignores_the_players_write_counter`.

<a id="f14"></a>

### F14 — The pdb parser works on a real 692-track library, first contact

Until now it had only ever seen a synthetic database built by our own test
fixtures — a real risk of being self-consistently wrong. Against deck A's actual
library it parsed **692 tracks, 329 artists, 275 albums, 21 genres, 24 keys, 35
playlists and 2 folders**, and produced identical results from the NFS copy and
the physical copy.

It handled without complaint: an artist name containing an emoji and quoting
(`'❂RAINDAAMAGE'`), tracks with an empty artist field, and duplicate titles
under different track ids — i.e. the UTF-16BE PioString path, the
zero-string-offset path (see the `comment` bug fixed earlier), and the
multi-page table chain walking all work on real data.


<a id="f15"></a>

### ~~F15~~ — **RETRACTED.** The evidence was a tap artefact

F15 claimed that media insert/eject produces no DJ-Link traffic, on the strength
of an S04 capture showing 844 keep-alives and zero packets on 50001, 50002 and
TCP. From that I concluded that `ProLinkStatusListener` could never learn media
presence passively, and changed the Mixxx design to poll MOUNT `EXPORT` instead.

**That capture was taken on `bridge1`, which cannot see deck-to-deck unicast**
(F17). Status packets on 50002 are unicast. So the capture could not have shown
them whatever the decks were doing, and the "zero" measured our tap, not the
protocol.

The comparison is unambiguous — same two decks, same network, only the tap
differs:

| Capture | Tap | udp/50002 | udp/50001 | TCP |
|---|---|---|---|---|
| S04 | `bridge1` | **0** | 0 | 0 |
| S05 | `pktap,en12,en9` | **1440** | 0 | 1672 |
| S06 | `pktap,en12,en9` | **2020** | 186 | 636 |

So: **status traffic on 50002 exists in quantity between decks**, and whether
media presence is advertised there is **reopened, not answered**. The design
change to `research/10` is reverted pending a proper S4b capture on the members.

*Method note worth keeping.* This is the second finding contaminated by the same
tap bug, and both times the capture looked healthy — keep-alives present, decks
visible, packets round-tripping byte-exactly. A negative result is only as good
as the instrument's ability to have seen a positive one, and that has to be
demonstrated rather than assumed. Caught by the author asking whether F15's
evidence predated the pktap fix. It did.



<a id="f16"></a>

### F16 — A real LINK browse decoded end to end. `0x0001` identified; `0x3e03` absent

Deck A browsing deck B's USB over LINK, captured on the bridge **members**
(see F17). 410 KB of dbserver responses, **1957 messages, every byte consumed**
by our codec.

| Requests | | Responses | |
|---|---|---|---|
| `INTRODUCE` | 1 | `SUCCESS` | 74 |
| `MENU_ROOT` | 2 | `MENU_HEADER` | 215 |
| `RENDER_MENU` | 215 | `MENU_ITEM` | 1371 |
| `MENU_PLAYLIST` | 38 | `MENU_FOOTER` | 215 |
| `GET_METADATA` | 34 | `ARTWORK` | 82 |
| `GET_ARTWORK` | 82 | | |
| **`0x0001`** | **23** | — | — |

**`0x0001` is fire-and-forget.** The accounting is exact: 215 renders produce
215 headers and 215 footers, 82 artwork requests produce 82 artwork replies, and
introduce + root + playlist + metadata account for the SUCCESS replies. Nothing
is left over for `0x0001`, so it **draws no reply at all**.

Its wire form is a bare 32-byte message — zero arguments and an all-zero
argument-type blob:

```
11 87 23 49 ae   magic
11 03 80 01 b7   transaction id -- the SAME as the RENDER_MENU it follows
10 00 01         type 0x0001
0f 00            argument count 0
14 00 00 00 0c   12-byte argument-type blob, all zeroes
```

Reusing the preceding render's transaction id and expecting nothing back makes
"done with that menu, release its state" the natural reading, which fits a
protocol the docs describe as stateful per client. The semantics are inferred;
the wire behaviour is observed.

*Implemented:* the server now consumes it silently and clears its pending
result set. Previously it fell through to the unknown-request path and answered
`0x4003 ERROR` — precisely the kind of unsolicited reply that could
desynchronise a client which is not listening for one, and a plausible reason a
real player would have refused to browse us.

**`0x3e03` did not appear.** C11 flagged it as the first thing a player sends
after `Introduce`, from dysentery's capture, and treated it as the likely
blocker for the serve side. A CDJ-2000NXS browsing another CDJ-2000NXS never
sends it. So it is device-, firmware- or context-specific rather than universal
— worth handling if it ever shows up, but not the obstacle it looked like.

**The browse never touches NFS.** No traffic on UDP 111 or 2049 anywhere in the
capture: browsing another player's media is **dbserver only**. That is a direct
partial answer to O1 — and it sharpens the remaining question to whether *audio*
crosses the wire, which S6 measures.

*Evidence:* `captures/S05-link-browse`.

<a id="f17"></a>

### F17 — `bridge1` is the wrong tap: it never sees deck-to-deck unicast

The first S5 attempt, captured on `bridge1`, recorded 210 keep-alives and **no
TCP at all** during a browse that plainly worked on the deck's display.

A BSD bridge **floods** broadcast frames, so keep-alives, hellos and claim
packets all reach the bridge interface's BPF tap and a capture there looks
healthy. Learned **unicast** is forwarded member-to-member directly and never
reaches that tap. Everything of interest is unicast.

The verification was the trap: confirming both decks are visible only proves
*broadcast* arrives. Across every earlier capture taken on `bridge1`, every
unicast frame recorded had the Mac itself as an endpoint — delivered locally
rather than bridge-forwarded — which is why the NFS transfers recorded perfectly
and gave false confidence.

Capturing `pktap,en12,en9` (both members at once) fixed it: the same browse then
yielded 3400 packets including 1441 dbserver and 1440 status packets. **Verify a
tap with unicast, not broadcast**: a LINK browse must produce TCP on 12523 and
1051.


<a id="f18"></a>

### F18 — **Audio travels over NFS.** O1 answered  *(confirmed)*

The question that gated whether TriMiXxX can ever be *played from*, not merely
browsed. Deck A loading and playing a track off deck B's USB:

```
LOOKUP  Contents / Tomcraft / Loneliness / Tomcraft - Loneliness - Klub Cut.mp3
        7,633,531 bytes
READ    378 requests, 2,875,850 bytes delivered (38% of the file)
        highest byte touched: 7,633,531  -- i.e. the very end
```

So a CDJ reads another player's **audio file itself** over NFS. dbserver serves
metadata, waveforms and cues; the samples come over NFSv2 READ. Nothing in the
published sources states this — `research/06` §1 marks audio-over-NFS as
*inferred* and notes "no reference client streams audio this way".

**It streams rather than downloads.** 38% of the file was read during load plus
~30 s of playback plus cue juggling, and one read touched the final byte (the
usual MP3 tail-metadata probe). A deck does not pull the whole track up front;
it reads progressively and seeks on demand as you jump between hot cues.

*Consequences, both directions:*

- **Serving (objective 2) is viable.** A real CDJ will play from us if we serve
  NFS. But our server must answer **random-access reads with low latency during
  playback** — a slow or stalling response is an audio dropout on someone's
  deck, not merely a slow transfer. That is a much stronger requirement than
  the bulk `export.pdb` fetch, and worth load-testing before trusting it live.
- **Consuming is unaffected.** `research/10` has Mixxx fetch the whole file to
  cache before handing it to `getOrAddTrack()`, which is strictly more
  conservative than what a CDJ itself does. No change needed.

*Evidence:* `captures/S06-load-and-play`.

<a id="f19"></a>

### F19 — Real CDJs use **8192-byte** NFS reads, and rely on IP fragmentation

Request sizes in that transfer: **8192 × 333**, 2048 × 30, and a handful of odd
sizes at the tail. 8192 is the NFSv2 maximum, and at that size a reply is five
or six IP fragments on a 1500-byte MTU — the hardware simply relies on kernel
reassembly.

Our client defaults to **1280**, chosen from `research/06` §4 to stay under the
MTU and avoid fragmentation entirely. That is safe and measured 1459 KiB/s on
the `export.pdb` pull, but it is 6.4× more round trips than the hardware uses.
Experiment E7's throughput matrix is now worth running with 8192 in it, since
we know a real deck sustains playback that way.

**This also exposed a bug in our own tooling.** The pcap reader ignored IP
fragments, so it saw only the first fragment of every 8192-byte reply and
reported 1,578 bytes transferred where the truth was 2,875,850 — a 1800×
under-count that would have made the audio look like it never crossed the wire.
Fragment reassembly is now implemented and tested; without it the headline
finding above would have been read exactly backwards.


<a id="f20"></a>

### F20 — Media state **is** advertised, in 50002 status packets

The question F15 got wrong, now answered on the correct tap. 1507 packets on
50002 during S4b, of which 1503 are type-`0x0a` CDJ status and all 1503 decode.
Offsets `0x6f` (USB) and `0x73` (SD) track the slots exactly, and the timeline
matches the physical actions to the second:

```
t= 0.00  D=1  usb=empty                 D=2 usb=loaded
t=10.43  D=1  usb=loaded                <- stick inserted into deck A
t=50.06  D=2  usb=unmounting            <- eject pressed on deck B
t=51.63  D=2  usb=empty
t=83.25  D=2  usb=loaded                <- re-inserted
```

Media presence is therefore discoverable in real time, at the ~200 ms status
cadence, with no polling — *if* you can receive the packets. Which is F21.

Also present: two type-`0x05` media queries and two type-`0x06` responses, the
Link-Info exchange. The `0x06` payload carries the volume name as UTF-16BE
(`00 53 00 41 …` = "SA…", i.e. the stick labelled SAM2).

<a id="f21"></a>

### F21 — **Status is unicast to announced peers only.** This decides the Mixxx design

Every one of the 1507 status packets went deck-to-deck:

```
169.254.202.84  -> 169.254.103.172   756  unicast
169.254.103.172 -> 169.254.202.84    751  unicast
packets addressed to the Mac (169.254.99.100):  0
```

Not one was broadcast, and not one reached the Mac — which was on the network
with an address the whole time, but had **never announced itself**. The same is
true of the `0x05`/`0x06` media queries.

So F15's *conclusion* — that media presence cannot be learned passively — turns
out to hold, but for an entirely different reason than the one I gave. I claimed
the packets are not sent; in fact they are sent constantly, just never to us.
The distinction matters because it tells us the remedy.

**The design consequence, stated properly:**

| Mode | Media state | dbserver | Risk |
|---|---|---|---|
| **Passive** (no announcement) | poll MOUNT `EXPORT` | unavailable | none — cannot disturb a live rig |
| **Announced** (virtual CDJ) | pushed in real time, ~200 ms | available if number ≤ 4 | contends for a device number |

That is a better outcome than the polling-only design F15 implied: announcing
buys a real-time media feed rather than merely enabling dbserver. `research/10`'s
`ProLinkStatusListener` is therefore viable exactly as designed — but it is
strictly an *announced-mode* component, and the passive path needs the `EXPORT`
poll as its own mechanism rather than as a fallback.

*Evidence:* `captures/S4b-media-insert`;
`test_status_packets_are_unicast_to_peers_only`.

<a id="f22"></a>

### F22 — A CDJ-2000nexus on firmware 1.44 sends **284-byte** status packets

Every one of the 1503 was `0x11c` (284) bytes. `research/03` §1.1 maps `0xd4`
(212) to "Nexus" and `0x11c`/`0x124` to "Newer firmware / Nexus 2". So the
length does not identify the generation on its own — a plain CDJ-2000nexus on
current firmware sits in the "Nexus 2" row. A parser keying behaviour off the
packet length would mis-classify these decks.


<a id="f23"></a>

### F23 — We can emit a status packet indistinguishable from a real deck's

Built the emitter (`proto/djl_status.build_status`). Given the same identity and
state, our synthesised packet is **byte-identical** to a real CDJ-2000nexus one
— all 284 bytes.

The method matters. Across 749 consecutive packets from an idle deck, only
**six** bytes ever changed: the USB slot state, the link flag, two
still-unidentified bytes at `0x6a`/`0x74`, and the 16-bit packet counter. So
rather than construct a packet field-by-field from a specification full of
unknowns, the emitter starts from a captured skeleton and substitutes only the
fields we understand. The ~270 bytes whose meaning is still unknown are
reproduced exactly rather than guessed — which is the difference between
"plausible" and "indistinguishable".

Wired into `announce --status` and into `serve`, which now emits status
unconditionally: without it a player sees us as a deck with empty slots however
loudly we announce, because media presence is advertised here and nowhere else.
Sent **unicast per peer** at 200 ms, matching the hardware (F21).

*Observed but not imitated:* a real deck sends each status packet from a
**different, incrementing source port** (6688, 6689, 6690 …). Ours uses one
socket. Nothing suggests a receiver cares, and if something does, this is where
to look.

<a id="f24"></a>

### F24 — Why a CDJ would not list us: the **media query** goes unanswered

The serve-side test, and the answer is specific. Deck B had accepted us
completely:

- 433 status packets **unicast to us** — so we were in its device table (F21);
- a portmap `GETPORT` for mountd and for nfsd, both answered;
- `MNT('/C/')` against our NFS server, answered with a valid 32-byte filehandle.

It mounted our export. And it still would not offer us as a LINK source.

The capture shows why: **22 media queries (type `0x05`) from the deck, and zero
responses from us.**

```
Qspt1WmJOL 05 CDJ-2000nexus ... 02 000c  a9feca54  00000003  00000003
                                 ^D=2     ^its IP   ^target=3 ^slot=3 (USB)
```

It was asking "device 3, what is in your USB slot?" — repeatedly — and we never
answered, so it concluded the slot was empty. Announcing and emitting status
say *that we exist* and *that a slot is occupied*; the media query is how a
player learns *what the medium actually contains*, and it will not browse
something it believes is empty.

The reply (type `0x06`, 192 bytes) is the Link-Info panel's contents:

| Offset | Field |
|---|---|
| `0x24` | our device number |
| `0x28` | slot |
| `0x2c`–`0x6b` | media name, UTF-16 **big**-endian |
| `0x6c`… | creation date, e.g. `2025-06-24` |
| `0xa4` | **track count** |
| `0xac` | **playlist count** |
| `0xb4` / `0xbc` | total / free bytes |

Implemented and byte-identical to a real response given the same inputs. The
counts are taken from the parsed `export.pdb`, so they are the true ones — a
deck told there are no tracks has no reason to offer the medium.

*This also completes the picture of what a device must do to be browsable:*
announce on 50000, emit status on 50002, **answer media queries on 50002**,
answer the port query on 12523, serve dbserver, and serve NFS. The media query
is the step no reference implementation performs, because none of them serve.

*Evidence:* `captures/S10-serve-to-cdj`.


<a id="f25"></a>

### F25 — **A real CDJ browsed our Mac.** And why every category was empty

With media queries answered (F24), deck B listed the Mac as a LINK source and
opened a dbserver session. The categories appeared — ARTIST, ALBUM, TRACK,
GENRE, KEY, PLAYLIST — and every one of them was **empty**.

The capture shows exactly why, and C11 had predicted it:

```
DECK -> US   INTRODUCE      [2]
US -> DECK   SUCCESS        [0, 3]
DECK -> US   0x3e03         [0x2010301]
US -> DECK   ERROR          [0x3e03, 0]        <- we reject it
DECK -> US   MENU_ROOT      -> SUCCESS [0x1000, 6]
DECK -> US   RENDER_MENU    -> 6 items, correctly labelled
DECK -> US   MENU_ROOT      (again)
DECK -> US   DISCONNECT
```

The deck received our six root-menu items and rendered their labels — that part
works — then re-fetched the root and gave up without ever requesting a
category's contents.

**This refines F16.** I concluded `0x3e03` was device- or context-specific
because it never appears in a CDJ-to-CDJ browse. That was right but the wrong
inference: a deck browsing a **foreign** device does send it, immediately after
`Introduce`. It is plausibly a capability or identity probe that a player skips
with peers it already trusts. So C11's original instinct — that this is the
first thing a player sends and the likely blocker for the serve side — was
correct after all, and F16's "not the obstacle it looked like" was too hasty.

*Implemented:* reply `0x4b02` with `[0x3e03, 0, <our device number>, ""]`,
modelled on a real reply between two players and byte-identical to it.

*Still unknown:* what either message means. We reproduce the observed exchange
without understanding it, which is honest but worth revisiting -- the empty
string argument in particular looks like somewhere a name belongs.


<a id="f26"></a>

### F26 — Root-menu items need three details a bare implementation gets wrong

With `0x3e03` answered, the deck still rendered our six categories and then
declined to open any of them. Diffing our root items against a real player's
showed three differences, all in fields that look inconsequential:

| Argument | Real player | Ours (wrong) |
|---|---|---|
| 2 — category id | `1`, `2`, `3`, `5`, `0xa` … | `0` |
| 4 — label | `"\ufffaPLAYLIST\ufffb"` | `"PLAYLIST"` |
| 8 — flags | `0` | `0x01000000` |

The label wrapper is the striking one: **U+FFFA** (interlinear annotation
anchor) and **U+FFFB** (terminator). Presumably a marker telling the player
"this is a known category, substitute your own localised string" — which would
explain why a bare label displays correctly but is not treated as an openable
category.

The category id turns out to be the **low byte of the corresponding menu
request type**: GENRE `0x1001` → 1, ARTIST `0x1002` → 2, ALBUM `0x1003` → 3,
PLAYLIST `0x1105` → 5, LABEL `0x100a` → 0xa. So the root menu is telling the
player which request to send to open each entry.

Argument 8 carries `0x01000000` on *track* items — the value `research/04` §4.4
gives as an example — but zero on menu-category items. Copying the track value
everywhere was my error.

Our root item is now byte-identical to a real player's given the same category.


<a id="f27"></a>

### F27 — **A CDJ browsed our library's categories.** Two bugs it exposed

With the root items matching a real player's structure (F26), deck B opened
GENRE, ARTIST, ALBUM, TRACK and PLAYLIST and listed their contents from our
Mac. 61 renders, 280 menu items, 29 metadata lookups. The serve side works.

Two defects surfaced immediately, both mine.

**1. Concurrent menus.** A deck does *not* browse one menu at a time:

```
RENDER offset=20 limit=6 total=692     <- track list
GET_METADATA                            <- highlighted track
RENDER offset=0  limit=8 total=8        <- its metadata
RENDER offset=21 limit=6 total=692      <- back to the list, no new MENU_TRACK
```

It dips into an 8-item metadata menu and resumes the 692-item list at the next
offset **without re-issuing the menu request**. Holding one result set meant
the metadata replaced the list and every later page returned nothing — which is
exactly how it presented: scroll far enough and the list goes blank and the
tracks already shown disappear.

Both menus use the same descriptor (`0x2020301`, `M=0x02`), so the menu target
does not separate them. The distinguishing field is the render's **`total`**
argument, which echoes the size of the menu being paged. Result sets are now
kept per size, with the most recent as fallback.

**2. `MENU_CLOSE` must not discard state.** F16 read `0x0001` as "done with
that menu, release its state" from where it sits in the stream, and I made the
server act on it. That was wrong: a deck sends it *while still scrolling the
list it is supposedly finished with*, so honouring it destroyed the result set
mid-browse. It is now acknowledged and otherwise ignored. The inference was
plausible and the observation that it draws no reply still stands — but the
semantics were a guess, and guessing cost a real bug.

*Also implemented:* `GET_ARTWORK`, reading the image off the served medium via
the pdb's artwork table, and track menu items now carry their artwork id —
without it a player never asks for the image at all, which is why INFO showed
no cover.


<a id="f28"></a>

### F28 — **A CDJ does not treat the NFS filehandle as opaque**

Browsing, pagination and artwork all worked; loading a track failed with "media
collapsed or unavailable". The capture shows the deck mounting `/C/`, looking
up `Contents` twice, and stopping — and our replies were `NFSERR_STALE`.

The handle it sent back is not the handle we gave it:

```
served:   8a5edab282632443219e051e 4ade2d1d5bbc671c781051bf1437897cbdfea0f1
returned: 8a5edab282632443219e051e 03012d0000001b58000000000303010000000162
          |____ first 12 kept ____| |______ replaced by the player _______|
```

RFC 1094 is unambiguous that the filehandle is **opaque** and must be echoed
back verbatim; both reference clients treat it that way, and so did we. A
CDJ-2000NXS keeps only the leading **12 bytes** and overwrites the remaining 20
with its own data.

It is not arbitrary. A real player's own handles are a 4-byte value repeated
three times followed by 20 zero bytes (`01c1cec8 01c1cec8 01c1cec8 00…`, from
`LinkInfo.pcapng`). So the leading 12 bytes are evidently the volume identity —
the only part it considers the server's — and the rest is its own file
reference, which it feels free to author.

*Fixed:* the handle table is keyed on the first 12 bytes. A truncated SHA-256
of the path is still ample there to stay collision-free and deterministic.

*Consequence for the Mixxx serve side, and for anyone else attempting this:* a
server that trusts the spec here works perfectly for browsing and then fails at
exactly the moment a DJ tries to load a track — the worst possible time to
discover it. No reference implementation could have caught this, because none
of them serve.


<a id="f29"></a>

### F29 — The filehandle fix let the deck walk the whole path; analysis data is the next gate

After F28, the deck resolves a full track path over our NFS server:

```
MNT '/C/'                              -> OK
LOOKUP 'Contents'                      -> NFS_OK
LOOKUP '6 SENSE'                       -> NFS_OK
LOOKUP 'ASW Various Artists 3'         -> NFS_OK
LOOKUP '6 SENSE - Mechanical Mania.mp3' -> NFS_OK   size 6,942,380 (correct)
```

...and then issues **no READ at all**, still reporting "media collapsed or
unavailable". The attributes we return are right — the size matches the file on
disk exactly — so the path walk is not the problem.

What is left is what the server was rejecting: the `serve` log shows
`GET_WAVEFORM_PREVIEW` and `GET_CUE_POINTS` answered with `0x4003`. The working
hypothesis is that a player fetches analysis data as part of loading and
abandons the load when it is unavailable — consistent with browsing, artwork
and metadata all working while loading alone fails.

*Implemented:* an ANLZ container reader (`proto/anlz`) and dbserver handlers
for the waveform, beat-grid and cue requests, serving the tag bytes straight
out of the ``.DAT``/``.EXT`` files on the medium. Verified against real files
from the stick -- the ``.DAT`` carries `PPTH PVBR PQTZ PWAV PWV2 PCOB PCOB` and
the ``.EXT`` carries `PPTH PWV3 PCOB PCOB PCO2 PCO2 PWV5 PWV4 PSSI`.

Deliberately no interpretation: to *serve* analysis we need only hand a player
the bytes rekordbox wrote, and parsing a beat grid we would immediately
re-serialise would add a step and a chance to get it wrong. Consuming needs the
interpretation; serving does not.

**Untested against hardware.** The response argument layouts for these binary
replies are modelled on `research/04` §5 and the artwork response, not on a
capture -- no capture we have contains a player fetching analysis from a peer.

<a id="o6"></a>

### ~~O6~~ — resolved: **three** independent bugs made us serve paths that do not exist

One `LOOKUP` in the S10f capture failed legitimately: the deck asked for
`'❂RAINDAAMAGE'✯how do you like your tea_` where the disk has
`✧BRAINDAAMAGE✧`. Since the deck can only ask for the path *we* gave it, this
was ours. Chasing it turned up three separate defects, each of which alone is
enough to fail a track load.

**1. The UTF-16 PioString was decoded wrongly** (`proto/piostring`). The raw
bytes settle it:

```
0x2f6d4  90        selector, UTF-16
0x2f6d5  20 00     stored length 32  ->  28 bytes of text
0x2f6d7  00        padding byte      <-  we were not skipping this
0x2f6d8  27 27 42 00 52 00 ...       <-  UTF-16 LITTLE-endian, not big
         ✧     B     R
```

So the payload starts at `offset + 4`, not `offset + 3`, and the encoding is
**UTF-16LE**, not BE. Both framed forms therefore have a 4-byte header and a
stored length covering the whole string — the same shape as the long-ASCII
form, which we already had right.

*Why it survived a round-trip test suite and a 692-track parse.* The two errors
**cancel exactly for ASCII**: reading big-endian from one byte early is
byte-for-byte identical to reading little-endian from the correct offset
whenever every character has a zero high byte. Encoder and decoder agreed with
each other perfectly while both were wrong. F14 explicitly worried about the
parser being "self-consistently wrong" — this is what that looks like.
The tests now pin **literal bytes lifted from a real `export.pdb`** against the
names as they appear on the medium's own filesystem, which is an independent
source and cannot round-trip into agreement.

**2. The medium is FAT32, and `export.pdb` does not record the directory's
case.** The database says `Gesaffelstein`, `Hard Work Always Pays Off`,
`WASEI _JJ_ CHIKADA`; the directory entries are `GESAFFELSTEIN`,
`Hard work always pays off`, `Wasei _JJ_ Chikada`. A real player resolves these
through its FAT driver without noticing. A server comparing bytes answers
`NFSERR_NOENT`.

**3. The pdb stores NFC, the filesystem reports NFD.** `02. Akiba - カガミ.mp3`
is composed (`U+30AC`) in the database and decomposed (`U+30AB U+3099`) on
disk — rekordbox wrote the two through different APIs. Again identical to a
human, again not equal as bytes.

*Fixed:* the decoder reads UTF-16LE from `offset + 4`; `Vfs.lookup` tries an
exact match first and falls back to comparing `NFC(name).casefold()`, and
always returns the handle for the name **as stored** — hashing the requested
spelling would mint a handle absent from the table, so every later use of it
would come back `NFSERR_STALE`.

Measured against the author's stick, walking every path a player can ask for
through the VFS one `LOOKUP` per component, exactly as a CDJ does:

| | paths | failed before | failed after |
|---|---|---|---|
| audio | 692 | 33 | **0** |
| ANLZ | 692 | 0 | **0** |
| artwork | 653 | 0 | **0** |

(24 of the 33 from the decode bug, 9 from case.)

*Consequence beyond the serve side.* The decode bug is in `proto/piostring`,
which feeds the **consume** path too, so before this fix Mixxx would have
imported 24 of these 692 tracks with unopenable paths. Any implementation
reading `export.pdb` needs all three fixes; only the first is a protocol
matter, and the other two are the kind that a reference client never hits
because it reads the medium through a real FAT driver.
<a id="f30"></a>

### F30 — The load sequence, decoded. Analysis data is **transformed**, not forwarded

S10g showed the deck resolving the audio file over NFS twice, getting the right
size both times, and issuing **no READ** — then sitting on "LOADING FILE..." for
15 s. The dbserver stream says why. The tail of a load attempt:

```
GET_TRACK_INFO        -> path, correct
0x2504                -> we answered 0x4003 ERROR
GET_BEAT_GRID         -> 8216B, wrong structure
GET_WAVEFORM_PREVIEW  -> EMPTY
                         (deck gives up)
```

Three defects, all found by diffing against `S06-load-and-play`, where a real
CDJ-2000NXS loads track `0xc8` from another one — and, decisively, with the
**very USB stick from that capture** to hand, so both the input and the output
of a working implementation were available.

**1. `0x2504` is the MP3 VBR seek index, and it is the likely gate on
playback.** A real deck answers with 1604 bytes, exactly the size of a `PVBR`
payload — fixed-size, so the match holds across two different media. Without a
table mapping playing time to byte offset a player cannot seek within a VBR
MP3, so it has no way to *begin* streaming. That fits the symptom precisely: the
path resolves, the size is right, and not one READ follows. We were erroring on
it. Its reply type is `0x4502`.

**2. `0x3100` wants a bare `SUCCESS`.** C11 catalogued it without knowing what
it does; a real deck answers `SUCCESS [0x3100, 0]` mid-load. We errored.

**3. Every binary reply had the wrong envelope.** All of them share one shape:

```
[request type, 0, byte length, blob, *trailing]
 uint32        uint32 uint32    binary
```

Argument 0 echoes the **request's message type**, not the track id — ours sent
the track id. And `GET_WAVEFORM_PREVIEW` puts the track id at argument **2**,
not 1: its arguments are `[descriptor, 3, track_id, 0, b""]`. We read argument
1, asked for analysis of track 3, found none, and returned an empty blob.

### The part F29 got wrong

F29 said a server need only hand a player the bytes rekordbox wrote, and that
parsing them would add a step and a chance to get it wrong. That was
comfortable and false. **A real CDJ transforms every analysis blob**, and the
file is big-endian while the wire is little-endian:

| Request | Wire form |
|---|---|
| `0x2504` VBR index | `PVBR` payload, every 32-bit word byte-swapped |
| `GET_BEAT_GRID` | 20-byte LE prefix, then 16-byte entries — the file's 8-byte `(beat, tempo, time)` byte-swapped and padded with eight `0xff` |
| `GET_WAVEFORM_PREVIEW` | each packed `PWAV` byte split into `(height = b & 0x1f, whiteness = b >> 5)`, then the 100-byte `PWV2` appended — 900 bytes, not 800 |
| `GET_WAVEFORM_DETAIL` | 20-byte LE prefix, then the `PWV3` payload verbatim |
| `GET_CUE_POINTS` | two blobs: 36-byte records `[order, hot cue, 0, 0, frame]` and `(time, loop_time)` pairs, **sorted by time** — rekordbox had stored them newest-first |

Cue positions travel as a **frame index at 150 fps**, truncated not rounded
(271 ms → 40), which is the same 150 that appears in the detail waveform's
prefix.

*Verified:* our converters reproduce the captured blobs **byte for byte** —
1604, 16628, 900, 70771, 108 and 24 bytes — from that track's own ANLZ files.

*Not derivable:* both 20-byte prefixes carry a fifth word (`0x06114a48`,
`0x0612e0b4`) that is not in the file and differs between two replies seconds
apart. It looks like a pointer or timestamp on the serving deck. We send zero.
If a player turns out to care, that is the field to suspect — it is the only
byte in any of these replies we cannot account for.

*Method note.* F29 is the second finding in this project to be wrong in the
same way: an assumption that felt safe, stated as though observed, where the
capture that would have settled it existed already. The rule that keeps
working is to diff against a real implementation doing the same job.


<a id="f31"></a>

### F31 — `GET_TRACK_INFO` is **six** items, and one of them is the file size

With the analysis replies right (F30), the deck showed the preview waveform and
then stuck on "NOW LOADING...", finally reporting **"CDJ DOES NOT DECODE THIS
FORMAT"** — having issued **no READ of any kind**. S10h contains 2772 NFS calls
and every one is a `LOOKUP`; procedure 6 never appears. A verdict about the
audio format, reached without reading one byte of audio, has to come from the
metadata. It did.

`GET_TRACK_INFO` (`0x2102`) answers **6** in a real load; ours answered **1**,
the path alone:

| # | type | value | |
|---|---|---|---|
| 1 | `0x04` | `1` | unresolved |
| 2 | `0x0b` | 471 | duration, seconds |
| 3 | `0x0d` | 13201 | tempo ×100 |
| 4 | `0x23` | comment | |
| 5 | `0x00` | path | **argument 0 = 7,633,531 = the file size** |
| 6 | `0x2f` | `1` | unresolved |

**Argument 0 of the path item carries the file size.** It is zero on every
other menu item in every capture we have, which is exactly why it went
unnoticed — the field looked structural. It is also the one thing a load needs
that browsing does not, which fits a deck that renders the track perfectly,
resolves its path over NFS, and then never opens it: it had no idea how many
bytes to ask for.

*Verified:* all six of our items are now **byte-identical** to that real deck's,
for the same track off the same medium.

**Two fields remain unattributed.** Items 1 and 6 both carry `1`, and
`disc_number` is the only field of that track equal to 1 — not enough to
attribute either. They are sent as the observed constants. Type `0x2f` is the
better suspect for a codec identifier, since the format complaint has to come
from somewhere; a capture of a non-MP3, or of a track on disc 2, would settle
it.

*Method note.* Returning one item was not an oversight so much as an
unexamined assumption — `research/04` §5 says track info "is the Path", and it
is, and that was enough for everything except the one operation that matters.
The reply had been byte-comparable against a real one since the S06 capture was
taken; nobody compared it until the symptom forced it.


<a id="f32"></a>

### F32 — **Playback works.** And metadata was wrong in three ways that never showed

A real CDJ-2000NXS now loads and plays a track from the Mac. S10i records
**1141 NFS READs**, and the author reports loading two tracks and scrubbing
through both with no delay — so random-access reads during playback, the
requirement F18 flagged as much harder than a bulk transfer, hold up.

What F31 fixed was the file size. What remained broken was the **main
waveform**, and chasing it exposed three defects in `GET_METADATA`, none of
which is visible on a screen.

A real reply is **thirteen** items; ours was nine.

| | real | ours |
|---|---|---|
| count | 13 | 9 |
| artist item's id | `122` — the **artist's** row id | `200` — the track's |
| album / genre / key | the referenced row's id | the track's |
| title item's artwork id | `0xba` | `0` |
| missing entirely | colour, date added, **bitrate**, label | — |

The id error is the interesting one. A metadata item carries the id of the row
it *references*, which is how a player offers "more by this artist" from a
loaded track. Putting the track's own id there renders identically and means
something entirely different.

**A fourth thing, in every menu item we have ever sent.** Argument 10 tracks
argument 7: across all 1,700 menu items in the reference captures, an item with
`flags = 0x01000000` also carries `0x100` there, and an item with zero flags
carries zero. Both are non-zero only on items naming a track. We sent argument
10 as zero unconditionally, so every track row we served was subtly unlike a
real one. It is now derived from the flags, so the two cannot drift apart.

*Verified:* all thirteen metadata items and all six track-info items are now
**byte-identical** to a real deck's, for the same track off the same medium.

*Also:* `0x3d03` — two arguments, `(descriptor, track id)`, sent once during
playback and absent from every CDJ-to-CDJ capture — was the last request we
still answered with `0x4003`. It is now acknowledged like `0x3100`. **That reply
is a guess**: no capture shows a real one. The justification is F25, where
erroring on an unknown request stopped browsing dead.

**Still open (O7): the main waveform does not display.** After these fixes the
only remaining difference between our replies and a real deck's, across every
message type involved in a load, is the fifth prefix word of `BEAT_GRID` and
`WAVEFORM_DETAIL` (F30) — which we send as zero. Those are precisely the two
replies that feed the main waveform, and every reply that is byte-identical
corresponds to a feature that works:

| Reply | matches a real deck? | feature | works? |
|---|---|---|---|
| `VBR_INDEX` | byte-identical | playback | yes |
| `WAVEFORM_PREVIEW` | byte-identical | preview waveform | yes |
| `CUE_POINTS` | byte-identical | hot cues | yes |
| `BEAT_GRID` | **one word differs** | beat grid | ? |
| `WAVEFORM_DETAIL` | **one word differs** | main waveform | **no** |

Suggestive, not conclusive. The word increments at roughly 40,000 per second
between two replies 2.6 s apart, so it is a free-running counter on the serving
deck rather than anything derived from the track. If it is a generation or
cache token, zero may read as "no data".


<a id="f33"></a>

### F33 — ~~O7~~ resolved: the opaque word must be non-zero. **The serve side is complete**

Sending a deck-shaped counter in the fifth prefix word of `BEAT_GRID` and
`WAVEFORM_DETAIL`, instead of zero, makes the scrolling waveform draw cleanly.
Nothing else changed.

S10j is the first session with **zero errors**: every request a CDJ-2000NXS
makes is answered, and a load is 20 `LOOKUP`s and 201 `READ`s with one of each
analysis request.

| Feature | State |
|---|---|
| Appears on LINK, categories, pagination, artwork | works |
| Load, play, scrub, hot cues | works |
| Preview waveform | works |
| **Scrolling (main) waveform** | **works** |

**The prediction that failed, and why.** F32 reasoned that a value the client
cannot recompute is a value the client cannot check, so it must be ignored --
and rated the fix unlikely on that basis. That is wrong, and the error is worth
keeping: *a receiver does not have to validate a field to reject it.* Zero is a
perfectly serviceable sentinel for "absent", and here it is treated as one. The
hypothesis was only tested because it was the last difference left, not because
the reasoning favoured it.

**Still unexplained.** We know the word must be non-zero and must not go
backwards. We do not know what it means. The two captured values are per-reply,
monotonic and about 40,000 apart per second, which fits a free-running counter
or an allocator address on the serving deck; ours is a counter of the same
shape. A player evidently checks that it is *there*, not what it is.


<a id="f34"></a>

### F34 — Row offset `0x5a` is the container. **The disc-number half was wrong**

Only MP3 played: AAC, WAV and AIFF were each fetched and then rejected with
**"CDJ DOES NOT DECODE THIS FORMAT"**, with a hardcoded `1` in `GET_TRACK_INFO`
item 6.

Settled *for the pdb* by building the medium the question needed — one source
track rendered into all 40 formats a CDJ-2000NXS accepts, each file's title tag
carrying its own format so it is identifiable from the deck's display — then
diffing raw pdb rows across containers. Offset `0x5a`, which dysentery's schema
leaves as `unknown6`:

| Container | `0x5a` | rows |
|---|---|---|
| `.mp3` | **1** | 617 |
| `.m4a` (AAC) | **4** | 8 |
| `.flac` | **5** | 1 |
| `.wav` | **11** | 12 |
| `.aiff` | **12** | 4 |

No exceptions within a container across the whole 651-track library. That part
stands, and it is now parsed as `FileType`.

### What this got wrong

**Item 1, type `0x04`, is not the disc number.** Serving `disc_number` there
**broke MP3 loading** — the two MP3s tested are disc 2 and disc 1, and the
disc-2 one stopped working. So `0x04` does not carry it, and a value of 2 is not
acceptable there.

The reasoning had been: the one real observation was `1`, and `disc_number` was
the only field of that track equal to 1. That is a coincidence dressed as a
derivation, and it should not have been shipped to hardware as a conclusion —
especially in the same change as a genuinely evidenced finding, which is what
made the regression hard to attribute at a glance. **Both items are reverted to
the observed constant `1`.**

Item 6 is a *separate* question this does not settle either way. `0x5a` is
certainly the container, and for an MP3 it is `1` — identical to the constant —
so serving it cannot be what broke MP3. But nothing yet shows that this argument
is where a player expects it, so it is pinned to the constant until a capture
says otherwise.

*Lesson, and it is the same one as F29 and F32.* Constructing the evidence was
the right move and produced a real finding. The error was bundling an inference
with it and describing both as resolved. A guess and a measurement should not
travel in the same commit.

*Next:* a deck-to-deck capture of **deck B loading these format variants from
deck A's USB** — the only source of truth for what a real player sends in items
1 and 6 for a non-MP3.


<a id="f35"></a>

### F35 — `GET_TRACK_INFO` item **1** is the container, item 6 is a constant

Ground truth, from one deck loading the format variants off another's USB:

| Track | item 1 (`0x04`) | item 6 (`0x2f`) |
|---|---|---|
| `MP3 MPEG1 128k 44k1` | **1** | 1 |
| `AAC 128k 44k1 st` | **4** | 1 |
| `WAV 16b 44k1` | **11** | 1 |
| `AIFF 16b 44k1` | **12** | 1 |

Item 1 carries the container -- exactly the pdb `0x5a` values from F34 -- and
item 6 is `1` for every format. **Both earlier readings were wrong, in opposite
directions, and the two errors cancelled for the only format that had ever been
captured.** F31 guessed item 6 was the codec because the format complaint had to
come from somewhere; F34 guessed item 1 was the disc number because the one
observation was `1` and `disc_number` was the only field of that track equal to
1. Serving the disc number there is what broke MP3: a disc-2 MP3 announces
itself as **AAC**, so the deck fetches it and cannot decode it.

Note the same type byte means different things in the two replies: `0x04` is the
title in `GET_METADATA` (id = track id, label = the title) and the container in
`GET_TRACK_INFO` (id = container, label empty). Reading `research/04`'s item-type
table as global is what made "item 1 is the title" look already-answered and
sent the search to item 6 instead.

<a id="f36"></a>

### F36 — Automatic device numbering: byte `31`, and a type-`05` reply

First capture with a deck in **AUTO** rather than a fixed number. Deck B booted
alone in auto; deck A joined at a manual 1.

**Byte `31` of the stage-2 claim is confirmed on hardware.** `research/02` §1.3
reads it as `01` auto / `02` specific and C13 marked it confirmed -- but every
prior capture had *both* decks manually numbered, so only `02` had ever been
seen. The auto deck sends `01`. Our `AssignmentMode` already had both values and
now has evidence for each.

**An auto-numbered deck picked 2, not 1**, while alone on an empty network, and
1 was free. So auto assignment is not "lowest free number"; the deck's previous
manual setting was 2, which suggests it is remembered.

**Type `05` is not only a mixer packet.** `research/02` §1.7 files it under mixer
channel assignment. In the same instant deck A transmitted its stage-3 claim,
deck B **unicast** a type-`05` back to it carrying its own number:

```
0x04 (deck A)  5173707431576d4a4f4c 04 ... 01 01     claims 1
0x05 (deck B)  5173707431576d4a4f4c 05 ... 02 01     "I hold 2"
                                    ^^ only the type byte and the number differ
```

38 bytes, identical layout to `CLAIM_NUMBER`, unicast rather than broadcast, and
**absent from every earlier capture** -- all of which had two manually-numbered
decks. Reading it as "this number is taken" fits what an auto-assigning device
needs to publish. n=1, so the rule is inferred; the packet and its layout are
observed. Decoded as `NumberInUse` and round-trips byte-exactly.

Also reconfirmed, now with the variable moved deliberately: C13's stage-3 repeat
count. Deck B booted alone sent **three** type-`04` packets, deck A joining an
occupied network sent **one**.


<a id="f37"></a>

### F37 — SD is `/B/`, and **both slots share one dbserver connection**

One deck reading the other's SD, first alone and then alongside a USB. Four
things our code assumed are now observed, and one is the design constraint for
serving two media at once.

**SD mounts `/B/`.** F12 confirmed `/C/` for USB and explicitly left `/B/`
untested; a deck reading a peer's SD calls `MNT('/B/')`. Our slot table was
right.

**The descriptor's slot byte is the discriminator, and `MediaSlot` matches.**

| Slot | descriptor byte | requests seen |
|---|---|---|
| SD | **2** | 185 |
| USB | **3** | 102 |

**One connection carries both.** With an SD *and* a USB in the same peer, every
request for both media travelled over a **single** dbserver TCP connection —
client port 1054 in both sessions — distinguished only by that byte. So serving
two media is not two servers: it is one server holding a `Library` **per slot**,
selected from the descriptor. That is the concrete constraint on the TriMiXxX
two-slot design, and it is the opposite of what a per-slot-server implementation
would assume.

**The media query is per slot** and reports that slot's own medium:

```
0x05 slot=2 -> 0x06 slot=2  name='Sam CDJ1000mk3'  113 tracks, 11 playlists
0x05 slot=3 -> 0x06 slot=3  name='SAM2'            692 tracks, 35 playlists
```

One query per slot, issued when the deck first browses it — not repeated.

**`UMNT` is per slot and follows the eject.** Ejecting SD then USB produced
`UMNT('/B/')` then `UMNT('/C/')`, in that order, 12 s apart. Confirms C9 and ties
it to a physical action.

**A real deck never calls `EXPORT`.** Not once in either session: it goes
straight to `MNT` with the documented path. C6 made `EXPORT` enumeration our
preferred path because one capture showed `/C/` and `/C/EXPORT` on different
peers, and that is still the more robust client behaviour — but it is *not* what
the hardware does, so a server that only answers `MNT` would satisfy a real
player. Worth knowing before treating `EXPORT` as load-bearing.


<a id="f38"></a>

### F38 — LOAD SETTINGS over LINK is a **UDP exchange, not a file read**

A CDJ can adopt the utility settings a medium carries — LCD brightness, whether
the key display is alphanumeric or classic, jog tension, auto-cue level. Doing
that from a **peer's** medium turns out not to touch the medium's filesystem at
all.

Deck B, told to load settings from deck A's USB:

```
MNT '/C/'                          <- mounts the export
   ...and then reads nothing. Zero LOOKUPs, zero READs.
0x35  deck B -> deck A   40B, unicast   requester=2, slot=3
0x36  deck A -> deck B   80B, unicast   requester=2, slot=3, then 32 bytes
```

Two packet types nobody has documented, on UDP 50002. The mount is real and
then abandoned, which is the tell: a server that only implemented NFS would see
a mount, conclude nothing was wanted, and never learn a request had been made.

**The bytes come from `PIONEER/MYSETTING.DAT`.** With the medium back on the
Mac, that file's 32 bytes at offset `0x70` are exactly what crossed the wire.
The container is uniform across the four variants a real stick carries:

```
0x00  u32       header length, always 96
0x04  char[32]  brand      "PIONEER" / "PIONEER DJ" / "PioneerDJ"
0x24  char[32]  creator    "rekordbox"
0x44  char[32]  version    "0.001" / "7.1.0" / "1.000"
0x64  u32       payload length
0x68  payload   -- 0x12345678, one word, then the settings
      u16       checksum, then two pad bytes
```

**Little-endian**, unlike the big-endian ANLZ files sitting beside it, and the
`0x12345678` appears **big-endian** on the wire. So the type-`0x36` payload is
the file's payload with its two leading words byte-swapped and the settings
verbatim.

*Implemented and verified:* reading the file off the medium and building a reply
reproduces the real deck's 80 bytes **byte for byte**, and the announcer answers
a captured `0x35` with exactly that.

**Four settings files exist; only one is understood.**

| File | payload | magic | |
|---|---|---|---|
| `MYSETTING.DAT` | 32 | yes | the one observed on the wire |
| `MYSETTING2.DAT` | 40 | **no** | settings appear to start at `0x68` |
| `DEVSETTING.DAT` | 24 | yes | brand `PIONEER DJ`, version 7.1.0 |
| `DJMMYSETTING.DAT` | 44 | yes | mixer settings |

`MYSETTING2.DAT` is left uninterpreted rather than guessed at: without the magic,
its first eight bytes are settings data, and reading them as a header would be
inventing a format. Nothing in the capture says how a deck would ask for the
other three — the `0x35` request has no field that obviously selects one — so
that is open.

*Not interpreted:* the 32 settings bytes themselves. They look like `0x80`-based
enumerations (`80`/`81`/`88`/`01`/`00`) but nothing maps them to the named
options on the deck's screen, and a server only has to hand over what the medium
holds. Decoding them would be needed to *display* settings, not to serve them.


<a id="f39"></a>

### F39 — **Every supported container plays, and settings load.** Serve objective complete

With item 1 of `GET_TRACK_INFO` carrying the container from pdb `0x5a`, a real
CDJ-2000NXS loads and plays all four formats off the Mac. **Zero dbserver
errors** in the session — 1089 messages, none refused.

| File | READs | highest byte touched |
|---|---|---|
| `37 AIFF 16b 44k1.aiff` | 704 | 75,081,912 |
| `33 WAV 16b 44k1.wav` | 603 | 74,972,300 |
| `03 MP3 MPEG1 128k 44k1.mp3` | 95 | 6,911,124 |
| `29 AAC 128k 44k1 st.m4a` | 89 | 7,078,404 |

The two lossless files are ~75 MB and the deck read across their whole length —
so streaming a 24-bit-scale file over NFS from a general-purpose host holds up,
not just the small compressed ones. The MP3 is the disc-2 track that F34's
regression broke, which confirms that fix is genuinely gone rather than masked.

`LOAD SETTINGS` also worked in the same run: one `0x35`, one `0x36`, answered
from the medium's own `MYSETTING.DAT` (F38).

**Both objectives are now demonstrated end to end against real hardware.** What
remains in Phase A is the two-slot work (F37) and the unexplained fields listed
in `STATUS.md`, none of which block anything.


<a id="f40"></a>

### F40 — Root-menu ids are derived from the **item type**, and F26's rule was wrong for KEY

Two slots worked on the first try, and browsing exposed two bugs that had been
there since the root menu first rendered.

**1. `0x14` is BITRATE, not KEY.** F26 derived a root item's category id from
the low byte of the corresponding *menu request* type. That agrees for five
categories and is a coincidence: a real player's root items show a constant
offset between the id and the **item type**, not the request type.

| Category | id | item type | difference |
|---|---|---|---|
| GENRE | 1 | `0x80` | `0x7f` |
| ARTIST | 2 | `0x81` | `0x7f` |
| ALBUM | 3 | `0x82` | `0x7f` |
| PLAYLIST | 5 | `0x84` | `0x7f` |
| LABEL | `0xa` | `0x89` | `0x7f` |
| BITRATE | **`0x14`** | `0x93` | `0x7f` |

`MENU_KEY` is `0x1014`, so F26's rule gave KEY the id `0x14` — which a real
player uses for **BITRATE**. The deck did exactly as told: opening our "KEY"
category, it sent `MENU_BITRATE`, got `0x4003`, and showed nothing. KEY's id is
`item_type - 0x7f` = `0x0c`.

The id is now derived from the item type, so the two cannot disagree again.

**2. Drilling into a category was never implemented.** `MENU_ARTISTS_FOR_GENRE`,
`MENU_ALBUMS_FOR_ARTIST` and `MENU_TRACKS_FOR_ALBUM` all fell through to the
unknown-request path and returned `0x4003`. **A deck renders that as an empty
folder, not as an error**, which is why browsing looked correct until you went
two levels deep — ARTIST listed every artist, and picking one showed "EMPTY".

The filter id is argument 2 in all three, observed across 32 requests. The
request shapes are confirmed; **the reply shapes are inferred** by analogy with
the flat menus, because no capture we have shows a real player answering one.
`MENU_BITRATE` is implemented too, and that one *is* confirmed — a real server's
reply puts the value in the id and leaves both labels empty for the deck to
format.

*Worth noting for the Mixxx port:* an error and an empty folder are
indistinguishable to the user on a CDJ's screen. Any menu type we do not
implement will look like a category that exists and is empty, so the list of
implemented types is a user-visible surface rather than an internal detail.


<a id="f41"></a>

### F41 — Result sets must be keyed on the **descriptor**, not just the item count

With the drill-downs implemented, browsing an album corrupted the view: the
track list turned into metadata part-way through paging.

F27 keyed each pending result set on its **item count**, because that was what
distinguished the two menus in that capture — a 692-item track list and an
8-item metadata lookup. F32 then made a metadata reply exactly **13** items. So
a 13-track album collides:

```
MENU_TRACKS_FOR_ALBUM  desc=0x2010301  count=13   <- the list being scrolled
GET_METADATA           desc=0x2020301  count=13   <- overwrites it
RENDER off=1 total=13                             <- serves metadata
```

The **descriptor** supplies the missing bit, and it was there all along. Its
menu-target byte separates the list a deck is scrolling (`M=1`) from the
transient menu it dips into (`M=2`), and it appears in argument 0 of both the
menu request *and* the render that pages through it. Keying on
`(descriptor, count)` makes the two independent.

*Worth noting about how this arrived.* F27's key was correct when written and
became wrong when an unrelated change — making metadata 13 items to match a real
deck — happened to collide with a plausible album length. Nothing in the
protocol changed. That is the kind of coupling a comment cannot prevent, so the
key now uses the field that actually identifies a menu rather than a proxy that
happened to be unique.

*Still not ground truth.* No capture shows a real player answering a drill-down,
so their reply shapes remain inferred (F40), and the sort-order menus — by BPM,
by key, by rating — have never been exercised at all.


<a id="f42"></a>

### F42 — Drill-downs are a **grid**, not three messages. Plus ALL entries and the SORT menu

A deck-to-deck session driven through every category, every drill level, search
and all twelve sort orders. **The real server answered all 1137 requests with
zero errors**, and it settles most of what F40 left inferred.

**The request numbering is systematic:** `0x1000 | depth << 8 | category`, where
*category* is the **menu request** type's low byte. Every one of the thirteen
drill types in the session is generated by that formula:

```
0x1101 0x1102 0x1103 0x110a 0x1111 0x1112 0x1114   depth 1
0x1201 0x1202        0x120a               0x1214   depth 2
0x1301               0x130a                        depth 3
```

So `research/04`'s three named messages — ARTISTS_FOR_GENRE, ALBUMS_FOR_ARTIST,
TRACKS_FOR_ALBUM — are three points in a grid. Implementing it as a grid makes
LABEL, BITRATE, HISTORY and KEY work for free, which is why those four showed
EMPTY before.

Each level adds one filter id to the arguments, and the chains differ per
category: GENRE narrows to an artist, then an album, then tracks; ARTIST skips
straight to albums; ALBUM straight to tracks.

*Note the category byte here is the **request** numbering, not the root-item id
numbering of F40 — KEY is `0x14` here and `0x0c` there. Two schemes that
disagree, coexisting in one protocol, is exactly how F40's bug happened.*

**`ALL` entries.** A filtered list is headed by `id=0xffffffff`,
`type=0xa0`, label `ALL` wrapped in the same U+FFFA/U+FFFB as a category — but
**only when there is more than one entry**; a single-entry level goes out bare.
Both were observed in the same session. Choosing it sends `0xffffffff` as that
level's filter, meaning "do not narrow here".

**`0x1400` is the SORT menu.** Argument 2 names the menu being sorted, and the
reply is the same twelve options regardless:

| id | type | | id | type | |
|---|---|---|---|---|---|
| 0 | `0xa1` | DEFAULT | 6 | `0x80` | GENRE |
| 1 | `0xa2` | ALPHABET | `0xa` | `0x89` | LABEL |
| 2 | `0x81` | ARTIST | `0xc` | `0x8b` | KEY |
| 3 | `0x82` | ALBUM | `0xd` | `0x93` | BITRATE |
| 4 | `0x85` | BPM | `0x10` | `0x97` | DJ PLAY COUNT |
| 5 | `0x86` | RATING | `0x11` | `0x8c` | DATE ADDED |

**The chosen sort is argument 1** of `MENU_TRACK` and `MENU_PLAYLIST` — the same
ids. Our `SortOrder` enum already had every value right from `research/04`;
only two of them were ever *applied*. All twelve are now.

**One place we deliberately differ from the hardware.** A CDJ sorts keys as
text, so a library using Camelot notation comes out
`1A 1B 10A 10B 11A 11B 12A 12B 2A 2B` — the wheel positions interleave, and two
harmonically adjacent keys can sit eleven screens apart. We sort by
`(position, letter)` instead, giving `1A 1B 2A 2B … 12A 12B`, the order the
wheel is actually drawn in. Classical names keep alphabetical order and sort
after, since mixing two notations has no meaningful single answer.

This is a considered divergence, not an oversight: the sort happens entirely on
the server, the deck renders whatever order it is handed, and reproducing a
hardware bug faithfully would make the feature worse for the person using it.
Everywhere else in this project the goal is to be indistinguishable from a real
deck; here being indistinguishable would mean being wrong.

*Still open from this capture:* `0x3b03`, `0x3903`, `0x3001` and `0x3401`
appeared around a loaded track and are undecoded; `MENU_FOLDER` uses track type
2 in its descriptor, which is the unanalysed-files browse we do not serve; and
a real root menu offers **12** categories where we offer 6.


<a id="f43"></a>

### F43 — Sorting picks the **second column**, and all twelve root categories

Sorting "did nothing" because two thirds of it was missing.

**1. A playlist ignored the sort entirely.** `MENU_PLAYLIST` carries it in
argument 1 exactly as `MENU_TRACK` does, and we read arguments 2 and 3 and
dropped 1. Most browsing happens inside a playlist, so that was the sort being
ignored almost everywhere. `DEFAULT` must still keep the curated order — that
is what a playlist is for.

**2. The sort selects the item's second column** — the feature that makes it
useful rather than cosmetic. Sort by BPM and the BPM appears beside each title.
Read off a real server:

| sort | item type | label 2 | argument 0 |
|---|---|---|---|
| DEFAULT / ALPHABET / ARTIST | `0x0704` | artist | artist id |
| ALBUM | `0x0204` | album | album id |
| BPM | `0x0d04` | *(empty)* | `0x3390` = 132.00 |
| RATING | `0x0a04` | *(empty)* | rating |
| GENRE | `0x0604` | genre | genre id |
| LABEL | `0x0e04` | label | label id |
| KEY | `0x0f04` | `6A` | key id |
| BITRATE | `0x1004` | *(empty)* | `0x140` = 320 |
| DJ PLAY COUNT | `0x2a04` | *(empty)* | play count |
| DATE ADDED | `0x2e04` | `2025-11-13` | track id |

The item type is **`(column field type << 8) | 0x04`**. So `0x0704` is not
"title and artist" as `research/04` names it — it is *a track whose second
column is the ARTIST field*, and `0x0d04` is the same item with a BPM column.
Numeric columns send an **empty** label and put the raw number in **argument
0**, the same slot that carries the file size in a track-info path item (F31);
the deck formats it. All ten shapes now match a real server exactly.

**3. All twelve root categories, and the derivation was still wrong.**

| | | | | | |
|---|---|---|---|---|---|
| GENRE `01`/`80` | ARTIST `02`/`81` | ALBUM `03`/`82` | TRACK `04`/`83` | PLAYLIST `05`/`84` | LABEL `0a`/`89` |
| KEY `0c`/`8b` | FOLDER `11`/`90` | SEARCH `12`/`91` | BITRATE `14`/`93` | HISTORY `16`/`95` | **DATE ADDED `1b`/`8c`** |

F26 computed the id from the *request* type's low byte and gave KEY the id
BITRATE uses. F40 replaced that with `item type - 0x7f` — right for eleven of
the twelve, and wrong for **DATE ADDED**, where the difference is `0x71`. Two
derivations, two exceptions. All twelve have now been observed, so there is
nothing left to derive and they are simply listed.

We offer eleven of them. `FOLDER` is left out deliberately: it browses
unanalysed files by directory using a track-type-2 descriptor we do not serve,
and an unimplemented category is indistinguishable from an empty one on the
deck's screen (F40) — so advertising it would be worse than omitting it.



---

## Corrections to the research docs

<a id="c1"></a>

### C1 — Stage-2 claim byte `30` is a role byte, not a constant

`research/02` §1.3 lists offset `30` of the type-`02` packet as "const `01`".
It is not constant: a **DJM-2000nexus sends `02`** there while a
**CDJ-2000nexus sends `01`**. It is the same CDJ/mixer role byte that appears
at offset `34` of the keep-alive and at offset `25` of the stage-1 claim.

| Device | byte `30` | occurrences |
|---|---|---|
| CDJ-2000nexus | `01` | 1 |
| DJM-2000nexus | `02` | 3 |

*Impact:* low for the passive path, but an announcer that hardcoded `01` would
send a malformed claim if ever impersonating a mixer.
*Evidence:* `test_claim_ip_byte_30_is_a_role_not_a_constant`.
*Implemented as:* `djl.ClaimIp.role`, defaulting via `djl.default_role()`.

<a id="c2"></a>

### C2 — The stage-3 claim is 38 bytes, not 42

`research/02` §0.1's table gives type `04` a subtype of `0x26` but a packet
length of `0x2a`, the only type where the two disagree — which implied four
trailing bytes no source describes. **All six type-`04` packets in the captures
are `0x26` (38) bytes.** The subtype byte equals the total length for *every*
packet type; the length column is simply wrong for this row.

*Impact:* an announcer following the doc would have sent four spurious trailing
bytes during device-number claiming.
*Evidence:* `test_claim_number_is_38_bytes_not_42`.

<a id="c3"></a>

### C3 — Nexus keep-alives carry `00` in byte `35`, not `01`

`research/02` §2 describes byte `35` as "`00` classic, **`01`** typical CDJ
keep-alive, **`64`** for CDJ-3000 compat". Every nexus-generation keep-alive
captured carries `00`:

| Device | byte `35` | occurrences |
|---|---|---|
| CDJ-2000nexus | `00` | 148 |
| DJM-2000nexus | `00` | 91 |
| "Virtual CDJ" (a reference tool) | `00` | 5 |

*Impact:* **direct, on the impersonation goal.** Since the target is to look
like a CDJ-2000nexus, the default is now `00`. `64` remains required for
CDJ-3000 coexistence (`research/02` §1.6), which is untouched by this.
*Evidence:* `test_nexus_keepalive_trailing_byte_is_zero`.

<a id="c4"></a>

### C4 — Keep-alive byte `25` is not a fixed role byte  *(superseded by F9)*

Documented as "`01` CDJ / `02` mixer". Both devices were observed sending both
values, so the role reading is wrong. **F9 now explains what it actually is.**

<a id="c6"></a>

### C6 — The USB export is not always `/C/`

`research/06` §3 gives USB = `/C/`. In one session the same player mounts
**`/C/`** on one peer and **`/C/EXPORT`** on another. The drive-letter prefix
identifies the slot; the remainder varies by device or firmware.

*Impact:* a client hardcoding `/C/` fails against half the devices in this
capture. Fixed by enumerating with `EXPORT` and matching on the prefix —
`core.slots.match_export`, wired into `NfsClient.resolve_export`, with the
documented table as fallback for players that do not implement `EXPORT`.
*Evidence:* `test_export_paths_vary_between_devices`.

<a id="c7"></a>

### C7 — In an `EXPORT` reply, the path is UTF-16LE but the groups are ASCII

Pioneer's UTF-16LE convention is **not** applied uniformly within a single
structure. Decoding the group field as UTF-16LE turns
`169.254.244.181/255.255.255.255` into CJK mojibake.

The groups turn out to be `host/netmask` pairs naming the peers permitted to
mount — so a player exports its media specifically to the devices it has
discovered, not to the world. Worth remembering for the serve side: our own
`EXPORT` reply should probably name the CDJs we have seen.

*Impact:* this was flagged in the code as an explicit assumption before the
capture was run, and the capture falsified it. Exactly the intended use of
these tests.
*Evidence:* `test_export_listing_decodes_with_ascii_groups`.

<a id="c8"></a>

### C8 — The `AUTH_UNIX` stamp is a per-call nonce, not a magic constant

`research/06` §2 describes the stamp as "a magic constant the clients copy to
look like a real CDJ", citing prolink-connect's `0x967b8703`. Every call in the
capture carries a **different** stamp — `0x967b8703`, `0x9922e112`,
`0xa4921306`, `0xdc1ac513`, … — so it is a nonce and its value is arbitrary.
`0x967b8703` is simply the first one in this very capture.

The rest of the credential is exactly as documented: `machine_name=""`,
`uid=0`, `gid=0`, empty gids.

*Impact:* none functionally, but it removes a distraction from experiment E2:
the stamp *value* cannot be why libcdj's mount failed, which leaves the
credential *flavour* (`AUTH_NULL` vs `AUTH_UNIX`) as the leading hypothesis.
*Evidence:* `test_auth_unix_stamp_is_random_per_call`.

<a id="c9"></a>

### C9 — Real players do call `UMNT`

`research/06` §2 lists UMNT (proc 3) as "not used (TODO in nfsclient.py)".
The capture shows a player unmounting `/C/` when it is done. Our server should
therefore answer it, which it does.

*Evidence:* `test_real_players_call_umnt`.


<a id="c10"></a>

### C10 — Transaction ids do not start at 1

`research/04` §3.2 says the transaction id "starts at 1, incremented per
query". Real players start much higher: every conversation in the captures
begins around **`0x03800001`** and counts up from there. The value is opaque
and only has to be unique per connection, so nothing breaks either way — but a
client starting at 1 is one more way to look unlike a CDJ, so ours starts in
the same region.

<a id="c11"></a>

### C11 — Three undocumented message types appear in normal browsing

Not in `research/04`'s tables, and seen in an ordinary LINK session:

| Type | Direction | Shape |
|---|---|---|
| `0x3e03` | request | 1 argument: the `r:m:s:t` descriptor |
| `0x4b02` | response | 4 arguments: `[0x3e03, 0, 2, ""]` — echoes the request type |
| `0x3100` | request | 4 arguments: descriptor, 4, 0, 0 |

`0x3e03`/`0x4b02` are a request/response pair issued immediately after
`Introduce`, before any menu — plausibly a capability or media query. They
decode cleanly as ordinary messages, so nothing is blocked by not knowing what
they mean, but a *server* that a real CDJ talks to will be asked `0x3e03` and
should probably answer rather than erroring. **Worth capturing deliberately
tonight** — it is the first thing a player sends us.


<a id="c12"></a>

### C12 — Keep-alives are every **2.0 s**, not 1.5 s

`research/02` §0 gives 1.5 s and marks it **confirmed**, citing four sources.
All four turn out to be either the *send* interval a reference tool chose
(`vcdj.py packet_interval = 1.5`, prolink-connect `ANNOUNCE_INTERVAL = 1500`)
or loose prose ("roughly every second and a half"). **Nobody had measured
hardware.**

A CDJ-2000NXS sends every **2.0026 s** — n=28, min 2.002, max 2.003, i.e. a
tight hardware timer, not a jittery approximation of 1.5.

*Impact:* modest but real. Sending faster than hardware is safe, so nothing was
broken; but the goal is to be indistinguishable from a CDJ, so
`KEEPALIVE_INTERVAL_S` now matches the hardware. It also re-bases the timeout
arithmetic: the 10 s device timeout is **5** missed keep-alives, not the "6-7"
§2 infers from 1.5 s.

*Evidence:* `captures/S01-cold-boot-a`.


<a id="c13"></a>

### C13 — Stage-3 repeat count depends on **peer presence at boot**, not on the assignment mode  *(resolved)*

`research/02` §1.3 reads byte `31` of the stage-2 claim as `01` auto-assign /
`02` specific number — **confirmed**, both decks are manual and both send `02`.

§1.0 then adds: *"When set to a specific (manual) number, the device sends only
**one** stage-3 packet (N=01)."* That attributes the repeat count to the wrong
variable. Three controlled boots, all manual, same firmware:

| Capture | Deck | Number | Booted into | Stage-3 packets |
|---|---|---|---|---|
| S01 | A | 1 | empty network | **3** |
| S1b | B | 2 | empty network | **3** |
| S02 | B | 2 | deck A present | **1** |

Deck B sends three when alone and one when joining — same deck, same number,
same setting. So the rule is:

> **Booting into an empty network → three stage-3 packets. Joining a network
> that already has peers → one.** The assignment mode does not enter into it.

> An earlier version of this entry claimed the doc's packet count was simply
> wrong; that came from deck A alone and was too strong. S1b isolated the real
> variable by re-booting deck B with nothing else on the wire.

*Impact:* our announcer always sends three, which matches the boot-alone case
and is what the golden vector pins. To be faithful when joining an occupied
network it should send one — worth doing, since the whole point is to be
indistinguishable.

*Evidence:* `captures/S01-cold-boot-a`, `captures/S1b-cold-boot-b-alone`,
`captures/S02-deck-b-joins`.




<a id="c14"></a>

### C14 — The status packet's name field is 20 bytes, not 21

`research/03` §0 gives the device name as `0x0b`–`0x1f`, 21 bytes. Byte `0x1f`
is a **constant `0x01`** in all 1503 captured packets, so the name is 20 bytes
(`0x0b`–`0x1e`) and `0x1f` is a structural constant — exactly mirroring the
keep-alive on port 50000, where the name occupies `0x0c`–`0x1f` and the same
constant sits at `0x20`.

*Impact:* an emitter following the doc writes a 21st name byte over that
constant. Caught because our synthesised packet differed from a real one in
exactly that byte.

<a id="c5"></a>

### C5 — Reference-repo licences (already applied to `research/09` and `10`)

`research/09` described python-prodj-link as "GPL-ish". It is **Apache-2.0**,
which is GPLv2-*incompatible* and therefore unusable as a code source for a
Mixxx PR. Verified from the `LICENSE` files: prolink-connect and prolink-cpp
are MIT (usable), dysentery EPL-1.0 and vizlink EPL-2.0 (not), libcdj has no
licence file at all (not).

This does not restrict research: protocol facts are not copyrightable, so
every repo remains usable as a **reference**. Only their code is off limits.

---

## Still open

### ~~O1~~ — resolved: over NFS, streamed in 8192-byte reads. See F18/F19.

Original framing kept below for context.

<a id="o1"></a>

### O1 (original) — How does audio actually travel between players?

README open question 1 asked whether LINK browsing uses dbserver or NFS.
dysentery `menus.adoc:19` answers the *browse* half directly: requesting the
root menu "is what a player will do when you use the Link button". The
`LinkInfo` captures corroborate it — TCP 12523 port discovery followed by a
dbserver conversation on 1051.

The genuinely open question is what carries the **audio** when a player loads a
track from another player's USB. dbserver serves metadata, waveforms and cues,
not audio. Notably, `LinkInfo.pcapng` also contains **UDP 111 portmap traffic**
alongside the dbserver conversation, which is the first published hint that a
real player touches the RPC stack during a LINK session. Worth pulling apart
before tonight's hardware run.

### ~~O2~~ — resolved: **yes**, see F10.

### ~~O3~~ — resolved, see F9.

<a id="o4"></a>

### O4 — What are `0x3e03` and `0x3100`? *(narrowed by F16)*

Neither appears in a CDJ-2000NXS-to-CDJ-2000NXS browse; `0x0001` does instead,
and is now handled. So these two are device-, firmware- or context-specific
rather than a universal part of the browse handshake. Still undocumented, still
worth handling defensively if a mixer or a different player model turns up, but
no longer the blocker C11 took them for.
