#pragma once

#include "camera.h"

#include <QList>
#include <QStringList>

struct AppState
{
    QList<Camera> cameras;
    int gridRows = 2;
    int gridColumns = 2;
    QStringList assignments;
    QString videoSink = QStringLiteral("auto");
};

class ConfigStore
{
public:
    static AppState load();
    static bool save(const AppState &state, QString *error = nullptr);
    static QString configFilePath();
};

