# 10 — Implementation plan: ProLink in Mixxx

The approved build plan for Phases 2 and 3. Supersedes the sketch in
[08-python-poc-plan.md](08-python-poc-plan.md) and corrects the licensing claims in
[09-mixxx-integration-notes.md](09-mixxx-integration-notes.md).

Grounded in a full read of the Mixxx source at `../mixxx` (fork of 2.5.6). Line
references are to that tree.

## Working-directory conventions

- `prolinks-compat/` — research docs + the Python PoC (`prolinks_poc/`, `tests/`,
  `fixtures/`). `captures/` and `research/ref-repos/` stay git-ignored.
- `mixxx/` — the C++ fork. No PoC artifacts land there; only `src/`, `res/`,
  `CMakeLists.txt` changes.

## Context

TriMiXxX is a Raspberry Pi running Mixxx that should behave like a CDJ on a
Pro DJ Link network. Today Mixxx can only see Rekordbox libraries on **locally
mounted** USB drives — `src/library/rekordbox/rekordboxfeature.cpp` scans
`/Volumes`, `/media` and `QDir::drives()`. It has no concept of a library reachable
over the network: there is not a single `QUdpSocket` or `QTcpSocket` anywhere in
`src/` (only `QNetworkAccessManager`, for MusicBrainz).

The goal: other CDJs on the Ethernet network appear in Mixxx's left library
sidebar exactly like a mounted Rekordbox USB does, browsable by playlist and
loadable to a deck.

### Decisions taken

| | |
|---|---|
| Phasing | Python PoC first (validate against the two real CDJ-2000NXS), then port to C++/Qt |
| Scope | Consume only; structure so the serve side drops in later |
| Transport | NFSv2-over-UDP + `export.pdb`. No dbserver/remotedb TCP for now |
| Discovery | Passive (listen on UDP 50000); active virtual-CDJ announcer later, behind a setting |
| Audio | Fetch the whole file to a local cache, then hand the local path to `getOrAddTrack()` |
| Rekordbox glue | Duplicate into ProLink now, extract a shared importer later |
| Pre-existing bugs | Fix in `rekordboxfeature.cpp` too, as a standalone change |

Rationale for each is in the [decision log](#decision-log).

---

## Licensing — read before writing any protocol code

Verified by reading the `LICENSE` files in `ref-repos/` (2026-07-29):

| Repo | License | Usable in a Mixxx PR? |
|---|---|---|
| **prolink-connect** | **MIT** | **Yes** — copy/adapt freely, keep the notice |
| **prolink-cpp** | **MIT** | **Yes** |
| python-prodj-link | **Apache-2.0** | **No** — Apache-2.0 is incompatible with GPLv2 |
| dysentery | EPL-1.0 | **No** |
| vizlink | EPL-2.0 | **No** |
| libcdj | *no LICENSE file* → all rights reserved | **No** — including the `.x` files |

Mixxx is **GPLv2-or-later** (`../mixxx/COPYING`). This corrects doc 09, which calls
python-prodj-link "GPL-ish"; it is Apache-2.0, which is *worse* for our purpose —
the one common OSI license explicitly incompatible with GPLv2. Mixxx's "or later"
means an Apache/EPL contribution would not make the tree undistributable, but it
would strip the GPLv2 option from the combined work, forcing Mixxx to GPLv3+.
Maintainers will not accept that in a feature PR.

Note the irony: python-prodj-link is by far the most complete reference for
objective 1, and it is the one we may not copy from.

**Operative rule: inspiration, not transcription.** Protocol *facts* — offsets,
magic numbers, procedure numbers, `/B/` and `/C/`, the UTF-16LE deviation,
1280-byte chunks, "4 READs in flight" — are **not copyrightable**. Only the *code
expression* is. So the restricted repos remain fully usable as **references and
inspiration**; what we must not do is copy their code.

This is not a limitation in practice, because the `research/` docs already did the
extraction: they are the intended intermediary between those repos and our code.

### Working discipline

- **Write from `research/*.md`, not from a source file open in the next window.**
  If a fact needed for the implementation is not yet in the research docs, add it
  there first (with its citation), then implement from the doc. This keeps the
  provenance trail honest and is the practical mechanic that makes "inspiration"
  hold up.
- **Structure and algorithms may be modelled on prolink-connect** (MIT) — copy it
  outright if useful, keeping the notice. It also happens to have the cleanest NFS
  layering of the seven.
- **Restricted repos (Apache/EPL/unlicensed) are read-only documentation.** Consult
  them freely to understand *what* the protocol does and to resolve ambiguity; do
  not paste, translate line-by-line, or port their file structure verbatim.
- **Deliberate divergences already planned** — a `selectors` reactor instead of
  python-prodj-link's asyncio+threads, and explicit `struct` codecs instead of its
  `construct` declarations. These are required for the C++ port anyway, and they
  mean the resulting code does not resemble the Apache-2.0 original even where it
  solves the same problem.

Two concrete deliverables follow:

1. **License the PoC GPLv2+ from commit one**, so nothing in it can be "cleaner"
   than what may go upstream and the boundary is enforced by construction.
2. Keep **`PROVENANCE.md`** — one row per non-obvious constant: value, meaning, and
   the *document* (research doc section, RFC section, or our own capture) it came
   from. "Where did this magic number come from" is the first question a maintainer
   asks about reverse-engineered code, and this is the answer.

---

# Phase A — Python PoC

Purpose: prove the protocol against hardware, and produce artifacts that make the
C++ port mechanical.

## Portability rules

The PoC exists to be ported. That imposes hard rules on how it is written:

| Rule | Why | Forbids |
|---|---|---|
| Explicit `struct.pack`/`unpack` at named constant offsets | 1:1 with `QDataStream`/`memcpy` | `construct`, `kaitai`, `scapy` in core, `__getattr__` tricks |
| One single-threaded `selectors` reactor, ticked by explicit `poll(now_monotonic)` | maps to `QUdpSocket::readyRead` + `QTimer` | `asyncio`, threads (python-prodj-link uses both — deliberately not copied) |
| Every protocol module encodes **and** decodes **both** directions from day one | the serve side becomes plumbing, not a rewrite; also gives a free dissector | client-only codecs |
| State machines as explicit `enum State` + `step(state, event) -> (state, actions)` | directly transcribable | implicit state in coroutine position |
| Core depends only on `socket`, `struct`, `selectors`, `enum`, `hashlib`, `time` | Mixxx cannot take Python deps; anything exotic signals a porting hazard | third-party libs below `cli.py` |
| Zero DJ-Link transmission unless explicitly asked | experiment E1 requires provable passivity | ambient keep-alive threads |

## Layout

```
prolinks_poc/
  cli.py                    # the ONLY place third-party deps are allowed

  proto/                    # pure bytes <-> structs. No sockets, no timers, no state.
    bytes.py                #   ByteReader/ByteWriter — THE portability seam
    errors.py
    djl.py                  #   UDP 50000: 0x0a/0x00/0x02/0x04/0x06/0x08 + mixer 0x01/0x03/0x05
    djl_status.py           #   UDP 50002: CDJ status 0x0a, mixer 0x29, media 0x05/0x06
    djl_beat.py             #   UDP 50001: beat 0x28, on-air 0x03 (decode-only)
    xdr.py                  #   XDR + the Pioneer UTF-16LE length-prefixed string
    rpc.py                  #   ONC RPC v2 (RFC 1057), AUTH_UNIX / AUTH_NULL
    portmap.py              #   prog 100000 v2: NULL/GETPORT/DUMP
    mountd.py               #   prog 100005 v1: MNT/UMNT/EXPORT
    nfs2.py                 #   prog 100003 v2: NULL/GETATTR/LOOKUP/READ/READDIR/STATFS
    piostring.py  pdb.py  pdb_rows.py  anlz.py

  net/                      # I/O. Thin. Everything here is Qt-replaceable.
    iface.py  loop.py  udp.py  rpcclient.py  nfsclient.py
    vfs.py  nfsserver.py    #   PHASE C SEAM — exercised offline at M8

  core/
    devices.py  discovery.py  announcer.py  statusmon.py
    slots.py  library.py  mediastore.py  cache.py

  capture/
    recorder.py             #   JSONL journal of every datagram in/out
    replay.py               #   offline regression + golden generation
    passivity.py            #   asserts zero bytes transmitted on DJ-Link ports

  spec/gen_spec.py          # emits spec/observed/*.md from constants + journals

tests/      fixtures/      captures/ (git-ignored)
```

Deltas from doc 08's sketch: `packets.py` split by port; `dbclient.py`/`dbserver.py`
not created (out of scope); `vfs.py` + `nfsserver.py` added as the serve-side seam;
`net/loop.py` replaces asyncio+threads — the single biggest structural divergence
from python-prodj-link, and intentional.

## Milestones

Ordered to front-load the two questions that could invalidate the decided scope
(M2, M4).

| M | Deliverable | Hardware verification |
|---|---|---|
| **M0** | `loop.py`, `udp.py`, `iface.py`, `recorder.py`, `cli sniff` — hex dump, no decoding | ~0x36-byte datagram from two IPs every ~1.5 s starting `51 73 70 74 31 57 6d 4a 4f 4c 06 00`; journal agrees with `tcpdump` packet-for-packet |
| **M1** | `proto/djl.py`, `core/devices.py`, `discovery.py`, `cli devices` | Two devices whose numbers/IPs/MACs match the CDJ screens. **Capture the literal 20-byte name field** — resolves the `CDJ-2000nexus` casing, currently *inferred* (doc 07 §2a) |
| **M2** | `xdr.py`, `rpc.py`, `portmap.py`, `rpcclient.py`, `cli rpcinfo` | **GO/NO-GO GATE.** Non-zero UDP ports for mountd (100005 v1) and nfsd (100003 v2), probed across {unit A, B} × {USB, SD, both, none} × {idle, loaded, playing} |
| **M3** | `mountd.py`, `cli exports` / `mount` | `EXPORT` enumerable; `MNT` returns status 0 and a 32-byte fhandle |
| **M4** | `nfs2.py`, `nfsclient.py` (windowed READ), `slots.py`, `cache.py`, `cli fetch`/`pull-db` | **The anchor test:** eject the stick, `shasum -a 256` the pdb on the Mac, re-insert, fetch over NFS, compare. Byte-identical or bust. Run under `--assert-passive` |
| **M5** | `piostring.py`, `pdb.py`, `pdb_rows.py`, `library.py`, `cli tracks`/`playlists` | Track count, titles and playlist tree match the CDJ browse screen and Rekordbox |
| **M6** | `anlz.py` + lazy ANLZ/artwork fetch | SHA-256 vs the physical stick; beatgrid and hot cues match the CDJ display |
| **M7** | `cli get-audio` — whole file to cache | SHA-256 vs stick; the file plays. Measure throughput across chunk {1024…8192} × window {1…8} |
| **M8** | `vfs.py` + `nfsserver.py` on loopback, over the *same* codecs | Our client round-trips a byte-identical pdb from our server. Then point **prolink-connect** at it — independent third-party validation of every reply encoder, at zero hardware cost |
| **M9** | `announcer.py` — keep-alive at D=7, then the full claim chain at D=3 | Both CDJs list us. Diff our keep-alive byte-for-byte against a real NXS keep-alive from M1; every differing byte must be justified. `--dry-run` mandatory |
| **M10** | `djl_status.py`, `statusmon.py` — slot presence | Play/pause/insert/eject track the display |
| **M11** | Freeze the port artifacts (below) | — |

```
M0 ─► M1 ─► M2 ══[GATE: E4]══► M3 ─[E2,E3]─► M4 ─[E1,E6,E7]─┬─► M5 ─► M6 ─► M7
             │                                               └─► M8 (offline)
             └─(no RPC)─► STOP: re-plan around dbserver           M9 ─► M10 ─► M11
```

M8 is offline and cheap. Do not defer it just because "serve is later" — it turns
Phase C from greenfield into swapping the VFS backend.

## Experiments

Each has a hypothesis, a procedure, a pass criterion, and a decision.

**E1 — does passive NFS access work with no announcement?**
*Hypothesis:* yes. dysentery `startup.adoc:468` states NFS access to mounted media
works "allowing passive implementations to fetch track metadata directly without
sending announcement packets"; the RPC servers have no notion of DJ-Link identity.
*Procedure:* never instantiate the announcer; run `rpcinfo`/`exports`/`mount`/`fetch`
against an IP learned purely by listening, wrapped in `--assert-passive` (fails on a
single byte transmitted to 50000/50001/50002/50004), cross-checked with `tcpdump`.
*If it fails:* run M9 stage 1 (keep-alive at D=7), retry. "NFS requires prior
announcement" is a first-class result — it would make the announcer a hard
dependency of the Mixxx feature rather than an optional extra, and M9 must move
before M2.

**E2 — the `NFSERR_ACCES` (13) that libcdj hit.** Its `vdj_nfs_explore.c` header
comment reads *"Does not wrok, mount gets 13 NFSERR_ACCES."*
*H1 (most likely):* libcdj uses glibc `clnt_create()`, which defaults the client
credential to **`AUTH_NULL`**; both working implementations send **`AUTH_UNIX`**
(python-prodj-link stamp `0xdeadbeef`; prolink-connect stamp `0x967b8703`, observed
from a real CDJ). Doc 06 §2's "creds are not enforced" is *inferred* from two
clients that both send `AUTH_UNIX` — nobody has tested `AUTH_NULL`.
*H2:* mounting an export with no media inserted. *H3:* a reserved-source-port
(<1024) requirement.
*Procedure:* `mount --slot usb` four ways — `AUTH_UNIX`/`0x967b8703`,
`AUTH_UNIX`/`0xdeadbeef`, `AUTH_NULL`, and `AUTH_UNIX` from port 1023 — each against
a populated and an empty slot. If `AUTH_NULL` fails, doc 06 must be corrected and
the Mixxx implementation must not skip `AUTH_UNIX`.

**E3 — are `/C/`=USB and `/B/`=SD right on an NXS?** Both reference clients agree,
but both were validated against XDJ-class hardware. Print the raw length-prefixed
UTF-16LE export bytes verbatim under four media conditions on both units; also probe
`/A/` and bare `/C`. *Decision:* if `EXPORT` is enumerable, drive mounts from it
(prolink-connect's approach) rather than the hardcoded table — more robust, and the
serve side needs the same code.

**E4 — does a CDJ-2000NXS serve NFS at all?** The evidence marked "confirmed" in
doc 06 §1 actually rests on libcdj's capture of an **XDJ**, plus dysentery text about
XDJ-XZ/Opus Quad over USB-virtual-ethernet. Neither is an NXS, which is a 2012 unit.
This is the M2 gate.
*Decision tree:* registered always → proceed. Registered only with media inserted →
proceed, but M10 becomes a *dependency* of the consume path, not an optional extra.
Portmapper answers but nfsd/mountd absent → stop, pivot to dbserver. Nothing on
UDP/111 in any condition on either unit → **the decided transport does not work on
this hardware**; stop, write it up, re-plan around remotedb TCP (doc 04).

**E5 — which NFSv2 procedures does the NXS implement?** Neither reference client
calls `READDIR`, `GETATTR`, `STATFS` or `READLINK`, and libcdj's `READDIR` attempt
returned *"RPC: Procedure unavailable"*. The serve side needs to know what a real
CDJ will call against us; `STATFS` would give the Link-Info panel's free/total
bytes; `READDIR` would remove the `PIONEER`-vs-`.PIONEER` guess.

**E6 — `PIONEER` vs `.PIONEER`, and is `analyze_path` root-relative?** `LOOKUP` both
from the root handle; then resolve one `analyze_path` verbatim and with the leading
component stripped. Removes the "try both and see" branch both reference clients carry.

**E7 — READ sizing and fragmentation.** The chunk × window matrix with `tcpdump`
watching for IP fragmentation. Picks the constant the C++ port ships with, and tells
us whether prolink-connect's 2048 (which necessarily fragments) is safe on an NXS or
whether python-prodj-link's MTU-conservative 1280 is required.

**E8 — filehandle lifetime across media change.** Mount, fetch, eject, re-insert,
reuse the cached root handle. Expect `NFSERR_STALE` (70). Confirms the
cache-invalidation rule for `mediastore.py` *and* what our own server must return.

## CLI surface

Single entry point `prolinks`. `<device>` accepts a player number resolved via
discovery, or a bare IP (which bypasses discovery entirely — required for E1).

Global flags: `--iface NAME`, `--capture-dir DIR`, `--record/--no-record`,
`--assert-passive`, `--json`, `-v/-vv`, `--offline`.

```
prolinks sniff [--decode] [--hex]          prolinks devices [--watch]
prolinks rpcinfo <dev>                     prolinks exports <dev>
prolinks mount <dev> --slot usb|sd|rb      prolinks stat <dev> --slot S --path P
prolinks ls <dev> --slot S --path P        prolinks nfsprobe <dev> --slot S
prolinks fetch <dev> --slot S --path P [-o F] [--chunk N] [--window N]
                                           [--auth unix|null] [--stamp HEX] [--sha256]
prolinks pull-db <dev> [--slot S]          prolinks pdb-dump <FILE.pdb> [--table T]
prolinks tracks <dev>                      prolinks track <dev> <ID>
prolinks playlists <dev> [--tree]          prolinks anlz-dump <FILE> [--tag PQTZ]
prolinks get-audio <dev> <ID> [--play] [--verify-against /Volumes/X]
prolinks announce [--number N] [--name S] [--claim] [--dry-run] [--duration S]
prolinks status [--watch]                  prolinks serve-loopback <dir> [--port N]
prolinks replay <journal.jsonl>            prolinks golden <fixture-dir>
prolinks spec [-o spec/observed/]
```

`announce` is the only command that transmits on DJ-Link ports, so passivity is a
property of the command set rather than a runtime flag. `--auth`/`--stamp`/
`--chunk`/`--window` exist so E2 and E7 are single invocations, not code edits.
`--verify-against` automates the physical-stick SHA-256 comparison.

## Artifacts to freeze

The PoC's real output is not the Python — it is the evidence and the goldens.

1. **pcaps per milestone**, each with a `NOTES.md` recording hardware state (unit,
   firmware version from the UTILITY screen, slot, media). A pcap without that is
   worthless in six months.
2. **JSONL packet journal** — `{ts_mono, ts_utc, dir, local_port, peer, len, hex,
   decoded, decode_error}`. More useful than the pcap: it carries our interpretation
   alongside the bytes.
3. **Committed binary fixtures** — a real `export.pdb`, 3–5 ANLZ `.DAT`/`.EXT` pairs
   spanning easy / CJK-titled / no-`.EXT`, artwork, one small audio file, with a
   `manifest.json` of remote path, size, SHA-256 and how it was obtained. Do not
   commit a 40 MB FLAC — commit its first and last 64 KiB plus the hash.
4. **Golden decode JSON** — canonical (sorted keys, lowercase-hex byte strings,
   floats forbidden: store `bpm_100`, not `bpm`). *The contract for the C++ port:
   same bytes in → same canonical JSON out → empty diff.* This turns "did I port the
   parser correctly" from a judgement call into a CI check. Highest-value artifact.
5. **`spec/observed/*.md`** — byte tables with a provenance column per field:
   `observed` (seen on the wire, with the distinct values), `derived` (RFC-mandated),
   or `assumed` (we send it but never verified). The `assumed` rows are the Phase C
   risk register.
6. **`MODEL.md`** — the three state machines (discovery TTL, the device-number claim
   FSM, the NFS download window) as state/event/action tables; **`PORTING.md`** —
   per-module target Mixxx path, class name, thread, and Qt signals, plus measured
   timing constants; **`PROVENANCE.md`**; **`FINDINGS.md`** — the E1–E8 verdicts and
   the resulting corrections to docs 06, 07 and 09.

---

# Phase B — Mixxx C++ integration

## B0. Prerequisite: two pre-existing Rekordbox bugs

Land as a standalone, separately-reviewable change to
`src/library/rekordbox/rekordboxfeature.cpp`. Both are latent in Rekordbox today and
are hard blockers for ProLink, where devices vanish asynchronously as a matter of
routine rather than as an exotic case.

1. **`buildPlaylistTree()` (`:677`)** calls `parent->appendChild()` on a `TreeItem`
   already owned by the live `TreeItemModel`, from a `QtConcurrent` worker thread,
   with no `beginInsertRows()`/`endInsertRows()`. It survives today only because
   `onTracksFound()` (`:1659`) follows with a blanket `triggerRepaint()` and views
   re-query `rowCount()` lazily.
   *Fix:* build into a detached root, return it, and splice on the GUI thread via
   `TreeItemModel::insertTreeItemRows()` (`treeitemmodel.cpp:170`), which brackets
   correctly and is forwarded by `SidebarModel::slotRowsAboutToBeInserted`
   (`sidebarmodel.cpp:503`).
2. **`location TEXT UNIQUE` (`:98`)** combined with the `where rb_id=:rb_id and
   device=:device` finder (`:407`). Two devices holding cloned media produce
   identical locations; the second `INSERT` silently fails and `trackID` stays `-1`.
   *Fix:* plain `location TEXT` + `UNIQUE(device, rb_id)` + `INSERT OR REPLACE`. The
   lookup is already keyed on `(rb_id, device)`, so nothing else changes.

Bug 2 bites the two-CDJ rig directly (two players, cloned USBs), so neither is optional.

## B1. Module layout

Hard boundary: **`src/network/prolink/` must not `#include` anything from
`src/library/`.** That is what makes the future server side and the future dbserver
client droppable in without rework, and the protocol code unit-testable without a
`Library`.

```
src/network/prolink/
  prolinkdefs.h                 ports, magic "Qspt1WmJOL", packet types, slots, timeouts
  prolinkdevice.{h,cpp}         value struct, keyed on MAC (stable across DHCP)
  prolinkpacket.{h,cpp}         UDP-50000 parse/build (0x06 now; 0x0a/00/02/04/08 later)
  prolinkdiscovery.{h,cpp}      QUdpSocket on 50000, passive peer table + reaper
  prolinkstatuslistener.{h,cpp} QUdpSocket on 50002 -- but see FINDINGS F15:
                                50002 is silent to an unannounced host, so media
                                presence must come from polling MOUNT EXPORT
  prolinkvirtualcdj.{h,cpp}     announcer + claim chain (stub in v1)
  prolinknetworkservice.{h,cpp} the ONE object the library layer talks to; owns the net thread
  rpc/xdrbuffer.{h,cpp}         XDR incl. the Pioneer UTF-16LE strings
  rpc/rpcclient.{h,cpp}         ONC RPC v2/UDP: XID correlation, AUTH_UNIX, retry
  rpc/portmapclient.{h,cpp}     GETPORT
  nfs/nfsv2defs.h  nfs/nfsv2client.{h,cpp}  nfs/nfsfiletransfer.{h,cpp}

src/library/prolink/
  prolinkconstants.h            table names, config keys, cache dir
  prolinkfeature.{h,cpp}        ProLinkFeature : BaseExternalLibraryFeature
  prolinkplaylistmodel.{h,cpp}  ProLinkPlaylistModel : BaseExternalPlaylistModel
  prolinkmedia.{h,cpp}          per-(device,slot) state machine, TreeItem ownership
  prolinkpdbimport.{h,cpp}      pdb→sqlite + ANLZ reader (duplicated from Rekordbox — B6)
  prolinkcachemanager.{h,cpp}   cache root, LRU eviction, startup purge
  prolinktrackfetcher.{h,cpp}   "make this track playable locally", progress + cancel
  dlgprolinkfetch.{h,cpp,ui}    modal progress dialog
```

Reserved, not created now: `src/network/prolink/server/`, `.../dbserver/`.

## B2. Threading

| Resource | Thread | Enforcement |
|---|---|---|
| All `QUdpSocket`s | **net thread** (`QThread`, "ProLink Net") | Sockets `new`'d inside a slot running on that thread, never in a ctor |
| `TreeItem` / `TreeItemModel` / `SidebarModel` | **GUI only** | Worker builds *detached* trees; GUI splices |
| sqlite writes (pdb import) | QtConcurrent pool | `mixxx::DbConnectionPooler` + `DbConnectionPooled` (`rekordboxfeature.cpp:457`) |
| `getOrAddTrack` / `GlobalTrackCache` | **GUI only** | `DEBUG_ASSERT_QOBJECT_THREAD_AFFINITY`, `trackcollectionmanager.cpp:474` |
| Audio playback | engine thread | Reads a plain local file; no ProLink code involved |

The net thread is fully event-driven — no blocking recv, no sleeps. `RpcClient`
retries on a 250 ms `QTimer` tick, so a dead peer costs a few wakeups. In-tree
precedent for a dedicated worker thread with an event loop:
`src/library/scanner/libraryscanner.cpp:109-140`.

**Why not reuse `src/network/`'s `NetworkTask`/`WebTask`:** they are
`QNetworkAccessManager`-based HTTP request/response, with no notion of a UDP socket,
an XID, or a windowed multi-datagram transfer. **Why not `QtConcurrent`:** an NFS
download is a long-lived socket conversation, not a CPU job; parking a pool thread on
a socket for 30 s would starve the analyzer.

## B3. Sidebar tree

```
Pro DJ Link
├─ 2 · CDJ-2000nexus
│  ├─ USB → All Tracks, Friday Warmup, Techno ▸ Peak Time
│  └─ SD  → All Tracks
└─ 3 · CDJ-2000nexus  (offline)
```

Device→slot rather than flattened, because SD and USB are genuinely different
libraries. Keep the slot level even when only one slot is populated — consistency
beats cleverness in a tree navigated under time pressure.

`TreeItem` payload follows the Rekordbox `QList<QString>` convention
(`rekordboxfeature.cpp:196`) but with an explicit kind tag (`device`/`slot`/
`playlist`) instead of the two magic `IS_RECORDBOX_DEVICE` sentinels. Unlike
`RekordboxFeature::activateChild` (`:1563`) we do **not** mutate node data to flip
device→playlist after the first parse — `ProLinkMedia::state` carries that, which
keeps the tree data immutable and kills the "re-activating a re-mounted device does
nothing" bug class.

**Two-tier timeout** — the critical difference from a USB mount. A CDJ can blip off
the network for 2 s (cable jiggle, switch STP re-convergence) and come straight back;
tearing down the tree, the DB rows and the cache on every blip would be infuriating.

| Event | At | Action |
|---|---|---|
| Keep-alive stops | 10 s | Label `" (offline)"`, unbold, repaint. **Rows, DB rows and cache all stay** |
| Keep-alive returns | any | Restore label. Zero re-parse, zero refetch |
| Still gone | 60 s | Remove row, clear DB rows, unpin cache |
| `[ProLink],refresh` | — | Remove offline devices immediately |

Append new devices at `pRoot->childRows()` rather than inserting at 0, so a user
browsing player 2 is not yanked when player 3 powers on. Call
`clearLastRightClickedIndex()` before **every** structural change:
`BaseExternalLibraryFeature` holds a raw `QModelIndex` whose `internalPointer()` Qt
cannot fix up (`baseexternallibraryfeature.h:57-58`).

## B4. Track load and cache

1. Double-click → `WTrackTableView::slotMouseDoubleClicked` (`wtracktableview.cpp:407`)
   → `getTrack(index)` (`:429`).
2. `ProLinkPlaylistModel::getTrack` **snapshots every field value**, then checks
   `QFile::exists(location)`.
3. Miss → `ProLinkTrackFetcher::fetchBlocking({audio, .DAT, .EXT})`: a modal
   `DlgProLinkFetch` with progress and Cancel, spinning a nested `QEventLoop`. The
   ANLZ requests are `required = false` — a missing one costs the beatgrid, not the load.
4. Net thread: cached root fhandle → `LOOKUP` → `NfsFileTransfer` with 4 in-flight
   1280-byte READs, out-of-order reassembly by offset, writing `<local>.part`, then
   **atomic rename**. Atomicity is mandatory: `SoundSource::getTypeFromFile`
   (`soundsource.cpp:56`) uses `QMimeDatabase::MatchContent` — it *reads bytes*, so a
   half-written file would be classified as unsupported.
5. `getOrAddTrack` (`trackcollectionmanager.cpp:471`) → `addTracksAddFile`
   (`trackdao.cpp:838`) → a real `TrackPointer`.
6. Apply the MP3 timing offset and the ANLZ beatgrid/cues (same semantics as
   `rekordboxfeature.cpp:1288-1301`), tagged with the rekordbox beats subversion so
   the analyzer will not overwrite the grid.

**Cache layout** — mirror the CDJ's tree 1:1, so pdb-relative paths concatenate
verbatim exactly as `insertTrack` already does (`rekordboxfeature.cpp:371`), with zero
path-translation logic:

```
<settingsPath>/prolink_cache/<mediaKey>/
    .meta.json                      { lastUsed, sizeBytes, label, originMac, pdbSha1 }
    PIONEER/rekordbox/export.pdb
    PIONEER/USBANLZ/P016/0000875E/ANLZ0000.{DAT,EXT}
    Contents/Artist/Album/Track.mp3
```

`mediaKey = sha1(export.pdb)[0:16]` — **content-addressed, not keyed on
`(mac, slot)`** — but hashed over a *stabilised* copy of the file, not the raw
bytes. A player rewrites its own bookkeeping in the pdb header as it operates
(`unknown1` at `0x10` and the write counter `sequence` at `0x14`), so a raw
digest changes whenever a play count is written and would invalidate the cache
for a library that has not changed by one track. Zero `0x10..0x18` before
hashing; see FINDINGS F13 and `prolinks_poc.proto.pdb.stable_digest`. Two CDJs playing off clones of the same USB then share one cache
entry, halving traffic and disk on exactly this two-deck rig; swapping media yields a
new key naturally, with no stale-media bug class; and the key survives DHCP
renumbering and player-number changes. The chicken-and-egg (we can only hash after
downloading) resolves by fetching to `.incoming/<uuid>.pdb`, hashing, then renaming —
and if the target dir already exists, discarding the download, which *is* the
two-CDJs-one-USB fast path.

Eviction: at startup, delete every `*.part` and every media dir older than 30 days;
after each completed audio fetch, LRU by **whole media dir** (never individual files,
so we never half-gut a medium) down to a 4 GB budget; never evict a pinned dir.
Prefetch ANLZ for the first ~100 rows on playlist activation (~5 MB, makes waveforms
instant); **never prefetch audio by default** — a 10 MB pull per arrow-key press would
saturate a 100 Mbit link shared with the CDJs' own linked playback.

| Failure | Behaviour |
|---|---|
| CDJ vanishes mid-fetch | 5 × 2 s retries → `fileFailed`, `.part` deleted, `getTrack` returns null, node goes offline |
| User cancels | `abort()`, `.part` deleted, nothing loads |
| Media swapped mid-session | `NFSERR_STALE` → `invalidate(slot)` → re-`MNT` → retry once; then treat as `mediaGone` |
| **CDJ vanishes *after* load** | **Playback continues from the cache file** — a real advantage of copy-then-play over streaming |
| Disk full | short write → `fileFailed("disk full")`; `enforceBudget()` runs immediately |
| ANLZ missing/corrupt | Track loads without a beatgrid; Mixxx analyzes it normally |

## B5. Registration touchpoints

- **`CMakeLists.txt`** — `option(PROLINK ... ON)` + `__PROLINK__`, mirroring
  `ENGINEPRIME` (`:2438`, `:2549`); a guarded `target_sources` block; test files.
  **No dependency changes:** `Network` is already in `QT_COMPONENTS` (`:2799`) and
  PUBLIC-linked to `mixxx-lib` (`:2850`); `rekordbox_metadata` and `Kaitai` are
  already linked (`:2690`, `:2705`).
- **`src/library/library.cpp`** — `#ifdef __PROLINK__` +
  `addFeature(new ProLinkFeature(this, m_pConfig))` guarded on
  `ConfigKey("[Library]", "ShowProLinkLibrary")`, after the Serato block (~`:208`).
- **`res/images/library/ic_library_prolink.svg`** + a `res/mixxx.qrc` entry. The name
  is load-bearing: `LibraryFeature`'s ctor builds
  `":/images/library/ic_library_%1.svg"` (`libraryfeature.cpp:18`).
  **Do not use Pioneer's Pro DJ Link logo — trademark.** A generic linked-players glyph.
- **Preferences** — `dlgpreflibrarydlg.ui` row 6 checkbox (bump the
  "write protected" label to row 7) + `dlgpreflibrary.cpp` `slotResetToDefaults`,
  `slotUpdate` (`:303`), `slotApply` (`:545`). Drive-by: `checkBox_show_serato` is
  missing from `slotResetToDefaults` today.
- **Controls**, matching this fork's own `[Rekordbox],refresh` precedent
  (`rekordboxfeature.cpp:1339`): `[ProLink],refresh` and a read-only
  `[ProLink],device_count`.
- **No new preferences page in v1.** Ship the `[Library]/ShowProLinkLibrary` checkbox
  only, and expose the rest as `[ProLink]` config keys documented in the feature's
  root HTML view: `NetworkInterface` (empty = auto), `CacheDir`, `CacheSizeMb`=4096,
  `CacheMaxAgeDays`=30, `DeviceTimeoutMs`=10000, `DeviceRemovalGraceMs`=60000,
  `PrefetchAnalysis`=1, `PrefetchAudioOnSelect`=0, `EnableVirtualCdj`=0,
  `VirtualCdjDeviceNumber`=7. A page is a lot of surface (ui file, dialog class,
  `addPageWidget`, light+dark icons, translations) for settings a normal user never
  touches, and would balloon the first PR.

**Hand-roll the XDR; do not link libnfs or libtirpc.** libnfs is NFSv3/v4-centric and
encodes `LOOKUP` names and mount paths as ASCII, but Pioneer uses **length-prefixed
UTF-16LE** (doc 06 §5) — using it means patching its wire encoder, which is worse than
writing our own. libtirpc is effectively Linux-only with a blocking API that fits a Qt
event loop badly. Both would add a `find_package` plus packaging changes on five
platforms for **six procedures**: `GETPORT`, `MNT`, `LOOKUP`, `READ`, `GETATTR`,
`NULL`. Hand-rolled estimate: ~850 lines of straightforward, fully unit-testable
big-endian struct work on `QByteArray` + `QDataStream` + `QUdpSocket` — and it is the
same code the objective-2 *server* needs in reverse.

## B6. Duplicated Rekordbox glue

Per the decision to duplicate now and extract later, copy into
`src/library/prolink/prolinkpdbimport.{h,cpp}`, written correctly from the start
(detached trees, `UNIQUE(device, rb_id)`): the pdb table walk
(`rekordboxfeature.cpp:502-651`), `insertTrack` (`:353`), `buildPlaylistTree`
(`:656`), `readAnalyze` (`:874`), `setHotCue` (`:839`), `colorFromID` (`:331`),
`getText` and the UTF-16 helpers (`:254-295`), and the MP3 timing-offset table (`:1248`).

Reused without copying: the Kaitai types in `lib/rekordbox-metadata/`, already a shared
static library. `kaitai::kstream(const std::string&)` exists
(`lib/kaitai/kaitai/kaitaistream.h:52`), so an in-memory parse is possible — but the
pdb requires **random access** (`page_ref_t::body()` seeks,
`rekordbox_pdb.cpp:647`), so it must be fully resident; streaming is impossible. We
write `export.pdb` into the cache dir anyway, so parse from that path: one code path,
no 2× memory spike, and free crash-recovery on restart.

The `analyze_path` column plumbing is reusable as-is — `ColumnCache` keys on the plain
column name (`trackschema.h:76`, `columncache.h:66`), so a `prolink_library` table with
an `analyze_path` column gets it for free. **No `columncache` changes.**

Leave a `TODO(prolink)` at the top of `prolinkpdbimport.cpp` naming the eventual
extraction (`rekordboxanlz.*` + `rekordboxpdbimporter.*`, parameterised by table
names), so the deferred refactor is discoverable rather than folklore.

## B7. Build order

| Step | Deliverable | Independently testable? |
|---|---|---|
| 1 | B0 Rekordbox bug fixes | Yes — the existing USB flow is unchanged |
| 2 | `xdrbuffer` + `rpcclient` + `portmapclient` + golden-vector tests | Yes |
| 3 | `nfsv2client` + `nfsfiletransfer` + tests | Yes — pull `export.pdb` off a real NXS |
| 4 | `prolinkpacket` + `prolinkdiscovery` + `prolinknetworkservice` | Yes — log discovered peers |
| 5 | `prolinkcachemanager` + `prolinkfeature` skeleton (tree only, no tracks) | Yes — **CDJs appear/disappear in the sidebar** |
| 6 | pdb pipeline: fetch → parse → playlist tree → `ProLinkPlaylistModel` | Yes — **browse a CDJ's playlists** |
| 7 | `prolinktrackfetcher` + `dlgprolinkfetch` + `getTrack()` | Yes — **load a CDJ's track to a deck** |
| 8 | `prolinkstatuslistener`, eviction, `[ProLink],refresh` | Polish |
| 9 | Registration: CMake option, `library.cpp`, qrc, icon, prefs | Ship |
| 10 | `prolinkvirtualcdj` behind `[ProLink]/EnableVirtualCdj` | Phase C prep |

---

# Phase C — serving our library (designed for, not built)

`src/network/prolink/server/` + `dbserver/`. Enabled by three things: the protocol
layer having no `src/library/` dependency; every codec doing both directions; and the
PoC's `vfs.py`/`nfsserver.py` having proved the reply encoders against two independent
third-party clients.

**No reference implementation exists for the server side in any of the seven repos** —
verified by grepping all of them for TCP listeners and RPC service registration. The
transferable assets are doc 04 §6 and doc 06 §6, plus python-prodj-link's
`data/pdbprovider.py` read *in reverse* (as documentation only — Apache-2.0).

---

# Verification

**Phase A:** the per-milestone hardware checks above. The anchor is M4 — the
NFS-fetched `export.pdb` SHA-256 must equal the same file read off the physically
mounted stick. M8 additionally validates our encoders against prolink-connect with no
hardware at all.

**Phase B:**

- Unit tests with golden vectors captured from the two NXS units, checked in as hex
  literals: `prolink_packet_test.cpp` (a real 0x06 keep-alive → number, name, MAC, IP);
  `prolink_xdr_test.cpp` (UTF-16LE `MNT("/C/")` byte-for-byte, and a length field of
  `0xFFFFFFFF` must be rejected **without allocating**); `prolink_nfsclient_test.cpp`
  (a loopback stub replaying captured replies, including out-of-order arrival).
- End-to-end on hardware: CDJ powers on → appears in the sidebar within ~2 s; expand →
  USB slot → playlists; double-click → progress dialog → the track loads with the
  correct beatgrid and hot cues; **pull the Ethernet cable mid-fetch** → clean failure,
  no crash; pull it *after* load → playback continues.
- Regression: mounted Rekordbox USB browsing still works after B0.
- `cmake -DPROLINK=OFF` still builds and links.

## Risks

**R1 (highest) — the nested event loop in `getTrack()`.** `TrackModel::getTrack` is
`const` and called synchronously from the view; there is no asynchronous path
(returning null and loading later would bypass the `loadTrack`/`PlayerManager` chain
and break Auto DJ, samplers, preview decks and controller mappings alike). Re-entering
the Qt event loop from inside a const model method the view is mid-call on is
inherently hazardous: during the loop the user can click another feature, the device
can vanish, and `index` becomes dangling. Mitigation is threefold and non-optional:
(a) snapshot every field before spinning; (b) pin the medium so removal is deferred;
(c) never touch `index` after the loop — which means deliberately inlining
`BaseExternalPlaylistModel::getTrack`'s body (`:35-73`) against the snapshot rather
than calling it, with a comment explaining why. *Test:* pull the cable during a fetch.

**R2 — cache files enter the real Mixxx library.** `getOrAddTrack` writes them into
`library`/`track_locations` for real. Two consequences: with metadata sync on, Mixxx
may write ID3 tags into the cache copy (harmless — it is a copy); and after eviction
those rows become **Missing Tracks**, cluttering that feature. Start with: never
auto-evict a medium that produced rows this session, and document it. This is a
user-visible side effect the Rekordbox feature does not have and must be called out in
any upstream PR.

**R3 — binding UDP 50000 may fail** (rekordbox, prolink-tools, or another Mixxx
instance already holds it). Use `QUdpSocket::ShareAddress | ReuseAddressHint`;
semantics differ across macOS (`SO_REUSEPORT`) and Windows (`SO_REUSEADDR`). On failure
the feature must degrade to an explanatory HTML root view — **never a `QMessageBox`**
(see the comment at `library.cpp:170-172`). Test on all three platforms.

**R4 — multi-homed hosts.** The Pi has `eth0` (CDJ network, 169.254/16 link-local) and
`wlan0`. A broadcast keep-alive can arrive on either; the NFS socket must bind a source
address on the *same* subnet as the peer, or link-local routing will silently pick the
wrong NIC and every RPC will time out. Record the receiving interface per device in
`ProLinkDiscovery` and pass it as `RpcClient`'s local bind;
`[ProLink]/NetworkInterface` is the manual override.

**R5 — shutdown ordering.** `~ProLinkFeature` must, in order: (1)
`m_pNetwork->shutdown()` — quit and wait the net thread, so no queued signal lands on a
half-destroyed feature; (2) `waitForFinished()` the parse future (the Rekordbox
precedent, `:1417`); (3) drop the temp tables. Backwards produces a shutdown crash that
only reproduces with a CDJ on the network.

**R6 — NFSv2 32-bit offsets.** `READ` offset and `fattr.size` are `uint32`, a hard
4 GiB ceiling. Fine for audio; assert and fail cleanly rather than wrapping.

**R7 — macOS sandbox.** The default cache under `getSettingsPath()` needs no
`Sandbox::askForAccess`. If the user relocates it to an external volume via
`[ProLink]/CacheDir`, apply the same guard Rekordbox uses at `rekordboxfeature.cpp:495`.

**Open, for hardware.** Does an NXS *broadcast* status on 50002, or only unicast to
announced vCDJs? This gates passive slot detection; the fallback (speculatively
probing both slots with `MNT` + a `LOOKUP` of the pdb path) is already designed in, so
it is a quality-of-life question, not a blocker. And does an NXS serve NFS acceptably
*while it is itself playing*? The 4 × 1280 B window caps us near 1 MB/s, deliberately
modest; make it a config key if it matters.

---

## Decision log

- **Python PoC before C++** — chosen over going straight to C++, for a much faster
  protocol iteration loop against real hardware. The C++ port then becomes largely
  mechanical, gated by the golden-decode contract.
- **NFS + pdb, not dbserver** — reuses Mixxx's existing Kaitai pdb parser, works
  passively with no device number (so zero risk of disrupting a live set), and is the
  only path to the actual audio bytes. dbserver (doc 04) is added later, if the pdb
  turns out to be missing something.
- **Cache to disk, not a streaming `SoundSource`** — a streaming provider would require
  changing `SoundSourceProxy`'s extension-based dispatch and `Track`'s `FileInfo`
  assumption, i.e. core changes that will not land upstream. Copy-then-play also means
  playback survives the source CDJ leaving the network.
- **Duplicate the Rekordbox glue now, extract later** — keeps the working Rekordbox
  feature untouched and avoids rebase friction with the local commits `7ce93c7` and
  `e5063fc`. Accepted cost: pdb fixes must be applied twice until the extraction lands.
- **Fix both Rekordbox bugs anyway** — they are real bugs today, and bug 2 breaks this
  specific two-CDJ rig.
