#pragma once
// The running board's control socket, <run dir>/control.sock: one command per
// line, one JSON line back ({"ok": bool, "text": "..."}). `pi-qemu deck …`
// is its client (decks/controlclient); the deck window drives the same
// VirtualS3 and Sticks objects directly.

#include <QLocalServer>
#include <QObject>

namespace s3 { struct Sequence; }
class Machine;
class VirtualS3;
class Sticks;
class Wiring;
class QLocalSocket;

class ControlServer : public QObject {
    Q_OBJECT
public:
    ControlServer(Machine* m, VirtualS3* s3, Sticks* sticks, const Wiring* wiring, QObject* parent = nullptr);
    bool listen(const QString& path, QString* error);
    static QString help();

signals:
    void powerOnRequested();

private:
    void onLine(QLocalSocket* client, const QString& line);
    void reply(QLocalSocket* client, bool ok, const QString& text);
    void play(QLocalSocket* client, const s3::Sequence& s, int from);

    QLocalServer  m_server;
    Machine*      m_machine;
    VirtualS3*    m_s3;
    Sticks*       m_sticks;
    const Wiring* m_wiring;
};
