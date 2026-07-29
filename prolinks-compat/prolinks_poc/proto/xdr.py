"""XDR (RFC 4506) primitives, plus Pioneer's one deviation from them.

Standard XDR: everything big-endian, everything padded to a 4-byte boundary,
variable-length data prefixed with a 32-bit byte count.

**The deviation.** Standard NFS and MOUNT encode path and file names as ASCII.
Pioneer encodes the mount path and *every* ``LOOKUP`` filename as **UTF-16LE**,
still length-prefixed -- and the prefix is the length in **bytes**, not
characters. ``research/06`` §5 calls this "the single most important
non-standard detail of the whole path", and it is also the reason we cannot
simply link ``libnfs``: its wire encoder emits ASCII, so using it would mean
patching the library rather than writing these hundred lines.

Every reader here validates a length prefix against the bytes actually
remaining **before** allocating, so a corrupt datagram claiming a 4 GiB payload
costs nothing. That property must survive into the C++ port.
"""

from __future__ import annotations

from .bytes import ByteReader, ByteWriter, align4
from .errors import DecodeError

__all__ = ["XdrWriter", "XdrReader", "NFS_MAXDATA"]

#: NFSv2's ceiling on a single READ payload (RFC 1094). Used as the default
#: sanity cap on variable-length opaque reads.
NFS_MAXDATA = 8192


class XdrWriter(ByteWriter):
    """Big-endian XDR encoder."""

    def boolean(self, value: bool) -> "XdrWriter":
        return self.u32(1 if value else 0)

    def opaque_fixed(self, data: bytes) -> "XdrWriter":
        """Fixed-length opaque: raw bytes, padded to 4. No length prefix.

        This is how the 32-byte NFS filehandle travels. The handle is an
        *uninterpreted token*: we must echo back exactly what the server gave
        us, never parse or normalise it.
        """
        self.raw(data)
        return self.pad4()

    def opaque_var(self, data: bytes) -> "XdrWriter":
        """Variable-length opaque: u32 byte count, bytes, padding."""
        self.u32(len(data))
        self.raw(data)
        return self.pad4()

    def string_ascii(self, text: str) -> "XdrWriter":
        return self.opaque_var(text.encode("ascii", errors="replace"))

    def string_utf16le(self, text: str) -> "XdrWriter":
        """Pioneer's UTF-16LE length-prefixed string. See the module docstring.

        The prefix counts *bytes*, so an n-character ASCII path yields a
        prefix of 2n. Getting this wrong is the most likely cause of an
        otherwise inexplicable ``NFSERR_NOENT``.
        """
        return self.opaque_var(text.encode("utf-16-le"))

    def array_u32(self, values) -> "XdrWriter":
        """Counted array of 32-bit ints (e.g. the AUTH_UNIX supplementary gids)."""
        values = list(values)
        self.u32(len(values))
        for value in values:
            self.u32(value)
        return self


class XdrReader(ByteReader):
    """Big-endian XDR decoder with bounds checks on every length prefix."""

    def boolean(self) -> bool:
        return self.u32() != 0

    def opaque_fixed(self, length: int) -> bytes:
        data = self.raw(length)
        self.skip(align4(length) - length)
        return data

    def opaque_var(self, max_length: int = NFS_MAXDATA) -> bytes:
        """Read a length-prefixed opaque, rejecting implausible lengths first."""
        length = self.u32()
        if length > max_length:
            raise DecodeError(
                f"opaque length {length} exceeds the {max_length} cap "
                "(refusing to allocate)"
            )
        if length > self.remaining():
            raise DecodeError(
                f"opaque claims {length}B but only {self.remaining()}B remain"
            )
        data = self.raw(length)
        self.skip(align4(length) - length)
        return data

    def string_ascii(self, max_length: int = 1024) -> str:
        return self.opaque_var(max_length).decode("ascii", errors="replace")

    def string_utf16le(self, max_length: int = 1024) -> str:
        """Decode a Pioneer UTF-16LE string.

        An odd byte length cannot be valid UTF-16, but we decode leniently
        rather than raise: seeing the mangled value in a capture is far more
        useful for diagnosis than losing the whole datagram, and the caller
        (experiment E3) keeps the raw bytes anyway.
        """
        return self.opaque_var(max_length).decode("utf-16-le", errors="replace")

    def string_utf16le_raw(self, max_length: int = 1024) -> tuple[str, bytes]:
        """Both the decoded string and its literal bytes.

        Experiment E3 needs the raw form: the ``/B/`` and ``/C/`` export names
        are confirmed only against XDJ-class hardware, so what a CDJ-2000NXS
        actually returns must be recorded verbatim, not just as our reading
        of it.
        """
        raw = self.opaque_var(max_length)
        return raw.decode("utf-16-le", errors="replace"), raw

    def array_u32(self, max_count: int = 256) -> list[int]:
        count = self.u32()
        if count > max_count:
            raise DecodeError(f"array count {count} exceeds cap {max_count}")
        if count * 4 > self.remaining():
            raise DecodeError(
                f"array of {count} u32 needs {count * 4}B, {self.remaining()}B remain"
            )
        return [self.u32() for _ in range(count)]
