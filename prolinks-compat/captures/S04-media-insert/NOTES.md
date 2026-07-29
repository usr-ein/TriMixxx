# S04-media-insert

- capture interface: bridge1
- description: both decks up; insert stick A into deck A, stick B into deck B,
  eject A to the Mac for the anchor test, re-insert, browse on the deck

## Hardware state
- deck A (en12): ip=169.254.103.172  D=1  fw=v1.44  USB=stick A (SAM2)
- deck B (en9):  ip=169.254.202.84   D=2  fw=v1.44  USB=stick B
- neither deck has an SD card inserted
- bridge: bridge1 = en12 + en9, Mac at 169.254.99.100

## Outcome -- the big ones
- **E4 PASSED. A CDJ-2000NXS serves NFS.** Both decks: nfsd 2049, mountd 48276,
  portmapper 111. Same ports libcdj saw on an XDJ and dysentery's capture showed.
  -> FINDINGS F10
- **E1 CONFIRMED under an armed guard.** Zero datagrams on any DJ-Link port
  while pulling a megabyte over NFS. -> FINDINGS F11
- **E3 RESOLVED.** Both decks export '/C/', raw 2f0043002f00, confirming both
  the path and the UTF-16LE encoding. groups = 169.254.0.0/255.255.0.0, i.e. the
  whole link-local subnet -- which is *why* passive access works. -> FINDINGS F12
- **Anchor test passed.** 1,077,248-byte export.pdb pulled in 842 READs, zero
  short reads, 1459 KiB/s. Differs from the physically-read file in exactly two
  header bytes: the deck's own write counter. -> FINDINGS F13
- **pdb parser first contact with real data**: 692 tracks, 329 artists, 35
  playlists, identical from both copies. -> FINDINGS F14

## Outcome -- the negative result
- **Media insert/eject produced ZERO DJ-Link traffic.** 844 keep-alives and
  nothing else: no 50001, no 50002, no TCP. Passively, a player never tells us
  its media changed. -> FINDINGS F15, which changes the Mixxx design: poll
  MOUNT EXPORT rather than listen on 50002.

## Still to check
- insert an SD card and re-run `exports` -- does /B/ appear? The polling design
  in F15 depends on EXPORT listing only populated slots.
