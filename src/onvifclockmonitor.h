#pragma once

#include "camera.h"

#include <QList>
#include <QObject>
#include <QSet>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

class OnvifClockMonitor final : public QObject
{
    Q_OBJECT

public:
    explicit OnvifClockMonitor(QObject *parent = nullptr);

    void setCameras(const QList<Camera> &cameras);
    void setEnabled(bool enabled);
    void setIntervalSeconds(int seconds);
    void start();
    void checkNow();

signals:
    void clockMeasured(const QString &cameraId, qint64 offsetMs);
    void clockCheckFailed(const QString &cameraId,
                          const QString &message);

private:
    void probe(const Camera &camera);
    void finishReply(QNetworkReply *reply);

    QList<Camera> m_cameras;
    QNetworkAccessManager *m_manager = nullptr;
    QTimer *m_timer = nullptr;
    QSet<QString> m_activeCameraIds;
    bool m_enabled = true;
    int m_intervalSeconds = 60;
};
