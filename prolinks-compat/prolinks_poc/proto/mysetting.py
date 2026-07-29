"""``PIONEER/*SETTING*.DAT`` — the "My Settings" files rekordbox writes.

These hold the utility settings a player can adopt from a medium: LCD
brightness, whether the key display is alphanumeric or classic, jog tension,
auto-cue level and so on. A deck applies them from a locally inserted USB, and
also from a **peer's** medium over LINK — and that path does not read the file
over NFS at all. The requesting deck mounts the export, touches nothing, and
asks over UDP 50002 instead; the owner reads its own copy and returns the bytes
inline. See ``docs/FINDINGS.md`` F38, and
:func:`~prolinks_poc.proto.djl_status.build_settings_response`.

So a server needs to read this file itself. The container is uniform across the
four variants found on a real medium:

```
0x00  u32   header length, always 96
0x04  char[32]  brand      "PIONEER" / "PIONEER DJ" / "PioneerDJ"
0x24  char[32]  creator    "rekordbox"
0x44  char[32]  version    "0.001" / "7.1.0" / "1.000"
0x64  u32   payload length
0x68  payload
      u16   checksum, then two padding bytes
```

All little-endian, unlike the big-endian ANLZ files beside them. In
``MYSETTING.DAT``, ``DEVSETTING.DAT`` and ``DJMMYSETTING.DAT`` the payload opens
with the constant ``0x12345678`` and a second word, then the settings bytes
themselves. ``MYSETTING2.DAT`` does not — its payload appears to be settings
from the first byte — so it is left alone here rather than guessed at.

Only ``MYSETTING.DAT`` has been observed on the wire. The other three are
recognised so that a caller can find them, and deliberately not interpreted.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

from .errors import DecodeError

__all__ = [
    "MY_SETTING_PATH", "MY_SETTING2_PATH", "DEV_SETTING_PATH", "DJM_SETTING_PATH",
    "SettingsFile", "parse", "settings_payload",
]

MY_SETTING_PATH = "PIONEER/MYSETTING.DAT"
MY_SETTING2_PATH = "PIONEER/MYSETTING2.DAT"
DEV_SETTING_PATH = "PIONEER/DEVSETTING.DAT"
DJM_SETTING_PATH = "PIONEER/DJMMYSETTING.DAT"

HEADER_LENGTH = 0x60
OFF_PAYLOAD_LENGTH = 0x64
OFF_PAYLOAD = 0x68

#: Leads the payload of every variant except ``MYSETTING2.DAT``. The same value
#: appears big-endian in the type-``0x36`` reply, which is what ties the file to
#: the wire.
PAYLOAD_MAGIC = 0x12345678

#: Settings bytes a type-``0x36`` reply carries, after the magic and one word.
SETTINGS_LENGTH = 32


@dataclass(frozen=True)
class SettingsFile:
    brand: str
    creator: str
    version: str
    #: The declared payload, from ``0x68`` for ``payload_length`` bytes.
    payload: bytes

    @property
    def has_magic(self) -> bool:
        return (
            len(self.payload) >= 8
            and struct.unpack_from("<I", self.payload, 0)[0] == PAYLOAD_MAGIC
        )

    @property
    def settings(self) -> bytes:
        """The settings bytes, skipping the magic and the word after it.

        Empty when the payload does not carry the magic, which is the case for
        ``MYSETTING2.DAT`` -- better than returning its first eight bytes as
        though they meant something.
        """
        return self.payload[8:] if self.has_magic else b""


def _text(data: bytes, offset: int) -> str:
    return data[offset : offset + 32].split(b"\x00", 1)[0].decode("ascii", "replace")


def parse(data: bytes) -> SettingsFile:
    """Parse a settings file.

    Strict about the framing and lenient about nothing else: a declared payload
    running past the buffer raises rather than being silently clipped, because a
    truncated settings block handed to a real deck is a worse outcome than a
    failed load.
    """
    if len(data) < OFF_PAYLOAD:
        raise DecodeError(
            f"settings file is {len(data)}B; need at least {OFF_PAYLOAD}B for the header"
        )
    (header_length,) = struct.unpack_from("<I", data, 0)
    if header_length != HEADER_LENGTH:
        raise DecodeError(
            f"unexpected header length {header_length:#x}, expected {HEADER_LENGTH:#x}"
        )
    (payload_length,) = struct.unpack_from("<I", data, OFF_PAYLOAD_LENGTH)
    end = OFF_PAYLOAD + payload_length
    if end > len(data):
        raise DecodeError(
            f"payload claims {payload_length} bytes from {OFF_PAYLOAD:#x}, "
            f"only {len(data) - OFF_PAYLOAD} remain"
        )
    return SettingsFile(
        brand=_text(data, 0x04),
        creator=_text(data, 0x24),
        version=_text(data, 0x44),
        payload=data[OFF_PAYLOAD:end],
    )


def settings_payload(data: bytes) -> bytes:
    """The settings bytes to put in a type-``0x36`` reply, or empty.

    Returns empty rather than raising for anything unusable: a medium with no
    settings on it is normal, and the reply has a representation for that.
    """
    try:
        return parse(data).settings[:SETTINGS_LENGTH]
    except DecodeError:
        return b""
