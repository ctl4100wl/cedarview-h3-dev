#pragma once

#include "camera.h"

#include <QList>
#include <QWidget>

class QGridLayout;
class QEvent;
class QTimer;
class VideoTile;

class GridView final : public QWidget
{
    Q_OBJECT

public:
    explicit GridView(QWidget *parent = nullptr);
    ~GridView() override;

    void setGridSize(int rows, int columns);
    void setTileCount(int count);
    void setCameras(const QList<Camera> &cameras);
    void setAssignments(const QStringList &assignments);
    QStringList assignments() const;
    int rows() const { return m_rows; }
    int columns() const { return m_columns; }
    int tileCount() const { return m_tileCount; }
    int selectedIndex() const { return m_selectedIndex; }
    void setFullscreenMode(bool fullscreen);
    void setPlaybackBackend(const QString &backend);
    void setLiveEdgeSettings(bool enabled, int delayThresholdMs,
                             bool ultraLiveMode);

public slots:
    void assignCamera(const Camera &camera);
    void assignCameraToIndex(const QString &cameraId, int index);
    void clearSelected();

signals:
    void assignmentsChanged();
    void playbackError(const QString &cameraName, const QString &message);
    void cameraStreamChanged(const QString &cameraId, int subtype);
    void exitFullscreenRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Camera findCamera(const QString &id) const;
    void selectTile(int index);
    void rebuild();

    QGridLayout *m_layout = nullptr;
    QList<VideoTile *> m_tiles;
    QList<Camera> m_cameras;
    QStringList m_assignments;
    int m_rows = 2;
    int m_columns = 2;
    int m_tileCount = 4;
    int m_selectedIndex = 0;
    QString m_playbackBackend = QStringLiteral("mpv");
    bool m_liveEdgeCorrectionEnabled = true;
    int m_liveEdgeDelayThresholdMs = 1250;
    bool m_ultraLiveMode = false;
    QTimer *m_cursorTimer = nullptr;
    bool m_fullscreenMode = false;
    bool m_cursorHidden = false;
};
