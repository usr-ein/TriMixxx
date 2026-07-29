# prolinks-compat

Making a Mixxx-based unit interoperate with Pioneer CDJs over the Pro DJ Link
(ProLink) Ethernet protocol. See `CLAUDE.md` for the mission and
`research/10-mixxx-prolink-implementation-plan.md` for the approved build plan.

**→ `STATUS.md` says where the work currently stands.** Start there.

## Layout

| Path | What |
|---|---|
| `STATUS.md` | Current state of both objectives, and what is being worked on |
| `research/` | Protocol documentation, doc 10 is the build plan |
| `prolinks_poc/` | The Python proof-of-concept (phase 2) |
| `tests/` | 183 tests, including replay against real Pioneer captures |
| `docs/FINDINGS.md` | Corrections and confirmations the PoC produced, with evidence |
| `docs/HARDWARE.md` | Runbook for a session with real CDJs |
| `docs/CAPTURE-PLAN.md` | The capture scenarios and how to run them |
| `captures/` | `S*/` named scenarios (notes tracked, pcaps not); `journals/` is disposable |

## Quick start

```bash
uv venv && uv pip install -e '.[dev]'
.venv/bin/python -m pytest tests/ -q

prolinks interfaces                  # which NIC faces the CDJs
prolinks devices --watch             # who is on the network (passive)
prolinks rpcinfo <ip>                # does this player serve NFS?
prolinks pull-db <ip> --slot usb     # fetch its rekordbox database
prolinks tracks --file export.pdb    # list what is on it
prolinks announce --dry-run          # what we would broadcast as a virtual CDJ
prolinks db-browse <ip> --as 2       # browse a player the way its LINK button does
prolinks serve --volume /Volumes/X   # share a rekordbox stick with real CDJs
```

Nothing transmits on a DJ-Link port except `announce`. `--assert-passive` will
fail a run if anything tries.

## Testing without hardware

The codecs are exercised against real captures from `research/ref-repos/`
(git-ignored):

```bash
prolinks pcap research/ref-repos/dysentery/doc/assets/LinkInfo.pcapng
```

272 DJ-Link packets and 208 dbserver messages from a CDJ-2000nexus and a
DJM-2000nexus decode and re-encode byte-for-byte, in both directions. That is
what produced the corrections in `docs/FINDINGS.md`.

## Licensing

GPLv2-or-later, matching Mixxx, so nothing here can be "cleaner" than what may
go upstream. Protocol *facts* are not copyrightable and every reference project
is usable for **reference and inspiration**; their *code* is not copied.
python-prodj-link is Apache-2.0 and dysentery is EPL — both GPLv2-incompatible.
prolink-connect and prolink-cpp are MIT and may be adapted with attribution.
See `research/10` for the working discipline.
