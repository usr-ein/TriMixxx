# 00 — Reference material

This catalogs the open-source projects that reverse-engineered the Pioneer Pro DJ
Link / ProLink protocol. They are cloned (git-ignored) into `ref-repos/`. Re-clone
with the commands in the table if the folder is empty.

## The cloned reference projects

| Repo | Lang | What it is | Best for |
|---|---|---|---|
| **Deep-Symmetry/dysentery** | Clojure + AsciiDoc | The canonical reverse-engineering effort. Its `doc/modules/ROOT/pages/*.adoc` is the authoritative protocol analysis ("DJ Link Packet Analysis"). | The ground truth for every packet. Start here. |
| **evanpurkhiser/prolink-connect** | TypeScript | A clean, modern client library: discovery, status, remotedb (dbserver), NFS, local pdb parsing. | Reading a well-structured *client* implementation; XDR/RPC and remotedb message code. |
| **evanpurkhiser/prolink-tools** | TypeScript | End-user app (OBS overlays, now-playing) built on prolink-connect. | Seeing what features need what data; UX context. Not protocol-level. |
| **flesniak/python-prodj-link** | Python | A full Python client: vcdj, packets (via `construct`), dbserver client, NFS client, and a complete **pdblib** that parses `export.pdb` + ANLZ. | The closest analogue to our Phase-2 Python PoC. Byte-level `construct` definitions. pdb/ANLZ parsing. |
| **teknopaul/libcdj** | C | C library + CLI tools. Docs on NFS, auto-IP, device-number "id use" replies, timing. | Low-level details: auto-IP, NFS mount specifics, the id-use conflict reply. |
| **grantHarris/prolink-cpp** | C++ | C++ implementation (discovery/status focus). | A second C/C++ perspective; sanity-checking. |
| **nzoschke/vizlink** | Go/Java | ProLink visualizer. | Minor; a Go/JVM perspective. |

```
# Re-clone all (run from research/ref-repos/)
git clone --depth 1 https://github.com/Deep-Symmetry/dysentery
git clone --depth 1 https://github.com/evanpurkhiser/prolink-connect
git clone --depth 1 https://github.com/evanpurkhiser/prolink-tools
git clone --depth 1 https://github.com/flesniak/python-prodj-link
git clone --depth 1 https://github.com/teknopaul/libcdj
git clone --depth 1 https://github.com/grantHarris/prolink-cpp
git clone --depth 1 https://github.com/nzoschke/vizlink
```

## Other important resources NOT cloned (worth fetching later)

- **Deep-Symmetry/crate-digger** — Java library + the definitive **Kaitai Struct**
  spec for `export.pdb` and the ANLZ analysis files. This is *the* reference for
  the rekordbox database format (see doc 05). The dysentery doc set links to it.
- **Deep-Symmetry/beat-link** — the production-grade Java client built on the
  dysentery findings; the most complete client in existence. Useful when a detail
  is ambiguous in the docs.
- The rendered dysentery analysis site: <https://djl-analysis.deepsymmetry.org/>
  (same content as `dysentery/doc/`, nicer to read with the bytefield diagrams).
- **rekordcrate** (Rust) and **kaitai** community ports — alternative pdb parsers.

## How the literature is biased (important)

Every project above is a **client**: it pretends to be a player so it can *read*
from real CDJs (for lighting/overlay/sync tooling). dysentery decoded the wire by
capturing real CDJs talking to *clients/each other*.

Consequence for us:
- **Objective 1 (see other libraries / consume):** very well covered. We are doing
  exactly what these projects do.
- **Objective 2 (share our library / serve):** essentially **undocumented**. No
  open-source project implements the *server* side of dbserver or NFS as a CDJ.
  We will need to read the client code "in reverse" to infer the server contract,
  and capture real CDJ-to-CDJ LINK browsing on the author's two CDJ-2000NXS units
  to fill the gaps. See doc 07.
