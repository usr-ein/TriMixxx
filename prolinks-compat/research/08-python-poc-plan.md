# 08 — Python proof-of-concept plan

Phase 2 of the project (see `../CLAUDE.md`): a standalone Python program that
proves out both objectives before any Mixxx work. This doc turns the research
(docs 01–07) into a concrete, incremental build plan with checkpoints that are
each independently testable against the two real CDJ-2000NXS units.

## Guiding principles

- **Mirror python-prodj-link's structure where it already solved a problem.** It
  has working code for discovery, the dbserver client, NFS client, and pdb/ANLZ
  parsing. We *consume* (objective 1) by doing what it does. Don't reinvent the
  `construct` packet definitions — adapt them.
- **The server side (objective 2) is greenfield.** Build it behind a capture-first
  loop: sniff what a real NXS asks, answer it, repeat. See doc 07 §4.
- **Capture everything.** Run `tcpdump`/Wireshark on the Mac dongle for every
  milestone so we build a private pcap corpus that becomes regression fixtures.

## Suggested package layout

```
prolinks_poc/
  net.py            # socket setup: bind 50000/50001/50002, broadcast helpers
  packets.py        # construct-based encode/decode (port from python-prodj-link)
  vcdj.py           # Virtual CDJ: keep-alive loop + (optional) status emitter
  discovery.py      # listen on 50000, maintain device list (doc 02)
  status.py         # parse 50002 status / 50001 beats (doc 03)
  dbclient.py       # remotedb/dbserver CLIENT (doc 04)  -> objective 1
  nfsclient.py      # NFS/RPC CLIENT (doc 06)            -> objective 1
  pdb.py            # export.pdb + ANLZ parser (doc 05)  -> objective 1 (bulk)
  dbserver.py       # remotedb/dbserver SERVER (doc 04)  -> objective 2 (HARD)
  nfsserver.py      # portmap+mount+nfs SERVER (doc 06)  -> objective 2 (HARD)
  library.py        # adapter: our USB-drive tracks -> served metadata/files
  cli.py            # subcommands per milestone
```

## Milestones (each is a demo + a capture)

### M0 — Be seen (doc 02, 07 §1)
- Bind UDP 50000; broadcast the keep-alive (`0x06`) every ~1.5 s with name
  `CDJ-2000nexus`, a real MAC/IP on the link, and a chosen device number.
- Run the device-number **claim handshake** (`0x00`/`0x02`/`0x04`, defend with
  `0x08`) so we don't collide with the two NXS.
- **Recommended number: `D=3`** (slots 1–2 taken by the two NXS; 3 is free,
  low enough for dbserver, full legitimacy — doc 07 §2).
- ✅ Pass: both CDJ-2000NXS show TriMiXxX in their LINK device list.

### M1 — See others: discovery + status (doc 02, 03)
- Maintain a live device table from 50000; parse 50002 status & 50001 beats.
- ✅ Pass: print each NXS's player#, loaded track id, BPM, play/master/on-air,
  matching what the CDJ screens show.

### M2 — See others' libraries via dbserver (doc 04) — OBJECTIVE 1a
- Implement 12523 port query → connect dbserver port → setup/menu handshake.
- Query: root menu → track list → track metadata by `rekordbox_id`+slot →
  artwork → waveform → beat grid → cue points.
- ✅ Pass: dump a real NXS USB's full track list + one track's full metadata.

### M3 — See others' libraries via NFS (doc 06, 05) — OBJECTIVE 1b
- portmap GETPORT → MOUNT slot export → LOOKUP/READ `export.pdb` + ANLZ.
- Parse the downloaded `export.pdb` with `pdb.py` for a full offline library.
- ✅ Pass: download + parse a NXS USB's `export.pdb`; reconcile with M2 results.
- This is the more robust path for *bulk* library browsing; M2 is for live lookups.

### M4 — Serve our library: NFS server (doc 06 §6, 07 §4) — OBJECTIVE 2a
- Stand up userspace portmap (111) + mountd + nfsd **v2/UDP** (forced v2).
- Mint opaque filehandles; expose a synthetic `/PIONEER/...` tree built from the
  USB drive Mixxx mounted, including a **generated `export.pdb` + ANLZ** (see
  doc 05 §6 — likely the hardest sub-task; consider crate-digger/rekordcrate
  generators or a minimal hand-rolled writer).
- ✅ Pass: a real NXS, with us claimed as a player, can browse our tracks via
  LINK and load+play one.
- ⚠️ **Capture-bound** — confirm whether NXS LINK-browse drives dbserver, NFS,
  or both (doc 07 open question #1) *before* committing here.

### M5 — Serve our library: dbserver server (doc 04 §6) — OBJECTIVE 2b (if needed)
- Only if M4's capture shows NXS uses dbserver to browse us.
- Announce our dbserver port on 12523; implement the menu/render request handling
  and binary responses (artwork/waveform/cues) from `library.py`.
- ✅ Pass: NXS browses our menus live and renders metadata/artwork.

## Tooling & test setup

- Mac + USB-Ethernet dongle, two CDJ-2000NXS, a switch (or daisy-chain via the
  CDJs' built-in switch / a DJM). Use link-local 169.254.x.x auto-IP (doc 01).
- `scapy` for ad-hoc packet crafting; `construct` for the real codecs.
- Wireshark with the dysentery/`prolink` dissector if available; else raw + our
  own decoder. Save pcaps under `captures/` (git-ignored) as fixtures.
- Python `socket` with `SO_REUSEADDR`/`SO_BROADCAST`; one thread/asyncio task per
  port. Beware: must send *and* receive from port 50000 (doc 02).

## Risks / unknowns to retire early (see doc 07 open questions)

1. Does NXS LINK-browse use dbserver, NFS, or both? (Gates M4 vs M5.) — capture.
2. Exact byte-faithful NXS keep-alive/status (name casing, sizes). — capture.
3. Will an NXS offer *us* as a LINK source at all, and under what advert? — capture.
4. Can a foreign-generated `export.pdb`/ANLZ tree be served verbatim? — capture.
