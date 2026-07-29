"""Network-interface enumeration via ``getifaddrs(3)``.

We need the IPv4 address, netmask, broadcast address and MAC of the interface
facing the CDJs:

* the **MAC and IP** go verbatim into our keep-alive, and ``research/02`` §4.3
  is emphatic that they must be the real ones -- peers put the advertised IP
  into their unicasts, so spoofing breaks the return path;
* the **broadcast address** is where discovery packets go;
* choosing the **right interface** matters on a multi-homed host. The Pi has
  ``eth0`` on the CDJ network and ``wlan0`` elsewhere; if the RPC socket binds
  a source address on the wrong subnet, link-local routing silently sends the
  datagrams out the wrong NIC and every request times out with no error.

Implemented with ``ctypes`` rather than a dependency, per the stdlib-only rule.
``QNetworkInterface`` covers all of this in the Qt port.
"""

from __future__ import annotations

import ctypes
import ctypes.util
import ipaddress
import socket
import sys
from dataclasses import dataclass

__all__ = ["Interface", "list_interfaces", "find_interface", "interface_for_peer"]

_IS_MACOS = sys.platform == "darwin"
_IS_LINUX = sys.platform.startswith("linux")

# Link-layer address families: macOS uses AF_LINK, Linux uses AF_PACKET.
_AF_LINK = 18
_AF_PACKET = 17


@dataclass(frozen=True)
class Interface:
    name: str
    ip: str
    netmask: str
    mac: bytes

    @property
    def broadcast(self) -> str:
        """Subnet broadcast address, derived from IP and netmask.

        Computed rather than read from ``ifa_broadaddr`` because link-local
        interfaces do not always populate that field, and because this matches
        how the reference virtual CDJs derive it.
        """
        network = ipaddress.IPv4Network(f"{self.ip}/{self.netmask}", strict=False)
        return str(network.broadcast_address)

    @property
    def network(self) -> ipaddress.IPv4Network:
        return ipaddress.IPv4Network(f"{self.ip}/{self.netmask}", strict=False)

    @property
    def mac_str(self) -> str:
        return ":".join(f"{b:02x}" for b in self.mac)

    @property
    def is_link_local(self) -> bool:
        return self.ip.startswith("169.254.")

    def contains(self, peer_ip: str) -> bool:
        try:
            return ipaddress.IPv4Address(peer_ip) in self.network
        except ValueError:
            return False

    def __str__(self) -> str:
        return f"{self.name} {self.ip}/{self.netmask} mac={self.mac_str}"


class _Ifaddrs(ctypes.Structure):
    pass


_Ifaddrs._fields_ = [
    ("ifa_next", ctypes.POINTER(_Ifaddrs)),
    ("ifa_name", ctypes.c_char_p),
    ("ifa_flags", ctypes.c_uint),
    ("ifa_addr", ctypes.c_void_p),
    ("ifa_netmask", ctypes.c_void_p),
    ("ifa_dstaddr", ctypes.c_void_p),
    ("ifa_data", ctypes.c_void_p),
]


def _sockaddr_family(pointer: int) -> int:
    """Read the address family from a ``struct sockaddr``.

    The two platforms lay out the first bytes differently: BSD/macOS put a
    one-byte ``sa_len`` first and the family second, while Linux starts with a
    two-byte family. Reading only the two bytes we need keeps this safe
    regardless of which concrete sockaddr variant we were handed.
    """
    head = ctypes.string_at(pointer, 2)
    return head[1] if _IS_MACOS else int.from_bytes(head, sys.byteorder)


def _read_ipv4(pointer: int) -> str | None:
    """Extract the address from a ``sockaddr_in``.

    ``sin_addr`` sits at offset 4 on both platforms: macOS has
    ``len(1) + family(1) + port(2)``, Linux has ``family(2) + port(2)``.
    """
    if not pointer:
        return None
    return socket.inet_ntoa(ctypes.string_at(pointer + 4, 4))


def _read_mac(pointer: int) -> bytes | None:
    """Extract the hardware address from a link-layer sockaddr."""
    if not pointer:
        return None
    if _IS_MACOS:
        # struct sockaddr_dl: len, family, index(2), type, nlen, alen, slen,
        # then data[] holding the interface name followed by the address.
        header = ctypes.string_at(pointer, 8)
        name_len, addr_len = header[5], header[6]
        if addr_len != 6:
            return None
        return ctypes.string_at(pointer + 8 + name_len, 6)
    # struct sockaddr_ll: family(2), protocol(2), ifindex(4), hatype(2),
    # pkttype(1), halen(1), addr[8].
    header = ctypes.string_at(pointer, 12)
    if header[11] != 6:
        return None
    return ctypes.string_at(pointer + 12, 6)


def list_interfaces() -> list[Interface]:
    """Every interface that has both an IPv4 address and a MAC.

    Loopback and address-less interfaces are filtered out: neither can carry
    DJ-Link traffic, and offering them in ``--iface`` would only invite
    mistakes.
    """
    libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
    libc.getifaddrs.restype = ctypes.c_int
    libc.getifaddrs.argtypes = [ctypes.POINTER(ctypes.POINTER(_Ifaddrs))]
    libc.freeifaddrs.argtypes = [ctypes.POINTER(_Ifaddrs)]

    head = ctypes.POINTER(_Ifaddrs)()
    if libc.getifaddrs(ctypes.byref(head)) != 0:
        raise OSError(ctypes.get_errno(), "getifaddrs failed")

    ipv4: dict[str, tuple[str, str]] = {}
    macs: dict[str, bytes] = {}
    try:
        node = head
        while node:
            entry = node.contents
            name = entry.ifa_name.decode()
            if entry.ifa_addr:
                family = _sockaddr_family(entry.ifa_addr)
                if family == socket.AF_INET:
                    address = _read_ipv4(entry.ifa_addr)
                    netmask = _read_ipv4(entry.ifa_netmask) or "255.255.255.0"
                    if address and not address.startswith("127."):
                        ipv4[name] = (address, netmask)
                elif family in (_AF_LINK, _AF_PACKET):
                    mac = _read_mac(entry.ifa_addr)
                    if mac and mac != b"\x00" * 6:
                        macs[name] = mac
            node = entry.ifa_next
    finally:
        libc.freeifaddrs(head)

    return [
        Interface(name=name, ip=address, netmask=netmask, mac=macs[name])
        for name, (address, netmask) in sorted(ipv4.items())
        if name in macs
    ]


def find_interface(name: str | None) -> Interface:
    """Resolve ``--iface``.

    With no name, prefer a link-local (169.254/16) interface: on a CDJ network
    with no DHCP that is by definition the one facing the players. Otherwise
    fall back to the first usable interface.
    """
    interfaces = list_interfaces()
    if not interfaces:
        raise RuntimeError("no usable IPv4 interface with a MAC address found")

    if name:
        for interface in interfaces:
            if interface.name == name:
                return interface
        available = ", ".join(i.name for i in interfaces)
        raise RuntimeError(f"interface {name!r} not found; available: {available}")

    for interface in interfaces:
        if interface.is_link_local:
            return interface
    return interfaces[0]


def interface_for_peer(peer_ip: str) -> Interface | None:
    """The interface whose subnet contains *peer_ip*, if any.

    This is the multi-homed-host fix: bind the RPC source socket here rather
    than letting the routing table guess.
    """
    for interface in list_interfaces():
        if interface.contains(peer_ip):
            return interface
    return None
