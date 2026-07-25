#pragma once

#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

struct Camera
{
    QString id;
    QString name;
    QString host;
    QString username;
    QString password;
    int channel = 1;
    int subtype = 1;
    int latencyMs = 300;
    bool forceTcp = true;

    static Camera create()
    {
        Camera camera;
        camera.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        return camera;
    }

    QString resolvedRtspUrl() const
    {
        if (host.trimmed().isEmpty()) {
            return {};
        }

        QUrl url;
        url.setScheme(QStringLiteral("rtsp"));
        url.setHost(host.trimmed());
        url.setPort(554);
        url.setUserName(username);
        url.setPassword(password);
        url.setPath(QStringLiteral("/cam/realmonitor"));

        QUrlQuery query;
        query.addQueryItem(QStringLiteral("channel"), QString::number(channel));
        query.addQueryItem(QStringLiteral("subtype"), QString::number(subtype));
        url.setQuery(query);
        return url.toString(QUrl::FullyEncoded);
    }

    QJsonObject toJson() const
    {
        return {
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("host"), host},
            {QStringLiteral("username"), username},
            {QStringLiteral("password"), password},
            {QStringLiteral("channel"), channel},
            {QStringLiteral("subtype"), subtype},
            {QStringLiteral("latencyMs"), latencyMs},
            {QStringLiteral("forceTcp"), forceTcp},
        };
    }

    static Camera fromJson(const QJsonObject &object)
    {
        Camera camera;
        camera.id = object.value(QStringLiteral("id")).toString();
        camera.name = object.value(QStringLiteral("name")).toString();
        camera.host = object.value(QStringLiteral("host")).toString();
        camera.username = object.value(QStringLiteral("username")).toString();
        camera.password = object.value(QStringLiteral("password")).toString();
        camera.channel = object.value(QStringLiteral("channel")).toInt(1);
        camera.subtype = object.value(QStringLiteral("subtype")).toInt(1);
        camera.latencyMs = object.value(QStringLiteral("latencyMs")).toInt(300);
        camera.forceTcp = object.value(QStringLiteral("forceTcp")).toBool(true);

        // Import CedarView 0.1.0 configurations that stored the complete URL.
        if (camera.host.isEmpty()) {
            const QUrl oldUrl(
                object.value(QStringLiteral("rtspUrl")).toString());
            if (oldUrl.isValid()) {
                camera.host = oldUrl.host();
                camera.username = oldUrl.userName();
                camera.password = oldUrl.password();
                const QUrlQuery query(oldUrl);
                camera.channel = query.queryItemValue(
                    QStringLiteral("channel")).toInt();
                camera.subtype = query.queryItemValue(
                    QStringLiteral("subtype")).toInt();
                if (camera.channel < 1) {
                    camera.channel = 1;
                }
                camera.subtype = qBound(0, camera.subtype, 1);
            }
        }
        if (camera.id.isEmpty()) {
            camera.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        return camera;
    }
};
