#include "onvifclockmonitor.h"

#include <QAuthenticator>
#include <QDate>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTime>
#include <QTimer>
#include <QUrl>
#include <QXmlStreamReader>

namespace {

constexpr int RequestTimeoutMs = 6000;

const QByteArray GetSystemDateAndTimeEnvelope = QByteArrayLiteral(
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
    "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\">"
    "<s:Body><tds:GetSystemDateAndTime/></s:Body></s:Envelope>");

QDateTime parseUtcDateTime(const QByteArray &xml)
{
    QXmlStreamReader reader(xml);
    bool insideUtc = false;
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QStringView name = reader.name();
            if (name == QStringLiteral("UTCDateTime")) {
                insideUtc = true;
            } else if (insideUtc && name == QStringLiteral("Year")) {
                year = reader.readElementText().toInt();
            } else if (insideUtc && name == QStringLiteral("Month")) {
                month = reader.readElementText().toInt();
            } else if (insideUtc && name == QStringLiteral("Day")) {
                day = reader.readElementText().toInt();
            } else if (insideUtc && name == QStringLiteral("Hour")) {
                hour = reader.readElementText().toInt();
            } else if (insideUtc && name == QStringLiteral("Minute")) {
                minute = reader.readElementText().toInt();
            } else if (insideUtc && name == QStringLiteral("Second")) {
                second = reader.readElementText().toInt();
            }
        } else if (reader.isEndElement() &&
                   reader.name() == QStringLiteral("UTCDateTime")) {
            insideUtc = false;
        }
    }

    const QDate date(year, month, day);
    const QTime time(hour, minute, second);
    if (!date.isValid() || !time.isValid()) {
        return {};
    }
    return QDateTime(date, time, Qt::UTC);
}

} // namespace

OnvifClockMonitor::OnvifClockMonitor(QObject *parent)
    : QObject(parent),
      m_manager(new QNetworkAccessManager(this)),
      m_timer(new QTimer(this))
{
    m_timer->setInterval(m_intervalSeconds * 1000);
    connect(m_timer, &QTimer::timeout,
            this, &OnvifClockMonitor::checkNow);
    connect(m_manager, &QNetworkAccessManager::authenticationRequired,
            this, [](QNetworkReply *reply, QAuthenticator *authenticator) {
                authenticator->setUser(
                    reply->property("cedarviewUsername").toString());
                authenticator->setPassword(
                    reply->property("cedarviewPassword").toString());
            });
}

void OnvifClockMonitor::setCameras(const QList<Camera> &cameras)
{
    m_cameras = cameras;
}

void OnvifClockMonitor::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!m_enabled) {
        m_timer->stop();
    } else if (!m_timer->isActive()) {
        start();
    }
}

void OnvifClockMonitor::setIntervalSeconds(int seconds)
{
    m_intervalSeconds = qBound(15, seconds, 3600);
    m_timer->setInterval(m_intervalSeconds * 1000);
}

void OnvifClockMonitor::start()
{
    if (!m_enabled) {
        return;
    }
    m_timer->start();
    QTimer::singleShot(1500, this, &OnvifClockMonitor::checkNow);
}

void OnvifClockMonitor::checkNow()
{
    if (!m_enabled) {
        return;
    }
    for (const Camera &camera : m_cameras) {
        if (!camera.id.isEmpty() && !camera.host.isEmpty() &&
            !m_activeCameraIds.contains(camera.id)) {
            probe(camera);
        }
    }
}

void OnvifClockMonitor::probe(const Camera &camera)
{
    QUrl endpoint;
    endpoint.setScheme(QStringLiteral("http"));
    endpoint.setHost(camera.host);
    endpoint.setPort(camera.onvifPort);
    endpoint.setPath(QStringLiteral("/onvif/device_service"));

    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral(
                          "application/soap+xml; charset=utf-8; "
                          "action=\"http://www.onvif.org/ver10/device/wsdl/"
                          "GetSystemDateAndTime\""));
    request.setTransferTimeout(RequestTimeoutMs);

    QNetworkReply *reply =
        m_manager->post(request, GetSystemDateAndTimeEnvelope);
    reply->setProperty("cedarviewCameraId", camera.id);
    reply->setProperty("cedarviewUsername", camera.username);
    reply->setProperty("cedarviewPassword", camera.password);
    reply->setProperty("cedarviewRequestStarted",
                       QDateTime::currentMSecsSinceEpoch());
    m_activeCameraIds.insert(camera.id);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { finishReply(reply); });
}

void OnvifClockMonitor::finishReply(QNetworkReply *reply)
{
    const QString cameraId =
        reply->property("cedarviewCameraId").toString();
    m_activeCameraIds.remove(cameraId);

    const qint64 finishedMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 startedMs =
        reply->property("cedarviewRequestStarted").toLongLong();
    if (reply->error() != QNetworkReply::NoError) {
        emit clockCheckFailed(cameraId, reply->errorString());
        reply->deleteLater();
        return;
    }

    const QDateTime cameraTime = parseUtcDateTime(reply->readAll());
    if (!cameraTime.isValid()) {
        emit clockCheckFailed(
            cameraId, tr("ONVIF returned no valid UTC clock"));
        reply->deleteLater();
        return;
    }

    // Compare against the midpoint of the HTTP round trip so LAN latency
    // does not appear as camera clock error.
    const qint64 localMidpointMs =
        startedMs + (finishedMs - startedMs) / 2;
    emit clockMeasured(cameraId,
                       cameraTime.toMSecsSinceEpoch() - localMidpointMs);
    reply->deleteLater();
}
