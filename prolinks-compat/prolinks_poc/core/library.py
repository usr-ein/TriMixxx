"""The library model: a parsed ``export.pdb`` with its foreign keys resolved.

The pdb stores tracks with integer references into side tables — artist, album,
genre, key, colour, artwork. This joins them once so the rest of the program
deals in strings, and exposes the playlist tree.

This is the shape the Mixxx feature needs too: it is what
``prolink_library`` / ``prolink_playlists`` / ``prolink_playlist_tracks`` get
populated from, so keeping the field names aligned with Mixxx's columns is
deliberate.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from ..proto.pdb import PageType, Pdb

__all__ = ["Track", "Playlist", "Library"]


@dataclass
class Track:
    """One track, with its references resolved to names."""

    id: int
    title: str
    artist: str = ""
    album: str = ""
    genre: str = ""
    key: str = ""
    label: str = ""
    comment: str = ""
    color: str = ""
    #: Absolute path to the audio file **in the player's namespace**, taken
    #: from the pdb. Concatenate onto a cache root to get a local path; that
    #: is exactly what Mixxx's Rekordbox feature already does for USB media.
    path: str = ""
    filename: str = ""
    #: Path to the ANLZ ``.DAT``; the ``.EXT`` is the same with the extension
    #: swapped. Drives the beatgrid/cue fetch.
    analyze_path: str = ""
    artwork_path: str = ""
    bpm_100: int = 0
    duration: int = 0
    bitrate: int = 0
    sample_rate: int = 0
    file_size: int = 0
    track_number: int = 0
    year: int = 0
    rating: int = 0
    date_added: str = ""

    #: The raw foreign keys, kept alongside the resolved names. A dbserver
    #: metadata item carries the *referenced row's* id -- artist 122, album 86 --
    #: not the track's own, and a player uses it to open "more from this
    #: artist". Sending the track id there is wrong in a way that still renders
    #: correctly, so it survives casual inspection.
    artist_id: int = 0
    album_id: int = 0
    genre_id: int = 0
    key_id: int = 0
    label_id: int = 0
    color_id: int = 0
    artwork_id: int = 0

    #: Container/codec identifier (:class:`~prolinks_poc.proto.pdb.FileType`).
    #: A player takes our word for this, so getting it wrong makes it fetch
    #: the file and then refuse to decode it.
    file_type: int = 0
    disc_number: int = 0

    @property
    def bpm(self) -> float:
        """Stored as an integer ×100 so the wire format has no floats."""
        return self.bpm_100 / 100.0

    @property
    def duration_text(self) -> str:
        return f"{self.duration // 60}:{self.duration % 60:02d}"

    @property
    def analyze_ext_path(self) -> str:
        """The ``.EXT`` companion, by extension swap (``research/05`` §1)."""
        if not self.analyze_path:
            return ""
        base = self.analyze_path.rsplit(".", 1)[0]
        return f"{base}.EXT"

    def __str__(self) -> str:
        artist = self.artist or "(unknown artist)"
        return (
            f"{self.id:>8}  {self.duration_text:>6}  {self.bpm:>6.1f}  "
            f"{self.key:<4} {artist[:28]:<28}  {self.title[:40]}"
        )


@dataclass
class Playlist:
    id: int
    name: str
    parent_id: int
    is_folder: bool
    sort_order: int = 0
    track_ids: list[int] = field(default_factory=list)
    children: list["Playlist"] = field(default_factory=list)

    @property
    def track_count(self) -> int:
        return len(self.track_ids)


class Library:
    """Everything on one medium, assembled from its ``export.pdb``."""

    def __init__(self, pdb: Pdb) -> None:
        self.pdb = pdb

        def by_id(page_type, value_key="name") -> dict[int, str]:
            return {
                row["id"]: row.get(value_key, "") for row in pdb.rows(page_type)
            }

        self.artists = by_id(PageType.ARTISTS)
        self.albums = by_id(PageType.ALBUMS)
        self.genres = by_id(PageType.GENRES)
        self.keys = by_id(PageType.KEYS)
        self.labels = by_id(PageType.LABELS)
        self.colors = by_id(PageType.COLORS)
        self.artwork = by_id(PageType.ARTWORK, "path")

        #: track id -> artwork id, kept because a menu item must carry it or
        #: the player never asks for the image.
        self.artwork_ids: dict[int, int] = {}
        self.tracks: dict[int, Track] = {}
        for row in pdb.rows(PageType.TRACKS):
            track = Track(
                id=row["id"],
                title=row.get("title", ""),
                artist=self.artists.get(row.get("artist_id", 0), ""),
                album=self.albums.get(row.get("album_id", 0), ""),
                genre=self.genres.get(row.get("genre_id", 0), ""),
                key=self.keys.get(row.get("key_id", 0), ""),
                label=self.labels.get(row.get("label_id", 0), ""),
                color=self.colors.get(row.get("color_id", 0), ""),
                artwork_path=self.artwork.get(row.get("artwork_id", 0), ""),
                comment=row.get("comment", ""),
                path=row.get("path", ""),
                filename=row.get("filename", ""),
                analyze_path=row.get("analyze_path", ""),
                bpm_100=row.get("bpm_100", 0),
                duration=row.get("duration", 0),
                bitrate=row.get("bitrate", 0),
                sample_rate=row.get("sample_rate", 0),
                file_size=row.get("file_size", 0),
                track_number=row.get("track_number", 0),
                year=row.get("year", 0),
                rating=row.get("rating", 0),
                date_added=row.get("date_added", ""),
                artist_id=row.get("artist_id", 0),
                album_id=row.get("album_id", 0),
                genre_id=row.get("genre_id", 0),
                key_id=row.get("key_id", 0),
                label_id=row.get("label_id", 0),
                color_id=row.get("color_id", 0),
                artwork_id=row.get("artwork_id", 0),
                file_type=row.get("file_type", 0),
                disc_number=row.get("disc_number", 0),
            )
            self.tracks[track.id] = track
            self.artwork_ids[track.id] = row.get("artwork_id", 0)

        self.playlists: dict[int, Playlist] = {
            row["id"]: Playlist(
                id=row["id"],
                name=row.get("name", ""),
                parent_id=row.get("parent_id", 0),
                is_folder=row.get("is_folder", False),
                sort_order=row.get("sort_order", 0),
            )
            for row in pdb.rows(PageType.PLAYLIST_TREE)
        }

        # Entries carry an explicit ordering field; the on-disk row order is
        # not the playlist order.
        entries = sorted(
            pdb.rows(PageType.PLAYLIST_ENTRIES),
            key=lambda row: row.get("entry_index", 0),
        )
        for row in entries:
            playlist = self.playlists.get(row.get("playlist_id", 0))
            if playlist is not None:
                playlist.track_ids.append(row["track_id"])

        for playlist in self.playlists.values():
            parent = self.playlists.get(playlist.parent_id)
            if parent is not None and parent is not playlist:
                parent.children.append(playlist)

    @classmethod
    def from_file(cls, path: Path | str) -> "Library":
        return cls(Pdb(Path(path).read_bytes()))

    @classmethod
    def from_bytes(cls, data: bytes) -> "Library":
        return cls(Pdb(data))

    # -- queries ---------------------------------------------------------

    @property
    def root_playlists(self) -> list[Playlist]:
        """Top-level playlists and folders, in rekordbox's own sort order."""
        return sorted(
            (p for p in self.playlists.values() if p.parent_id not in self.playlists),
            key=lambda p: (p.sort_order, p.name),
        )

    def track_list(self) -> list[Track]:
        return sorted(self.tracks.values(), key=lambda t: (t.artist.lower(), t.title.lower()))

    def playlist_tracks(self, playlist_id: int) -> list[Track]:
        playlist = self.playlists.get(playlist_id)
        if playlist is None:
            return []
        return [self.tracks[tid] for tid in playlist.track_ids if tid in self.tracks]

    def search(self, term: str) -> list[Track]:
        term = term.lower()
        return [
            track
            for track in self.track_list()
            if term in track.title.lower()
            or term in track.artist.lower()
            or term in track.album.lower()
        ]

    def summary(self) -> dict[str, int]:
        return {
            "tracks": len(self.tracks),
            "artists": len(self.artists),
            "albums": len(self.albums),
            "genres": len(self.genres),
            "keys": len(self.keys),
            "playlists": sum(1 for p in self.playlists.values() if not p.is_folder),
            "folders": sum(1 for p in self.playlists.values() if p.is_folder),
        }

    def format_playlist_tree(self, playlists=None, depth: int = 0) -> list[str]:
        lines = []
        for playlist in playlists if playlists is not None else self.root_playlists:
            marker = "[+]" if playlist.is_folder else "   "
            count = "" if playlist.is_folder else f"  ({playlist.track_count} tracks)"
            lines.append(f"{'  ' * depth}{marker} {playlist.name}{count}  #{playlist.id}")
            if playlist.children:
                lines.extend(
                    self.format_playlist_tree(
                        sorted(playlist.children, key=lambda p: (p.sort_order, p.name)),
                        depth + 1,
                    )
                )
        return lines
