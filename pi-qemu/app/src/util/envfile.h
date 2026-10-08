#pragma once
// Shell assignments in a file, KEY='value', read as sh would: the stock image's
// pin (image/stock.env), the hotspot's password (wifi-fallback/hotspot.env),
// and pi-qemu/image/secrets.env -- the console password, the home Wi-Fi: in the
// main checkout, gitignored (secrets.example.env is its template), and never
// printed.

#include <QHash>
#include <QString>

namespace envfile {

// The assignments in `text`, unquoted as sh would ('...', "...", bare words).
QHash<QString, QString> parse(const QString& text);
// Those of `path`; fails if it cannot be read.
QHash<QString, QString> read(const QString& path);

} // namespace envfile

namespace secrets {

// From the environment if set there, else from secrets.env; empty if neither.
QString value(const QString& key);

} // namespace secrets
