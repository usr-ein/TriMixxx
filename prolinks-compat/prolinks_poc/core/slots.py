"""Media slots and their NFS export paths.

``research/06`` §3 gives SD as ``/B/``, USB as ``/C/`` and the rekordbox
collection as ``/``. Real captures show that is **not the whole story**: in
``LinkInfo.pcapng`` one player mounts ``/C/`` on one peer and ``/C/EXPORT`` on
another, in the same session. The prefix identifies the slot; the rest varies
by device or firmware.

So the table below is a *fallback*, and :func:`match_export` is the preferred
path: enumerate with MOUNT ``EXPORT`` -- which real players do, and which real
players answer -- then match by prefix. See FINDINGS.md, correction C6.

``/A/`` is used by no observed client and is presumed internal.
"""

from __future__ import annotations

import enum

__all__ = [
    "MediaSlot",
    "export_path_for",
    "export_prefix_for",
    "match_export",
    "slot_from_name",
    "PDB_PATH",
    "PDB_EXT_PATH",
]


class MediaSlot(enum.IntEnum):
    """Values match the slot numbering used in CDJ status packets."""

    NONE = 0
    CD = 1
    SD = 2
    USB = 3
    REKORDBOX = 4


_EXPORTS = {
    MediaSlot.SD: "/B/",
    MediaSlot.USB: "/C/",
    MediaSlot.REKORDBOX: "/",
}

_NAMES = {
    "sd": MediaSlot.SD,
    "usb": MediaSlot.USB,
    "rb": MediaSlot.REKORDBOX,
    "rekordbox": MediaSlot.REKORDBOX,
    "cd": MediaSlot.CD,
}

#: Where the rekordbox database lives on every export, relative to its root.
#: ``research/05``. Whether the leading directory is ``PIONEER`` or
#: ``.PIONEER`` on an HFS-formatted stick is experiment E6.
PDB_PATH = "PIONEER/rekordbox/export.pdb"
#: Newer media carry a second database alongside it. The exact filename is
#: marked "needs confirmation" in ``research/06`` §6 -- treat a miss as normal.
PDB_EXT_PATH = "PIONEER/rekordbox/exportExt.pdb"


def export_path_for(slot: MediaSlot) -> str:
    """The documented NFS export string for *slot*.

    Only a fallback for when ``EXPORT`` is unavailable -- prefer
    :func:`match_export`.
    """
    try:
        return _EXPORTS[slot]
    except KeyError:
        raise ValueError(f"{slot.name} has no NFS export path") from None


def export_prefix_for(slot: MediaSlot) -> str:
    """The slot's drive-letter prefix, e.g. ``/C/`` for USB.

    Both ``/C/`` and ``/C/EXPORT`` have been observed for the USB slot, so
    matching on the prefix is what survives the variation.
    """
    return export_path_for(slot)


def match_export(exports, slot: MediaSlot) -> str | None:
    """Pick the export serving *slot* from a MOUNT ``EXPORT`` listing.

    *exports* is a sequence of :class:`~prolinks_poc.proto.mountd.Export` or of
    plain strings. Returns the matching path, or ``None`` if the slot is not
    exported -- which is the normal answer for an empty slot, not an error.

    Prefers an exact match on the documented path, then falls back to any
    export sharing the slot's prefix, so ``/C/EXPORT`` is found for USB
    without hardcoding that spelling.
    """
    prefix = export_prefix_for(slot)
    paths = [getattr(export, "path", export) for export in exports]
    if prefix in paths:
        return prefix
    for path in paths:
        if path.startswith(prefix):
            return path
    # The rekordbox export is "/", which prefixes everything, so it can only
    # be claimed when nothing more specific matched.
    if prefix == "/" and paths:
        return paths[0]
    return None


def slot_from_name(name: str) -> MediaSlot:
    """Parse the ``--slot`` argument."""
    try:
        return _NAMES[name.lower()]
    except KeyError:
        options = ", ".join(sorted(_NAMES))
        raise ValueError(f"unknown slot {name!r}; expected one of: {options}") from None
