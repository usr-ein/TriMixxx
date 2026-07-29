# ProLink protocol research

Research documentation for **prolinks-compat** — making the TriMiXxX unit (Mixxx
on a Raspberry Pi) interoperate with Pioneer CDJs over the Pro DJ Link / ProLink
Ethernet protocol. See `../CLAUDE.md` for the mission.

These docs synthesize the open-source reverse-engineering literature (chiefly
Deep-Symmetry/dysentery, plus prolink-connect and python-prodj-link) into a single
implementation-ready spec. Source repos live, git-ignored, in `ref-repos/`.

## Read in order

| Doc | Topic | Serves objective |
|---|---|---|
| [00-references.md](00-references.md) | The reference projects, what each is good for, and how the literature is biased toward *clients*. | — |
| [01-protocol-overview.md](01-protocol-overview.md) | Architecture, network topology (auto-IP), the full **port map**, the `Qspt1WmJOL` magic header, device-number conventions. | both |
| [02-device-discovery-and-keepalive.md](02-device-discovery-and-keepalive.md) | UDP 50000: announcement, the **device-number claim handshake**, keep-alive cadence, impersonation fields. | be seen |
| [03-status-and-beat-sync.md](03-status-and-beat-sync.md) | UDP 50002 status + 50001 beat: play state, tempo, sync/master handoff, on-air, mixer integration. | be seen / see others |
| [04-metadata-dbserver-protocol.md](04-metadata-dbserver-protocol.md) | TCP **remotedb/dbserver**: port discovery (12523), message wire format, menu requests, binary responses (art/waveform/cues). | **see + serve libraries** |
| [05-rekordbox-pdb-and-analysis.md](05-rekordbox-pdb-and-analysis.md) | The on-media **`export.pdb` (DeviceSQL)** and **ANLZ** analysis file formats. | see + serve libraries |
| [06-nfs-file-access.md](06-nfs-file-access.md) | The **NFS/RPC** path: portmap/mount/nfs to pull `export.pdb`, ANLZ, art, audio directly. | see + serve libraries |
| [07-appearing-as-a-legit-cdj.md](07-appearing-as-a-legit-cdj.md) | **Synthesis & strategy**: the Virtual CDJ approach, the device-number tradeoff, the consume-vs-serve asymmetry, a prioritized capability checklist, and the open questions to resolve with hardware captures. | both |
| [08-python-poc-plan.md](08-python-poc-plan.md) | Phase-2 build plan: package layout + testable milestones M0–M5. *(superseded by doc 10)* | both |
| [09-mixxx-integration-notes.md](09-mixxx-integration-notes.md) | Phase-3 notes for porting into Mixxx and upstreaming. *(licensing section corrected by doc 10)* | both |
| **[10-mixxx-prolink-implementation-plan.md](10-mixxx-prolink-implementation-plan.md)** | **The approved build plan.** Decisions taken, licensing rules, the Python PoC milestones M0–M11 and their hardware experiments, and the full Mixxx C++ design (module layout, threading, sidebar tree, track caching, registration touchpoints, risks). | both |

## TL;DR for the impatient

- **The network**: link-local Ethernet (169.254.x.x auto-IP). Every DJ Link packet
  starts with the 10-byte magic `51 73 70 74 31 57 6d 4a 4f 4c` ("Qspt1WmJOL").
- **Ports**: UDP **50000** discovery/keep-alive, **50001** beat, **50002** status;
  TCP **12523** to discover the dynamic **dbserver** port; UDP **111** portmap →
  dynamic **mount/NFS** ports. (Doc 01 has the full table.)
- **Being accepted is cheap**: there is no authentication. Broadcast a CDJ-style
  keep-alive every ~1.5 s after winning the device-number claim handshake.
- **Consuming libraries (objective 1) is well-trodden** — clone what
  python-prodj-link/prolink-connect already do: query dbserver and/or download &
  parse `export.pdb` over NFS.
- **Serving our library (objective 2) is greenfield** — no open-source project
  implements the CDJ-impersonating *server*. It requires standing up dbserver
  and/or NFS-v2 servers and generating a valid `export.pdb`/ANLZ from Mixxx's
  library, and must be validated with packet captures from the two CDJ-2000NXS.

## Conventions in these docs

- Facts are tagged **(confirmed)** (verified against hardware per the sources),
  **(inferred)** (deduced from client code/captures), or
  **(untested-needs-capture)** (must be checked on the real NXS units).
- Non-obvious facts cite their source inline, e.g. `(startup.adoc)` or
  `(prolink-connect remotedb/index.ts)`.

## Top open questions to resolve with hardware captures

1. ~~**Does an NXS LINK-browse drive the dbserver menu protocol, pull `export.pdb`
   over NFS, or both?**~~ **Answered from the literature.** dysentery
   `menus.adoc:19`: requesting the root menu "is what a player will do when you use
   the *Link* button to connect to media mounted on another player." Corroborated by
   `missing.adoc:14-18` (a booting CDJ opens two TCP connections to its peer: 12523
   for port discovery, then 1051 for the Link Info track data). **LINK-browse is
   dbserver.** NFS is a parallel surface that no CDJ *client* is known to use — it is
   how open-source tools bypass the dbserver's player-number limits.
   **Reframed, and now the more important question:** *how do the audio bytes travel
   when a CDJ actually loads and plays a track off another player's USB?* dbserver
   serves metadata, waveforms and cues, not audio. NFS is the only file-transfer
   surface the CDJs expose, so it is the strong candidate — but no source here states
   it. This gates whether TriMiXxX can be *played from*, not merely *browsed*.
2. **Does a CDJ-2000NXS serve NFS at all?** Doc 06 §1's "confirmed" rests on an
   **XDJ** capture, not an NXS (a 2012 unit). This is the go/no-go gate for the whole
   chosen transport — see doc 10, experiment E4.
3. Byte-faithful NXS keep-alive/status fields — exact name casing (`CDJ-2000nexus`
   is inferred), packet sizes.
4. What advertisement makes an NXS offer *us* as a LINK source.
5. Whether an NXS accepts a foreign-generated pdb/ANLZ tree over NFS.
