"""End-to-end loopback test: our NFS client against our NFS server.

Milestone M8 in miniature, and the most valuable test here. It drives the
entire stack -- event loop, RPC correlation and retry, portmap, mount, NFSv2,
the windowed download -- with no hardware, and because the server uses the same
codecs in reverse it validates every *reply encoder* too. That is what turns
Phase C from greenfield into a backend swap.

What it deliberately cannot prove: that our bytes are what a *Pioneer* device
expects. Only a capture can settle that, which is why the milestone also calls
for pointing prolink-connect at this server.
"""

from __future__ import annotations

import hashlib

import pytest

from prolinks_poc.core.slots import PDB_PATH, MediaSlot
from prolinks_poc.net.loop import EventLoop
from prolinks_poc.net.nfsclient import NfsClient
from prolinks_poc.net.nfsserver import NfsServer
from prolinks_poc.net.vfs import Vfs
from prolinks_poc.proto import mountd, nfs2, portmap

# Deliberately not a multiple of the chunk size, so the final READ is short and
# the "have we finished?" arithmetic is actually exercised.
PDB_BYTES = bytes(range(256)) * 43 + b"tail"
SMALL_BYTES = b"hello prolink"


@pytest.fixture
def stack():
    loop = EventLoop()
    vfs = Vfs.from_mapping(
        {
            PDB_PATH: PDB_BYTES,
            "PIONEER/USBANLZ/P016/0000875E/ANLZ0000.DAT": SMALL_BYTES,
            "Contents/Artist/Album/Track.mp3": b"\xff\xfb" + bytes(5000),
        }
    )
    server = NfsServer(loop, vfs, exports={"/C/": "/", "/B/": "/"})
    server.start()
    client = NfsClient(
        loop, "127.0.0.1", local_ip="127.0.0.1",
        portmap_port=server.portmap_port, timeout=1.0, retries=2,
    )
    try:
        yield client, server, vfs
    finally:
        client.close()
        server.close()
        loop.close()


def test_portmap_null(stack):
    client, _server, _vfs = stack
    assert client.ping_portmap() is True


def test_getport_resolves_both_programs(stack):
    client, server, _vfs = stack
    assert client.get_port(mountd.PROGRAM, mountd.VERSION) == server.mountd_port
    assert client.get_port(nfs2.PROGRAM, nfs2.VERSION) == server.nfsd_port


def test_getport_returns_zero_for_an_unregistered_program(stack):
    client, _server, _vfs = stack
    assert client.get_port(100024, 1) == 0


def test_portmap_dump(stack):
    client, server, _vfs = stack
    mappings = client.dump_portmap()
    ports = {m.program: m.port for m in mappings}
    assert ports[nfs2.PROGRAM] == server.nfsd_port
    assert ports[mountd.PROGRAM] == server.mountd_port
    assert all(m.protocol == portmap.IPPROTO_UDP for m in mappings)


def test_exports_are_listed(stack):
    client, _server, _vfs = stack
    exports = client.list_exports()
    assert {e.path for e in exports} == {"/C/", "/B/"}
    # Experiment E3 relies on the raw bytes being preserved.
    assert exports[0].path_raw == exports[0].path.encode("utf-16-le")


def test_mount_returns_a_32_byte_handle(stack):
    client, _server, vfs = stack
    handle = client.mount_slot(MediaSlot.USB)
    assert len(handle) == 32
    assert handle == vfs.root_handle()


def test_mounting_an_unknown_export_fails(stack):
    client, _server, _vfs = stack
    with pytest.raises(mountd.MountError):
        client.mount("/Z/")


def test_lookup_path_walks_component_by_component(stack):
    client, _server, _vfs = stack
    root = client.mount_slot(MediaSlot.USB)
    handle, attrs = client.lookup_path(root, PDB_PATH)
    assert len(handle) == 32
    assert attrs.is_regular
    assert attrs.size == len(PDB_BYTES)


def test_lookup_of_a_missing_name_raises_noent(stack):
    client, _server, _vfs = stack
    root = client.mount_slot(MediaSlot.USB)
    with pytest.raises(nfs2.NfsError) as excinfo:
        client.lookup(root, "NOPE")
    assert excinfo.value.is_missing


def test_stale_handle_is_reported_as_stale(stack):
    """Experiment E8: what a media swap looks like from the client side."""
    client, _server, _vfs = stack
    with pytest.raises(nfs2.NfsError) as excinfo:
        client.lookup(b"\xaa" * 32, "PIONEER")
    assert excinfo.value.is_stale


def test_download_is_byte_identical(stack):
    """The anchor property, in miniature: what we asked for is what we got."""
    client, _server, _vfs = stack
    root = client.mount_slot(MediaSlot.USB)
    handle, attrs = client.lookup_path(root, PDB_PATH)
    data = client.download(handle, attrs.size)
    assert data == PDB_BYTES
    assert hashlib.sha256(data).hexdigest() == hashlib.sha256(PDB_BYTES).hexdigest()


@pytest.mark.parametrize("chunk,window", [(64, 1), (256, 4), (1280, 4), (4096, 8)])
def test_download_across_chunk_and_window_settings(stack, chunk, window):
    """Experiment E7's matrix, run against loopback so the real run only has
    to measure throughput rather than also debug correctness."""
    client, _server, _vfs = stack
    root = client.mount_slot(MediaSlot.USB)
    handle, attrs = client.lookup_path(root, PDB_PATH)
    assert client.download(handle, attrs.size, chunk=chunk, window=window) == PDB_BYTES


def test_download_reports_progress_monotonically(stack):
    client, _server, _vfs = stack
    root = client.mount_slot(MediaSlot.USB)
    handle, attrs = client.lookup_path(root, PDB_PATH)

    seen: list[int] = []
    client.download(handle, attrs.size, chunk=256, on_progress=lambda d, t: seen.append(d))
    assert seen == sorted(seen), "progress must never go backwards"
    assert seen[-1] == attrs.size


def test_download_to_file_is_atomic(stack, tmp_path):
    """No ``.part`` is left behind, and the destination is complete.

    In Mixxx this matters more than it looks: ``SoundSourceProxy`` sniffs file
    *content*, so a truncated download would be classified as an unsupported
    format rather than as an incomplete file.
    """
    client, _server, _vfs = stack
    root = client.mount_slot(MediaSlot.USB)
    handle, attrs = client.lookup_path(root, PDB_PATH)

    destination = tmp_path / "nested" / "export.pdb"
    client.download_to_file(handle, attrs.size, destination)

    assert destination.read_bytes() == PDB_BYTES
    assert not list(tmp_path.rglob("*.part"))


def test_empty_file_download_short_circuits(stack):
    client, server, vfs = stack
    vfs.add_file("empty.bin", b"")
    root = client.mount_slot(MediaSlot.USB)
    handle, attrs = client.lookup_path(root, "empty.bin")
    assert attrs.size == 0
    assert client.download(handle, 0) == b""


def test_getattr_and_statfs(stack):
    """Experiment E5 probes these against hardware; here we prove our codecs."""
    client, _server, _vfs = stack
    root = client.mount_slot(MediaSlot.USB)
    assert client.getattr(root).is_directory

    statfs = client.statfs(root)
    assert statfs.bsize == 512
    assert statfs.total_bytes == 512 * 1_000_000


def test_readdir_lists_children(stack):
    client, _server, _vfs = stack
    root = client.mount_slot(MediaSlot.USB)
    entries, eof = client.readdir(root)
    assert eof is True
    assert {e.name for e in entries} == {"PIONEER", "Contents"}
    # Names travel UTF-16LE in both directions.
    assert entries[0].name_raw == entries[0].name.encode("utf-16-le")


def test_root_handle_is_stable_across_restart():
    """A client's cached root handle should survive a server restart.

    Deterministic handles are what let the Mixxx feature keep using a cached
    handle after a transient network blip instead of re-mounting.
    """
    assert Vfs.from_mapping({"a": b"1"}).root_handle() == Vfs.from_mapping({"b": b"2"}).root_handle()


def test_rpc_stats_show_no_retries_on_a_healthy_link(stack):
    client, _server, _vfs = stack
    root = client.mount_slot(MediaSlot.USB)
    handle, attrs = client.lookup_path(root, PDB_PATH)
    client.download(handle, attrs.size)
    assert client.rpc.stats["retries"] == 0
    assert client.rpc.stats["timeouts"] == 0
