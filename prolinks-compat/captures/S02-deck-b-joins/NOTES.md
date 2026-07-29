# S02-deck-b-joins

- capture interface: bridge1
- description: deck A settled at D=1, deck B cold boots into the occupied network

## Hardware state
- deck A (en12): ip=169.254.103.172  mac=74:5e:1c:56:67:ac  D=1 (MANUAL)
                 firmware=v1.44  media=none
- deck B (en9):  ip=169.254.202.84   mac=74:5e:1c:56:ca:54  D=2 (MANUAL)
                 firmware=v1.44  media=none
- bridge: bridge1 = en12 + en9, Mac at 169.254.99.100

## Devices
```
  D  name                  ip               mac                kind   model
  1  CDJ-2000nexus         169.254.103.172  74:5e:1c:56:67:ac  CDJ    nexus/earlier
  2  CDJ-2000nexus         169.254.202.84   74:5e:1c:56:ca:54  CDJ    nexus/earlier
```
Note: both decks report the identical 20-byte name, so **name does not identify
a device** -- group by MAC. Grouping by name is what produced the wrong first
reading of keep-alive byte 0x25 (FINDINGS O3).

## Timeline
-  0:00 capture started, deck A alone and settled (peers=1)
- 21:31 deck B powered on: 3x Hello, 3x ClaimMac, 3x ClaimIp (D=2, byte 0x31 = 02 MANUAL)
- 24:01 deck B sends **one** ClaimNumber (N=1) -- deck A had sent three in S01
- 24:03 deck A's peer count goes 1 -> 2
- capture stopped

## Outcome
- 154/154 DJ-Link packets round-trip byte-exact
- **tap verified**: both decks visible through the bridge, both directions
- no conflict packets -- expected, the decks hold different numbers
- FINDINGS C13 **revised**: deck A (manual, alone) sent 3 stage-3 packets;
  deck B (manual, joining) sent 1. The repeat count is not governed by the
  assignment mode. Still to isolate: peer presence vs the number being claimed.
- FINDINGS O3: byte 0x25 is 0x02 for deck A and 0x01 for deck B, each perfectly
  stable -- including across deck A's peer count changing from 1 to 2. That
  rules out peer count, role, and "the other player's number".

## Follow-ups this suggests
- S1b: boot deck B **alone** -- does it send 1 or 3 stage-3 packets?
- S2c: boot deck A into a network where deck B is already up.
