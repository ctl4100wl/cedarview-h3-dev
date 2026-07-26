#include "configstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

QString ConfigStore::configFilePath()
{
    const QString directory = QStandardPaths::writableLocation(
        QStandardPaths::AppConfigLocation);
    return QDir(directory).filePath(QStringLiteral("config.json"));
}

AppState ConfigStore::load()
{
    AppState state;
    QFile file(configFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return state;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return state;
    }

    const QJsonObject root = document.object();
    state.gridRows = qBound(1, root.value(QStringLiteral("gridRows")).toInt(2), 5);
    state.gridColumns = qBound(1, root.value(QStringLiteral("gridColumns")).toInt(2), 5);
    state.gridMode = root.value(QStringLiteral("gridMode")).toString();
    if (state.gridMode != QStringLiteral("auto") &&
        !state.gridMode.startsWith(QStringLiteral("count:"))) {
        state.gridMode = QStringLiteral("count:%1")
                             .arg(state.gridRows * state.gridColumns);
    }
    state.defaultUsername =
        root.value(QStringLiteral("defaultUsername"))
            .toString(QStringLiteral("admin"));
    state.defaultPassword =
        root.value(QStringLiteral("defaultPassword")).toString();
    state.theme = root.value(QStringLiteral("theme"))
                      .toString(QStringLiteral("dark"));
    if (state.theme != QStringLiteral("light")) {
        state.theme = QStringLiteral("dark");
    }
    state.playbackBackend =
        root.value(QStringLiteral("playbackBackend"))
            .toString(QStringLiteral("mpv"));
    if (state.playbackBackend != QStringLiteral("gstreamer")) {
        state.playbackBackend = QStringLiteral("mpv");
    }
    const QJsonArray cameras = root.value(QStringLiteral("cameras")).toArray();
    for (const QJsonValue &value : cameras) {
        if (value.isObject()) {
            const Camera camera = Camera::fromJson(value.toObject());
            if (!camera.host.isEmpty()) {
                state.cameras.append(camera);
            }
        }
    }

    const QJsonArray assignments =
        root.value(QStringLiteral("assignments")).toArray();
    for (const QJsonValue &value : assignments) {
        state.assignments.append(value.toString());
    }

    return state;
}

bool ConfigStore::save(const AppState &state, QString *error)
{
    const QFileInfo info(configFilePath());
    if (!QDir().mkpath(info.absolutePath())) {
        if (error) {
            *error = QStringLiteral("Could not create the configuration directory.");
        }
        return false;
    }

    QJsonArray cameras;
    for (const Camera &camera : state.cameras) {
        cameras.append(camera.toJson());
    }

    QJsonArray assignments;
    for (const QString &cameraId : state.assignments) {
        assignments.append(cameraId);
    }

    const QJsonObject root{
        {QStringLiteral("version"), 4},
        {QStringLiteral("gridRows"), state.gridRows},
        {QStringLiteral("gridColumns"), state.gridColumns},
        {QStringLiteral("gridMode"), state.gridMode},
        {QStringLiteral("defaultUsername"), state.defaultUsername},
        {QStringLiteral("defaultPassword"), state.defaultPassword},
        {QStringLiteral("theme"), state.theme},
        {QStringLiteral("playbackBackend"), state.playbackBackend},
        {QStringLiteral("cameras"), cameras},
        {QStringLiteral("assignments"), assignments},
    };

    QSaveFile file(configFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    QFile::setPermissions(configFilePath(),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}
