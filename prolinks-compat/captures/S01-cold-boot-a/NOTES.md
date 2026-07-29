# S01-cold-boot-a

- started: 2026-07-29T17:13:46Z
- capture interface: bridge1
- description: deck A cold boot, alone, no media

## Hardware state
- deck A (en12): ip=169.254.103.172  mac=74:5e:1c:56:67:ac  D=1 (set MANUALLY)
                 firmware=v1.44  slot=-  media=none
- deck B (en9):  powered off for this scenario
- bridge: bridge1 = en12 + en9, Mac at 169.254.99.100

## Devices
```
  D  name                  ip               mac                kind   model
  1  CDJ-2000nexus         169.254.103.172  74:5e:1c:56:67:ac  CDJ    nexus/earlier

literal 20-byte name field:
  'CDJ-2000nexus'  43444a2d323030306e6578757300000000000000

device numbers ever seen: [1]
free player numbers 1-6:  [2, 3, 4, 5, 6]
```

## Timeline
- -0:09 deck A powered on; three DHCP DISCOVERs, no server, falls back to link-local
-  0:00 first DJ-Link packet: 3x Hello, 300 ms apart
-  0:01 3x ClaimMac, 3x ClaimIp (D=1, byte 0x31 = 02), 3x ClaimNumber
-  0:04 keep-alives begin, every 2.003 s, peers=1
-  1:01 capture stopped

## Outcome
- 42/42 DJ-Link packets round-trip byte-exact
- FINDINGS C12: keep-alive cadence is 2.0026 s, not the documented 1.5 s
- FINDINGS O3: byte 0x25 was 0x02 for all 30 keep-alives while alone (peers=1)
- FINDINGS O5: byte 0x31 = 02 ("manual") yet three stage-3 packets were sent,
  which the doc says only happens on AUTO. **Need deck A's PLAYER No. setting.**
- keep-alive committed as a golden vector: tests/test_djl.py::NXS_KEEPALIVE

## Resolved
- PLAYER No. is set **manually to 1**, not AUTO. Byte 0x31 = 02 therefore does
  mean "manual" as documented -- but the deck sent three stage-3 packets, where
  research/02 §1.0 says manual sends one. See FINDINGS C13.
- Firmware v1.44. (prolink-connect's synthetic status packet hardcodes the
  firmware string "1.43", so 1.44 is the value to use when we get to
  impersonating status packets on UDP 50002.)

## Still to record
- deck B firmware and PLAYER No. setting, before S2
