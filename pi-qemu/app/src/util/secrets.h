#pragma once
// pi-qemu/image/secrets.env, in the main checkout and gitignored
// (secrets.example.env is its template): the console password, the home
// Wi-Fi. Shell assignments, KEY='value'. An environment variable of the same
// name wins. Never printed.

#include <QHash>
#include <QString>

namespace secrets {

QString value(const QString& key); // empty if neither has it
// The assignments in `text`, unquoted as sh would ('...', "...", bare).
QHash<QString, QString> parse(const QString& text);

} // namespace secrets
