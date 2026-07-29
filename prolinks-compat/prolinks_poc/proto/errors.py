"""The single exception family used by :mod:`prolinks_poc.proto`."""


class ProlinkError(Exception):
    """Base for everything this package raises."""


class DecodeError(ProlinkError):
    """Malformed or truncated bytes on the wire.

    Raised instead of returning a sentinel so that a bad datagram can never be
    mistaken for a valid one. The C++ port uses a sticky ``ok`` flag on the
    reader rather than exceptions; the *semantics* that must carry over are
    that a truncated or absurdly-sized field is rejected **before** any
    allocation is attempted.
    """


class ProtocolError(ProlinkError):
    """Well-formed bytes that say something we cannot act on.

    For example an RPC reply whose accept-status is not SUCCESS, or an NFS
    reply carrying a non-zero status code.
    """
