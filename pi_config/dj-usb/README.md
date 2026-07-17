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
