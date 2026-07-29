# S2c-deck-a-joins

- capture interface: bridge1
- description: deck B up and settled, deck A cold boots into the occupied network

## Hardware state
- deck A (en12): ip=169.254.103.172  mac=74:5e:1c:56:67:ac  D=1 (MANUAL)  fw=v1.44
- deck B (en9):  ip=169.254.202.84   mac=74:5e:1c:56:ca:54  D=2 (MANUAL)  fw=v1.44
- bridge: bridge1 = en12 + en9, Mac at 169.254.99.100

## Purpose
The mirror of S02, run as an explicit **prediction**. Deck A had come up with
keep-alive byte 0x25 = 0x02 in every capture so far, and had sent three stage-3
packets whenever observed booting. Booting it into a network where deck B was
already present was predicted to flip both.

## Outcome -- BOTH PREDICTIONS CONFIRMED
- deck A sent **1** stage-3 packet (predicted 1) -> C13 closed
- deck A came up **byte 0x25 = 0x01** for the first time ever (predicted 0x01)
  -> O3 closed, promoted to FINDINGS F9
- deck B, which booted alone in this scenario, came up 0x02 as expected

## The rule
A device latches "was I first on this network?" once at boot and never
re-evaluates it. That single latch drives two visible behaviours:
- stage-3 claim repeated **3x** if first, **1x** if joining
- keep-alive byte 0x25 = **0x02** if first, **0x01** if joining

Implemented in VirtualCdj; both handshake variants are golden vectors and our
announcer reproduces each byte-for-byte.
