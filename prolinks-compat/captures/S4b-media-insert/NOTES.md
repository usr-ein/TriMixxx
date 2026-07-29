# S4b-media-insert

- started: 2026-07-29T18:25:24Z
- interface: pktap,en12,en9
- description: both decks up; insert, eject, re-insert -- on the correct tap

1. Start the capture.
2. Insert stick A into deck A. Wait 20s.
3. Eject stick B from deck B (hold USB EJECT). Wait 20s.
4. Re-insert stick B. Wait 20s.
5. On deck A, press USB and scroll a little. Wait 20s.
6. Ctrl-C

## Hardware state
- deck A (en12): ip=?  firmware=?  slot=?  media=?
- deck B (en9):  ip=?  firmware=?  slot=?  media=?
- bridge: bridge1 = en12 + en9

## Devices
```
(paste 'prolinks devices' here once the decks are up)
```

## Timeline
- 0:00 capture started
- 
