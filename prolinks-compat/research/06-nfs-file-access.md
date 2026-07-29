# 06 — ProLink NFS / RPC File-Access Path

> **⚠ Pre-hardware document.** Written from published reverse-engineering
> literature before any capture from real CDJs existed. Much of it has since
> been confirmed, and a good deal corrected, by testing against two
> CDJ-2000NXS. **`docs/PROTOCOL.md` is the current specification; where this
> document disagrees with it, this document is wrong.** `docs/FINDINGS.md`
> records each correction with its evidence.

How a Pioneer player reads raw files directly off another device over NFS
(rekordbox `export.pdb`, `ANLZ` analysis files, album art, and audio), and
what it takes to serve files the same way for **prolinks-compat**.

Primary source: `libcdj/doc/cdj-nfs.md`.
Reference implementations cross-checked:
`python-prodj-link/prodj/network/{nfsclient,nfsdownload,rpcreceiver,packets_nfs}.py`,
`prolink-connect/src/nfs/{index,programs,rpc,xdr,utils}.ts`.
Protocol-context source: `dysentery/doc/modules/ROOT/pages/{startup,media}.adoc`.

Markers: **(confirmed)** = observed in capture / asserted by primary doc or two
independent implementations. **(inferred)** = derived from RPC/NFSv2 standards
or single-implementation behavior, not independently verified here.

---

## 1. Why NFS — what it is for, and when it beats the dbserver

A standalone CDJ runs a classic Sun/ONC RPC stack (portmapper + `mountd` +
`nfsd`) and **exports the contents of its loaded SD/USB media over NFS v2 / UDP**
(confirmed — `libcdj/doc/cdj-nfs.md` lines 3-9; `rpcinfo` shows programs
`100003` nfs v2/udp, `100005` mountd v1/udp, `100000` portmapper v2/udp).

What clients pull over this path:

- **`export.pdb`** — the rekordbox database for the media. Fetching this file
  and parsing it locally (crate-digger style) gives the *entire* library
  (tracks, playlists, artists, albums, keys, colors) without issuing per-track
  queries to the metadata `dbserver` (TCP 1051). (confirmed — dysentery
  `startup.adoc:468`: "NFS access to the rekordbox databases on mounted USB
  media does work … allowing passive implementations to fetch track metadata
  directly without sending announcement packets.")
- **`ANLZ####.DAT` / `.EXT` / `.2EX`** — per-track analysis files (beat grid,
  cue points, waveform preview/detail, color waveform, phrase data). (confirmed
  — these live under `/PIONEER/USBANLZ/...` on the same media that NFS exports;
  the file-fetch primitive is path-generic.)
- **Album artwork** — `/PIONEER/Artwork/...` image files. (confirmed — generic
  file fetch; both reference clients expose `fetchFile(path)` with no
  file-type restriction.)
- **Audio** — the actual track audio files can be read over the same READ
  primitive for features that need raw samples. (inferred — NFS READ is
  byte-range generic; large/slow but possible. No reference client streams
  audio this way by default.)

**When NFS is preferred over the dbserver:**

| Situation | Path | Why |
|---|---|---|
| Full library / playlists / bulk metadata | **NFS** (`export.pdb`) | One file download → parse locally; no per-item round trips, no dbserver query limits (confirmed) |
| Waveforms, beat grids, cues, phrases | **NFS** (ANLZ files) | These are *only* available as files; dbserver returns metadata, not full analysis (confirmed) |
| Artwork bitmaps | **NFS** (Artwork dir) | File fetch (confirmed) |
| Single live "what is track N" lookup | dbserver (TCP 1051) | Lighter for one-off; but NFS works too |
| Passive / observe-only implementations | **NFS** | dbserver requires you to announce yourself as a device; NFS lets a passive listener pull metadata silently (confirmed — `startup.adoc:468`) |

**Generations / devices that expose NFS:** standalone CDJ-2000 / 2000nexus /
2000NXS2 / CDJ-900-class players export NFS (confirmed — primary doc captured an
"XDJ" at `192.168.1.59`: `libcdj/doc/cdj-nfs.md:5`). The XDJ-XZ and Opus Quad
also serve NFS over their USB-to-host virtual-ethernet interface (confirmed —
`startup.adoc:462-472`). The protocol is NFS **version 2 over UDP** specifically
(confirmed — `libcdj/doc/cdj-nfs.md:3,11`: "Ubuntu does not seem to support
version 2", forcing `--nfs-version 2` to interoperate).

---

## 2. The RPC stack — ONC / Sun RPC

Transport is **ONC RPC v2 (RFC 1057) over UDP**, NFS payload is **NFS v2
(RFC 1094)**. Each request is a single UDP datagram; each reply is a single UDP
datagram, correlated by a 32-bit XID.

### RPC programs and ports

| Program | Number | Version | Port | Transport | Source |
|---|---|---|---|---|---|
| portmapper | `100000` | 2 | **111** (fixed) | UDP | confirmed (`packets_nfs.py:62-63,79`; `xdr.ts:179-180`; doc:9) |
| mountd | `100005` | **1** | dynamic (via portmap) | UDP | confirmed (`packets_nfs.py:81,115`; `xdr.ts:198-199`; doc:8) |
| nfs | `100003` | **2** | dynamic (via portmap) | UDP | confirmed (`packets_nfs.py:80,93`; `xdr.ts:238-239`; doc:7) |

Only **portmapper is on a well-known port (111)**. The mountd and nfsd ports are
assigned dynamically and must be discovered.

### Dynamic port discovery — portmap `GETPORT`

Procedure `GETPORT` (portmap proc **3**) takes `{program, version, protocol,
port=0}` and returns a single big-endian `uint32` port number, or `0` if the
program is not registered (confirmed — `nfsclient.py:92-103` `PortmapGetPort`;
`programs.ts:22-41` `makeProgramClient`).

```
GETPORT(prog=100005, vers=1, prot=17 /*UDP*/, port=0) -> mountd UDP port
GETPORT(prog=100003, vers=2, prot=17 /*UDP*/, port=0) -> nfsd   UDP port
```

`prot` is the IP protocol number: **17 = UDP** (6 = TCP). The CDJ path always
uses UDP (confirmed — `packets_nfs.py:73-76`; `programs.ts:27`; `nfsclient.py`
calls `PortmapGetPort(..., "udp")`).

### Portmap procedure numbers (confirmed — `packets_nfs.py:64-71`)

| Proc | Name |
|---|---|
| 0 | NULL |
| 1 | SET |
| 2 | UNSET |
| **3** | **GETPORT** (the only one used) |
| 4 | DUMP |
| 5 | CALLIT |

### MOUNT program procedure numbers (program 100005, v1)

| Proc | Name | Used by clients? | Source |
|---|---|---|---|
| 0 | NULL | no | `packets_nfs.py:116-123` |
| **1** | **MNT** (mount, returns root fhandle) | yes | confirmed (`nfsclient.py:107`; `programs.ts:104`) |
| 2 | DUMP | no | `packets_nfs.py` |
| 3 | UMNT (unmount) | not used (TODO in `nfsclient.py:200`) | enumerated |
> **Corrected on hardware — C9.** Real players **do** call `UMNT`, once per slot, following the physical eject. A server should answer it.
| 4 | UMNTALL | no | enumerated |
| **5** | **EXPORT** (list exports) | yes by prolink-connect | confirmed (`xdr.ts:203`; `programs.ts:75`) |

Note: python-prodj-link mounts the export path **directly** (it hardcodes the
slot→path map and never calls EXPORT). prolink-connect first calls **EXPORT**
to enumerate available exports, then matches the slot's path before MNT
(confirmed — `index.ts:123-130`). Either approach reaches the same root
filehandle.

### NFS v2 program procedure numbers (program 100003, v2)

Full enum is present (`packets_nfs.py:94-113`); the file-read path uses only
three. NULL/GETATTR are available for liveness/attr probing.

| Proc | Name | Used in download flow | Source |
|---|---|---|---|
| 0 | NULL | (liveness) | enumerated |
| 1 | GETATTR | (attrs) | confirmed struct (`packets_nfs.py:268`) |
| 3 | ROOT | no (obsolete) | enumerated |
| **4** | **LOOKUP** (resolve a name in a dir → fhandle + attrs) | yes | confirmed (`nfsclient.py:121-126`; `programs.ts:122-153`) |
| 5 | READLINK | no | enumerated |
| **6** | **READ** (byte-range read of a file) | yes | confirmed (`nfsclient.py:136-143`; `programs.ts:191-237`) |
| 8 | WRITE | no (read-only export) | enumerated |
| 16 | READDIR | no (clients walk known paths, not listings) | enumerated |
| 17 | STATFS | no | enumerated |

### RPC authentication

Calls use **`AUTH_UNIX` (flavor 1)** credentials with a verifier of
**`AUTH_NULL` (flavor 0)** (confirmed — `nfsclient.py:70-80`; `rpc.ts:71-79`).
The CDJ-style `AUTH_UNIX` body is `{stamp, machine_name="", uid=0, gid=0,
gids=[]}`. The "stamp" is a magic constant the clients copy to look like a real
CDJ — python-prodj-link uses `0xdeadbeef` (`nfsclient.py:19`); prolink-connect
uses the observed CDJ value `0x967b8703` (`rpc.ts:17`). The servers do **not
enforce** credentials (uid 0 / empty creds are accepted), so the stamp value is
cosmetic. (confirmed values; "not enforced" inferred from both clients sending
trivial/differing creds successfully.)

---

## 3. The mount path — slot → export mapping

The CDJ exports one filesystem **per media slot**, named by a single uppercase
drive letter wrapped in slashes. Both reference implementations agree on the
mapping (confirmed):

| Physical slot | Export path | python-prodj-link | prolink-connect |
|---|---|---|---|
| **SD card** | **`/B/`** | confirmed (`nfsclient.py:25`) | confirmed (`index.ts:36`) |
| **USB drive** | **`/C/`** | confirmed (`nfsclient.py:26`) | confirmed (`index.ts:35`) |
| rekordbox (collection / RB-linked) | **`/`** | — | confirmed (`index.ts:37`, `MediaSlot.RB`) |

(`/A/` is not used by either client; presumed reserved/internal — inferred.)

The slot you want comes from the device's media-slot status (media-slot
broadcast / status packets, type `06`/status, documented in
`dysentery/.../media.adoc`). Given a target player IP + slot, the client picks
the export string from this table and mounts it.

---

## 4. The read flow

End-to-end download of one file (confirmed — `nfsclient.py:182-201`
`handle_download`, `nfsdownload.py`; mirrored in `programs.ts`/`index.ts`):

1. **Resolve ports** — two portmap `GETPORT` calls to UDP/111: one for mountd
   (`100005`/1), one for nfsd (`100003`/2).
2. **MOUNT** — `MNT(export_path)` to the mountd port. The export path is the
   slot string (`/B/`, `/C/`, `/`), **XDR-encoded as a UTF-16LE length-prefixed
   string** (see §5). Reply is `{status, fhandle}`; status 0 = OK and yields the
   **32-byte root filehandle** (confirmed — `packets_nfs.py:125-132`,
   `NfsFhandle = Bytes(32)`).
3. **LOOKUP path components** — split the requested path on `/`, and for each
   component issue NFS `LOOKUP(dir_fhandle, name)` starting from the root
   filehandle. Each reply returns the child's `{fhandle, attrs}`; feed the
   returned fhandle into the next lookup. The final lookup yields the file's
   fhandle plus `attrs.size` (the total file length to download) (confirmed —
   `nfsclient.py:128-134` `NfsLookupPath`; `programs.ts:158-185` `lookupPath`).
   Note: component names are also UTF-16LE-encoded (§5).
4. **READ in chunks** — issue NFS `READ(fhandle, offset, count, totalcount=0)`
   repeatedly until `offset == size`. Each reply contains the file attrs plus a
   length-prefixed data blob. Append blobs in offset order.

**Chunk / datagram sizing:**

- python-prodj-link requests **1280 bytes per READ** by default, chosen so the
  whole reply datagram (1280 + ~142 B NFS/RPC/UDP/IP overhead) stays under the
  1500-byte Ethernet MTU and avoids IP fragmentation (confirmed —
  `nfsclient.py:29`, comment "+ 142 bytes total overhead is still safe below
  1500"; `setDownloadChunkSize`). It also runs **up to 4 READ requests in
  flight** for throughput (window > 4 gave no gain in author's tests) with a
  2 s per-request retry and 5-retry cap (confirmed — `nfsdownload.py:28-32`).
- prolink-connect requests **2048 bytes per READ** (`programs.ts:12`,
  `READ_SIZE = 2048`) and reads **strictly serially** (one in-flight READ,
  awaited) with a 1 s timeout + promise-retry (confirmed —
  `programs.ts:206-232`, `rpc.ts:105-137`). 2048 + overhead exceeds 1500, so
  these replies rely on **IP fragmentation/reassembly** by the kernel (inferred
  from the size vs MTU). The XDR `NFSData` type caps at 8192 bytes
  (`xdr.ts:248`), the NFS v2 max read size.

**Reassembly:** there is no protocol-level streaming. The client tracks
`read_offset` (next byte to request) and `write_offset` (next byte to commit),
stashes out-of-order replies in an `offset -> bytes` map, and flushes
contiguous blocks to the output buffer/file as they become available; the
download finishes when `write_offset == size` (confirmed — `nfsdownload.py`
`blocks` dict, `sendReadRequests`/`writeBlocks`/`readCallback`). Each UDP reply
is matched to its outstanding call by **RPC XID** (confirmed —
`rpcreceiver.py:57-77`; `rpc.ts:139-152`). On timeout the same offset is
re-requested.

**Caching (client-side optimization):** prolink-connect caches per-device the
RPC connections and per-(device,slot) root filehandle, since re-running
portmap+MNT for every file is wasteful; the root handle is invalidated and
re-fetched if a LOOKUP fails (stale handle after media change) (confirmed —
`index.ts:50-138,178-211`).

---

## 5. XDR encoding essentials

XDR (RFC 1014/4506) basics that matter here:

- **Everything is big-endian**, 4-byte aligned. All integers are 32-bit
  unsigned big-endian (`Int32ub` in python-prodj-link; `xdr.uint()` in
  prolink-connect). (confirmed — `packets_nfs.py` uses `Int32ub` throughout;
  `xdr.ts`.)
- **Opaque fixed data** (e.g. filehandle) is emitted raw, padded to a 4-byte
  boundary. The NFS v2 **filehandle is a fixed 32-byte opaque blob** — the
  client must treat it as an *uninterpreted token* and echo it back exactly
  (confirmed — `NfsFhandle = Bytes(32)` `packets_nfs.py:127`;
  `xdr.opaque(32)` `xdr.ts:207,247`).
- **Variable-length opaque/strings** are `uint32 length` followed by `length`
  bytes, padded up to a 4-byte multiple. READ data uses this form
  (`FocusedSeq length/data` in `packets_nfs.py:281-285`; `varOpaque(8192)` in
  `xdr.ts:248`).
- **Pioneer deviation — UTF-16LE strings.** Standard NFS/MOUNT path and
  filename strings are ASCII; Pioneer encodes the **mount path and every LOOKUP
  filename as UTF-16LE**, length-prefixed (confirmed — `packets_nfs.py:125`
  `PascalString(Int32ub, encoding="utf-16-le")` for `MountMntArgs` and
  line 253 for `NfsDiropArgs.name`; `xdr.ts:28-43,206,246` `StringUTF16LE` with
  explicit comment "For Pioneer players, it is a UTF-16LE encoded string"). The
  length prefix is the **byte length** of the UTF-16LE bytes, not the character
  count. This is the single most important non-standard detail of the whole
  path.
- **`AUTH_UNIX` body** is itself XDR: `{stamp:uint32, machine_name:string,
  uid:uint32, gid:uint32, gids:array<uint32>}` (confirmed — `RpcAuthUnix`
  `packets_nfs.py:41-47`; `UnixAuth` `xdr.ts:87-93`). python-prodj-link
  simplifies the `gids` array to a single zero (`packets_nfs.py:46` note
  "should be length-prefixed array?"); prolink-connect emits a proper
  `varArray` (`xdr.ts:92`). Both work because the server ignores creds.
- **NFS `fattr`** (file attributes, returned by LOOKUP/READ/GETATTR) is a fixed
  17-field struct; the only fields the clients consume are `type` (file vs dir)
  and `size` (confirmed — `NfsFattr` `packets_nfs.py:225-240`; `FileAttributes`
  `xdr.ts:264-279`; only `.size`/`.type` read in `nfsdownload.py:47-48`,
  `programs.ts:143-148`).

---

## 6. Serving side — what prolinks-compat must implement (objective #2)

Goal: impersonate a CDJ so that **real CDJs (or other clients) act as NFS
clients of us** and pull our library files (`export.pdb`, ANLZ, artwork, audio).
This is the direction that matters — we must be the **server** (portmap + mountd
+ nfsd), and real CDJs are the clients calling us.

### Required server programs

We must register and answer three RPC programs over **UDP**:

1. **portmapper** on fixed **UDP/111**, program `100000` v2. Must answer
   `GETPORT` (proc 3): given `(100005, 1, UDP)` return our mountd port; given
   `(100003, 2, UDP)` return our nfsd port. (Must also answer NULL; other procs
   can return errors.)
2. **mountd**, program `100005` v1, on whatever port we registered. Must answer
   **MNT (proc 1)**: accept a UTF-16LE export path (`/B/`, `/C/`, `/`), return
   `{status=0, fhandle=<32-byte root handle>}`. Should also answer **EXPORT
   (proc 5)** returning the export list (prolink-connect-style clients call this
   first), and NULL. UMNT (proc 3) can be a no-op success.
3. **nfsd**, program `100003` v2, on its registered port. Must answer at least
   **LOOKUP (proc 4)**, **READ (proc 6)**, and **GETATTR (proc 1)** / NULL.
   Should answer **READDIR (proc 16)** if any client enumerates directories
   (the two reference clients do not, but real CDJ firmware might —
   **inferred**, needs capture).

### Filehandle / path design (the hard part)

- We invent our own **32-byte opaque filehandles**; clients treat them as
  opaque tokens, so any internal encoding works (e.g. pack an inode/path-id +
  checksum into 32 bytes). The root handle returned by MNT must be accepted back
  on the first LOOKUP. (inferred from NFS v2 opaque-handle contract.)
- We must reproduce the **exact directory tree clients expect**, since clients
  walk known absolute paths rather than listing dirs. Concretely the tree under
  each export must contain at least:
  - `/PIONEER/rekordbox/export.pdb` (and `exportExt.pdb` on newer media —
    needs confirmation of exact name)
  - `/PIONEER/USBANLZ/.../ANLZ####.DAT` / `.EXT` / `.2EX`
  - `/PIONEER/Artwork/...`
  - audio files at their pdb-referenced paths
  The exact paths are dictated by what the requesting client builds from the
  `export.pdb` it just read — so the pdb and the served file tree must be
  self-consistent. (confirmed that clients do component-by-component LOOKUP from
  root; exact required tree is **inferred** and must be validated against real
  CDJ behavior / crate-digger expectations.)

### Wire-format must-haves

- All replies **big-endian, 4-byte aligned XDR**, single UDP datagram per
  reply, echoing the request **XID** (confirmed requirement from client
  matching logic, `rpcreceiver.py`/`rpc.ts`).
- LOOKUP filenames and MNT paths arrive **UTF-16LE length-prefixed** — the
  server must decode them as such, and emit any string fields the same way
  (confirmed — §5).
- READ replies: `{status=0, attrs(fattr), data(length-prefixed)}`. Keep each
  reply small enough to avoid surprising clients — clients request up to
  2048/8192 bytes; honoring up to ~8 KB is fine, but staying ≤ MTU-sized chunks
  avoids relying on IP fragmentation (inferred from §4 sizing notes).
- `fattr.size` must be the **true file size** — clients use it to decide how
  many READs to issue and when to stop (confirmed — `nfsdownload.py:47`,
  `programs.ts:198,208`). Getting size wrong truncates or hangs the download.
- Credentials: accept `AUTH_UNIX` with any uid/gids; do not enforce (matches
  real CDJ behavior — inferred from clients sending uid 0 / arbitrary stamp).

### Difficulties / risks

- **NFS v2 specifically over UDP** — modern OS NFS *servers* may not expose v2,
  and the kernel nfsd is awkward to drive with Pioneer's UTF-16LE strings; a
  **userspace RPC/NFS server is effectively mandatory** (the primary doc shows
  the author wrestling Linux kernel nfsd into `--nfs-version 2`,
  `cdj-nfs.md:11,36`). prolinks-compat should implement its own minimal
  userspace portmap+mountd+nfsd.
- **Port 111 binding** is privileged on most OSes; the impersonator needs to
  bind UDP/111 (root / capability) or run on a dedicated CDJ-network host.
- **Filehandle stability** across the session: clients cache the root handle and
  re-LOOKUP on failure (`index.ts:177-193`); our handles should stay valid for
  the life of the "media", and we should return a stale/`err_stale` (NFS status
  70) so the client re-mounts rather than hangs.
- **Unknown: READDIR and other procs.** Whether real CDJ *client* firmware ever
  lists directories or calls STATFS/READLINK is not established by these sources;
  must be captured against hardware.

---

## Summary

A standalone CDJ exposes its loaded SD/USB media as a classic Sun/ONC-RPC
**NFS v2-over-UDP** server (portmapper 100000/v2 on UDP/111, mountd 100005/v1,
nfsd 100003/v2, both on portmap-discovered dynamic UDP ports). A client does
`GETPORT` for mountd and nfsd, `MNT`s the slot's export (**SD=`/B/`, USB=`/C/`,
rekordbox=`/`**) to get a 32-byte opaque root filehandle, walks the path with
NFS `LOOKUP` (proc 4) component-by-component, then `READ`s (proc 6) the file in
~1.3–2 KB chunks correlated by RPC XID and reassembled by offset — pulling
`export.pdb` (full library/metadata without the dbserver), ANLZ analysis files
(waveforms/beatgrids/cues), artwork, and optionally audio. The one critical
non-standard detail is that **mount paths and LOOKUP filenames are UTF-16LE
length-prefixed XDR strings**, not ASCII; everything else is standard big-endian
4-byte-aligned XDR with un-enforced AUTH_UNIX creds. To *serve* (objective #2)
prolinks-compat must run a **userspace** portmap+mountd+nfsd answering GETPORT,
MNT/EXPORT, and LOOKUP/READ/GETATTR over UDP, minting its own 32-byte opaque
filehandles, exposing a self-consistent `/PIONEER/...` tree, and returning
accurate `fattr.size` values.

### Gaps / to confirm against hardware
- Exact `export.pdb` / `exportExt.pdb` filenames and the full required
  `/PIONEER/` tree clients expect (inferred here).
- Whether real CDJ *clients* ever call **READDIR/STATFS/READLINK/UMNT** (no
  reference client does; CDJ firmware untested).
- `/A/` slot meaning (unused by clients).
- prolink-connect's 2048-byte READ implies reliance on IP fragmentation; confirm
  real CDJ READ sizes / fragmentation behavior from a packet capture.
- Whether the server enforces `AUTH_UNIX` creds at all (assumed not).
- TCP variants: the doc's `rpcinfo` shows TCP/v3/v4 only on a *Linux* test box;
  the actual XDJ exported **UDP/v2 only** — confirm no CDJ ever uses TCP.
