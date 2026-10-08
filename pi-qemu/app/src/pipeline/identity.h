#pragma once
// A deck's identity: what goes onto p7 (/data) of its release card, kept in
// pi-qemu/.cache/decks/DECK/ (gitignored) and made the first time:
//
//   trimixxx.conf      its name
//   ssh/               its ssh host keys, kept, so known_hosts survives a
//                      reflash (`deck prepare` copies a deck's own here first)
//   NetworkManager/    its hotspot (release/hotspot.nmconnection.in, with
//                      pi_config/wifi-fallback/hotspot.env's password), the home
//                      Wi-Fi from image/secrets.env (release/home-wifi.nmconnection.in),
//                      and for the emulated trimixxx0 the management NIC's
//                      profile (release/pi-qemu-home.nmconnection)
//
// Any other network goes into NetworkManager/ by hand, as a keyfile.

#include <QString>

namespace identity {

QString prepare(const QString& deck); // its directory, brought up to date

// The home Wi-Fi's profile id: uuid5(DNS, "home-wifi." + ssid), as Python's
// uuid.uuid5 makes it, so every card gives the same network the same id.
QString homeWifiUuid(const QString& ssid);

} // namespace identity
