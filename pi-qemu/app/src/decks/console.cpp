#include "decks/console.h"

#include "util/fail.h"
#include "util/secrets.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QTextStream>

namespace console {

void run(const QString& socket, const QStringList& commands) {
    QLocalSocket s;
    s.connectToServer(socket);
    if (!s.waitForConnected(2000)) fail("no console at " + socket + ": is the deck up?");
    QByteArray buf;
    auto send = [&](const QByteArray& b) { s.write(b); s.flush(); };
    // Everything read since `buf` was last cleared, until `pattern` shows.
    auto readUntil = [&](const QRegularExpression& pattern, int ms) -> QString {
        buf.clear();
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            if (s.waitForReadyRead(300)) buf += s.readAll();
            if (pattern.match(QString::fromUtf8(buf)).hasMatch()) break;
        }
        return QString::fromUtf8(buf);
    };
    const QRegularExpression prompt(R"(\$ $)");

    send("\n");
    if (readUntil(QRegularExpression(R"(login: $|\$ $)"), 10000).contains("login:")) {
        const QString password = secrets::value("SAM1902_PASSWORD");
        if (password.isEmpty()) fail("no SAM1902_PASSWORD for the console (pi-qemu/image/secrets.env)");
        send("sam1902\n");
        readUntil(QRegularExpression("assword"), 10000);
        send(password.toUtf8() + "\n");
        if (!prompt.match(readUntil(prompt, 20000)).hasMatch()) fail("the console login failed");
    }
    send("export TERM=dumb PAGER=cat SYSTEMD_PAGER= SYSTEMD_COLORS=0; stty -echo cols 250\n");
    readUntil(prompt, 5000);
    QTextStream out(stdout);
    static const QRegularExpression ansi("\x1b\\[[0-9;]*m");
    for (const QString& cmd : commands) {
        const QString mark = QString("__console_done_%1__").arg(QDateTime::currentMSecsSinceEpoch());
        send(QString("%1; echo %2 $?\n").arg(cmd, mark).toUtf8());
        const QString got = readUntil(QRegularExpression(mark + R"( \d+)"), 600000);
        const int at = got.indexOf(mark);
        QString body = at < 0 ? got : got.left(at);
        body.remove(ansi).remove('\r');
        out << body.trimmed() << Qt::endl;
        if (at < 0) fail("no end to: " + cmd + " (10 minutes)");
        const QString status = got.mid(at + mark.size()).trimmed().section(QRegularExpression(R"(\s)"), 0, 0);
        if (status != "0") out << "[exit " << status << "]" << Qt::endl;
        readUntil(prompt, 2000);
    }
}

} // namespace console
