#pragma once
// The client side of a running board's control socket (board/controlserver):
// one command line out, one JSON line back.

#include <QString>

namespace control {

struct Reply {
    bool    ok = false;
    QString text;
};

// Fails if nothing answers on `socket`. Long enough by default for a long
// press or a stick's authorisation prompt.
Reply send(const QString& socket, const QString& line, int timeoutMs = 120000);

} // namespace control
