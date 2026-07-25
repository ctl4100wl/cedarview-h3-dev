#include "rtspscanner.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>

RtspScanner::RtspScanner(QObject *parent)
    : QObject(parent)
{
}

void RtspScanner::start()
{
    cancel();

    const QStringList targets = buildTargets();
    for (const QString &target : targets) {
        m_pending.enqueue(target);
    }
    m_total = m_pending.size();
    m_completed = 0;
    m_running = !m_pending.isEmpty();
    emit progressChanged(0, m_total);

    if (!m_running) {
        emit finished();
        return;
    }
    launchMore();
}

void RtspScanner::cancel()
{
    m_running = false;
    m_pending.clear();
    const auto sockets = m_active;
    m_active.clear();
    for (QTcpSocket *socket : sockets) {
        socket->abort();
        socket->deleteLater();
    }
}

QStringList RtspScanner::buildTargets() const
{
    QSet<quint32> targets;
    QSet<quint32> ownAddresses;

    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &networkInterface : interfaces) {
        if (!(networkInterface.flags() & QNetworkInterface::IsUp) ||
            (networkInterface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }

        for (const QNetworkAddressEntry &entry :
             networkInterface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) {
                continue;
            }
            bool ok = false;
            const quint32 own = entry.ip().toIPv4Address(&ok);
            if (!ok) {
                continue;
            }
            const bool privateOrLinkLocal =
                (own & 0xFF000000u) == 0x0A000000u ||
                (own & 0xFFF00000u) == 0xAC100000u ||
                (own & 0xFFFF0000u) == 0xC0A80000u ||
                (own & 0xFFFF0000u) == 0xA9FE0000u;
            if (!privateOrLinkLocal) {
                continue;
            }
            ownAddresses.insert(own);

            // Camera LANs are normally /24. Limit wider interfaces to their
            // local /24 so discovery remains fast and non-disruptive.
            const quint32 network = own & 0xFFFFFF00u;
            for (quint32 host = 1; host < 255; ++host) {
                targets.insert(network | host);
            }
        }
    }

    for (quint32 own : ownAddresses) {
        targets.remove(own);
    }

    QList<quint32> sorted = targets.values();
    std::sort(sorted.begin(), sorted.end());

    QStringList result;
    result.reserve(sorted.size());
    for (quint32 address : sorted) {
        result.append(QHostAddress(address).toString());
    }
    return result;
}

void RtspScanner::launchMore()
{
    if (!m_running) {
        return;
    }

    while (m_active.size() < MaxConcurrent && !m_pending.isEmpty()) {
        const QString address = m_pending.dequeue();
        auto *socket = new QTcpSocket(this);
        socket->setProperty("scanAddress", address);
        m_active.insert(socket);

        connect(socket, &QTcpSocket::connected, this, [this, socket] {
            finishSocket(socket, true);
        });
        connect(socket, &QTcpSocket::errorOccurred, this,
                [this, socket](QAbstractSocket::SocketError) {
                    finishSocket(socket, false);
                });
        QTimer::singleShot(ConnectTimeoutMs, socket, [this, socket] {
            finishSocket(socket, false);
        });
        socket->connectToHost(address, 554);
    }
}

void RtspScanner::finishSocket(QTcpSocket *socket, bool found)
{
    if (!m_active.remove(socket)) {
        return;
    }

    const QString address = socket->property("scanAddress").toString();
    socket->abort();
    socket->deleteLater();
    ++m_completed;

    if (found) {
        emit cameraFound(address);
    }
    emit progressChanged(m_completed, m_total);

    if (!m_pending.isEmpty()) {
        launchMore();
    } else if (m_active.isEmpty()) {
        m_running = false;
        emit finished();
    }
}
