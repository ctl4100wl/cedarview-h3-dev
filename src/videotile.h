#pragma once

#include "camera.h"

#include <QByteArray>
#include <QWidget>
#include <QStringList>

#include <gst/gst.h>

class QLabel;
class QProcess;
class QTimer;

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
    void setSelected(bool selected);
    void setFullscreenMode(bool fullscreen);
    void setPlaybackBackend(const QString &backend);
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;

signals:
    void selected(int index);
    void cleared(int index);
    void cameraDropped(const QString &cameraId, int index);
    void playbackError(int index, const QString &message);
    void exitFullscreenRequested();

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

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
    quintptr m_windowHandle = 0;
    bool m_fullscreenMode = false;
    QString m_playbackBackend = QStringLiteral("mpv");
};
