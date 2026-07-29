# Findings

Corrections and confirmations produced by the PoC, with the evidence for each.
Every entry is reproducible by running the test suite against the reference
captures in `research/ref-repos/` (git-ignored; see `research/00-references.md`
for the clone commands).

Status vocabulary matches the research docs: **confirmed** = observed on the
wire, **inferred** = deduced, **open** = needs hardware.

---

## Confirmations

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

### F2 — The whole UDP-50000 packet family round-trips byte-exactly  *(confirmed)*

272 packets from a real CDJ-2000nexus, DJM-2000nexus and a virtual CDJ decode
and re-encode to identical bytes across all five packet types we model. Every
field is preserved, including the ones whose meaning is still unknown.

*Evidence:* `tests/test_captures.py::test_every_captured_packet_round_trips_byte_exactly`.

### F3 — Device numbers match the documented ranges  *(confirmed)*

Players at 2 and 3; the DJM at `0x21` (33). Matches `research/02` §3.1.

### F4 — The mixer-assignment types are real and have the documented sizes  *(confirmed)*

Types `01`/`03`/`05` appear at 47/39/38 bytes (`0x2f`/`0x27`/`0x26`), matching
the sizes `research/02` §1.7 marks as *inferred* from `startup.adoc`'s prose.
They are still not field-decoded here (they arrive as `UnknownPacket`), which
is fine for the passive path but will need doing for mixer-attached operation.

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

### F6 — Observed RPC ports: mountd 48276, nfsd 2049  *(confirmed)*

Matching libcdj's `rpcinfo` numbers exactly. nfsd sits on the standard NFS
port; mountd does not, so portmap discovery remains mandatory.

*Evidence:* `test_observed_mountd_and_nfsd_ports`.


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

### F22 — A CDJ-2000nexus on firmware 1.44 sends **284-byte** status packets

Every one of the 1503 was `0x11c` (284) bytes. `research/03` §1.1 maps `0xd4`
(212) to "Nexus" and `0x11c`/`0x124` to "Newer firmware / Nexus 2". So the
length does not identify the generation on its own — a plain CDJ-2000nexus on
current firmware sits in the "Nexus 2" row. A parser keying behaviour off the
packet length would mis-classify these decks.


---

## Corrections to the research docs

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

### C2 — The stage-3 claim is 38 bytes, not 42

`research/02` §0.1's table gives type `04` a subtype of `0x26` but a packet
length of `0x2a`, the only type where the two disagree — which implied four
trailing bytes no source describes. **All six type-`04` packets in the captures
are `0x26` (38) bytes.** The subtype byte equals the total length for *every*
packet type; the length column is simply wrong for this row.

*Impact:* an announcer following the doc would have sent four spurious trailing
bytes during device-number claiming.
*Evidence:* `test_claim_number_is_38_bytes_not_42`.

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

### C4 — Keep-alive byte `25` is not a fixed role byte  *(superseded by F9)*

Documented as "`01` CDJ / `02` mixer". Both devices were observed sending both
values, so the role reading is wrong. **F9 now explains what it actually is.**

### C6 — The USB export is not always `/C/`

`research/06` §3 gives USB = `/C/`. In one session the same player mounts
**`/C/`** on one peer and **`/C/EXPORT`** on another. The drive-letter prefix
identifies the slot; the remainder varies by device or firmware.

*Impact:* a client hardcoding `/C/` fails against half the devices in this
capture. Fixed by enumerating with `EXPORT` and matching on the prefix —
`core.slots.match_export`, wired into `NfsClient.resolve_export`, with the
documented table as fallback for players that do not implement `EXPORT`.
*Evidence:* `test_export_paths_vary_between_devices`.

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

### C9 — Real players do call `UMNT`

`research/06` §2 lists UMNT (proc 3) as "not used (TODO in nfsclient.py)".
The capture shows a player unmounting `/C/` when it is done. Our server should
therefore answer it, which it does.

*Evidence:* `test_real_players_call_umnt`.


### C10 — Transaction ids do not start at 1

`research/04` §3.2 says the transaction id "starts at 1, incremented per
query". Real players start much higher: every conversation in the captures
begins around **`0x03800001`** and counts up from there. The value is opaque
and only has to be unique per connection, so nothing breaks either way — but a
client starting at 1 is one more way to look unlike a CDJ, so ours starts in
the same region.

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

### O4 — What are `0x3e03` and `0x3100`? *(narrowed by F16)*

Neither appears in a CDJ-2000NXS-to-CDJ-2000NXS browse; `0x0001` does instead,
and is now handled. So these two are device-, firmware- or context-specific
rather than a universal part of the browse handshake. Still undocumented, still
worth handling defensively if a mixer or a different player model turns up, but
no longer the blocker C11 took them for.
