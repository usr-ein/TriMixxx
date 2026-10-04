#pragma once
// The running deck's command line: a Unix socket in the run directory, one
// command per line, one JSON line back. `pi-qemu press play` from a terminal
// is a client of it; the window drives the same S3 and Sticks objects.
//
// The newest running deck also gets ~/.pi-qemu/current, so a bare
// `pi-qemu <command>` finds it.

#include <QLocalServer>
#include <QObject>

class Machine;
class S3;
class Sticks;
class Wiring;
class QLocalSocket;

class Control : public QObject {
    Q_OBJECT
public:
    Control(Machine* m, S3* s3, Sticks* sticks, const Wiring* wiring, QObject* parent = nullptr);
    bool listen(const QString& path, QString* error);
    static QString help();

signals:
    void powerOnRequested();

private:
    void onLine(QLocalSocket* client, const QString& line);
    void reply(QLocalSocket* client, bool ok, const QString& text);

    QLocalServer  m_server;
    Machine*      m_machine;
    S3*           m_s3;
    Sticks*       m_sticks;
    const Wiring* m_wiring;
};

// The client side: send one command to a running deck, print the answer.
int runClientCommand(const QString& socket, const QStringList& words);
QString currentControlSocket();
