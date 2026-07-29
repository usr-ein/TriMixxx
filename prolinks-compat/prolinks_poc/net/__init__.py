"""I/O layer: sockets, the event loop, interface enumeration, RPC/NFS clients.

Deliberately thin. Everything here has a direct Qt equivalent
(``QUdpSocket``, ``QTimer``, ``QNetworkInterface``), so the port replaces this
package wholesale while :mod:`prolinks_poc.proto` transcribes unchanged.
"""
