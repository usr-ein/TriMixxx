# DJ USB
This is installed on the Pi and lets us plug and yank USB sticks freely, safely, with no corruption of the stick's memory ever.

Mounts DJ sticks READ-ONLY at /media/DJ_USB_1 / /media/DJ_USB_2. Driven by
99-dj-usb.rules -> dj-usb@.service; not meant to be run by hand except to
debug.

Read-only is the whole safety model: with no writes there are no dirty pages,
so a stick can be yanked mid-set without corrupting a DJ's library. Mixxx
tolerates this -- its Rekordbox importer opens export.pdb and the ANLZ files
with std::ifstream::binary and parks everything it reads in Mixxx's own
database, never on the device.

The slot path matters and is not cosmetic. Mixxx's RekordboxFeature scans
/media, /media/$USER and /run/media/$USER, takes their IMMEDIATE children
only, and tests <child>/PIONEER/rekordbox/export.pdb. So /media/DJ_USB_1 is
found and /media/usb/DJ_USB_1 would not be.

## Writing a recorded set

`dj-usb write SLOT FILE...` is the one write the deck makes to a stick: a
recorded set ("Record set" on the browser's home list), its MP3 and its track
list, copied to the stick's root. Mixxx runs it as root through `sudo -n`
(`dj-usb-write.sudoers`, installed as `/etc/sudoers.d/dj-usb-write`).

- Only a recording, out of `/var/lib/rauc/trimixxx-recording`, onto one of
  the deck's own slots, under a name not already there.
- Only FAT32, exFAT and ext sticks: Linux does not write a journaled HFS+, and
  NTFS is left alone.
- The room is checked before anything changes.
- Under the same lock as mount and unmount, the stick is remounted read-write,
  each file is copied under a hidden name (`.NAME.part`), 16 MB at a time,
  each block on the stick before the next (so the FAT and the root
  directory are written ~27 times for a 3-hour set, and little waits in the
  page cache), synced and renamed, and the stick is remounted read-only,
  however the copy ends (a trap). Neither end of the copy follows a link.
- The copy may take 15 minutes at most (`COPY_LIMIT_S`: half a megabyte a
  second for 3 hours), then fails rather than hold the lock.
- Mixxx reads the progress off the stick's own count of what it has written
  (`/sys/class/block/DEV/stat`).
- Exits: 0 written, 2 refused, 3 too little room, 4 a filesystem it does not
  write, 5 the write failed (a stick pulled, or one too slow), 6 the name is
  taken.

While a set is written, the other port waits: a stick put in mounts once the
copy is done, and one pulled is unmounted then. `dj-usb@.service`'s stop
timeout (20 minutes) is set past the copy's cap for that wait; the default
90 s would kill the unmount and leave the slot mounted on a stick that has
gone.

A stick pulled mid-copy is left with at most a hidden part file, and FAT's
"not cleanly unmounted" flag, which Windows and macOS offer to repair; nothing
of the DJ's library is ever written.
