#include "cdj/panelclient.h"

#include "cdj/panel.h"

#include <QHostAddress>
#include <QSignalBlocker>
#include <QTcpSocket>

#include <utility>

namespace cdj {

PanelClient::PanelClient(quint16 port, int pollMs, QObject* parent)
    : QObject(parent), m_port(port), m_socket(new QTcpSocket(this)) {
    connect(m_socket, &QTcpSocket::readyRead, this, &PanelClient::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &PanelClient::onGone);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &PanelClient::onGone);
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &PanelClient::connectNow);
    connect(&m_poll, &QTimer::timeout, this, &PanelClient::poll);
    m_poll.start(pollMs);
    connectNow();
}

PanelClient::~PanelClient() {
    if (m_socket->state() == QAbstractSocket::ConnectedState && m_socket->bytesToWrite()) {
        m_socket->flush();
        m_socket->waitForBytesWritten(500);
    }
}

void PanelClient::connectNow() {
    m_retry.stop();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        const QSignalBlocker quiet(m_socket); // not one more retry for this one
        m_socket->abort();
    }
    m_in.clear();
    m_waiting.clear();
    m_socket->connectToHost(QHostAddress::LocalHost, m_port);
}

void PanelClient::send(const QString& line) {
    if (!m_greeted) {
        // A key let go of while the channel is away is let go of once it is
        // back: the CDJ's held contacts outlive a connection. Anything else
        // is dropped, as a click on a dead panel would be.
        if (line.startsWith("up ") && !m_releases.contains(line)) m_releases << line;
        return;
    }
    m_waiting << line;
    m_socket->write(line.toUtf8() + "\n");
}

void PanelClient::poll() {
    // One `state` at a time: a machine that answers slowly is not asked again
    // and again in the meantime.
    if (m_greeted && !m_waiting.contains(kStateLine)) send(kStateLine);
}

void PanelClient::onReadyRead() {
    m_in += m_socket->readAll();
    for (qsizetype end; (end = m_in.indexOf('\n')) >= 0;) {
        const QString answer = QString::fromUtf8(m_in.left(end)).trimmed();
        m_in.remove(0, end + 1);
        if (!m_greeted) { // "ok cdj2000-input": the channel is ours
            m_greeted = answer.startsWith("ok");
            if (m_greeted) {
                for (const QString& release : std::exchange(m_releases, {})) send(release);
                emit connectedChanged(true);
            }
            continue;
        }
        const QString line = m_waiting.isEmpty() ? QString() : m_waiting.takeFirst();
        if (line == kStateLine) {
            const QHash<QString, QString> state = stateOf(answer);
            if (state.isEmpty()) continue;
            emit stateRead(state);
            const QHash<QString, QString> lamps = lampsOf(answer);
            if (lamps != m_lamps) {
                m_lamps = lamps;
                emit lampsChanged(m_lamps);
            }
        } else if (answer.startsWith("err")) {
            emit refused(line, answer);
        }
    }
}

void PanelClient::onGone() {
    // Not up yet, or it went away: try again shortly, for as long as we live.
    if (m_greeted) {
        m_greeted = false;
        emit connectedChanged(false);
    }
    if (!m_retry.isActive()) m_retry.start(500);
}

} // namespace cdj
