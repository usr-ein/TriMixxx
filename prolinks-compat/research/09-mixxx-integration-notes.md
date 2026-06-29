# 09 — Mixxx integration notes (Phase 3)

Forward-looking notes for porting the validated Python PoC into Mixxx (C++/Qt) and
upstreaming it. Keep light until Phase 2 proves the approach; recorded now so the
PoC is built with the port in mind.

## Why this is a good fit for Mixxx

- Mixxx already has a **library abstraction** with external/feature libraries
  (iTunes, Rekordbox USB, Serato, Traktor) under `src/library/`. A "ProLink
  Network" library feature that lists peer CDJs and their tracks fits this model
  for **objective 1 (consume)**.
- Mixxx is **Qt**, so `QUdpSocket`/`QTcpSocket` + `QTimer` map cleanly onto the
  discovery/status/keep-alive loops. No new dependency needed for the core
  protocol. NFS/RPC and the pdb parser are the only chunky pieces.
- Mixxx already **reads rekordbox `export.pdb`** (the `RekordboxFeature`, parser
  under `src/library/rekordbox/`). That parser is directly reusable for doc 05 —
  both for parsing peers' downloaded pdb (objective 1) and as a model for
  *generating* our pdb (objective 2). **Check it first** before writing any pdb code.

## Mapping PoC modules → Mixxx

| PoC module (doc 08) | Mixxx home | Notes |
|---|---|---|
| `net.py`, `vcdj.py`, `discovery.py`, `status.py` | new `src/network/prolink/` (Qt sockets) | A `ProLinkDevice` running in its own thread/`QThread`. |
| `dbclient.py` | same | remotedb client; emits Qt signals with track metadata. |
| `nfsclient.py` | same | RPC/NFS client; or shell out to kernel NFS as a fallback. |
| `pdb.py` | reuse `src/library/rekordbox/` parser | Don't duplicate — extend the existing rekordbox pdb parser. |
| library feature (browse peers) | new `ProlinkFeature : LibraryFeature` under `src/library/prolink/` | Mirrors `RekordboxFeature`. |
| `dbserver.py`/`nfsserver.py`/`library.py` | new `src/network/prolink/server/` | Objective 2; greenfield; keep optional/behind a preference. |

## Upstreaming strategy

- **Land objective 1 first** (consume): lower risk, self-contained library feature,
  obvious user value (browse linked CDJs from Mixxx). PR this independently.
- **Objective 2 (serve) as a follow-up**, behind an experimental preference, since
  it's capture-validated rather than spec-backed and touches more surface
  (standing up NFS/dbserver servers raises security/permission questions on the Pi).
- Coordinate early with Mixxx maintainers (Zulip/GitHub) — the rekordbox parser
  authors will have opinions; reuse, don't fork. Note licensing of any code adapted
  from the reference repos (python-prodj-link is GPL-ish; dysentery is EPL — check
  before copying code vs reimplementing from the docs).

## Practical constraints on the Pi / TriMiXxX

- Standing up a userspace NFS **v2/UDP** server (doc 06) inside Mixxx is the
  trickiest part — may be cleaner as a small sidecar process the Pi runs, that
  Mixxx feeds via IPC, rather than in-process. Decide after the PoC.
- Generating a valid `export.pdb` + ANLZ for the mounted USB (doc 05 §6) is the
  biggest unknown for objective 2; budget real time for it, and prefer reusing an
  existing pdb *writer* (crate-digger/rekordcrate) over hand-rolling.
- The keep-alive/status loops must keep running while Mixxx plays; ensure they
  live off the audio/GUI threads.

## Open design questions for Phase 3

- One Mixxx instance = one virtual CDJ device number. How to coexist with two real
  NXS without claiming a number they want? (doc 07 §2 — claim `D=3`/`4`.)
- Should loading a peer's track stream audio (NFS/touch-audio, doc 06) or require
  the file be reachable? CDJs play their *own* loaded media; we likely fetch the
  audio file over NFS to a temp/cache and play locally.
- Map ProLink beat/tempo/master (doc 03) to Mixxx's sync engine? Out of scope for
  the library PR but a natural future extension (TriMiXxX as a sync peer).
