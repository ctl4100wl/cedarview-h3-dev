#pragma once

#include "camera.h"

#include <QList>
#include <QStringList>

struct AppState
{
    QList<Camera> cameras;
    int gridRows = 2;
    int gridColumns = 2;
    QString gridMode = QStringLiteral("count:4");
    QStringList assignments;
    QString defaultUsername = QStringLiteral("admin");
    QString defaultPassword;
    QString theme = QStringLiteral("dark");
};

class ConfigStore
{
public:
    static AppState load();
    static bool save(const AppState &state, QString *error = nullptr);
    static QString configFilePath();
};
