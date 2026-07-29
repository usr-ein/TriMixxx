# S13-format-ground-truth

- started: 2026-07-29T21:21:28Z
- interface: pktap,en12,en9
- description: deck B loads format
  variants from deck A's USB, both decks start off, deck A is 1, deck B is auto

I set deck B to be in AUTO instead of 2. deck A is still at 1. I will start deck B
  first, then deck A with the USB already plugged in. I also want to discover how
  the auto protocol works in announce.

  Ok, test done. I also opened the INFO menu on each track while playing to be sure.

3. Start the capture — on the members, not the bridge

  ./tools/capture.sh S13-format-ground-truth pktap,en12,en9 "deck B loads format
  variants from deck A's USB"

  4. Then, in this order

  1. Insert SAM1 into deck A, power both decks on
  2. On deck B: LINK → deck A → ARTIST → FORMAT TEST
  3. Load these, and jot the order into captures/S13-format-ground-truth/NOTES.md:

  ┌─────┬─────────────────────┬───────┬──────┐
  │  #  │        Track        │  ext  │ disc │
  ├─────┼─────────────────────┼───────┼──────┤
  │ 1   │ MP3 MPEG1 128k 44k1 │ .mp3  │ 2    │
  ├─────┼─────────────────────┼───────┼──────┤
  │ 2   │ AAC 128k 44k1 st    │ .m4a  │ 1    │
  ├─────┼─────────────────────┼───────┼──────┤
  │ 3   │ WAV 16b 44k1        │ .wav  │ 0    │
  ├─────┼─────────────────────┼───────┼──────┤
  │ 4   │ AIFF 16b 44k1       │ .aiff │ 1    │
  └─────┴─────────────────────┴───────┴──────┘

  4. Ctrl-C

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
