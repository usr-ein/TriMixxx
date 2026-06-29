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
| [08-python-poc-plan.md](08-python-poc-plan.md) | Phase-2 build plan: package layout + testable milestones M0–M5. | both |
| [09-mixxx-integration-notes.md](09-mixxx-integration-notes.md) | Phase-3 notes for porting into Mixxx and upstreaming. | both |

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

1. **Does an NXS LINK-browse drive the dbserver menu protocol, pull `export.pdb`
   over NFS, or both?** This gates whether the serving build centers on dbserver
   or NFS (docs 07, 08).
2. Byte-faithful NXS keep-alive/status fields — exact name casing (`CDJ-2000nexus`
   is inferred), packet sizes.
3. What advertisement makes an NXS offer *us* as a LINK source.
4. Whether an NXS accepts a foreign-generated pdb/ANLZ tree over NFS.
