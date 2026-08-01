# S28-master-beat-sync-taglist

- started: 2026-07-31T15:10:31Z
- interface: pktap,en9,en12
- description: Try the master button, the beat sync button, and the 'Tag Track' and 'Remove' tag list buttons. Also load the tag list

In this capture I also:
added to the track list, both on the deck A (which has the USB) and deck B (remote).
The tag status shows on both live, so smthg over the wire for sure.

I then removed from tracking some songs, from both decks.
I then went in the "TAG LIST" menu, on both CDJs.
I then sorted the TAG LIST there by various parameters.
I then created a new playlist from the tag list, to check.
I then Cleared the tag list from the menu.

For beat sync, I enabled it from both CDJs, in both orders.
I then alternated who is the master.
I exercised the tempo fader change, witnessed on both CDJs.
I then "caught up" the tempo after switching the master:
 - when a cdj is not the master, its tempo fader does nothing.
 - when it takes the master, but its tempo fader is at a different position than that of
 the other cdj, it needs to "catch" the tempo from the other one, which it does and then drives
 the global tempo.

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
