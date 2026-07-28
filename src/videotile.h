#pragma once

#include "camera.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QPoint>
#include <QWidget>
#include <QStringList>

#include <gst/gst.h>

class QLabel;
class QJsonArray;
class QLocalSocket;
class QProcess;
class QToolButton;
class QTimer;
class QWidget;
class QResizeEvent;

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
    void setLiveEdgeSettings(bool enabled, int delayThresholdMs,
                             bool ultraLiveMode);
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
    void resizeEvent(QResizeEvent *event) override;

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
    void prepareMpvIpc();
    void connectMpvIpc(QProcess *player, int attempt = 0);
    void readMpvIpc();
    void pollLiveEdge();
    void evaluateLiveEdgeSample(double timePosition);
    void flushToLiveEdge(bool automatic);
    void resetLiveEdgeTracking(bool resetCooldown = false);
    void releaseMpvIpc();
    void sendMpvCommand(const QJsonArray &command, int requestId = 0);
    void preparePlayerLog(const QStringList &arguments);
    void appendPlayerLog(const QByteArray &data) const;
    void updateOverlay();
    void showControls();
    void scheduleReconnect(const QString &detail);
    void startPlayback();
    void markLive();
    void setStatus(const QString &text, bool error = false);
    void positionOverlayWidgets();
    void setControlsVisible(bool visible);
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
    QLabel *m_cameraNameLabel = nullptr;
    QLabel *m_connectionLabel = nullptr;
    QToolButton *m_pauseButton = nullptr;
    QToolButton *m_streamButton = nullptr;
    QToolButton *m_liveButton = nullptr;
    QToolButton *m_retryButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    QTimer *m_controlsTimer = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QTimer *m_liveEdgeTimer = nullptr;
    QLocalSocket *m_ipcSocket = nullptr;
    QByteArray m_ipcBuffer;
    QString m_ipcSocketPath;
    QString m_playerLogPath;
    quintptr m_windowHandle = 0;
    bool m_fullscreenMode = false;
    bool m_paused = false;
    bool m_stopping = false;
    bool m_live = false;
    bool m_liveEdgeCorrectionEnabled = true;
    bool m_ultraLiveMode = false;
    bool m_coreIdle = false;
    bool m_pausedForCache = false;
    bool m_waitingAfterFlush = false;
    bool m_flushWasAutomatic = false;
    bool m_progressSeenAfterFlush = false;
    int m_liveEdgeDelayThresholdMs = 1250;
    int m_delayBadSamples = 0;
    double m_lastTimePosition = -1.0;
    double m_accumulatedDelaySeconds = 0.0;
    qint64 m_lastSampleMs = -1;
    qint64 m_lastProgressMs = -1;
    qint64 m_frameDropCount = 0;
    qint64 m_decoderFrameDropCount = 0;
    qint64 m_monitorReadyAtMs = 0;
    qint64 m_flushStartedMs = -1;
    qint64 m_autoCooldownUntilMs = 0;
    QElapsedTimer m_monotonicClock;
    int m_retryAttempt = 0;
    QPoint m_dragStartPosition;
    bool m_dragStarted = false;
    QString m_playbackBackend = QStringLiteral("mpv");
};
