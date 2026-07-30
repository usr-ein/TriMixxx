# S24c-e9-noportmap

- started: 2026-07-30T10:45:43Z
- interface: en9
- description: E9: portmap off 111, mountd 48276, nfsd 2049, no root

## Hardware state
- deck A (en12): ip=?  firmware=?  slot=?  media=?
- deck B (en9):  ip=?  firmware=?  slot=?  media=?
- bridge: bridge1 = en12 + en9

## Devices
```
(paste 'prolinks devices' here once the decks are up)
```

## Verdict: E9 FAILS — a portmapper on UDP/111 is mandatory (F46)

```
.venv/bin/prolinks -v serve --volume /Volumes/SAM2 --iface en9 --number 3 \
    --portmap-port 11111 --mountd-port 48276 --nfsd-port 2049
```

Deck never listed us. Not an announce problem — the DJ-Link layer is
byte-identical to the control (us at 3, deck at 2, SD slot advertised present
in all 145 status packets each way).

- t=28.29s the deck sends portmap `GETPORT` to UDP/111
- it repeats **31 times, once per second**, to the end of the capture
- it never tries 48276 or 2049, though both were bound and idle
- no TCP at all: no `12523` port query, no dbserver
- we sent no ICMP port-unreachable, so it was a plain timeout, not a refusal

No media query in this run, because the deck had cached our slot state from
the control 2.5 min earlier and went straight for the mount — consistent with
F37 ("one query per slot, not repeated").

## Timeline
- 0:00 capture started
- 0:28 deck starts hammering UDP/111
- 0:59 capture stopped, deck still retrying 
