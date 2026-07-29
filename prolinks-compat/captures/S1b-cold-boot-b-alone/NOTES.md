# S1b-cold-boot-b-alone

- capture interface: bridge1
- description: deck B cold boots into an empty network (deck A powered off)

## Hardware state
- deck A (en12): POWERED OFF for this scenario
- deck B (en9):  ip=169.254.202.84  mac=74:5e:1c:56:ca:54  D=2 (MANUAL)
                 firmware=v1.44  media=none
- bridge: bridge1 = en12 + en9, Mac at 169.254.99.100

## Purpose
Isolate the variable behind the stage-3 repeat count (FINDINGS C13). Deck A
booting alone sent three; deck B joining an occupied network sent one. Peer
presence and the number claimed were confounded -- this re-boots deck B with
nothing else on the wire, holding the number fixed.

## Timeline
- 0:00 deck B powered on, three DHCP DISCOVERs, falls back to link-local
- 0:00 3x Hello, 3x ClaimMac, 3x ClaimIp (D=2, MANUAL), **3x ClaimNumber**
- 0:03 keep-alives begin, peers=1, byte 0x25 = 0x02

## Outcome
- 29/29 DJ-Link packets round-trip byte-exact
- **C13 RESOLVED**: deck B sent 3 stage-3 packets alone vs 1 when joining.
  Peer presence at boot governs the count, not the assignment mode.
- **O3 narrowed sharply**: deck B came up byte 0x25 = 0x02 here, but 0x01 in
  S02 where deck A was already present. The byte is fixed at boot and held for
  the session -- constant in all ten device-sessions across all five captures
  we have. Same variable as C13.

## Next
- S2c: boot deck A into a network with deck B already up. Deck A has come up
  0x02 every time so far; 0x01 would confirm the hypothesis and close O3.
