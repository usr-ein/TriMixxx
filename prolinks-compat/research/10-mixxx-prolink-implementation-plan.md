# 10 — Implementation plan: two-way ProLink in Mixxx

The approved build plan for Phase B. Supersedes the sketch in
[08-python-poc-plan.md](08-python-poc-plan.md) and corrects the licensing claims in
[09-mixxx-integration-notes.md](09-mixxx-integration-notes.md).

Grounded in a full read of the Mixxx source at `../mixxx` (fork of 2.5.6). Line
references are to that tree.

**Revised 2026-07-30** for two changes of direction:

1. **Serving is v1 scope, not a later phase.** Phase A proved both directions
   against real hardware, so the old "consume now, serve someday" split has no
   remaining justification. Former Phase C is folded into Phase B.
2. **Kaitai Struct describes the wire formats.** With one large caveat that
   shapes the whole design — see [§Kaitai](#kaitai-struct-what-it-can-and-cannot-do-here).

> Implement from [`docs/PROTOCOL.md`](../docs/PROTOCOL.md), not from this file
> and not from `research/00`–`09`. This document says *how to build it in Mixxx*;
> PROTOCOL.md says *what the bytes are*. Where they disagree, PROTOCOL.md wins.

## Working-directory conventions

- `prolinks-compat/` — research docs, the Python PoC (`prolinks_poc/`, `tests/`),
  and the `.ksy` sources. `captures/` and `research/ref-repos/` stay git-ignored.
- `mixxx/` — the C++ fork. No PoC artifacts land there; only `src/`, `res/`,
  `CMakeLists.txt` changes, plus the generated Kaitai output.
- Commit straight to `main` in both repos — no PRs (standing convention for this
  project). Upstreaming to mixxxdj/mixxx is a separate, later exercise; see
  [§Upstreaming](#upstreaming-reality-check).

---

## Target behaviour

The feature as specified, in the order a user meets it.

### On boot

- If **enabled in Mixxx settings** (default off), Mixxx acts as a CDJ on the
  Pro DJ Link network. Disabled, it transmits nothing and binds nothing.
- It advertises a **given player number, or AUTO** — AUTO is the default.
- It watches the filesystem for mounted volumes carrying a rekordbox database,
  and **reports media inserted/ejected** to other players as it sees them appear
  and disappear.

### Serving

- Other CDJs **browse the media mounted in Mixxx** — the same categories, sorts,
  drill-downs and search a real CDJ offers — and load and play tracks from them.
- **Only the first two volumes** are served: the first fills the **USB** slot,
  the second the **SD** slot. A third is ignored.
- **The internal Mixxx library is never shared.** Only rekordbox media are.

### Consuming

- Mixxx finds other CDJs on the network and lists them in the library sidebar
  under **ProLink**:

  ```
  ProLink
  ├─ 1 · CDJ-2000nexus
  │  ├─ USB
  │  └─ SD
  └─ 2 · CDJ-2000nexus  (offline)
  ```

- Opening a slot **downloads and parses that medium's `export.pdb`** over NFS.
- Loading a track **downloads the whole file into a local cache**, which is
  **cleared at every boot**.

> **Reading of "menu bar".** The requirement says CDJs "display in the menu bar
> under ProLink", with the path `ProLink > 1 - CDJ 2000 Nxs > USB/SD`. A
> three-level browsable, track-loadable hierarchy is the library sidebar, not
> Qt's menu bar — Mixxx has no other surface that can do this. Built as a
> `LibraryFeature`.

### Decisions taken

| | |
|---|---|
| Phasing | Python PoC first (done — validated against two real CDJ-2000NXS), then port to C++/Qt |
| Scope | **Both directions in v1** |
| Consume transport | NFSv2-over-UDP + `export.pdb`. **No dbserver client** — we parse the database ourselves |
| Serve transport | The full stack: 50000 + 50002 + 12523 + dbserver 1051 + portmap/mountd/nfsd |
| Discovery | Active. The virtual CDJ is required for serving, and the consume side reuses it |
| Numbering | AUTO by default, from 1–4; explicit override; observer number 7 when consume-only |
| Served content | Mounted rekordbox media only. Never the Mixxx library |
| Audio (consume) | Whole file into a boot-scoped cache, then `getOrAddTrack()` on the local path |
| Audio (serve) | Streamed from the real volume via NFS, exactly as a CDJ does |
| Wire formats | Kaitai `.ksy` as the source of truth → generated **readers**; hand-written **writers** |
| Rekordbox glue | Duplicate into ProLink now, extract a shared importer later |
| Pre-existing bugs | Fix in `rekordboxfeature.cpp` too, as a standalone change |

Rationale for each is in the [decision log](#decision-log).

---

## Licensing — read before writing any protocol code

Verified by reading the `LICENSE` files in `ref-repos/` (2026-07-29):

| Repo | License | Usable in Mixxx? |
|---|---|---|
| **prolink-connect** | **MIT** | **Yes** — copy/adapt freely, keep the notice |
| **prolink-cpp** | **MIT** | **Yes** |
| python-prodj-link | **Apache-2.0** | **No** — incompatible with GPLv2 |
| dysentery | EPL-1.0 | **No** |
| vizlink | EPL-2.0 | **No** |
| libcdj | *no LICENSE file* → all rights reserved | **No** — including the `.x` files |

Mixxx is **GPLv2-or-later** (`../mixxx/COPYING`). Apache-2.0 and EPL would not make
the tree undistributable under "or later", but they would strip the GPLv2 option
from the combined work, forcing Mixxx to GPLv3+. Maintainers will not accept that.

**Operative rule: inspiration, not transcription.** Protocol *facts* — offsets,
magic numbers, procedure numbers, `/B/` and `/C/`, the UTF-16LE deviation — are
**not copyrightable**. Only code expression is.

In practice this is now a solved problem: **`docs/PROTOCOL.md` and
`docs/FINDINGS.md` are the extraction**, written from our own captures of our own
hardware. Write the C++ from those, and the provenance trail is clean by
construction.

### The `.ksy` files specifically

`lib/rekordbox-metadata/rekordbox_pdb.ksy` and `rekordbox_anlz.ksy` are already
vendored in Mixxx and are **EPL-1.0** (from Deep-Symmetry's crate-digger). That is
a pre-existing situation upstream, not ours to fix, and we reuse them as-is.

**Every new `.ksy` we add is authored by us from `docs/PROTOCOL.md`, licensed
GPLv2+, and must not be derived from any EPL or Apache source.** Put the
provenance in each file's `meta.doc` — the finding number for each non-obvious
field. This is the single most scrutinised thing in a reverse-engineered
contribution, and a `.ksy` that cites `F31` next to a byte offset answers the
question before it is asked.

---

## Phase A — Python PoC: **done**

Kept here as a record; the live state is in [`STATUS.md`](../STATUS.md).

Both objectives work end-to-end against two CDJ-2000NXS on firmware 1.44. The
last serve session was **568 dbserver requests with zero errors**: 11 browse
categories, drill-downs, ALL entries, search-as-you-type, all twelve sorts with
the sorted field as the second column, harmonic key matching, two media at once
as USB + SD, load and play of MP3/AAC/WAV/AIFF including 75 MB lossless, artwork,
hot cues, both waveforms, and LOAD SETTINGS.

Experiments E1–E8 all have verdicts; they are folded into `docs/FINDINGS.md`
(F1–F44, C1–C14, O1–O7) and settled into `docs/PROTOCOL.md`. 273 tests pass with
no hardware. 31 pcaps are archived in `prolinks-captures-2026-07-30.7z`.

**What Phase A hands to Phase B:**

| Artifact | Where | Role in the port |
|---|---|---|
| The specification | `docs/PROTOCOL.md` | Implement from this |
| The evidence | `docs/FINDINGS.md` | Cite this in comments |
| Capture corpus | `prolinks-captures-2026-07-30.7z`, 31 pcaps | Regression input for the `.ksy` and the writers |
| Working reference implementation | `prolinks_poc/` (~9.4k lines excl. CLI/capture) | The algorithm, in a language you can single-step |
| Test suite | `tests/`, 273 tests | Port the assertions, not the code |

**Still missing, and worth producing before the port** (they were listed as Phase A
deliverables and never made):

- **`PORTING.md`** — per-module → target Mixxx path, class name, thread, Qt
  signals. Cheap to write and it removes a hundred small decisions from the port.
- **Golden decode JSON** — canonical (sorted keys, hex byte strings, no floats:
  `bpm_100` not `bpm`) for every fixture. *This is the contract for the port:
  same bytes in → same canonical JSON out → empty diff.* It converts "did I port
  the parser correctly" from a judgement call into a CI check, and it is what
  makes step 2 of the build order self-verifying.

---

## Kaitai Struct: what it can and cannot do here

**The constraint that shapes everything below: Kaitai Struct cannot generate C++
serializers.** Serialization support exists only for the Java and Python targets
([doc.kaitai.io/serialization.html](https://doc.kaitai.io/serialization.html)),
and the runtime vendored at `lib/kaitai/` (`KAITAI_STRUCT_VERSION 11000L`,
i.e. 0.11) is read-only — `kaitaistream.h` exposes `read_u4be()` and friends and
has no `write_*` at all. Verified by inspection, not assumed.

For a consume-only feature that would be a footnote. For a **two-way** feature it
is structural: half of what we do is emit bytes, and Kaitai will not emit them.

### The split

| Layer | Parse | Build |
|---|---|---|
| `export.pdb` | **Kaitai** (already in tree) | never |
| ANLZ `.DAT`/`.EXT` | **Kaitai** (already in tree) | never — but the *wire* transform is ours (§5.11) |
| `PIONEER/*SETTING*.DAT` | **Kaitai** (new) | never |
| DJ Link UDP 50000 | **Kaitai** (new) | hand-written |
| Status / media / settings UDP 50002 | **Kaitai** (new) | hand-written (skeleton + poke, F23) |
| dbserver TCP 1051 | **Kaitai** (new) | hand-written |
| ONC RPC / portmap / mountd / NFSv2 | hand-written | hand-written |

RPC is the one place Kaitai is *not* worth it. XDR is 4-byte-aligned big-endian
primitives with no layout subtlety; the reply shape depends on the procedure,
which lives outside the byte stream (correlated by XID), so Kaitai would need
`params:` threaded through every type to express what a `switch` on an integer
does in four lines. Six procedures. Hand-write it — as the previous revision of
this plan already argued, and it is the same code the server needs in reverse.

Everywhere else Kaitai earns its place, because the hard part of those formats
*is* the layout: which byte is at `0x6f`, that the name starts at `0x0b` on 50002
and `0x0c` on 50000, that a zero-length dbserver blob is **absent** rather than
empty.

### Why this is still a win, not a consolation prize

1. **The `.ksy` becomes an executable `PROTOCOL.md`.** Every offset in the spec
   gets a machine-checkable counterpart. Documentation that can be run against
   31 pcaps does not rot.
2. **The Python target does serialize.** Generate `--target python` from the same
   `.ksy` into the PoC's test tree and you get, for free: a second independent
   parser to diff against the hand-written PoC codecs across the whole capture
   corpus, **and a reference encoder to diff the C++ writers against**. The
   serialization gap becomes a testing asset instead of a loss.
3. **Round-trip is the writer's unit test.** Every writer is tested as
   `build(x) → kaitai parse → compare fields`, plus a golden hex vector from a
   real capture. A writer with no generated counterpart is still fully pinned.

### Layout and build integration

Precedent in-tree: `rekordbox_pdb.cpp` is **generated code, checked in**, carrying
the banner *"This is a generated file! Please edit source .ksy file and use
kaitai-struct-compiler to rebuild"*, compiled as its own static target
`rekordbox_metadata` with `-Wno-switch` / `/w44063`
(`CMakeLists.txt:2685-2705`). Follow it exactly: **no JVM in the build.**

```
prolinks-compat/ksy/                      # source of truth, ours, GPLv2+
    prolink_djl.ksy                       # UDP 50000
    prolink_status.ksy                    # UDP 50002 — separate top-level type (C14)
    prolink_dbserver.ksy                  # TCP 1051
    rekordbox_mysetting.ksy               # PIONEER/*SETTING*.DAT
    regenerate.sh                         # ksc --target cpp_stl + --target python
    README.md                             # which finding pins which field

mixxx/src/network/prolink/generated/      # checked-in ksc output, never edited
    prolink_djl.{h,cpp}  prolink_status.{h,cpp}
    prolink_dbserver.{h,cpp}  rekordbox_mysetting.{h,cpp}
```

`.ksy` sources live in **prolinks-compat**, not Mixxx, because they are research
artifacts that the Python test suite consumes too; `regenerate.sh` writes into
both trees. New CMake target `prolink_protocol` mirroring `rekordbox_metadata`:
static, `EXCLUDE_FROM_ALL`, relaxed warnings, links `Kaitai`. That keeps generated
noise out of the warnings-as-errors path our own code sits in.

**Kaitai parses from a fully-resident buffer.** Fine for packets, and already the
case for the pdb, which needs random access anyway (`page_ref_t::body()` seeks,
`rekordbox_pdb.cpp:647`).

### The one format that will fight

`prolink_dbserver.ksy` is the hard one, and worth knowing before starting:

- A message is a **tagged field stream**, and the header separately carries a
  12-byte blob of *argument* tags describing the same arguments **under a
  different numbering** (`0f`/`10`/`11`/`14`/`26` vs `02`/`03`/`06`). Both must
  agree. Expressible: parse the tag blob first, then `switch-on` it per argument.
- **A zero-length binary argument is omitted entirely** — not sent as an empty
  blob. Needs `if:` keyed on the already-parsed argument tag. Expressible, but it
  is the rule that desynchronises a naive parser, so pin it with a capture test
  on day one.
- Strings are **UTF-16 big-endian counted in characters including the NUL** —
  the opposite endianness to the NFS layer. Kaitai's `encoding: UTF-16BE` with a
  computed byte length handles it; do not let the two conventions share a type.

If the tag-blob correlation turns out to fight Kaitai harder than expected, the
fallback is a hand-written parser for *this format only* — it is one file, the
PoC's `proto/dbserver.py` is 689 lines including docstrings, and nothing else in
the design depends on it being generated. Decide after the first `.ksy` spike;
do not let it block the other three.

---

# Phase B — Mixxx C++ integration

## B0. Prerequisite: two pre-existing Rekordbox bugs

Land as a standalone, separately-reviewable change to
`src/library/rekordbox/rekordboxfeature.cpp`. Both are latent in Rekordbox today
and are hard blockers for ProLink, where devices vanish asynchronously as a matter
of routine rather than as an exotic case.

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

Bug 2 bites the two-CDJ rig directly (two players, cloned USBs), so neither is
optional.

## B1. Module layout

Two hard boundaries:

- **`src/network/prolink/` must not `#include` anything from `src/library/`.**
  This is what keeps the protocol code unit-testable without a `Library`, and it
  is now load-bearing rather than aspirational: the serve side lives entirely
  below it and must never reach into Mixxx's collection.
- **The serve side must not reach into the consume side, or vice versa.** They
  share the codecs, the discovery table and the virtual CDJ. Nothing else.

```
src/network/prolink/
  prolinkdefs.h                  ports, magic "Qspt1WmJOL", packet types, slots, timeouts
  prolinkdevice.{h,cpp}          value struct, keyed on MAC (stable across DHCP)
  prolinkdiscovery.{h,cpp}       QUdpSocket on 50000, peer table + reaper
  prolinkvirtualcdj.{h,cpp}      claim chain, AUTO/manual numbering, keep-alive, defence
  prolinkstatus.{h,cpp}          UDP 50002 both ways: emit ours, read theirs
  prolinknetworkservice.{h,cpp}  the ONE object the rest of Mixxx talks to; owns both threads
  generated/                     checked-in Kaitai readers (see above)
  wire/prolinkwriter.{h,cpp}     the hand-written build side: QByteArray + endian helpers
  wire/djlbuild.{h,cpp}          0x0a/00/02/04/05/06/08
  wire/statusbuild.{h,cpp}       0x0a status from a captured skeleton (F23), 0x06, 0x36
  wire/dbserverbuild.{h,cpp}     messages, menu items, the five analysis blobs (§5.11)
  rpc/xdrbuffer.{h,cpp}          XDR incl. the Pioneer UTF-16LE strings
  rpc/rpcclient.{h,cpp}          ONC RPC v2/UDP client: XID correlation, AUTH_UNIX, retry
  rpc/rpcserver.{h,cpp}          ONC RPC v2/UDP dispatch, shared by the three services
  rpc/portmapclient.{h,cpp}      GETPORT
  nfs/nfsv2defs.h
  nfs/nfsv2client.{h,cpp}        LOOKUP / READ / GETATTR, windowed
  nfs/nfsfiletransfer.{h,cpp}    "fetch this whole file", progress + cancel

  server/                        --- objective 2, all of it ---
    prolinkmediawatcher.{h,cpp}  mounted volumes -> slots; insert/eject signals
    prolinkservedmedium.{h,cpp}  slot + volume + parsed pdb + settings blob
    prolinkpdbindex.{h,cpp}      in-memory index: artists, albums, genres, keys, sorts
    prolinkvfs.{h,cpp}           path -> 12-byte filehandle, NFC+casefold matching (O6)
    prolinknfsserver.{h,cpp}     portmap + mountd + nfsd over rpcserver
    prolinkmediaquery.{h,cpp}    0x05 -> 0x06 with true counts (F24); 0x35 -> 0x36 (F38)
    dbserver/
      prolinkdbserver.{h,cpp}    QTcpServer 1051 + the 12523 port query
      prolinkdbsession.{h,cpp}   one connection: preamble, TxIDs, concurrent menus
      prolinkdbmenus.{h,cpp}     root categories, drill grid, sorts, search, ALL entries
      prolinkanalysiswire.{h,cpp} ANLZ -> wire form: VBR index, beat grid, waveforms, cues

src/library/prolink/             --- objective 1, all of it ---
  prolinkconstants.h             table names, config keys, cache dir
  prolinkfeature.{h,cpp}         ProLinkFeature : BaseExternalLibraryFeature
  prolinkplaylistmodel.{h,cpp}   ProLinkPlaylistModel : BaseExternalPlaylistModel
  prolinkmedia.{h,cpp}           per-(device,slot) state machine, TreeItem ownership
  prolinkpdbimport.{h,cpp}       pdb -> sqlite + ANLZ reader (duplicated from Rekordbox — B9)
  prolinkcachemanager.{h,cpp}    cache root, boot purge, session pinning
  prolinktrackfetcher.{h,cpp}    "make this track playable locally", progress + cancel
  dlgprolinkfetch.{h,cpp,ui}     modal progress dialog
```

The PoC maps onto this almost one-to-one, which is the point of having written it
the way it was written:

| PoC | Mixxx |
|---|---|
| `proto/djl.py`, `djl_status.py` | `generated/prolink_djl` + `wire/djlbuild`, `generated/prolink_status` + `wire/statusbuild` |
| `proto/dbserver.py` | `generated/prolink_dbserver` + `wire/dbserverbuild` |
| `proto/xdr.py`, `rpc.py`, `portmap.py`, `mountd.py`, `nfs2.py` | `rpc/*`, `nfs/*` |
| `proto/pdb.py`, `piostring.py`, `anlz.py` | `lib/rekordbox-metadata` (already there) |
| `proto/mysetting.py` | `generated/rekordbox_mysetting` |
| `proto/analysis_wire.py` | `server/dbserver/prolinkanalysiswire` |
| `net/loop.py`, `udp.py` | Qt event loop + `QUdpSocket` — deleted, not ported |
| `net/rpcclient.py`, `nfsclient.py` | `rpc/rpcclient`, `nfs/nfsv2client` |
| `net/vfs.py`, `nfsserver.py` | `server/prolinkvfs`, `server/prolinknfsserver` |
| `net/dbserverd.py` | `server/dbserver/*` — the biggest single chunk |
| `core/discovery.py`, `devices.py` | `prolinkdiscovery`, `prolinkdevice` |
| `core/announcer.py` | `prolinkvirtualcdj` |
| `core/library.py`, `medium.py`, `slots.py` | `server/prolinkpdbindex`, `prolinkservedmedium`, `prolinkdefs.h` |
| `capture/*`, `cli.py` | not ported |

`net/loop.py` existing at all was the deliberate bet that paid off here: it was
written as an explicit `poll(now)` reactor precisely so it would evaporate against
`QUdpSocket::readyRead` + `QTimer` instead of having to be unwound from asyncio.

## B2. Threading

| Resource | Thread | Enforcement |
|---|---|---|
| Discovery, virtual CDJ, status, media query, consume-side RPC | **"ProLink Net"** (`QThread`) | Sockets `new`'d inside a slot running on that thread, never in a ctor |
| NFS server, dbserver, all served file I/O | **"ProLink Serve"** (`QThread`) | Same |
| `TreeItem` / `TreeItemModel` / `SidebarModel` | **GUI only** | Worker builds *detached* trees; GUI splices |
| sqlite writes (consume-side pdb import) | QtConcurrent pool | `mixxx::DbConnectionPooler` + `DbConnectionPooled` (`rekordboxfeature.cpp:457`) |
| `getOrAddTrack` / `GlobalTrackCache` | **GUI only** | `DEBUG_ASSERT_QOBJECT_THREAD_AFFINITY`, `trackcollectionmanager.cpp:474` |
| Audio playback | engine thread | Reads a plain local file; no ProLink code involved |

**Why two net threads and not one.** The serve side does blocking `pread()` on a
USB stick to answer NFS READs. On the same thread as the heartbeat, a cold read
from a slow stick delays the 2.0 s keep-alive and the 200 ms status tick — and
five missed keep-alives make us **vanish from every deck on the network**, mid-set.
The heartbeat must be isolated from disk. Conversely a dbserver render of a
692-item menu is real CPU work that must not delay a discovery packet.

Within the serve thread, NFS and dbserver share one event loop, as they did in the
PoC through 568 requests and 75 MB lossless scrubbing with zero errors. Revisit
only with evidence.

Both threads are fully event-driven — no blocking recv, no sleeps. `RpcClient`
retries on a 250 ms `QTimer` tick, so a dead peer costs a few wakeups. In-tree
precedent for a dedicated worker thread with an event loop:
`src/library/scanner/libraryscanner.cpp:109-140`.

**Why not `src/network/`'s `NetworkTask`/`WebTask`:** they are
`QNetworkAccessManager`-based HTTP request/response, with no notion of a UDP
socket, an XID, or a windowed multi-datagram transfer. **Why not `QtConcurrent`:**
an NFS download is a long-lived socket conversation, not a CPU job; parking a pool
thread on a socket for 30 s would starve the analyzer.

## B3. Identity: the virtual CDJ

Serving is impossible without a device number, so `ProLinkVirtualCdj` is now core
rather than the stub the previous revision described.

**AUTO (default).** Byte `0x31` of CLAIM_IP is `0x01` for automatic and `0x02` for
a specific number (F36). Setting the flag is not the same as choosing the number —
we still pick one:

1. Listen for **≥ 2.5 s** (one keep-alive interval plus margin) to populate the
   peer table. Silence is not evidence a number is free; only having watched is,
   and XDJ-XZ / Opus Quad do not defend their numbers at all.
2. Choose the lowest free number **in 1–4**.
3. Run the chain: `3× HELLO → 3× CLAIM_MAC → 3× CLAIM_IP → N× CLAIM_NUMBER`,
   ~300 ms apart, all broadcast. **N is 3 into an empty network, 1 into a
   populated one** (C13).
4. On `0x08` NUMBER_CONFLICT or a `0x05` NUMBER_IN_USE naming our candidate, drop
   it and restart at the next candidate.
5. Then keep-alive every **2.0026 s** forever, and **defend**: answer anyone
   claiming our number with `0x08`. A device that takes a number and does not
   defend it loses it to the next player that boots.

**Why 1–4.** Real players occupy 1–4 and that is the range a CDJ's LINK screen
enumerates. Our serve sessions ran as **device 3** and worked. Numbers ≥ 5 are
untested for serving; treat "all of 1–4 taken" as *serving unavailable* — degrade
to the observer number **7**, which announces without contending and keeps the
consume side fully functional. Surface that in the feature's status view; do not
fail silently, and do not steal a number from a player mid-set.

**Manual.** `[ProLink]/PlayerNumber` = 1–4 sets `0x31 = 0x02` and claims that
number specifically. On conflict, report it and fall back to observer — the user
asked for a specific number, so silently taking a different one is worse than not
serving.

**Consume-only mode.** Passive NFS access works with no announcement at all (F11,
F12): the export's access list is the whole link-local subnet. So when serving is
off or unavailable, we can still browse. But status is **unicast to announced
peers only** (F21) — 1507 status packets in one session, not one to an
unannounced host — so passive mode cannot see slot state and must probe both
slots with `MNT` speculatively. Announced mode is strictly better and is the
default whenever the feature is on.

## B4. Serving: media detection and slots

`ProLinkMediaWatcher` decides what exists. Everything else in `server/` follows
from it.

**Detection.** A volume qualifies if `PIONEER/rekordbox/export.pdb` exists and
parses. Enumerate with `QStorageInfo::mountedVolumes()` filtered to
`isReady() && isValid()`, plus a `QFileSystemWatcher` on the mount parents
(`/media/$USER`, `/run/media/$USER`, `/Volumes`) and a 2 s poll as backstop —
`QFileSystemWatcher` misses some mount events on Linux and gives nothing useful on
macOS. Reuse the platform paths from `rekordboxfeature.cpp:177-234` rather than
inventing a second list.

**Slot assignment.** First qualifying volume → **USB (slot 3)**, second → **SD
(slot 2)**, by mount order. On eject the slot frees; the next insert takes the
lowest free slot, USB first. A third volume is ignored and named in the status
view — silently dropping a stick the DJ just plugged in is the kind of thing that
gets discovered during a set.

USB-first because a single medium is what the hardware sessions exercised
(F24 asked `target=3, slot=3`), and because `/C/` is the export both reference
clients hardcode.

**Reporting insert and eject.** Three things change together, and all three
matter:

| | On insert | On eject |
|---|---|---|
| Status `0x6f`/`0x73` | set present | set empty — **this is the only place media presence is advertised** (F20) |
| Media query `0x05` | answer `0x06` with true track and playlist counts (F24) | answer with the slot empty |
| NFS handles | mint from the new tree | **invalidate — every stale handle is `NFSERR_STALE`** (E8) |

Also drop that slot's dbserver menu cache and any open result sets. A deck asks
the media query **once, when it first browses a slot** (F37) — it does not poll —
so the status byte flipping is the entire trigger for a re-query. Get that wrong
and the deck shows a medium that is no longer there, or refuses one that is.

**The Mixxx library is never served.** The served content is the volume's own
`export.pdb`, its own ANLZ files and its own audio, read straight off the stick.
No Mixxx collection code is involved, which is why `server/` can honour the
"no `src/library/` include" boundary without contortion.

## B5. Serving: the four servers

What a device must do to be browsable, in the order a deck exercises it — the
complete list, learned in Phase A by getting each one wrong in turn
(`PROTOCOL.md` §6):

1. **Announce** on 50000 — B3.
2. **Emit status** on 50002, unicast per peer every ~200 ms, slot bytes set.
   Built from a **captured 284-byte skeleton** with only understood fields
   substituted (F23): across 749 consecutive packets from an idle deck only six
   bytes ever changed, so reproducing the ~270 unknown ones exactly is the
   difference between plausible and indistinguishable. Ship the skeleton as a
   `constexpr uint8_t[284]` with a comment citing the capture it came from.
3. **Answer media queries** `0x05` → `0x06` with true counts (F24).
4. **Answer the port query** on TCP 12523 — fixed 19-byte query, 2-byte reply.
5. **Serve dbserver** on TCP 1051. **Never answer an unknown request with
   `0x4003`** (F25) — a deck that gets an error fetches the root menu and
   disconnects without opening anything. `0x3e03`, `0x3100` and `0x3d03` must all
   be acknowledged; `0x0001` MENU_CLOSE draws no reply at all and must not
   discard state.
6. **Serve NFS** — portmap + mountd + nfsd, keying filehandles on their **first
   12 bytes** (F28).
7. **Answer `0x35` → `0x36`** for LOAD SETTINGS from `PIONEER/MYSETTING.DAT`
   (F38). Not a file read: the deck mounts the export, reads nothing, and asks
   here instead.

An error and an empty folder are indistinguishable on a CDJ's screen, so the set
of menu types implemented is a **user-visible surface**, not an internal detail.

### The privileged-port problem

**A real CDJ calls portmap `GETPORT` against us** for both mountd and nfsd
(F24) — so **UDP/111 must be bound**, and 111 is a privileged port. mountd and
nfsd can sit on any ephemeral port, because we report them through portmap; 111
is the only fixed one. The PoC ran under `sudo`; Mixxx will not.

| Platform | Approach |
|---|---|
| **Linux (the Pi — the actual target)** | `sysctl net.ipv4.ip_unprivileged_port_start=111`, set in TriMiXxX's provisioning. Least invasive: no capabilities on the binary, survives package upgrades. Alternative: `setcap cap_net_bind_service=+ep` on the mixxx binary — but that is lost on every reinstall and disables `LD_LIBRARY_PATH`, which breaks some builds |
| **macOS** | Ports < 1024 need root and there are no capabilities. Serving is **not supported**; consume works fully. Also note macOS may already run `rpcbind` on 111 |
| **Windows** | No privileged-port restriction; untested, not a target |

**Bind 111 last, and degrade cleanly.** If it fails, keep discovery, the virtual
CDJ, status and the whole consume side running, and put a specific explanatory
message in the feature's root view naming the sysctl. **Never a `QMessageBox`**
(see the comment at `library.cpp:170-172`). A feature that refuses to start
because of one socket is worse than one that starts degraded and says so.

### The dbserver surface

The largest single piece of the port — `net/dbserverd.py` is 1199 lines. What it
must implement, all confirmed working in Phase A:

- **Root menu: eleven of twelve categories**, with ids listed explicitly and
  **not derived**. Two different derivations were tried in Phase A and each was
  wrong for a different category (F26, F40, F43). `FOLDER` is deliberately not
  served (unanalysed files by directory, track type 2).
- **Drill-down as a grid**: `0x1000 | depth << 8 | category`, thirteen observed
  types from one formula (F42). Chains differ per category.
- **`ALL` entries** — id `0xffffffff`, type `0xa0` — but only when there is more
  than one entry.
- **KEY has an extra level**: three harmonic tolerances, same key id, differing
  only in argument 0 (F44).
- **All twelve sorts**, and the sort **selects the item's second column**: item
  type is `(column field type << 8) | 0x04`. Numeric columns send an *empty*
  label and put the raw number in argument 0.
- **Search** — `[descriptor, sort, byte length, text, 0]`, argument **3** is the
  text; one request per keystroke.
- **Concurrent menus keyed on `(descriptor, item count)`** — the count alone
  collides (F27/F41).
- **Metadata: thirteen items**, each carrying the id of the row it *references*,
  with the artwork id on the title item.
- **Track info: six items**, where argument 0 of the path item is the **file
  size** and item 1 is the **container** from pdb offset `0x5a` (F31/F34/F35).
- **Analysis blobs are transformed, never forwarded** (F30): file is big-endian,
  wire is little-endian, and three of five change layout too. `0x2504`, the MP3
  VBR seek index, **gates playback** — without it a deck resolves the path
  perfectly and then issues no READ at all.
- **The opaque fifth prefix word must be non-zero** (F33) or the main waveform
  does not draw. Emit a monotonic counter of the same shape. We do not know what
  it means; that is recorded, not resolved.
- **Camelot keys sorted numerically** (`1A 2A … 12A`), the one deliberate
  divergence from the hardware, which text-sorts them into nonsense. Server-side
  only, so there is no interoperability cost.

## B6. Consuming: the sidebar

```
ProLink
├─ 1 · CDJ-2000nexus
│  ├─ USB → All Tracks, Friday Warmup, Techno ▸ Peak Time
│  └─ SD  → All Tracks
└─ 2 · CDJ-2000nexus  (offline)
```

Device → slot rather than flattened, because SD and USB are genuinely different
libraries. Keep the slot level even when only one slot is populated — consistency
beats cleverness in a tree navigated under time pressure.

**Our own served media never appear here.** We are in our own discovery table;
filter ourselves out by device number.

`TreeItem` payload follows the Rekordbox `QList<QString>` convention
(`rekordboxfeature.cpp:196`) but with an explicit kind tag (`device`/`slot`/
`playlist`) instead of the two magic `IS_RECORDBOX_DEVICE` sentinels. Unlike
`RekordboxFeature::activateChild` (`:1563`) we do **not** mutate node data to flip
device→playlist after the first parse — `ProLinkMedia::state` carries that, which
keeps tree data immutable and kills the "re-activating a re-mounted device does
nothing" bug class.

**Two-tier timeout** — the critical difference from a USB mount. A CDJ can blip off
the network for 2 s (cable jiggle, switch STP re-convergence) and come straight
back; tearing down the tree, the DB rows and the cache on every blip would be
infuriating. Note the timeout arithmetic changed with C12: keep-alive is 2.0026 s,
not 1.5 s, so 10 s is **five** missed keep-alives, not six or seven.

| Event | At | Action |
|---|---|---|
| Keep-alive stops | 10 s | Label `" (offline)"`, unbold, repaint. **Rows, DB rows and cache all stay** |
| Keep-alive returns | any | Restore label. Zero re-parse, zero refetch |
| Still gone | 60 s | Remove row, clear DB rows, unpin cache |
| `[ProLink],refresh` | — | Remove offline devices immediately |

Append new devices at `pRoot->childRows()` rather than inserting at 0, so a user
browsing player 1 is not yanked when player 2 powers on. Call
`clearLastRightClickedIndex()` before **every** structural change:
`BaseExternalLibraryFeature` holds a raw `QModelIndex` whose `internalPointer()` Qt
cannot fix up (`baseexternallibraryfeature.h:57-58`).

**Slot presence.** In announced mode, read it from the peer's status packets at
`0x6f`/`0x73` — the only place it is published (F20). Announced mode is the
default, so this is the normal path; the passive fallback speculatively `MNT`s
both slots.

## B7. Consuming: track load and the boot-scoped cache

1. Double-click → `WTrackTableView::slotMouseDoubleClicked` (`wtracktableview.cpp:407`)
   → `getTrack(index)` (`:429`).
2. `ProLinkPlaylistModel::getTrack` **snapshots every field value**, then checks
   `QFile::exists(location)`.
3. Miss → `ProLinkTrackFetcher::fetchBlocking({audio, .DAT, .EXT})`: a modal
   `DlgProLinkFetch` with progress and Cancel, spinning a nested `QEventLoop`. The
   ANLZ requests are `required = false` — a missing one costs the beatgrid, not the
   load.
4. Net thread: cached root fhandle → `LOOKUP` → `NfsFileTransfer` with 4 in-flight
   READs, out-of-order reassembly by offset, writing `<local>.part`, then
   **atomic rename**. Atomicity is mandatory: `SoundSource::getTypeFromFile`
   (`soundsource.cpp:56`) uses `QMimeDatabase::MatchContent` — it *reads bytes*, so a
   half-written file would be classified as unsupported.
5. `getOrAddTrack` (`trackcollectionmanager.cpp:471`) → `addTracksAddFile`
   (`trackdao.cpp:838`) → a real `TrackPointer`.
6. Apply the MP3 timing offset and the ANLZ beatgrid/cues (same semantics as
   `rekordboxfeature.cpp:1288-1301`), tagged with the rekordbox beats subversion so
   the analyzer will not overwrite the grid.

**READ size.** Real CDJs use 8192-byte reads, the NFSv2 maximum, relying on IP
fragmentation (F19). Our PoC client defaults to 1280 to stay under the MTU —
safe, measured at 1459 KiB/s, but 6.4× the round trips the hardware uses. Ship
1280 as the default with `[ProLink]/ReadSizeBytes` to raise it; a switched
100 Mbit link shared with the CDJs' own linked playback is the environment, and
being modest there is deliberate.

**Cache layout** — mirror the CDJ's tree 1:1, so pdb-relative paths concatenate
verbatim exactly as `insertTrack` already does (`rekordboxfeature.cpp:371`), with
zero path-translation logic:

```
<cacheRoot>/<mediaKey>/
    .meta.json                      { label, originMac, slot, pdbSha1 }
    PIONEER/rekordbox/export.pdb
    PIONEER/USBANLZ/P016/0000875E/ANLZ0000.{DAT,EXT}
    Contents/Artist/Album/Track.mp3
```

**`cacheRoot` is boot-scoped and purged at startup**, per the requirement. Use
`QStandardPaths::CacheLocation` + `/prolink/`, not the settings dir — it is the
directory the platform already understands to be disposable, and it keeps a
multi-gigabyte scratch area out of the user's Mixxx profile.

This removes a lot of machinery the previous revision needed: no LRU, no 4 GB
budget, no 30-day sweep, no pinning across sessions. What replaces it is one
startup step and one in-session rule:

- **At startup, before anything else:** delete `cacheRoot` recursively, then call
  `TrackCollection::purgeAllTracks(QDir(cacheRoot))` (`trackcollection.h:159`).
  This is not optional. `getOrAddTrack` writes cache files into `library` and
  `track_locations` for real, so without the purge **every ProLink track ever
  loaded becomes a Missing Track on the next boot**, and the count grows without
  bound. One call, at a point where nothing holds those tracks, and the problem
  does not exist. This is a strict improvement on the previous plan's
  "never auto-evict a medium that produced rows this session and document it",
  which merely deferred the mess.
- **Within a session, nothing is evicted.** A DJ who loaded a track twenty
  minutes ago can still reload it after the source CDJ has left the network,
  which is the real advantage of copy-then-play over streaming.

`mediaKey = stable_digest(export.pdb)[0:16]` — **content-addressed, not keyed on
`(mac, slot)`** — hashed over a *stabilised* copy. A player rewrites its own
bookkeeping in the pdb header as it operates (`unknown1` at `0x10`, the write
counter `sequence` at `0x14`), so a raw digest changes whenever a play count is
written; zero `0x10..0x18` before hashing (F13, `prolinks_poc.proto.pdb.stable_digest`).
Two CDJs playing off clones of the same USB then share one cache entry — exactly
this two-deck rig — and swapping media yields a new key naturally. The
chicken-and-egg (we can only hash after downloading) resolves by fetching to
`.incoming/<uuid>.pdb`, hashing, then renaming; if the target dir already exists,
discard the download, which *is* the two-CDJs-one-USB fast path.

Prefetch ANLZ for the first ~100 rows on playlist activation (~5 MB, makes
waveforms instant); **never prefetch audio by default** — a 10 MB pull per
arrow-key press would saturate the link the CDJs are themselves playing over.

| Failure | Behaviour |
|---|---|
| CDJ vanishes mid-fetch | 5 × 2 s retries → `fileFailed`, `.part` deleted, `getTrack` returns null, node goes offline |
| User cancels | `abort()`, `.part` deleted, nothing loads |
| Media swapped mid-session | `NFSERR_STALE` → `invalidate(slot)` → re-`MNT` → retry once; then treat as `mediaGone` |
| **CDJ vanishes *after* load** | **Playback continues from the cache file** |
| Disk full | short write → `fileFailed("disk full")` |
| ANLZ missing/corrupt | Track loads without a beatgrid; Mixxx analyzes it normally |

## B8. Registration and settings

- **`CMakeLists.txt`** — `option(PROLINK ... ON)` + `__PROLINK__`, mirroring
  `ENGINEPRIME` (`:2438`, `:2549`); a guarded `target_sources` block; the new
  `prolink_protocol` static target for generated Kaitai output; test files.
  **No dependency changes:** `Network` is already in `QT_COMPONENTS` (`:2799`) and
  PUBLIC-linked to `mixxx-lib` (`:2850`); `rekordbox_metadata` and `Kaitai` are
  already linked (`:2690`, `:2705`).
- **`src/library/library.cpp`** — `#ifdef __PROLINK__` +
  `addFeature(new ProLinkFeature(this, m_pConfig))` guarded on
  `ConfigKey("[Library]", "ShowProLinkLibrary")`, after the Serato block (~`:208`).
- **`res/images/library/ic_library_prolink.svg`** + a `res/mixxx.qrc` entry. The
  name is load-bearing: `LibraryFeature`'s ctor builds
  `":/images/library/ic_library_%1.svg"` (`libraryfeature.cpp:18`).
  **Do not use Pioneer's Pro DJ Link logo — trademark.** A generic linked-players
  glyph.
- **Preferences.** The previous revision argued for no preferences page. That no
  longer holds: "act as a CDJ on the network" and "which player number" are
  decisions a user must be able to make deliberately and see the result of, and
  burying them in `mixxx.cfg` for a feature that *transmits on a network shared
  with live equipment* is the wrong default. Add a small **ProLink page** with
  exactly four controls plus a status area:

  | Control | Default |
  |---|---|
  | ☐ Enable Pro DJ Link | off |
  | ☐ Act as a player (share mounted rekordbox media) | off |
  | Player number: `AUTO ▾ / 1 / 2 / 3 / 4` | AUTO |
  | Network interface: `Auto ▾` | Auto |
  | *Status:* number held, peers seen, media served, and any degradation (port 111, no free number) | — |

  The status area is the part that earns the page. Everything in this feature
  that goes wrong goes wrong silently — a taken number, an unbindable socket, a
  third USB ignored — and a DJ needs to see that before the set, not during it.

- **Remaining `[ProLink]` config keys**, no UI: `CacheDir`, `ReadSizeBytes`=1280,
  `DeviceTimeoutMs`=10000, `DeviceRemovalGraceMs`=60000, `PrefetchAnalysis`=1,
  `PrefetchAudioOnSelect`=0, `PortmapPort`=111, `DeviceName`="TriMiXxX".
- **Controls**, matching this fork's own `[Rekordbox],refresh` precedent
  (`rekordboxfeature.cpp:1339`): `[ProLink],refresh`, and read-only
  `[ProLink],device_count`, `[ProLink],player_number`, `[ProLink],serving`.
  A read-only `serving` control is what lets the TriMiXxX skin show a link
  indicator without any new plumbing.
- Drive-by while in `dlgpreflibrary.cpp`: `checkBox_show_serato` is missing from
  `slotResetToDefaults` today.

**Hand-roll the XDR; do not link libnfs or libtirpc.** libnfs is NFSv3/v4-centric
and encodes `LOOKUP` names and mount paths as ASCII, but Pioneer uses
**length-prefixed UTF-16LE** — using it means patching its wire encoder. libtirpc
is effectively Linux-only with a blocking API that fits a Qt event loop badly.
Both would add a `find_package` plus packaging changes on five platforms for a
handful of procedures. And neither helps at all on the side that matters most
here: **we are also an RPC *server*, which is not what either library is for.**

## B9. Duplicated Rekordbox glue

Per the decision to duplicate now and extract later, copy into
`src/library/prolink/prolinkpdbimport.{h,cpp}`, written correctly from the start
(detached trees, `UNIQUE(device, rb_id)`): the pdb table walk
(`rekordboxfeature.cpp:502-651`), `insertTrack` (`:353`), `buildPlaylistTree`
(`:656`), `readAnalyze` (`:874`), `setHotCue` (`:839`), `colorFromID` (`:331`),
`getText` and the UTF-16 helpers (`:254-295`), and the MP3 timing-offset table
(`:1248`).

Reused without copying: the Kaitai types in `lib/rekordbox-metadata/`, already a
shared static library, used by **both** sides — the consume side parses a
downloaded pdb, the serve side parses the mounted stick's.

The `analyze_path` column plumbing is reusable as-is — `ColumnCache` keys on the
plain column name (`trackschema.h:76`, `columncache.h:66`), so a `prolink_library`
table with an `analyze_path` column gets it for free. **No `columncache` changes.**

Leave a `TODO(prolink)` at the top of `prolinkpdbimport.cpp` naming the eventual
extraction (`rekordboxanlz.*` + `rekordboxpdbimporter.*`, parameterised by table
names), so the deferred refactor is discoverable rather than folklore.

**`getText` is one to port carefully, not copy.** The PioString UTF-16 form is
little-endian from `offset + 4`, not big-endian from `offset + 3` (O6). Our own
code had the latter, and the two errors **cancel exactly for ASCII** — a
692-track library parsed cleanly and only non-ASCII names came out as mojibake.
Round-trip tests cannot catch this class of bug; the test must pin literal bytes
from a real pdb against the filesystem's own spelling of the same name.

## B10. Build order

Each step is independently testable, and the two directions are interleaved so
that neither sits unverified for long.

| Step | Deliverable | Verified by |
|---|---|---|
| 1 | B0 Rekordbox bug fixes | Existing USB flow unchanged |
| 2 | **`.ksy` × 4 + generated readers + `wire/*` writers + golden vectors** | Offline, against the 31-pcap corpus. No Mixxx integration at all — the biggest de-risking step and the one that pays for Kaitai |
| 3 | `xdrbuffer`, `rpcclient`, `portmapclient`, `nfsv2client`, `nfsfiletransfer` | Pull `export.pdb` off a real NXS, SHA-256 against the ejected stick |
| 4 | `prolinkdiscovery`, `prolinkdevice`, `prolinknetworkservice` | Log discovered peers with numbers, names, MACs |
| 5 | `prolinkvirtualcdj` + `prolinkstatus` emit | **Both CDJs list us on their LINK screen.** Diff our keep-alive byte-for-byte against a real one |
| 6 | `prolinkcachemanager` + `prolinkfeature` skeleton (tree only) | **CDJs appear and disappear in the sidebar** |
| 7 | Consume pdb pipeline → `ProLinkPlaylistModel` | **Browse a CDJ's playlists** |
| 8 | `prolinktrackfetcher` + `dlgprolinkfetch` + `getTrack()` | **Load a CDJ's track to a deck**, with beatgrid and hot cues |
| 9 | `prolinkmediawatcher` + `prolinkmediaquery` | **A deck offers our USB as a LINK source** (it will not open yet) |
| 10 | `prolinkvfs` + `prolinknfsserver` (incl. the port-111 degradation path) | A deck `MNT`s us; `pull-db` from the PoC round-trips our own export |
| 11 | `dbserver/` — sessions, root menu, drills, sorts, search | **A deck browses our library**: 11 categories, all 12 sorts |
| 12 | `prolinkanalysiswire` + track info + metadata | **A deck loads and plays a track from us**, with cues and both waveforms |
| 13 | `prolinkmediaquery` settings `0x35`/`0x36` | LOAD SETTINGS from our medium |
| 14 | Registration: CMake, `library.cpp`, qrc, icon, prefs page, controls | Ship |
| 15 | Polish: eject/insert edge cases, `[ProLink],refresh`, shutdown ordering | — |

Steps 9–13 have a natural incremental signal that is worth exploiting: **run the
PoC's own client against the C++ server** at each one. `prolinks db-browse` and
`prolinks pull-db` already exercise every request a deck makes, over the same
codecs, with a passivity guard — so most serve-side bugs can be found on a laptop
before a CDJ is switched on.

---

# Verification

## Offline (no hardware)

- **Golden vectors from the Phase A corpus**, checked in as hex literals:
  `prolink_djl_test.cpp` (a real `0x06` keep-alive → number, name, MAC, IP, and
  the byte-`0x25` "was I first" latch), `prolink_status_test.cpp` (the 284-byte
  skeleton and the six bytes that move), `prolink_dbserver_test.cpp` (the omitted
  empty blob, the two tag numberings, UTF-16BE character counts),
  `prolink_xdr_test.cpp` (UTF-16LE `MNT("/C/")` byte-for-byte; a length field of
  `0xFFFFFFFF` must be rejected **without allocating**).
- **Round-trip per writer:** `build(x) → Kaitai parse → compare fields`, over
  every message in the corpus.
- **Cross-implementation diff:** the same `.ksy` compiled `--target python`,
  parsing the same corpus in `prolinks-compat/tests/`, must agree with the
  hand-written PoC codecs field-for-field. Any disagreement is a bug in exactly
  one of three places and the other two localise it.
- **The PoC client against the C++ server** on loopback: `db-browse`, `pull-db`,
  `tracks`, all under `--assert-passive`.
- `cmake -DPROLINK=OFF` still builds and links. Mounted Rekordbox USB browsing
  still works after B0.

## On hardware

The two CDJ-2000NXS, per `docs/HARDWARE.md`.

**Consume:** CDJ powers on → appears in the sidebar within ~2 s; expand → USB
slot → playlists; double-click → progress dialog → the track loads with the
correct beatgrid and hot cues; **pull the Ethernet cable mid-fetch** → clean
failure, no crash; pull it *after* load → playback continues.

**Serve:** the Phase A checklist, re-run against the C++ implementation — deck
lists us; 11 categories open; drill-downs; ALL entries; search filters as you
type; all twelve sorts with the sorted field as column two; harmonic key
matching; two media as USB + SD; load and play MP3/AAC/WAV/AIFF including a 75 MB
lossless; artwork; hot cues; both waveforms; LOAD SETTINGS. Capture every run and
diff the dbserver request/response stream against the Phase A pcaps —
**the S23 capture is the acceptance criterion: 568 requests, zero errors.**

**Both at once**, which Phase A never tested: Mixxx serving its stick to deck B
while browsing deck A's. This is the configuration TriMiXxX actually ships in and
the one where the two-thread split earns its keep. Watch specifically for
keep-alive jitter under NFS load — a missed heartbeat here is a disappearance
from the network mid-set.

**Eject/insert**, also new: eject while a deck is browsing us (expect the menu to
empty, not the deck to hang); re-insert (expect it to reappear without a restart);
plug a third stick (expect it ignored and named in the status view).

---

## Risks

**R1 (highest) — the nested event loop in `getTrack()`.** `TrackModel::getTrack` is
`const` and called synchronously from the view; there is no asynchronous path
(returning null and loading later would bypass the `loadTrack`/`PlayerManager`
chain and break Auto DJ, samplers, preview decks and controller mappings alike).
Re-entering the Qt event loop from inside a const model method the view is
mid-call on is inherently hazardous: during the loop the user can click another
feature, the device can vanish, and `index` becomes dangling. Mitigation is
threefold and non-optional: (a) snapshot every field before spinning; (b) pin the
medium so removal is deferred; (c) never touch `index` after the loop — which
means deliberately inlining `BaseExternalPlaylistModel::getTrack`'s body (`:35-73`)
against the snapshot rather than calling it, with a comment explaining why.
*Test:* pull the cable during a fetch.

**R2 (new, serve-side) — we now transmit on a network carrying a live set.** This
is a categorically different risk from anything in the consume-only plan. A bug in
the claim chain can take a number a playing deck is using; a malformed keep-alive
can confuse a peer; a status packet with the wrong device number can make a deck
show the wrong source. Mitigations: default **off**; watch ≥ 2.5 s before
claiming; never claim a number seen in use; back off immediately on `0x08` or
`0x05`; refuse to serve rather than contend when 1–4 are full; and a `--dry-run`
equivalent (`[ProLink]/EnableTransmit`=0) that runs the full state machine and
logs what it *would* send. Phase A's discipline of diffing every emitted packet
byte-for-byte against a real one applies unchanged.

**R3 — port 111 cannot be bound.** Covered in B5. Serving degrades to unavailable
with a specific message; everything else keeps working. The failure must be
*visible*, because the symptom otherwise is "the deck sees us but nothing opens",
which looks like a dozen other bugs.

**R4 — binding UDP 50000 may fail** (rekordbox, prolink-tools, or another Mixxx
instance already holds it). Use `QUdpSocket::ShareAddress | ReuseAddressHint`;
semantics differ across macOS (`SO_REUSEPORT`) and Windows (`SO_REUSEADDR`).
Degrade to the explanatory root view.

**R5 — multi-homed hosts.** The Pi has `eth0` (CDJ network, 169.254/16 link-local)
and `wlan0`. A broadcast keep-alive can arrive on either; every socket must bind a
source address on the *same* subnet as the peer, or link-local routing silently
picks the wrong NIC and every RPC times out. Record the receiving interface per
device in `ProLinkDiscovery`; `[ProLink]/NetworkInterface` is the manual override.
This bit us in Phase A on macOS (`IP_BOUND_IF`) and it will bite again.

**R6 — shutdown ordering.** `~ProLinkFeature` must, in order: (1) stop serving and
send nothing further; (2) `m_pNetwork->shutdown()` — quit and wait **both** threads,
so no queued signal lands on a half-destroyed feature; (3) `waitForFinished()` the
parse future (the Rekordbox precedent, `:1417`); (4) drop the temp tables.
Backwards produces a shutdown crash that only reproduces with a CDJ on the network.
With two threads there is now also a **join order**: serve thread first (it holds
file handles into a volume that may be unmounting), then net thread.

**R7 — NFSv2 32-bit offsets.** `READ` offset and `fattr.size` are `uint32`, a hard
4 GiB ceiling, in both directions. Fine for audio; assert and fail cleanly rather
than wrapping.

**R8 — macOS sandbox.** The boot-scoped cache under `QStandardPaths::CacheLocation`
needs no `Sandbox::askForAccess`. Serving a mounted volume does — apply the same
guard Rekordbox uses at `rekordboxfeature.cpp:495` before reading a stick.

**R9 — scale of the port.** The PoC is ~9.4k lines of Python excluding the CLI and
capture tooling, heavily commented; `net/dbserverd.py` alone is 1199. Expect the
same order in C++, dominated by the dbserver serve path and the NFS server. This
is not a weekend. Sequence it so that steps 1–8 (consume, complete) is a shippable
milestone on its own, with 9–13 (serve) following as a second.

**R10 — unknowns we ship.** Seven values in `PROTOCOL.md §9` are reproduced
without being understood, including one (the fifth prefix word) that must be
non-zero for the waveform to draw. Port them **as constants with their finding
number in a comment**, never "cleaned up" to a plausible zero. Sending a plausible
zero broke playback twice in Phase A. A reviewer will want to delete these; the
comment is what stops them.

---

## Upstreaming reality check

The previous revision assumed a consume-only first PR. Two-way changes that.

A feature that impersonates a CDJ, claims a device number on a network of
professional equipment, and runs an NFS server on a privileged port is a much
larger thing to ask a maintainer to own than "read a rekordbox USB over the
network". It is also, unavoidably, a reverse-engineered implementation of a
vendor protocol — with the trademark care that implies (no Pioneer logo, no
"Pro DJ Link" branding in the UI beyond the plain-language name).

The honest sequence:

1. **B0 upstream first**, standalone. Two real bug fixes in existing code, no new
   surface, useful to Mixxx regardless of anything else here. This also establishes
   the working relationship before the large thing arrives.
2. **Build everything in the fork.** TriMiXxX is the customer; it ships from the
   fork either way.
3. **Offer the consume side upstream** when it is proven — it is the half with a
   clear user story ("browse the CDJ's USB from Mixxx"), no transmission by
   default, and no privileged sockets.
4. **Serve last, if at all**, default-off, behind its own CMake option, with the
   provenance trail (`docs/PROTOCOL.md`, `docs/FINDINGS.md`, the `.ksy` files with
   per-field finding citations) as the argument that it was built cleanly.

Nothing in the build order depends on any of this being accepted.

---

## Decision log

- **Serve in v1, not a later phase** *(changed)* — Phase A proved both directions
  against hardware, so the risk that justified deferring is gone. Keeping the
  split would mean designing seams for a thing already known to work.
- **Kaitai for parsing, hand-written writers** *(new)* — forced: the C++ target
  cannot serialize. Taken as an opportunity to make the `.ksy` an executable
  spec and to generate a Python reference encoder for cross-checking, rather
  than as a workaround.
- **`.ksy` sources live in prolinks-compat, generated output checked into Mixxx**
  *(new)* — follows the `rekordbox_metadata` precedent exactly and keeps the JVM
  out of the build; the Python test suite consumes the same sources.
- **Boot-scoped cache with a startup `purgeAllTracks`** *(changed)* — the
  requirement asks for clearing at reboot, which deletes the LRU/budget/sweep
  machinery outright and, with the purge call, *solves* the Missing-Tracks
  side-effect the previous plan could only document.
- **No dbserver client** — the consume side parses `export.pdb` itself, reusing
  Mixxx's Kaitai parser, and NFS is the only path to the audio bytes anyway.
  A dbserver client would be a second way to read the same data.
- **Two net threads** *(new)* — a blocking read off a USB stick must never delay a
  keep-alive; five missed keep-alives is a disappearance from the network.
- **A preferences page after all** *(changed)* — the previous "config keys only"
  call was right for a passive reader and wrong for something that transmits.
  The status area is the real content.
- **AUTO numbering restricted to 1–4** — that is the range real players occupy and
  the LINK screen enumerates; serving as 3 is confirmed working. Above 4, degrade
  to consume-only at 7 rather than guess.
- **Cache to disk, not a streaming `SoundSource`** — a streaming provider would
  require changing `SoundSourceProxy`'s extension-based dispatch and `Track`'s
  `FileInfo` assumption, i.e. core changes that will not land upstream. Copy-then-play
  also means playback survives the source CDJ leaving the network.
- **Serve by streaming, not by copying** — the opposite call, for the opposite
  reason: a deck expects random-access reads with low latency and touches ~38% of
  a file during a load (F18). Copying would add latency to the one path where a
  stall is an audio dropout on someone else's deck.
- **Duplicate the Rekordbox glue now, extract later** — keeps the working Rekordbox
  feature untouched and avoids rebase friction with the local commits `7ce93c7` and
  `e5063fc`. Accepted cost: pdb fixes must be applied twice until the extraction lands.
- **Fix both Rekordbox bugs anyway** — they are real bugs today, and bug 2 breaks
  this specific two-CDJ rig.
- **Python PoC before C++** — vindicated. Five separate protocol facts were got
  wrong and corrected against hardware during Phase A (F26/F40, F27/F41,
  F31/F34/F35, F29/F30, O6); every one of those iterations would have been a
  rebuild-and-redeploy cycle in C++ on a Pi.
