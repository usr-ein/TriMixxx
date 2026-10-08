#pragma once
// An emulated deck's serial console, for when ssh is not there (a broken
// network, a boot that stops early). pi-qemu gives the Pi a console on the
// mini UART (the S3 has the PL011): <run dir>/console.sock, a getty on
// ttyS0, every byte also in console.log. For a person, `nc -U` on the socket
// is the console itself.

#include <QString>
#include <QStringList>

namespace console {

// Logs in as sam1902 if it has to (SAM1902_PASSWORD, from the environment or
// pi-qemu/image/secrets.env), runs each command in turn and prints what it
// printed, with [exit N] after one that failed.
void run(const QString& socket, const QStringList& commands);

} // namespace console
