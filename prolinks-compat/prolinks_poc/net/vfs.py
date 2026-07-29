"""A read-only virtual filesystem behind the NFS server — the Phase C seam.

Objective 2 is to serve Mixxx's own library to real CDJs. Whatever backs that
tree later -- a real directory, or a synthesised ``/PIONEER/`` layout generated
from Mixxx's database -- the NFS wire layer above it should not have to change.
So the wire layer talks only to this interface, and the backing store is
swappable.

**Filehandles.** NFSv2 handles are 32 opaque bytes that the client must echo
back verbatim; their internal structure is entirely the server's business. Here
they are a truncated SHA-256 of the path, which makes them deterministic (the
same tree always yields the same handles, so a client's cached root handle
survives a server restart) and collision-resistant in practice. The tradeoff is
that they cannot express "this handle is stale" on their own, so the server
keeps an explicit table and returns ``NFSERR_STALE`` for anything absent from
it -- which is exactly the behaviour experiment E8 expects from real hardware
after a media swap.
"""

from __future__ import annotations

import hashlib
from dataclasses import dataclass, field
from pathlib import Path

from ..proto.nfs2 import FHANDLE_SIZE, FType, Fattr

__all__ = ["VfsNode", "Vfs"]

#: Fixed timestamp for synthesised attributes. Deterministic on purpose:
#: byte-identical replies across runs make capture diffs meaningful.
_EPOCH = 1_600_000_000


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
        self._handles[handle] = node
        self._paths[handle] = path
        return handle

    # -- addressing ------------------------------------------------------

    @staticmethod
    def handle_for(path: str) -> bytes:
        return hashlib.sha256(path.encode("utf-8")).digest()[:FHANDLE_SIZE]

    def resolve(self, handle: bytes) -> VfsNode | None:
        """The node for *handle*, or ``None`` -- meaning ``NFSERR_STALE``."""
        return self._handles.get(handle)

    def path_of(self, handle: bytes) -> str | None:
        return self._paths.get(handle)

    def root_handle(self) -> bytes:
        return self.handle_for("/")

    def lookup(self, dir_handle: bytes, name: str) -> tuple[bytes, VfsNode] | None:
        parent = self.resolve(dir_handle)
        if parent is None or not parent.is_dir:
            return None
        child = parent.children.get(name)
        if child is None:
            return None
        parent_path = self._paths.get(dir_handle, "/")
        child_path = ("" if parent_path == "/" else parent_path) + "/" + name
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
