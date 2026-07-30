# S24b-e9-control

- started: 2026-07-30T10:43:11Z
- interface: en9
- description: control: portmap on 111, as sudo, with mac on player 3

sudo .venv/bin/prolinks -v serve --volume /Volumes/SAM2 --iface en9 --number 3

worked.
I also triggered the SYNC button just for lulz and recorded it.

## Hardware state
- deck A (en12): ip=?  firmware=?  slot=?  media=?
- deck B (en9):  ip=?  firmware=?  slot=?  media=?
- bridge: bridge1 = en12 + en9

## Devices
```
(paste 'prolinks devices' here once the decks are up)
```

## Timeline (control for E9 — see S24c)

- 0:00 capture started
- 7.60s deck sends the media query (`0x05`); we answer
- 44.09s deck sends portmap `GETPORT` ×2 to UDP/111; we answer both
- 44.09s deck immediately mounts: `MNT` to the mountd port we just advertised
- 44.11s **only then** TCP `12523` port query
- 44.12s dbserver connection opens
- 52.08s NFS `READ`s begin
- SYNC button pressed near the end, recorded on 50001

**Ordering finding (F46):** portmap and the NFS mount come *before* the
dbserver port query. `PROTOCOL.md` §6 listed dbserver ahead of NFS; the deck
does the opposite, which is why a missing portmapper stops it being listed at
all rather than merely stopping playback.
