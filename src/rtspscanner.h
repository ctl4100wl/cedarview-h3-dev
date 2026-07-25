#pragma once

#include <QObject>
#include <QQueue>
#include <QSet>
#include <QStringList>

class QTcpSocket;

class RtspScanner final : public QObject
{
    Q_OBJECT

public:
    explicit RtspScanner(QObject *parent = nullptr);

    bool isRunning() const { return m_running; }

public slots:
    void start();
    void cancel();

signals:
    void cameraFound(const QString &address);
    void progressChanged(int completed, int total);
    void finished();

private:
    QStringList buildTargets() const;
    void launchMore();
    void finishSocket(QTcpSocket *socket, bool found);

    QQueue<QString> m_pending;
    QSet<QTcpSocket *> m_active;
    int m_total = 0;
    int m_completed = 0;
    bool m_running = false;
    static constexpr int MaxConcurrent = 48;
    static constexpr int ConnectTimeoutMs = 450;
};

