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

### C4 — Keep-alive byte `25` is not a fixed role byte

Documented as "`01` CDJ / `02` mixer". In fact **both** devices alternate:

| Device | byte `25` values |
|---|---|
| CDJ-2000nexus | `01` ×117, `02` ×31 |
| DJM-2000nexus | `02` ×59, `01` ×32 |

python-prodj-link's comment on this field — "sometimes other player's id" —
is closer to the truth than the role reading. Meaning still **open**. The
codec preserves it verbatim rather than assuming a value; correlating it
against the rest of the session is a good use of the hardware time tonight.

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


### C13 — A manually-numbered device still sends **three** stage-3 packets

`research/02` §1.3 reads byte `31` of the stage-2 claim as `01` auto-assign /
`02` specific number, and §1.0 adds: *"When set to a specific (manual) number,
the device sends only **one** stage-3 packet (N=01) then proceeds to
keep-alive."*

Deck A's `PLAYER No.` was set **manually to 1**, and the capture shows byte
`31` = `02` — so the *mode* reading is correct and confirmed. But it sent
**three** stage-3 packets, N=1,2,3, exactly like the auto-assign case. The
packet-count half of the claim is wrong.

*Impact:* none on our code, which already sends three of every stage — but it
would have been a natural "optimisation" to make, and doing so would have made
our claim sequence detectably unlike a real deck's.

**Consequence worth noting:** with the doc corrected, our announcer's claim
handshake is now **byte-identical to a real CDJ-2000NXS** across all twelve
packets — hellos, stage-1, stage-2 and stage-3 — given the same identity. Held
as a golden vector in `tests/test_djl.py::NXS_CLAIM_SEQUENCE`, so any future
drift in the announcer fails a test rather than a CDJ.

*Evidence:* `captures/S01-cold-boot-a`;
`test_our_announcer_reproduces_the_claim_handshake_byte_for_byte`.


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

### O2 — Does a CDJ-2000NXS serve NFS? *(the go/no-go gate, experiment E4)*

Unchanged, and still the single most important thing to establish. The
"confirmed" NFS evidence in `research/06` §1 rests on an **XDJ** capture. The
portmap traffic noted in O1 is suggestive but not yet decoded.

### O3 — Keep-alive byte `25` semantics *(see C4)*

New datum: with a **single** deck alone on the network (`peers=1`), byte `25`
was `0x02` for all 30 keep-alives — perfectly stable. In the dysentery captures,
where several devices were present, the same model alternated `01`/`02`. So the
byte is not random and not a role: it plausibly encodes something about the
*other* devices present. S2 and S3 should settle it, since they add a second
deck to an otherwise identical setup.

### ~~O5~~ — resolved, see C13.

### O4 — What are `0x3e03` and `0x3100`? *(see C11)*

Both are sent by a real player during an ordinary browse and neither is
documented. Our server currently answers them with `0x4003` (error). Since
`0x3e03` arrives before any menu request, a player may well refuse to browse us
until it is answered properly.
