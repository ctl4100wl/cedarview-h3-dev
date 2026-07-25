#pragma once

#include "camera.h"

#include <QFrame>

#include <gst/gst.h>

class QLabel;
class QPushButton;
class QTimer;
class QWidget;

class VideoTile final : public QFrame
{
    Q_OBJECT

public:
    explicit VideoTile(int index, QWidget *parent = nullptr);
    ~VideoTile() override;

    int index() const { return m_index; }
    QString cameraId() const { return m_camera.id; }
    bool hasCamera() const { return !m_camera.id.isEmpty(); }
    void setVideoSinkPreference(const QString &preference);
    void play(const Camera &camera);
    void stop();
    void setSelected(bool selected);

signals:
    void selected(int index);
    void cleared(int index);
    void playbackError(int index, const QString &message);

protected:
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void pollBus();

private:
    static GstBusSyncReply busSyncHandler(GstBus *bus, GstMessage *message,
                                          gpointer userData);
    static void sourceSetupHandler(GstElement *playbin, GstElement *source,
                                   gpointer userData);
    bool createPipeline(QString *error);
    GstElement *createVideoSink(QString *selectedName);
    void setStatus(const QString &text, bool error = false);
    void releasePipeline();

    int m_index = 0;
    Camera m_camera;
    QString m_sinkPreference = QStringLiteral("auto");
    GstElement *m_pipeline = nullptr;
    GstElement *m_videoSink = nullptr;
    QWidget *m_videoSurface = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_stopButton = nullptr;
    QTimer *m_busTimer = nullptr;
    quintptr m_windowHandle = 0;
};
