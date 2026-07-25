#pragma once

#include "camera.h"

#include <QList>
#include <QWidget>

class QGridLayout;
class VideoTile;

class GridView final : public QWidget
{
    Q_OBJECT

public:
    explicit GridView(QWidget *parent = nullptr);

    void setGridSize(int rows, int columns);
    void setCameras(const QList<Camera> &cameras);
    void setAssignments(const QStringList &assignments);
    void setVideoSinkPreference(const QString &preference);
    QStringList assignments() const;
    int rows() const { return m_rows; }
    int columns() const { return m_columns; }
    int selectedIndex() const { return m_selectedIndex; }

public slots:
    void assignCamera(const Camera &camera);
    void clearSelected();

signals:
    void assignmentsChanged();
    void playbackError(const QString &cameraName, const QString &message);

private:
    Camera findCamera(const QString &id) const;
    void selectTile(int index);
    void rebuild();

    QGridLayout *m_layout = nullptr;
    QList<VideoTile *> m_tiles;
    QList<Camera> m_cameras;
    QStringList m_assignments;
    QString m_sinkPreference = QStringLiteral("auto");
    int m_rows = 2;
    int m_columns = 2;
    int m_selectedIndex = 0;
};

