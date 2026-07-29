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
    name: str
    is_dir: bool
    data: bytes = b""
    children: dict[str, "VfsNode"] = field(default_factory=dict)
    fileid: int = 0

    @property
    def size(self) -> int:
        return 0 if self.is_dir else len(self.data)


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
    def from_directory(cls, directory: Path) -> "Vfs":
        """Mirror a real directory. Files are read eagerly into memory.

        Fine for the fixtures this serves in milestone M8; a production server
        would stream from disk instead.
        """
        directory = Path(directory)
        vfs = cls()
        for path in sorted(directory.rglob("*")):
            if path.is_file():
                vfs.add_file(str(path.relative_to(directory)), path.read_bytes())
        return vfs

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
        return node.data[offset : offset + count]

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
