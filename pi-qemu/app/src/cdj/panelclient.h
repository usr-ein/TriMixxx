#pragma once
// A CDJ's control channel held open (cdj2000-emulator's cdj2000_input.c):
// TCP on 127.0.0.1, one line out and one answer back each, in order, after
// a greeting. It asks for `state` every so often and hands on the lamps, and
// sends what it is given. The channel serves several clients at once, so a
// window holding it open and `cdj press` do not take it from each other.

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>

class QTcpSocket;

namespace cdj {

class PanelClient : public QObject {
    Q_OBJECT
public:
    explicit PanelClient(quint16 port, int pollMs = 100, QObject* parent = nullptr);
    ~PanelClient() override; // what was sent goes out first

    bool connected() const { return m_greeted; }
    void send(const QString& line); // dropped while not connected

signals:
    void connectedChanged(bool connected);
    void lampsChanged(const QHash<QString, QString>& lamps);
    void refused(const QString& line, const QString& answer); // an "err ..." to a line we sent

private:
    void connectNow();
    void onReadyRead();
    void onGone();
    void poll();

    quint16     m_port;
    QTcpSocket* m_socket;
    QTimer      m_poll, m_retry;
    QByteArray  m_in;
    QStringList m_waiting; // lines sent, oldest first, each awaiting its answer
    bool        m_greeted = false;
    QHash<QString, QString> m_lamps;
};

} // namespace cdj
