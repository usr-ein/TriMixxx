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

### O1 — How does audio actually travel between players? *(reframed)*

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

### O4 — What are `0x3e03` and `0x3100`? *(see C11)*

Both are sent by a real player during an ordinary browse and neither is
documented. Our server currently answers them with `0x4003` (error). Since
`0x3e03` arrives before any menu request, a player may well refuse to browse us
until it is answered properly.
