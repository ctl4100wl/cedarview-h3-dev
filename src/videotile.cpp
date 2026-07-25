#include "videotile.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <gst/video/videooverlay.h>

VideoTile::VideoTile(int index, QWidget *parent)
    : QFrame(parent),
      m_index(index)
{
    setObjectName(QStringLiteral("videoTile"));
    setFrameShape(QFrame::StyledPanel);
    setMinimumSize(200, 130);
    setSelected(false);

    m_videoSurface = new QWidget(this);
    m_videoSurface->setAttribute(Qt::WA_NativeWindow);
    m_videoSurface->setAttribute(Qt::WA_OpaquePaintEvent);
    m_videoSurface->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_videoSurface->setAutoFillBackground(true);
    m_videoSurface->setStyleSheet(QStringLiteral("background: #080a0d;"));
    m_videoSurface->setMinimumHeight(90);
    m_windowHandle = static_cast<quintptr>(m_videoSurface->winId());

    m_titleLabel = new QLabel(tr("Empty tile"), this);
    m_titleLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));
    m_statusLabel = new QLabel(tr("Select a camera"), this);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #88909b;"));

    m_stopButton = new QPushButton(tr("Clear"), this);
    m_stopButton->setFlat(true);
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QPushButton::clicked, this, [this] {
        stop();
        emit cleared(m_index);
    });

    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(8, 3, 5, 4);
    footer->addWidget(m_titleLabel);
    footer->addWidget(m_statusLabel, 1);
    footer->addWidget(m_stopButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(0);
    layout->addWidget(m_videoSurface, 1);
    layout->addLayout(footer);

    m_busTimer = new QTimer(this);
    m_busTimer->setInterval(150);
    connect(m_busTimer, &QTimer::timeout, this, &VideoTile::pollBus);
}

VideoTile::~VideoTile()
{
    releasePipeline();
}

void VideoTile::setVideoSinkPreference(const QString &preference)
{
    m_sinkPreference = preference;
}

void VideoTile::play(const Camera &camera)
{
    releasePipeline();
    m_camera = camera;
    m_titleLabel->setText(camera.name);
    m_stopButton->setEnabled(true);
    setStatus(tr("Connecting…"));

    QString error;
    if (!createPipeline(&error)) {
        setStatus(tr("Failed"), true);
        emit playbackError(m_index, error);
        return;
    }

    m_busTimer->start();
    const GstStateChangeReturn result =
        gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (result == GST_STATE_CHANGE_FAILURE) {
        setStatus(tr("Failed"), true);
        emit playbackError(m_index, tr("GStreamer rejected the stream."));
    }
}

void VideoTile::stop()
{
    releasePipeline();
    m_camera = Camera{};
    m_titleLabel->setText(tr("Empty tile"));
    m_stopButton->setEnabled(false);
    setStatus(tr("Select a camera"));
    m_videoSurface->update();
}

void VideoTile::setSelected(bool selected)
{
    setProperty("selected", selected);
    setStyleSheet(selected
        ? QStringLiteral(
              "QFrame#videoTile { border: 2px solid #ff7a1a; "
              "background: #171a1f; border-radius: 4px; }")
        : QStringLiteral(
              "QFrame#videoTile { border: 1px solid #363b43; "
              "background: #171a1f; border-radius: 4px; }"));
}

void VideoTile::mousePressEvent(QMouseEvent *event)
{
    emit selected(m_index);
    QFrame::mousePressEvent(event);
}

GstElement *VideoTile::createVideoSink(QString *selectedName)
{
    QStringList candidates;
    if (m_sinkPreference == QStringLiteral("glimagesink")) {
        candidates << QStringLiteral("glimagesink");
    } else if (m_sinkPreference == QStringLiteral("ximagesink")) {
        candidates << QStringLiteral("ximagesink");
    } else {
        candidates << QStringLiteral("glimagesink")
                   << QStringLiteral("ximagesink");
    }

    for (const QString &name : candidates) {
        GstElement *sink = gst_element_factory_make(name.toUtf8().constData(),
                                                    "cedarview-video-sink");
        if (sink) {
            g_object_set(sink, "sync", FALSE, nullptr);
            if (selectedName) {
                *selectedName = name;
            }
            return sink;
        }
    }
    return nullptr;
}

bool VideoTile::createPipeline(QString *error)
{
    m_pipeline = gst_element_factory_make("playbin", nullptr);
    if (!m_pipeline) {
        *error = tr("The GStreamer playbin plugin is missing.");
        return false;
    }

    QString sinkName;
    m_videoSink = createVideoSink(&sinkName);
    if (!m_videoSink) {
        *error = tr("Neither glimagesink nor ximagesink is installed.");
        releasePipeline();
        return false;
    }

    const QByteArray uri = m_camera.resolvedRtspUrl().toUtf8();
    g_object_set(m_pipeline,
                 "uri", uri.constData(),
                 "video-sink", m_videoSink,
                 nullptr);
    g_signal_connect(m_pipeline, "source-setup",
                     G_CALLBACK(VideoTile::sourceSetupHandler), this);

    GstBus *bus = gst_element_get_bus(m_pipeline);
    gst_bus_set_sync_handler(bus, VideoTile::busSyncHandler, this, nullptr);
    gst_object_unref(bus);
    setStatus(tr("Connecting via %1").arg(sinkName));
    return true;
}

void VideoTile::sourceSetupHandler(GstElement *, GstElement *source,
                                   gpointer userData)
{
    auto *tile = static_cast<VideoTile *>(userData);
    GObjectClass *klass = G_OBJECT_GET_CLASS(source);

    if (g_object_class_find_property(klass, "latency")) {
        g_object_set(source, "latency", tile->m_camera.latencyMs, nullptr);
    }
    if (tile->m_camera.forceTcp &&
        g_object_class_find_property(klass, "protocols")) {
        // GstRTSPLowerTrans TCP is bit 2, i.e. integer value 4.
        g_object_set(source, "protocols", 4, nullptr);
    }
    if (g_object_class_find_property(klass, "drop-on-latency")) {
        g_object_set(source, "drop-on-latency", TRUE, nullptr);
    }
    if (g_object_class_find_property(klass, "tcp-timeout")) {
        g_object_set(source, "tcp-timeout",
                     static_cast<guint64>(10 * G_USEC_PER_SEC), nullptr);
    }
}

GstBusSyncReply VideoTile::busSyncHandler(GstBus *, GstMessage *message,
                                          gpointer userData)
{
    auto *tile = static_cast<VideoTile *>(userData);
    if (gst_is_video_overlay_prepare_window_handle_message(message)) {
        GstVideoOverlay *overlay = GST_VIDEO_OVERLAY(GST_MESSAGE_SRC(message));
        gst_video_overlay_set_window_handle(
            overlay, static_cast<guintptr>(tile->m_windowHandle));
        return GST_BUS_PASS;
    }
    return GST_BUS_PASS;
}

void VideoTile::pollBus()
{
    if (!m_pipeline) {
        return;
    }

    GstBus *bus = gst_element_get_bus(m_pipeline);
    while (GstMessage *message = gst_bus_pop(bus)) {
        switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *gstError = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_error(message, &gstError, &debug);
            const QString text = gstError
                ? QString::fromUtf8(gstError->message)
                : tr("Unknown playback error");
            setStatus(tr("Offline"), true);
            emit playbackError(m_index, text);
            if (gstError) {
                g_error_free(gstError);
            }
            g_free(debug);
            break;
        }
        case GST_MESSAGE_STATE_CHANGED:
            if (GST_MESSAGE_SRC(message) == GST_OBJECT(m_pipeline)) {
                GstState oldState;
                GstState newState;
                GstState pending;
                gst_message_parse_state_changed(
                    message, &oldState, &newState, &pending);
                Q_UNUSED(oldState)
                Q_UNUSED(pending)
                if (newState == GST_STATE_PLAYING) {
                    setStatus(tr("Live"));
                }
            }
            break;
        case GST_MESSAGE_EOS:
            setStatus(tr("Stream ended"), true);
            break;
        default:
            break;
        }
        gst_message_unref(message);
    }
    gst_object_unref(bus);
}

void VideoTile::setStatus(const QString &text, bool error)
{
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error
        ? QStringLiteral("color: #ff6b6b;")
        : QStringLiteral("color: #88909b;"));
}

void VideoTile::releasePipeline()
{
    m_busTimer->stop();
    if (!m_pipeline) {
        m_videoSink = nullptr;
        return;
    }
    GstBus *bus = gst_element_get_bus(m_pipeline);
    gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
    gst_object_unref(bus);
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    gst_object_unref(m_pipeline);
    m_pipeline = nullptr;
    m_videoSink = nullptr;
}
