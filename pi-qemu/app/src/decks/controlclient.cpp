#include "decks/controlclient.h"

#include "util/fail.h"
#include "util/tool.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>

namespace control {

Reply send(const QString& socket, const QString& line, int timeoutMs) {
    QLocalSocket s;
    s.connectToServer(socket);
    if (!s.waitForConnected(2000)) fail("nothing answers on " + socket + ": is the deck up? (" + tool("deck list") + ")");
    s.write(line.toUtf8() + "\n");
    s.flush();
    QByteArray in;
    QElapsedTimer t;
    t.start();
    while (!in.endsWith('\n') && t.elapsed() < timeoutMs) {
        if (s.waitForReadyRead(int(qMin<qint64>(timeoutMs - t.elapsed(), 1000)))) in += s.readAll();
        else if (s.state() != QLocalSocket::ConnectedState) break;
    }
    const QJsonObject r = QJsonDocument::fromJson(in).object();
    if (r.isEmpty()) fail("no answer from the deck to: " + line);
    return {r["ok"].toBool(), r["text"].toString()};
}

} // namespace control
