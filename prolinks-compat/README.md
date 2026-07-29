# prolinks-compat

Making a Mixxx-based unit interoperate with Pioneer CDJs over the Pro DJ Link
(ProLink) Ethernet protocol. See `CLAUDE.md` for the mission and
`research/10-mixxx-prolink-implementation-plan.md` for the approved build plan.

## Layout

| Path | What |
|---|---|
| `research/` | Protocol documentation, doc 10 is the build plan |
| `prolinks_poc/` | The Python proof-of-concept (phase 2) |
| `tests/` | 116 tests, including replay against real Pioneer captures |
| `FINDINGS.md` | Corrections and confirmations the PoC produced, with evidence |
| `HARDWARE.md` | Runbook for a session with real CDJs |

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
```

Nothing transmits on a DJ-Link port except `announce`. `--assert-passive` will
fail a run if anything tries.

## Testing without hardware

The codecs are exercised against real captures from `research/ref-repos/`
(git-ignored):

```bash
prolinks pcap research/ref-repos/dysentery/doc/assets/LinkInfo.pcapng
```

272 real packets from a CDJ-2000nexus and a DJM-2000nexus decode and re-encode
byte-for-byte. That is what produced the corrections in `FINDINGS.md`.

## Licensing

GPLv2-or-later, matching Mixxx, so nothing here can be "cleaner" than what may
go upstream. Protocol *facts* are not copyrightable and every reference project
is usable for **reference and inspiration**; their *code* is not copied.
python-prodj-link is Apache-2.0 and dysentery is EPL — both GPLv2-incompatible.
prolink-connect and prolink-cpp are MIT and may be adapted with attribution.
See `research/10` for the working discipline.
