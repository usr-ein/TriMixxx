"""A read-only virtual filesystem behind the NFS server — the Phase C seam.

Objective 2 is to serve Mixxx's own library to real CDJs. Whatever backs that
tree later -- a real directory, or a synthesised ``/PIONEER/`` layout generated
from Mixxx's database -- the NFS wire layer above it should not have to change.
So the wire layer talks only to this interface, and the backing store is
swappable.

**Filehandles, and the one place a CDJ breaks the spec.** NFSv2 says a handle
is 32 *opaque* bytes that the client must echo back verbatim. **A CDJ-2000NXS
does not.** Handed a handle, it returns one whose first 12 bytes match ours and
whose remaining 20 it has rewritten with its own data:

```
served:   8a5edab282632443219e051e 4ade2d1d5bbc671c781051bf1437897cbdfea0f1
returned: 8a5edab282632443219e051e 03012d0000001b58000000000303010000000162
          |____ first 12 kept ____| |______ replaced by the player _______|
```

That fits the shape of a real player's own handles, which are a 4-byte value
repeated three times followed by zeros (`01c1cec8 01c1cec8 01c1cec8 00…`) --
evidently the leading 12 bytes are the volume identity and the rest is the
player's own file reference, so it feels free to overwrite them.

Consequently **only the first 12 bytes can be relied upon**, and the handle
table is keyed on those. Handles are a truncated SHA-256 of the path, so 12
bytes is still ample to be collision-free and deterministic -- the same tree
yields the same handles, and a client's cached root handle survives a restart.
Anything not in the table is ``NFSERR_STALE``, which is the behaviour
experiment E8 expects after a media swap.

**Unicode normalisation.** A name can reach us spelled differently from how the
filesystem spells it, and the two must still match. On the author's own stick
``export.pdb`` stores ``02. Akiba - カガミ.mp3`` composed (NFC, ``U+30AC``) while
the filesystem reports it decomposed (NFD, ``U+30AB U+3099``) -- rekordbox wrote
the database and the file through different APIs. A player looking up the path
the database gave it therefore asks for a name that, byte for byte, is not the
one in our directory listing, and an exact match answers ``NFSERR_NOENT`` for a
file that is plainly there. :meth:`Vfs.lookup` falls back to comparing NFC
forms, and always returns the handle for the name **as stored**, so that every
handle we hand out is one we can resolve again later.
"""

from __future__ import annotations

import hashlib
import unicodedata
from dataclasses import dataclass, field
from pathlib import Path

from ..proto.nfs2 import FHANDLE_SIZE, FType, Fattr

__all__ = ["VfsNode", "Vfs"]

#: Fixed timestamp for synthesised attributes. Deterministic on purpose:
#: byte-identical replies across runs make capture diffs meaningful.
_EPOCH = 1_600_000_000


def _fold(name: str) -> str:
    """The key two spellings of the same filename must agree on.

    A rekordbox medium is FAT32 -- case-insensitive and case-preserving -- and
    ``export.pdb`` does not necessarily record a name with the same case as the
    directory entry it refers to. On the author's stick the database says
    ``Gesaffelstein`` where the directory is ``GESAFFELSTEIN``, and
    ``Hard Work Always Pays Off`` where the directory is
    ``Hard work always pays off``. A real player resolves those through its FAT
    driver without noticing; a server doing exact byte comparison answers
    ``NFSERR_NOENT`` and the track will not load.
    """
    return unicodedata.normalize("NFC", name).casefold()


@dataclass
class VfsNode:
    """A file or directory.

    A node is backed either by ``data`` held in memory or by ``source_path`` on
    disk. The second form matters as soon as we serve a real USB stick: reading
    a 60 GB library into memory to answer a 1280-byte READ is not an option, so
    file-backed nodes record their size up front and read the requested range
    on demand.
    """

    name: str
    is_dir: bool
    data: bytes | None = None
    source_path: Path | None = None
    file_size: int = 0
    children: dict[str, "VfsNode"] = field(default_factory=dict)
    fileid: int = 0

    def add_child(self, child: "VfsNode") -> None:
        self.children[child.name] = child

    def child_named(self, name: str) -> "VfsNode | None":
        """Find a child by *name*, the way the medium's own filesystem would.

        The exact hit is tried first and is what almost every lookup takes, so
        a server backed by a genuinely case-sensitive tree still resolves two
        names differing only in case correctly. Only on a miss do we fall back
        to the fold, which is what makes a rekordbox medium work at all.
        """
        child = self.children.get(name)
        if child is not None:
            return child
        wanted = _fold(name)
        for candidate in self.children.values():
            if _fold(candidate.name) == wanted:
                return candidate
        return None

    @property
    def size(self) -> int:
        if self.is_dir:
            return 0
        return len(self.data) if self.data is not None else self.file_size

    def read(self, offset: int, count: int) -> bytes:
        """Read a byte range, from memory or from disk."""
        if self.is_dir:
            return b""
        if self.data is not None:
            return self.data[offset : offset + count]
        if self.source_path is None:
            return b""
        try:
            with self.source_path.open("rb") as handle:
                handle.seek(offset)
                return handle.read(count)
        except OSError:
            # A file that vanished mid-session (media ejected) reads as empty
            # rather than taking the server down.
            return b""


class Vfs:
    """A tree of :class:`VfsNode`, addressable by 32-byte filehandle."""

    def __init__(self) -> None:
        self.root = VfsNode(name="", is_dir=True, fileid=1)
        self._handles: dict[bytes, VfsNode] = {}
        self._paths: dict[bytes, str] = {}
        self._next_fileid = 2
        self._register(self.root, "/")

    # -- construction ----------------------------------------------------

    @classmethod
    def from_mapping(cls, files: dict[str, bytes]) -> "Vfs":
        """Build from ``{"PIONEER/rekordbox/export.pdb": b"..."}``."""
        vfs = cls()
        for path, data in files.items():
            vfs.add_file(path, data)
        return vfs

    @classmethod
    def from_directory(cls, directory: Path, follow_symlinks: bool = False) -> "Vfs":
        """Mirror a real directory, reading file contents lazily.

        Only the tree structure and file sizes are walked up front; contents
        are read per-request. That is what makes serving a mounted USB stick
        practical.
        """
        directory = Path(directory)
        vfs = cls()
        for path in sorted(directory.rglob("*")):
            try:
                if not path.is_file() or (path.is_symlink() and not follow_symlinks):
                    continue
                size = path.stat().st_size
            except OSError:
                continue
            vfs.add_disk_file(str(path.relative_to(directory)), path, size)
        return vfs

    def add_disk_file(self, path: str, source: Path, size: int) -> VfsNode:
        """Register a file whose contents stay on disk until requested."""
        node = self.add_file(path, b"")
        node.data = None
        node.source_path = Path(source)
        node.file_size = size
        return node

    def add_file(self, path: str, data: bytes) -> VfsNode:
        components = [part for part in path.replace("\\", "/").split("/") if part]
        if not components:
            raise ValueError("cannot add a file at the root path")
        node = self.root
        walked: list[str] = []
        for component in components[:-1]:
            walked.append(component)
            child = node.children.get(component)
            if child is None:
                child = VfsNode(name=component, is_dir=True, fileid=self._take_fileid())
                node.children[component] = child
                self._register(child, "/" + "/".join(walked))
            node = child
        leaf = VfsNode(
            name=components[-1], is_dir=False, data=data, fileid=self._take_fileid()
        )
        node.children[components[-1]] = leaf
        self._register(leaf, "/" + "/".join(components))
        return leaf

    def _take_fileid(self) -> int:
        self._next_fileid += 1
        return self._next_fileid

    def _register(self, node: VfsNode, path: str) -> bytes:
        handle = self.handle_for(path)
        self._handles[handle[: self.HANDLE_PREFIX]] = node
        self._paths[handle[: self.HANDLE_PREFIX]] = path
        return handle

    # -- addressing ------------------------------------------------------

    @staticmethod
    def handle_for(path: str) -> bytes:
        return hashlib.sha256(path.encode("utf-8")).digest()[:FHANDLE_SIZE]

    #: Bytes of a filehandle a CDJ preserves. See the module docstring.
    HANDLE_PREFIX = 12

    def resolve(self, handle: bytes) -> VfsNode | None:
        """The node for *handle*, or ``None`` -- meaning ``NFSERR_STALE``.

        Matches on the leading bytes only, because a CDJ rewrites the rest.
        """
        return self._handles.get(handle[: self.HANDLE_PREFIX])

    def path_of(self, handle: bytes) -> str | None:
        return self._paths.get(handle[: self.HANDLE_PREFIX])

    def root_handle(self) -> bytes:
        return self.handle_for("/")

    def lookup(self, dir_handle: bytes, name: str) -> tuple[bytes, VfsNode] | None:
        parent = self.resolve(dir_handle)
        if parent is None or not parent.is_dir:
            return None
        child = parent.child_named(name)
        if child is None:
            return None
        # Build the path from ``child.name``, never from the requested *name*:
        # when the two differ only by normalisation, hashing the request would
        # mint a handle that is not in the table and every later use of it
        # would come back NFSERR_STALE.
        parent_path = self._paths.get(dir_handle[: self.HANDLE_PREFIX], "/")
        child_path = ("" if parent_path == "/" else parent_path) + "/" + child.name
        return self.handle_for(child_path), child

    def read(self, handle: bytes, offset: int, count: int) -> bytes | None:
        node = self.resolve(handle)
        if node is None or node.is_dir:
            return None
        return node.read(offset, count)

    def attrs_for(self, node: VfsNode) -> Fattr:
        return Fattr(
            type=FType.NFDIR if node.is_dir else FType.NFREG,
            mode=0o040755 if node.is_dir else 0o100644,
            nlink=2 if node.is_dir else 1,
            uid=0,
            gid=0,
            size=node.size,
            blocksize=512,
            rdev=0,
            blocks=(node.size + 511) // 512,
            fsid=1,
            fileid=node.fileid,
            atime_sec=_EPOCH, atime_usec=0,
            mtime_sec=_EPOCH, mtime_usec=0,
            ctime_sec=_EPOCH, ctime_usec=0,
        )
