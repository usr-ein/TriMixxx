"""One served medium: a slot, a library, and the files behind it.

TriMiXxX has two USB ports and should present them to the network as a USB and
an SD, which is what a CDJ expects to see. F37 settled the shape of that, and it
is not the obvious one: a player browsing two media on the same peer opens **one**
dbserver connection and distinguishes them purely by the slot byte in each
request's descriptor. So serving two media is one server holding a medium per
slot, not a server per slot.

This is the per-slot state that used to live on :class:`~prolinks_poc.net.dbserverd.DbServer`
directly. Splitting it out is what makes the second slot a dict entry rather than
a second copy of everything.
"""

from __future__ import annotations

import logging
from dataclasses import dataclass, field
from pathlib import Path

from ..proto import anlz, mysetting
from ..proto.pdb import Pdb
from .library import Library
from .slots import PDB_PATH, MediaSlot, export_path_for

log = logging.getLogger(__name__)

__all__ = ["Medium"]


@dataclass
class Medium:
    """A library plus the medium it came from, bound to a slot."""

    slot: MediaSlot
    library: Library
    #: Where the medium is mounted locally. ``None`` for a synthetic library,
    #: in which case artwork and analysis are simply unavailable.
    root: Path | None = None
    #: Volume label, reported in the media-query reply and shown on the deck.
    volume_name: str = ""
    #: The medium's saved utility settings (F38), or empty if it has none.
    settings: bytes = b""

    #: track id -> (.DAT, .EXT). A load asks for four tags across the two files
    #: within milliseconds, and asks again when the DJ reloads the same track.
    _analysis_cache: dict[int, tuple] = field(default_factory=dict, repr=False)

    # -- construction ----------------------------------------------------

    @classmethod
    def from_volume(cls, volume: Path, slot: MediaSlot) -> "Medium":
        """Read a mounted rekordbox medium.

        Raises ``FileNotFoundError`` if it holds no ``export.pdb`` -- serving a
        medium we cannot enumerate would put an empty library on the network,
        and a deck told a medium has no tracks has no reason to offer it (F24).
        """
        volume = Path(volume)
        pdb_path = volume / PDB_PATH
        library = Library(Pdb(pdb_path.read_bytes()))
        settings = b""
        try:
            settings = mysetting.settings_payload(
                (volume / mysetting.MY_SETTING_PATH).read_bytes()
            )
        except OSError:
            pass  # a medium without saved settings is normal
        return cls(
            slot=slot,
            library=library,
            root=volume,
            volume_name=volume.name,
            settings=settings,
        )

    # -- identity --------------------------------------------------------

    @property
    def export_path(self) -> str:
        """The NFS export a player mounts for this slot: ``/C/`` or ``/B/``."""
        return export_path_for(self.slot)

    @property
    def vfs_prefix(self) -> str:
        """Subtree this medium occupies in the shared VFS.

        Derived from the export so the two never disagree: ``/C/`` becomes
        ``C``. Keeping the media in separate subtrees is what keeps their
        filehandles distinct -- see :meth:`~prolinks_poc.net.vfs.Vfs.mount_directory`.
        """
        return self.export_path.strip("/")

    @property
    def track_count(self) -> int:
        return len(self.library.tracks)

    @property
    def playlist_count(self) -> int:
        return sum(1 for p in self.library.playlists.values() if not p.is_folder)

    # -- files on the medium ---------------------------------------------

    def artwork_for(self, artwork_id: int) -> bytes:
        """The cover image for *artwork_id*, or empty if unavailable."""
        path = self.library.artwork.get(artwork_id)
        if not path or self.root is None:
            return b""
        try:
            return (self.root / path.lstrip("/")).read_bytes()
        except OSError:
            log.debug("no artwork at %s on %s", path, self.volume_name)
            return b""

    def analysis_files(self, track_id: int):
        """The parsed ``.DAT`` and ``.EXT`` for a track, either possibly ``None``.

        Both are read together because a load wants tags from each; parsing a
        container is walking a tag list, far cheaper than the second file read
        it saves. Anything missing or corrupt comes back as ``None``: a track
        analysed by an older rekordbox legitimately lacks the newer tags, and a
        missing waveform should cost the waveform, not the load.
        """
        track = self.library.tracks.get(track_id)
        if track is None or self.root is None or not track.analyze_path:
            return None, None
        cached = self._analysis_cache.get(track_id)
        if cached is not None:
            return cached

        def load(relative: str):
            if not relative:
                return None
            try:
                data = (self.root / relative.lstrip("/")).read_bytes()
            except OSError:
                log.debug("no analysis file at %s for track %s", relative, track_id)
                return None
            try:
                return anlz.AnlzFile(data)
            except Exception:
                log.debug("could not parse %s", relative)
                return None

        pair = (load(track.analyze_path), load(track.analyze_ext_path))
        self._analysis_cache[track_id] = pair
        return pair

    def __str__(self) -> str:
        return (
            f"{self.slot.name} {self.export_path} {self.volume_name or '(unnamed)'}: "
            f"{self.track_count} tracks, {self.playlist_count} playlists"
        )
