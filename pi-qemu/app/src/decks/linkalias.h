#pragma once
// pi-qemu deck alias: a real deck's Ethernet as an ssh alias of its own. Its
// usual alias (trimixxx-pi) goes over Wi-Fi. Cabled to the Mac's switch, its
// eth0 also answers on its IPv6 link-local address, through the Mac's wired
// interface on that link (en12, a dock's): ALIAS-eth, written into
// ~/.ssh/config, so every --host verb takes it (--host trimixxx-pi-eth).
//
// pi-qemu writes only its own marked block, at the end of ~/.ssh/config:
// a Host block per alias, rewritten each time; the rest stays as it was.

#include <QString>
#include <QStringList>

class Deck;

namespace linkalias {

// The alias of `d`'s Ethernet, written into `config` (empty: ~/.ssh/config);
// fails, saying why, when there is no such link. Returns the alias.
QString writeEthernet(Deck& d, const QString& config = {});

// ---- the parts, apart (tests/tst_linkalias) ----

// eth0's link-local address, from `ip -6 -o addr show dev eth0 scope link`.
QString linkLocal(const QString& ipAddr);
// The Mac's wired devices, from `networksetup -listallhardwareports`: never
// Wi-Fi, Bluetooth or Thunderbolt (bridge or port).
QStringList wiredDevices(const QString& hardwarePorts);
// From `ssh -G ALIAS`: the key the alias's host is known by (its HostKeyAlias,
// or its HostName, [bracketed]:port off port 22), and the options the new
// alias keeps (User, Port, IdentityFile...), as config lines.
QString hostKeyAlias(const QString& sshG);
QStringList keptOptions(const QString& sshG);
// `config` with `host`'s block in pi-qemu's marked block set to `lines` (its
// comment, then its options); the other hosts there kept, the block added at
// the end when there is none.
QString withHost(const QString& config, const QString& host, const QStringList& lines);

} // namespace linkalias
