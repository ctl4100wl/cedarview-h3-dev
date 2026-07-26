#pragma once

#include "camera.h"

#include <QByteArray>
#include <QPoint>
#include <QWidget>
#include <QStringList>

#include <gst/gst.h>

class QLabel;
class QProcess;
class QToolButton;
class QTimer;
class QWidget;

class VideoTile final : public QWidget
{
    Q_OBJECT

public:
    explicit VideoTile(int index, QWidget *parent = nullptr);
    ~VideoTile() override;

    int index() const { return m_index; }
    QString cameraId() const { return m_camera.id; }
    bool hasCamera() const { return !m_camera.id.isEmpty(); }
    void play(const Camera &camera);
    void stop();
    void pause();
    void resume();
    void reconnectNow();
    void setStreamSubtype(int subtype);
    void hideControls();
    void setSelected(bool selected);
    void setFullscreenMode(bool fullscreen);
    void setPlaybackBackend(const QString &backend);
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;

signals:
    void selected(int index);
    void cleared(int index);
    void cameraDropped(const QString &cameraId, int index);
    void streamSubtypeChanged(const QString &cameraId, int subtype);
    void playbackError(int index, const QString &message);
    void exitFullscreenRequested();

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QStringList playerArguments() const;
    void playMpv();
    void playGStreamer();
    bool createGStreamerPipeline(QString *error);
    GstElement *createGStreamerSink(QString *selectedName);
    static GstBusSyncReply busSyncHandler(GstBus *bus, GstMessage *message,
                                          gpointer userData);
    static void sourceSetupHandler(GstElement *playbin, GstElement *source,
                                   gpointer userData);
    void pollGStreamerBus();
    void collectPlayerOutput();
    void updateOverlay();
    void showControls();
    void scheduleReconnect(const QString &detail);
    void startPlayback();
    void markLive();
    void setStatus(const QString &text, bool error = false);
    void releasePlayer(bool immediate = false);
    void releaseGStreamer();

    int m_index = 0;
    Camera m_camera;
    QProcess *m_player = nullptr;
    GstElement *m_pipeline = nullptr;
    GstElement *m_videoSink = nullptr;
    QTimer *m_busTimer = nullptr;
    QByteArray m_playerOutput;
    QWidget *m_videoSurface = nullptr;
    QLabel *m_statusLabel = nullptr;
    QWidget *m_controls = nullptr;
    QLabel *m_cameraNameLabel = nullptr;
    QLabel *m_connectionLabel = nullptr;
    QToolButton *m_pauseButton = nullptr;
    QToolButton *m_streamButton = nullptr;
    QToolButton *m_retryButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    QTimer *m_controlsTimer = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    quintptr m_windowHandle = 0;
    bool m_fullscreenMode = false;
    bool m_paused = false;
    bool m_stopping = false;
    bool m_live = false;
    int m_retryAttempt = 0;
    QPoint m_dragStartPosition;
    bool m_dragStarted = false;
    QString m_playbackBackend = QStringLiteral("mpv");
};
