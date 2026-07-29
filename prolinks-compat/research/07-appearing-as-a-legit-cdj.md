# 07 — Appearing as a Legitimate CDJ (the "Virtual CDJ" approach)

> **⚠ Pre-hardware document.** Written from published reverse-engineering
> literature before any capture from real CDJs existed. Much of it has since
> been confirmed, and a good deal corrected, by testing against two
> CDJ-2000NXS. **`docs/PROTOCOL.md` is the current specification; where this
> document disagrees with it, this document is wrong.** `docs/FINDINGS.md`
> records each correction with its evidence.

This is the **synthesis** document for prolinks-compat. It pulls together what
it actually takes to make TriMiXxX (a Raspberry Pi running Mixxx) appear on a
Pioneer Pro DJ Link / "ProLink" network as a legitimate CDJ, the constraints
and gotchas, and — crucially — the **asymmetry** between the two project
objectives:

1. **Consuming** other CDJs' libraries (browse/load their tracks) — *well-trodden,
   every reference project does this.*
2. **Serving** our own library to real CDJs (so a DJ can hit LINK on a real
   CDJ-2000NXS and browse TriMiXxX) — *largely unimplemented in the open-source
   world; the hard, capture-driven part of this project.*

It builds directly on the sibling docs and cross-references them rather than
repeating their byte tables:

- **doc 01** — protocol overview, port map, packet families, magic header.
- **doc 02** — discovery, announcement, device-number claim, keep-alive (UDP 50000).
- **doc 03** — detailed status & beat sync (UDP 50001/50002).
- **doc 04** — metadata / dbserver (remotedb) TCP protocol (incl. §6 on serving).
- **doc 06** — NFS / RPC file-access path (incl. §6 on serving).

Markers: **(confirmed)** = stated in dysentery or implemented identically across
≥2 reference codebases; **(inferred)** = derived from one source / structure;
**(untested-needs-capture)** = no open-source project does this; must be captured
from the two real CDJ-2000NXS units to verify. Inline citations:
`(vcdj.adoc)`, `(media.adoc)`, `(missing.adoc)`, `(stagehand.adoc)`,
`(prolink-connect)`, `(python-prodj-link)`, plus `[doc NN §X]` for siblings.

> Numbers in `code font` are hexadecimal (byte offsets / values). Plain prose
> numbers (1.5 s, port 50002) are decimal. Mirrors dysentery's convention.

---

## 1. The "Virtual CDJ" concept

### 1.1 What it is and why it works

There is no authentication, handshake secret, or pairing in Pro DJ Link. A
device is "legitimate" purely by **behaving** like one on the wire. The minimal
trick — discovered by Diogo Santos and the foundation of every reference project
— is:

> Bind a UDP socket to **port 50002**, and start broadcasting **CDJ-style
> keep-alive packets to port 50000** (broadcast address) roughly every **1.5 s**,
> carrying your **real interface MAC and IP** and a device number. As soon as you
> do this, the other players and mixers begin **unicasting detailed status
> packets directly to your port 50002**. `(vcdj.adoc:11-20)` **(confirmed)**

That is the whole gate. You do not need to send status packets, beat packets, or
participate in sync to *be seen* and to *receive* everyone else's state. Sending
your own status is only needed for a few specific behaviours (§1.3).

### 1.2 Minimal behaviours to be accepted as a peer — step by step

This is the "objective (a): be seen" path. Everything here is **(confirmed)**
across dysentery + python-prodj-link + prolink-connect.

1. **Pick the interface.** Bind UDP sockets to `0.0.0.0` on ports **50000, 50001,
   50002** with `SO_BROADCAST` on the transmit sockets [doc 01 §6]. Bind/send the
   50000 socket from port 50000 itself so you can receive the unicast
   conflict/assignment replies [doc 02 §5].

2. **Discover & self-locate.** Listen on 50000 for keep-alive (`0x06`) and
   announce (`0x0a`) packets. Each carries a peer's name, device number, **IP and
   MAC**. Use the **first peer's IP** to choose the matching local interface and
   derive your real IP, MAC, and the subnet **broadcast address**
   (`prolink-connect autoconfigFromPeers`; `python-prodj-link guess_own_iface`)
   [doc 02 §0]. You must know your broadcast address because keep-alives are
   broadcast (`vcdj.py` derives it via `IPv4Network(ip/netmask).broadcast_address`).

3. **Choose a device number** (see §2 — this is the load-bearing decision).

4. **(Optional but recommended for legitimacy) run the claim handshake.** To
   coexist as a "real" deck, run the full §1 startup chain from [doc 02]:
   `0x0a`×3 hello → `0x00`×3 (MAC) → `0x02`×3 (IP + propose `D`) → `0x04`×3
   (assert `D`), then keep-alive. While claiming, **listen for a `0x08` conflict
   packet** and back off to another number if you get one [doc 02 §1.5].
   For a purely passive observer using a high number (7+), the reference projects
   **skip the claim** and jump straight to broadcasting keep-alives — that is
   enough to be seen (`vcdj.py` only sends keep-alives; `prolink-connect`'s
   `Announcer` only sends the `0x06` announce). **(confirmed)**

5. **Broadcast keep-alive (`0x06`) every ~1.5 s, forever.** 0x36-byte packet:
   magic, name@`0c`–`1f`, device byte `02`@`21` (CDJ), `D`@`24`, real MAC@`26`,
   real IP@`2c`, peer count@`30` [doc 02 §2]. This is the packet that both keeps
   you alive (peers drop you after ~10 s of silence) and triggers directed status
   traffic to your 50002.

6. **Receive.** You will now get CDJ status (`0x0a`) and mixer status (`0x29`) on
   50002 every ~200 ms per device [doc 03], beat packets (`0x28`) on 50001, and
   unsolicited media-slot broadcasts (`0x06` type, port 50002) when media is
   inserted/ejected on standalone CDJs `(media.adoc:109-120)`.

### 1.3 When you must *also send* a status packet

Being seen needs only keep-alives. But to *query metadata for some tracks*, the
CDJ wants to see that we are a "real" playing device. prolink-connect ships a
`makeStatusPacket` that emits a **mostly-empty CDJ status packet** on 50002, with
these load-bearing notes baked in as comments **(confirmed via source,
inferred-from-dysentery-issue origin)**:

- Bytes `0x68` and `0x75` **must be `1`** for the CDJ to report metadata for some
  unanalyzed MP3 files (`prolink-connect makeStatusPacket`, citing
  dysentery issue #15).
- Byte `0xb6` **must be `1`** or the CDJ thinks our device is "running older
  firmware" (`prolink-connect makeStatusPacket`).
- Device id is written at `0x21` **and** `0x24`; firmware string at `0x7c`
  (prolink-connect uses `1.43`).

Practical rule: **for objective 1 (consuming), be prepared to emit a synthetic
status packet** so the players treat us as a normal deck. For passive monitoring
you can omit it. For objective 2 (serving), status is not what unlocks library
serving — that's the dbserver/NFS server (§4).

### 1.4 The "don't pose as a player at all" alternative (Stagehand)

Worth knowing as a contrast: Pioneer's own *Stagehand* iPad app **does not pose
as a CDJ**. It claims a brand-new device-type byte (`05`), a new model code
(`0x20`), drops most of the handshake (only `0x0a`×3 → `0x02`×3), and the mixer
and CDJ-3000 push state to it once they see the marker `(stagehand.adoc:30,
44-48)`. This proves the network is permissive about *new* device types — but
Stagehand is a **passive monitor with a tiny command vocabulary**; it never
serves a library and never browses one. For TriMiXxX we want the *opposite*
(serve + consume), so impersonating a real CDJ-2000NXS is the right model, not
the Stagehand persona. Stagehand is only useful here as evidence of how loose the
admission rules are.

---

## 2. The device-number problem (the central constraint)

The device/player number `D` (1 byte, shown on a CDJ as its deck number) is the
single most consequential choice. The tension is fundamental and cannot be fully
resolved — only navigated.

### 2.1 The two facts in tension

- **Fact A (the 4/6-player slot limit).** Real decks occupy `1`–`4` (and `5`–`6`
  on CDJ-3000 networks). A number in `1`–`6` **occupies a slot**: if you take one,
  that's one fewer real CDJ the network can hold. Claiming a number already in
  use earns you a `0x08` conflict packet and you get kicked / must back off
  `(vcdj.adoc:14)` [doc 02 §1.5]. **(confirmed)**

- **Fact B (metadata requires a low number).** The dbserver (remotedb)
  introduce/setup step requires `D_ours` to be a valid player number **1–4**
  that is **actually present, is not the player you're contacting, and isn't
  already in use by another linked player** [doc 04 §2.3]. dysentery is blunt:
  > "use of a non-standard player number (outside the range 1–4, or 1–6 for
  > CDJ-3000s) will interfere with your ability to perform metadata requests using
  > `dbserver` queries." `(vcdj.adoc:17)`
  And for streaming tracks specifically: "the virtual CDJ device number must be 6
  or lower … A device number greater than 6 will not receive responses."
  `(vcdj.adoc:264-265)` **(confirmed)**

So: **high numbers (5/7) are safe but can't dbserver-query; low numbers (1–4)
can query but contend for a slot.**

### 2.2 What the reference projects chose

| Project | Default vCDJ number | Rationale |
|---|---|---|
| `python-prodj-link` | **5** (`vcdj.py self.player_number = 5`) | inside 1–6 → metadata-capable on a CDJ-3000 net; but on a *nexus-only* net, 5 is **not** a metadata-valid number (needs 1–4) |
| `prolink-connect` | **7** (`DEFAULT_VCDJ_ID = 0x07`) | "out of 1–6 range, thus will not be able to request metadata via remotedb" — chosen for safety, accepts the metadata loss |
| dysentery suggestion | **7** | "so as not to conflict with any actual players" `(vcdj.adoc:14)` |

Note the contradiction baked into the ecosystem: prolink-connect's README
*recommends 5* in prose but its code *defaults to 7*. The README's "5" advice is
oriented at CDJ-3000 networks where 5 is a legal-but-extra slot.

### 2.3 Recommended strategy for TriMiXxX with **two real CDJ-2000NXS**

This is the concrete recommendation for this build. The two NXS units will
typically claim `1` and `2` (or whatever the mixer channels assign). That leaves
`3` and `4` free.

**Recommendation: claim device number `3` (or `4`) — a free slot in 1–4 — and run
the full claim handshake.** Reasoning:

- With only two real decks, slots 3 and 4 are free, so **Fact A doesn't bite** —
  there's no slot pressure. We can be a "real" fourth-ish deck.
- Being in `1`–`4` **satisfies Fact B**, so we can issue dbserver metadata queries
  to the two NXS units (objective 1, the rich path). This is the decisive
  advantage and the reason to prefer a low number *for this specific rig*.
- CDJ-2000NXS networks do **not** use the CDJ-3000 device-byte/model-code variants,
  so we use the plain nexus templates (device byte `02`, keep-alive byte `35`=`01`)
  [doc 02 §1.6, §4]. No 3000-compat dance required (no 3000 on this network).

**Fallbacks / nuance:**

- If you only ever want passive monitoring + NFS metadata (no live dbserver) and
  want zero risk of slot contention, use **`7`**. NFS library reads work
  regardless of device number [doc 06 §1; doc 01 §3]. But you lose live dbserver
  and streaming-track metadata.
- **Do the safe-claim algorithm regardless** [doc 02 §3.4]: passively watch the
  net for ≥2 s, record every `D` in keep-alive/claim packets, pick the lowest free
  number in 1–4, claim with auto-assign (`a`=`01`), and back off on `0x08`. This
  is mandatory because the XDJ-XZ/Opus-Quad family don't defend their numbers, so
  you can't trust conflict packets alone [doc 02 §1.5] — though with two plain
  NXS units this is a non-issue, it's cheap insurance.
- python-prodj-link's `dbclient` notes a hack: **player number 0** "seems to work
  if less than 4 players are on the network" but "messes up rendering on the
  players sometimes" [doc 04 §2.3]. Avoid for TriMiXxX — it's a degraded mode and
  we don't need it with slots free.

**Verdict: `D = 3`, full nexus claim, name `CDJ-2000nexus`.** This maximizes
capability (dbserver + serving + sync-visible) on the actual two-NXS rig.

---

## 3. Objective 1 — CONSUMING other CDJs' libraries

This direction is fully solved by the reference projects; we are re-treading a
paved road. The full chain:

### 3.1 Discover & identify who has media

1. From the keep-alive/announce stream (§1.2) build a device list (name, `D`, IP,
   MAC). `python-prodj-link clientlist.py eatKeepalive` is the canonical model —
   note it **drops NXS-GW (Kuvo gateway) keep-alives** and **refuses a new client
   whose `D` collides** with a known one.
2. From **CDJ status packets** on 50002 [doc 03] read which slots have media: the
   `S_r` slot field and the USB/SD "local" bytes (`U_l`@`0x6f`, `S_l`@`0x73`)
   tell you USB/SD presence `(vcdj.adoc:376-379)`. Standalone CDJs also **push
   unsolicited media-slot broadcasts** (type `0x06` on 50002) when media changes
   `(media.adoc:109-120)`.
3. Optionally send a **media query** (type `0x05` on 50002) with `(D, D_r, S_r)`
   to get a **media response** (type `0x06`): media name (UTF-16), creation date,
   **track count, playlist count, `T_r` track type, total/free space, UI color**
   `(media.adoc:9-107)`. `python-prodj-link` calls this `query_link_info`
   (`vcdj.py:59`) and the reply is the "Link Info" panel data. `T_r` here tells
   you whether to ask for the rekordbox menu (`01`) or unanalyzed menu (`02`)
   `(media.adoc:93-94)`.

### 3.2 Get the track list & metadata — two routes

- **Route A: dbserver / remotedb (TCP)** [doc 04]. Needs `D` in 1–4 (§2). Query
  port **12523** → real dbserver port (1051), connect, `0x01` preamble,
  `Introduce` with our `D`, then setup→render menus and binary fetches
  (metadata, artwork, waveforms, beat grid, cues, playlists). This is the live,
  per-track path and is **why we want `D=3`**.
- **Route B: NFS (UDP)** [doc 06]. Portmap (111) → mount → NFS v2; pull
  `export.pdb` and parse the whole library at once [doc 05], plus ANLZ files for
  waveforms/cues. **Works with any device number** and even passively
  `(startup.adoc via doc 06 §1)`. Preferred for bulk library import; lighter on
  the players (no per-item round trips).

For TriMiXxX, **use Route B (NFS + pdb) for bulk import of the two NXS libraries**,
and Route A (dbserver) for live "what's loaded right now" lookups and anything
the pdb lacks.

### 3.3 What we must send to be *allowed* to query

- For **dbserver**: we must be announced (keep-alives running) and use a
  metadata-valid `D` (1–4). Some unanalyzed-MP3 metadata additionally requires us
  to be **emitting a status packet** with bytes `0x68`/`0x75`/`0xb6` set (§1.3).
  **(confirmed for the byte requirements; inferred that status is generally
  expected.)**
- For **NFS**: essentially nothing protocol-wise beyond reaching the player's RPC
  ports; the servers ignore AUTH_UNIX creds [doc 06 §5]. You don't even strictly
  need to announce, though announcing keeps you a well-behaved peer.

### 3.4 Load a track onto a real deck (optional)

Send **Load Track command** (`0x19` on 50002) with `(target D, source D_r,
S_r, track_id)`; the deck replies `0x1a` ack. `python-prodj-link
command_load_track` (`vcdj.py:81`) is the reference. **(confirmed.)** This lets
TriMiXxX push a track from another deck's media (or, once §4 works, from *our*
media) onto a real CDJ.

---

## 4. Objective 2 — SERVING our library to real CDJs (the hard direction)

This is the project's defining challenge. **No open-source project implements the
server side.** prolink-connect and python-prodj-link are **clients only**;
dysentery documents the protocol **from the client's perspective only**
[doc 04 §6; doc 06 §6]. So everything here is a from-scratch implementation, and
many fields are "send 0 / unknown" from the client view that we, as the server,
must *decide* and then *validate against the real NXS units* — i.e. heavily
**(untested-needs-capture)**.

### 4.1 What a real CDJ does when it wants to browse our library

When a DJ on a CDJ-2000NXS hits **LINK** to browse another player's media, the
literature (from `missing.adoc` "Background Research", the original LinkInfo
captures) shows the browsing CDJ:

> opens **two TCP connections** to the other CDJ: the first to **port 12523**
> ("RemoteDBServer") to learn the metadata port, the second to that returned port
> (observed **1051**) where the track-info / Link-Info data flows.
> `(missing.adoc:10-21)` **(confirmed for the connection shape; the LinkInfo
> body is partially studied.)**

So when TriMiXxX is the *target*, a real NXS will:

1. See our keep-alives / media-slot info and decide we have media to browse.
2. TCP-connect to **our** port 12523 and send the 19-byte `RemoteDBServer` query
   → expects a **2-byte port** back [doc 04 §1].
3. TCP-connect to that port, do the `0x01` preamble + `Introduce`, then drive
   **menu setup→render** and **binary fetches** against us [doc 04 §2–5].
4. **And/or mount our media over NFS** (portmap 111 → mount → NFS v2) to read
   `export.pdb` + ANLZ + artwork + audio [doc 06 §6].

We must decide which surface(s) to stand up. **Both are plausible paths a real
CDJ uses; which one the NXS prefers for the LINK/USB browse workflow is exactly
what we must capture (§4b).**

### 4.2 Services we must stand up to serve

From [doc 04 §6] and [doc 06 §6], the full server surface:

**A. dbserver (remotedb) server — TCP:**
1. **Listen on TCP 12523**, answer `RemoteDBServer` with a 2-byte BE port we bind
   (e.g. 1051). Fully specified, easy. `(missing.adoc; doc 04 §6.1)` **(confirmed
   shape, untested as a server.)**
2. **Listen on that dbserver port.** Per connection: echo the `0x01` preamble;
   on `Introduce` (`0x0000`, TxID `0xfffffffe`) reply `0x4000` with arg2 = **our**
   player number; maintain **per-connection menu state** so a following `0x3000`
   render can paginate it [doc 04 §6.1]. **(inferred from client behavior;
   untested-needs-capture.)**
3. **Implement setup→render** (`0x4000` count → `0x4001`/`0x4101`×n/`0x4201`) and
   **binary responders** (artwork `0x4002`, waveform `0x4402`/`0x4a02`, beat grid
   `0x4602`, cues `0x4702`/`0x4e02`, ANLZ tags `0x4f02`), mapping our Mixxx
   library → rekordbox-shaped items/blobs [doc 04 §6.1, §5].
4. **Honor exactly:** the empty-blob omission rule, the 12-slot arg-type list,
   TxID echo, and the UTF-16BE/char-count string semantics [doc 04 §3] — clients
   mis-parse otherwise. **(confirmed format; untested as emitter.)**

**B. NFS server — UDP (likely the more important path):**
- **portmapper (100000 v2)** on UDP **111**, plus **mountd (100005 v1)** and
  **nfsd (100003 v2)** on ports we register [doc 06 §6]. Must be a **userspace
  RPC/NFS-v2-over-UDP server** because modern OS NFS servers won't expose v2
  [doc 06 §6].
- Serve the slot exports (SD=`/B/`, USB=`/C/`) [doc 06 §3], with **mount paths and
  LOOKUP filenames encoded as UTF-16LE** (the Pioneer deviation) [doc 06 §5].
- Expose a rekordbox-shaped file tree: `/PIONEER/rekordbox/export.pdb` (+ maybe
  `exportExt.pdb`), `/PIONEER/USBANLZ/...` analysis files, `/PIONEER/Artwork/...`,
  and the audio files referenced by the pdb [doc 06 §6].

> **Strong recommendation:** prioritize the **NFS path** for serving. It's the
> path that exposes the *whole* library via one `export.pdb` the CDJ already knows
> how to consume, sidesteps the entirely-unimplemented stateful dbserver
> emission, and is the same path that works through all-in-one USB interfaces
> [doc 06 §1]. The dbserver server is a second-phase effort. **But which the NXS
> actually uses for LINK browse must be captured first (§4b).**

### 4.3 Where the literature is thin / what we must capture

The whole serving direction is built from the *client* view, so the open
unknowns (all **untested-needs-capture**) are:

- **Does an NXS LINK-browse use dbserver, NFS, or both?** And in what order?
  (§4b — top priority capture.)
- Every "send 0 / unknown" arg the docs describe from the client side becomes a
  *server output decision*: item arg 8 (flag/column-config), render args 4 & 6,
  the `0x4f02` 5th arg, beat-grid trailing 8 bytes, cue-response final binary arg
  [doc 04 §6.2]. Real NXS clients may be picky.
- **Message framing as a server:** rekordbox packs multiple messages per TCP
  segment and a message may span segments [doc 04 §6.2]; we must frame by message
  length. Whether a real CDJ-client tolerates one-message-per-write is untested.
- The exact `export.pdb` / ANLZ shapes the NXS will accept from a *foreign*
  server (vs a genuine rekordbox export) — must diff against a real USB export.

These all resolve by capturing the two NXS units actually browsing a real USB
stick and a real rekordbox export, then replaying/serving the same bytes.

---

## 4b. The "linked" (LINK/USB) browse workflow specifically

On a CDJ-2000NXS, the DJ presses **LINK** (a.k.a. the USB/LINK source) to browse
another player's USB. What the literature reveals about the wire behaviour:

- The browsing CDJ opens the **two TCP connections** described in §4.1: one to
  **12523** to discover the metadata port, one to the returned port (**1051**)
  carrying the Link-Info track data `(missing.adoc:10-21)`. **(confirmed shape.)**
- Deep Symmetry's captures (`LinkInfo.pcapng`, `LinkInfo2.pcapng`) show the
  initial display of the already-loaded track, then updated info as the linked
  CDJ loads further tracks — i.e. the Link-Info stream is the dbserver
  metadata/menu protocol [doc 04] in action `(missing.adoc:14-21)`. **(confirmed.)**
- The **media-query (`0x05`)/response (`0x06`)** exchange on 50002 supplies the
  Link-Info *panel* (media name, track/playlist counts, size, color) before/around
  the browse `(media.adoc)`; `python-prodj-link query_link_info` models the client
  side.

**What TriMiXxX must answer for the LINK workflow:**

1. Advertise via keep-alive + (likely) media-slot info that we have browsable
   media [§1, §3.1]. **(inferred — exact trigger for the NXS to offer us in its
   LINK list is untested-needs-capture.)**
2. Answer the **12523** `RemoteDBServer` query with our dbserver port [doc 04 §1].
3. Answer the **media-query (`0x05`)** with a valid **media-response (`0x06`)** so
   the NXS shows a sane Link-Info panel (track count, `T_r=01` for rekordbox,
   color, sizes) `(media.adoc)`. **(confirmed format; untested as emitter.)**
4. Serve the **menu/metadata/binary** dbserver protocol so the NXS can list and
   preview tracks [doc 04 §4–5]. **And/or** serve the same content over NFS
   (`export.pdb`) [doc 06] — TBD which the NXS uses.

> The single biggest unknown for this project is whether an NXS, when LINK-
> browsing, drives the **dbserver menu protocol live** or **pulls `export.pdb`
> over NFS and renders locally** (or both). The reference repos don't answer it
> because they're consumers. **Capture this first** — it decides whether the
> serving effort centers on the stateful dbserver emitter (hard) or the NFS/pdb
> file server (more mechanical). **(untested-needs-capture.)**

---

## 5. Legitimacy details (don't get rejected)

### 5.1 Identity fields

- **MAC / IP:** use the **real** NIC MAC and IP of the interface receiving DJ-Link
  traffic. Spoofing breaks the directed-return path — peers put your advertised
  IP into their unicasts `(vcdj.adoc:12; doc 02 §4.3)`. **(confirmed.)**
- **Device name (20-byte ASCII, NUL-padded):** to impersonate, set it to
  **`CDJ-2000nexus`**. The reference tools deliberately use *non*-CDJ names
  (`prolink-connect` `prolink-typescript`, `python` `Virtual CDJ`) so they're
  filtered out — we want the opposite [doc 02 §4.1]. **Caution:** the exact ASCII
  casing `CDJ-2000nexus` is the documented Pioneer model string but is
  **(inferred)** — no supplied capture contains a literal CDJ-2000 name dump (only
  `XDJ-1000` appears). **Verify the exact bytes from the real NXS keep-alive
  before relying on them. (untested-needs-capture.)**
- **Device byte `0x21` = `02`** (CDJ/nexus) [doc 02 §0.2]. **(confirmed.)**

### 5.2 Packet sizes / firmware quirks

- **Match the nexus status size:** real nexus players send **`0xd4`-byte** status
  packets with subtype `03`@`0x20`, firmware ASCII@`7c`–`7f`, `nx`@`0xcc`=`0f`
  `(vcdj.adoc:84-88, 404, 576)`. prolink-connect's template approximates this.
  Wrong sizes / missing fields can make a player treat us as older/limited.
- **The "older firmware" trap:** byte `0xb6`=`1` in our status, else the CDJ flags
  us as old firmware (§1.3) **(confirmed via prolink-connect)**.
- **Keep-alive byte `0x35`:** `01` for a plain nexus CDJ; `64` is the CDJ-3000
  value [doc 02 §2]. Use `01` — we're impersonating nexus, and there's no 3000 on
  this rig. (`0x64` mismatch is what kicks CDJ-3000s set to 5/6 off the net, per
  `stagehand.adoc:153` / dysentery — not our case, but get our byte right.)
- **Model code semantics:** `stagehand.adoc:156-168` documents byte `0x35` as a
  per-product model code (`00` legacy, `20` Stagehand, `31` DJM-A9, `64`
  CDJ-3000). A nexus CDJ uses the legacy/`01` family. **(confirmed for the others;
  the exact NXS value is worth confirming in capture.)**

### 5.3 Behaviours that get you rejected or cause misbehaviour

- **Claiming a number already in use** → `0x08` conflict; you get kicked. Always
  watch first and back off [doc 02 §1.5]. **(confirmed.)**
- **Malformed / wrong-length status** → ignored or treated as degraded; some
  metadata won't be served (the `0x68`/`0x75`/`0xb6` bytes) [§1.3].
- **Wrong magic header** — every UDP packet must start with `Qspt1WmJOL`
  (`51 73 70 74 31 57 6d 4a 4f 4c`) [doc 01 §4]. (The task-prompt variant elsewhere
  is wrong; use this.) **(confirmed.)**
- **Don't probe an XDJ-RX** — it doesn't implement the protocol and dysentery has
  crashed it [doc 01 §1]. (Not on this rig, but a general rule.)
- **NXS-GW / Kuvo gateway packets** should be ignored, not treated as a peer
  (`clientlist.py eatKeepalive` drops `is_nxs_gw`). **(confirmed.)**

---

## 6. Prioritized capability checklist for the Python PoC

Ordered by the three objectives in the prompt: **(a) be seen, (b) see others,
(c) be browsed by others.** Each item tagged with confidence and the doc it
draws on.

### Phase A — Be seen (foundation) — all **(confirmed)**
- [ ] A1. Bind UDP 50000/50001/50002 on `0.0.0.0`; send 50000 from port 50000;
  `SO_BROADCAST` on. [doc 01 §6]
- [ ] A2. Passive discovery: parse keep-alive (`0x06`) + announce (`0x0a`);
  auto-detect our interface/IP/MAC/broadcast from the first peer. [doc 02 §0]
- [ ] A3. Safe device-number selection → **claim `D=3`** with full
  `0x0a`/`0x00`/`0x02`/`0x04` handshake; listen for `0x08` and back off.
  [doc 02 §1, §3.4]
- [ ] A4. Broadcast `0x06` keep-alive every 1.5 s with name `CDJ-2000nexus`,
  device byte `02`, real MAC/IP, byte `35`=`01`. [doc 02 §2, §4]
- [ ] A5. **Capture-verify** the real NXS keep-alive bytes (name casing, `0x35`,
  status size) against ours. **(untested-needs-capture)**

### Phase B — See others (objective 1, consuming)
- [ ] B1. Parse CDJ status (`0x0a`) + mixer status (`0x29`) on 50002; track
  loaded `(D_r, S_r, track_id)`, media presence. **(confirmed)** [doc 03]
- [ ] B2. Emit a synthetic CDJ status packet with `0x68`/`0x75`/`0xb6`=`1`
  (unlocks unanalyzed-MP3 metadata; makes us look like a real deck).
  **(confirmed bytes)** [§1.3]
- [ ] B3. Media query (`0x05`) → media response (`0x06`) to read each NXS's slots.
  **(confirmed)** [§3.1; media.adoc]
- [ ] B4. **NFS import (preferred):** portmap → mount → pull `export.pdb` + ANLZ,
  parse locally. **(confirmed client-side)** [doc 06; doc 05]
- [ ] B5. **dbserver live lookups:** 12523 → 1051 → preamble/Introduce →
  setup/render + binary fetches (needs `D` in 1–4 → why A3 picks 3).
  **(confirmed)** [doc 04]
- [ ] B6. (Optional) Load Track command (`0x19`) to push a track to a real deck.
  **(confirmed)** [§3.4]

### Phase C — Be browsed by others (objective 2, serving) — the hard part
- [ ] C1. **Capture-first:** MITM/sniff the two NXS units LINK-browsing a real USB
  + rekordbox export. Determine **dbserver vs NFS vs both** and the exact request
  order. **(untested-needs-capture — gating decision for all of Phase C.)**
- [ ] C2. Answer **12523** `RemoteDBServer` → 2-byte port. **(confirmed shape,
  untested as server)** [doc 04 §6.1]
- [ ] C3. Answer **media query (`0x05`)** with a valid **media response (`0x06`)**
  (track/playlist counts, `T_r=01`, color, sizes) for the Link-Info panel.
  **(confirmed format, untested as emitter)** [media.adoc]
- [ ] C4. **NFS server (userspace, v2/UDP):** portmap 111 + mountd + nfsd; UTF-16LE
  paths; serve `export.pdb` + ANLZ + artwork + audio mapped from the Mixxx
  library. **(untested-needs-capture)** [doc 06 §6]
- [ ] C5. **dbserver server (second phase, if C1 shows it's needed):** per-conn
  preamble + Introduce reply (our `D`) + stateful setup→render + binary
  responders; exact arg/blob/string semantics. **(untested-needs-capture)**
  [doc 04 §6]
- [ ] C6. Validate every server output against the real NXS as client; iterate on
  the "unknown but send 0" arg slots. **(untested-needs-capture)** [doc 04 §6.2]

---

## Summary

Appearing as a legitimate CDJ is, at its core, trivially cheap on admission and
expensive on fidelity: there is no auth — broadcasting a CDJ-style keep-alive to
UDP 50000 every ~1.5 s with your real MAC/IP and a device number, while listening
on 50002, is enough to be accepted and to start receiving every other device's
detailed status (the Diogo Santos "Virtual CDJ" trick). The make-or-break choice
is the device number: high numbers (7) are collision-safe but **cannot** issue
dbserver metadata queries, while numbers 1–4 **can** query but occupy a real deck
slot — so for TriMiXxX's specific rig of **two CDJ-2000NXS** (leaving slots 3/4
free) the right call is to **claim `D=3` with the full nexus claim handshake and
name `CDJ-2000nexus`**, getting both dbserver access and full legitimacy at no
slot cost. Consuming libraries (objective 1) is a paved road — discover via
keep-alive/status, read media via `0x05`/`0x06`, then bulk-import via NFS/`export.pdb`
and do live lookups via dbserver — every reference project does this. Serving our
library (objective 2) is the asymmetric, unsolved half: **no open-source project
implements the server side**, dysentery documents the protocol only from the
client's view, so standing up a 12523/dbserver responder and/or a userspace
NFS-v2 server that emits rekordbox-shaped data is a from-scratch, capture-driven
build whose every "unknown-but-send-0" field must be validated against the real
NXS units.

### Most important open questions to resolve with hardware captures

1. **LINK browse: dbserver, NFS, or both — and in what order?** This single
   capture (an NXS LINK-browsing a USB + a rekordbox export) decides whether the
   serving effort centers on the stateful dbserver emitter or the NFS/`export.pdb`
   file server. *Top priority.* [§4b, C1]
2. **Exact CDJ-2000NXS keep-alive & status bytes:** confirm the literal device
   name casing (`CDJ-2000nexus`?), keep-alive byte `0x35`, status packet size
   (`0xd4`), and the model-code/firmware fields, so our impersonation is
   byte-faithful. [§5.1, §5.2, A5]
3. **What makes an NXS offer *us* in its LINK source list** — which advertisement
   (keep-alive alone? media-slot broadcast? media-response to a query?) triggers
   the NXS to consider TriMiXxX browsable. [§4b]
4. **Server-side "unknown" fields:** the item arg-8 flag, render args 4/6,
   `0x4f02` 5th arg, beat-grid trailing bytes — what real NXS clients require us
   to emit vs tolerate as zero. [§4.3, C6]
5. **`export.pdb`/ANLZ acceptance:** whether an NXS accepts a foreign-generated
   pdb + analysis tree over NFS the same as a genuine rekordbox export, and which
   tables/fields are mandatory. [§4.2-B, C4]
6. **Status packet necessity for serving:** whether we must also emit a believable
   status packet (not just keep-alives) for the NXS to engage the browse/query
   path against us. [§1.3, §4b]
