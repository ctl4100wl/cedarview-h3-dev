#include "videotile.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QProcess>
#include <QStackedLayout>
#include <QTimer>
#include <QtMath>

#include <gst/video/videooverlay.h>

namespace {

class AspectRatioSurface final : public QWidget
{
public:
    explicit AspectRatioSurface(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override
    {
        return qMax(1, qRound(width * 9.0 / 16.0));
    }

    QSize sizeHint() const override { return QSize(640, 360); }
};

} // namespace

VideoTile::VideoTile(int index, QWidget *parent)
    : QWidget(parent),
      m_index(index)
{
    setObjectName(QStringLiteral("videoTile"));
    setMinimumSize(200, 130);
    QSizePolicy tilePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tilePolicy.setHeightForWidth(true);
    setSizePolicy(tilePolicy);
    setAcceptDrops(true);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setSelected(false);

    m_videoSurface = new AspectRatioSurface(this);
    m_videoSurface->setAttribute(Qt::WA_NativeWindow);
    m_videoSurface->setAttribute(Qt::WA_OpaquePaintEvent);
    m_videoSurface->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_videoSurface->setAcceptDrops(true);
    m_videoSurface->installEventFilter(this);
    m_videoSurface->setAutoFillBackground(true);
    m_videoSurface->setStyleSheet(QStringLiteral("background: #080a0d;"));
    m_videoSurface->setMinimumHeight(90);
    m_windowHandle = static_cast<quintptr>(m_videoSurface->winId());

    m_statusLabel = new QLabel(tr("Drop a camera here"), this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_statusLabel->setStyleSheet(QStringLiteral(
        "background: rgba(8, 10, 13, 175); color: #9ca3af; "
        "padding: 8px;"));

    auto *layout = new QStackedLayout(this);
    layout->setStackingMode(QStackedLayout::StackAll);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_videoSurface);
    layout->addWidget(m_statusLabel);
    layout->setCurrentWidget(m_statusLabel);

    m_busTimer = new QTimer(this);
    m_busTimer->setInterval(120);
    connect(m_busTimer, &QTimer::timeout,
            this, &VideoTile::pollGStreamerBus);
}

VideoTile::~VideoTile()
{
    releaseGStreamer();
    releasePlayer(true);
}

int VideoTile::heightForWidth(int width) const
{
    return qMax(1, qRound(width * 9.0 / 16.0));
}

QSize VideoTile::sizeHint() const
{
    return QSize(640, 360);
}

void VideoTile::play(const Camera &camera)
{
    releaseGStreamer();
    releasePlayer();
    m_camera = camera;
    m_playerOutput.clear();
    if (m_playbackBackend == QStringLiteral("gstreamer")) {
        playGStreamer();
    } else {
        playMpv();
    }
}

void VideoTile::playMpv()
{
    setStatus(tr("Starting mpv…"));
    m_player = new QProcess(this);
    m_player->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_player, &QProcess::started, this, [this] {
        const QString bufferText = m_camera.latencyMs > 0
            ? tr("%1 ms buffer").arg(m_camera.latencyMs)
            : tr("no buffer");
        setStatus(tr("Connecting via %1 • %2")
                      .arg(m_camera.transport.toUpper(), bufferText));
        QProcess *expectedPlayer = m_player;
        QTimer::singleShot(1800, this, [this, expectedPlayer] {
            if (m_player == expectedPlayer &&
                m_player->state() == QProcess::Running) {
                setStatus(tr("Live"));
            }
        });
    });
    connect(m_player, &QProcess::readyReadStandardOutput,
            this, &VideoTile::collectPlayerOutput);
    connect(m_player, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    setStatus(tr("mpv missing"), true);
                    emit playbackError(
                        m_index,
                        tr("Could not launch mpv. Install the mpv package."));
                }
            });
    connect(m_player,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
                collectPlayerOutput();
                if (m_player && m_player->property("cedarviewStopping").toBool()) {
                    return;
                }
                setStatus(tr("Offline"), true);
                QString detail = QString::fromUtf8(m_playerOutput).trimmed();
                if (detail.size() > 800) {
                    detail = detail.right(800);
                }
                if (detail.isEmpty()) {
                    detail = tr("mpv exited (%1, code %2)")
                                 .arg(status == QProcess::CrashExit
                                          ? tr("crashed")
                                          : tr("stopped"))
                                 .arg(exitCode);
                }
                emit playbackError(m_index, detail);
            });

    m_player->start(QStringLiteral("mpv"), playerArguments(),
                    QIODevice::ReadOnly);
}

void VideoTile::playGStreamer()
{
    setStatus(tr("Starting GStreamer / Cedrus…"));
    QString error;
    if (!createGStreamerPipeline(&error)) {
        setStatus(tr("GStreamer failed"), true);
        emit playbackError(m_index, error);
        return;
    }
    m_busTimer->start();
    if (gst_element_set_state(m_pipeline, GST_STATE_PLAYING)
        == GST_STATE_CHANGE_FAILURE) {
        setStatus(tr("GStreamer failed"), true);
        emit playbackError(
            m_index, tr("GStreamer rejected the RTSP stream."));
    }
}

GstElement *VideoTile::createGStreamerSink(QString *selectedName)
{
    const QStringList candidates{
        QStringLiteral("xvimagesink"),
        QStringLiteral("ximagesink"),
    };
    for (const QString &name : candidates) {
        GstElement *sink = gst_element_factory_make(
            name.toUtf8().constData(), nullptr);
        if (!sink) {
            continue;
        }
        g_object_set(sink, "sync", FALSE, nullptr);
        if (g_object_class_find_property(
                G_OBJECT_GET_CLASS(sink), "force-aspect-ratio")) {
            g_object_set(sink, "force-aspect-ratio", TRUE, nullptr);
        }
        if (selectedName) {
            *selectedName = name;
        }
        return sink;
    }
    return nullptr;
}

bool VideoTile::createGStreamerPipeline(QString *error)
{
    // GStreamer typefind chooses H.264 or H.265 from the stream. Raising only
    // these two stateless decoder factories makes playbin prefer Cedrus while
    // keeping the normal software decoder as a fallback.
    for (const char *name : {"v4l2slh264dec", "v4l2slh265dec"}) {
        GstElementFactory *factory = gst_element_factory_find(name);
        if (factory) {
            gst_plugin_feature_set_rank(
                GST_PLUGIN_FEATURE(factory), GST_RANK_PRIMARY + 100);
            gst_object_unref(factory);
        }
    }

    m_pipeline = gst_element_factory_make("playbin", nullptr);
    if (!m_pipeline) {
        *error = tr("The GStreamer playbin plugin is missing.");
        return false;
    }

    QString sinkName;
    m_videoSink = createGStreamerSink(&sinkName);
    if (!m_videoSink) {
        *error = tr("Neither xvimagesink nor ximagesink is installed.");
        releaseGStreamer();
        return false;
    }

    const QByteArray uri = m_camera.resolvedRtspUrl().toUtf8();
    g_object_set(m_pipeline,
                 "uri", uri.constData(),
                 "video-sink", m_videoSink,
                 nullptr);
    // Video only: disable audio and text flags while keeping video enabled.
    g_object_set(m_pipeline, "flags", 0x00000001, nullptr);
    g_signal_connect(m_pipeline, "source-setup",
                     G_CALLBACK(VideoTile::sourceSetupHandler), this);

    GstBus *bus = gst_element_get_bus(m_pipeline);
    gst_bus_set_sync_handler(bus, VideoTile::busSyncHandler, this, nullptr);
    gst_object_unref(bus);
    setStatus(tr("Connecting via GStreamer • %1").arg(sinkName));
    return true;
}

void VideoTile::sourceSetupHandler(GstElement *, GstElement *source,
                                   gpointer userData)
{
    auto *tile = static_cast<VideoTile *>(userData);
    GObjectClass *klass = G_OBJECT_GET_CLASS(source);
    if (g_object_class_find_property(klass, "latency")) {
        g_object_set(source, "latency",
                     qMax(0, tile->m_camera.latencyMs), nullptr);
    }
    if (g_object_class_find_property(klass, "protocols")) {
        // GstRTSPLowerTrans: UDP=1, UDP multicast=2, TCP=4.
        const int protocols =
            tile->m_camera.transport == QStringLiteral("udp") ? 1 : 4;
        g_object_set(source, "protocols", protocols, nullptr);
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
        GstVideoOverlay *overlay =
            GST_VIDEO_OVERLAY(GST_MESSAGE_SRC(message));
        gst_video_overlay_set_window_handle(
            overlay, static_cast<guintptr>(tile->m_windowHandle));
    }
    return GST_BUS_PASS;
}

void VideoTile::pollGStreamerBus()
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
                : tr("Unknown GStreamer playback error");
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

QStringList VideoTile::playerArguments() const
{
    QStringList arguments{
        QStringLiteral("--no-config"),
        QStringLiteral("--no-terminal"),
        QStringLiteral("--no-audio"),
        QStringLiteral("--no-osc"),
        QStringLiteral("--osd-level=0"),
        QStringLiteral("--input-default-bindings=no"),
        QStringLiteral("--input-cursor=no"),
        QStringLiteral("--profile=low-latency"),
        QStringLiteral("--framedrop=vo"),
        QStringLiteral("--hwdec=auto"),
        QStringLiteral("--hwdec-codecs=h264,hevc"),
        // Avoid OpenGL entirely. XVideo is tried first, then plain X11.
        QStringLiteral("--vo=xv,x11"),
        QStringLiteral("--wid=%1").arg(m_windowHandle),
        QStringLiteral("--rtsp-transport=%1").arg(m_camera.transport),
        QStringLiteral("--msg-level=all=warn"),
    };

    // The native child window is always 16:9. Fill preserves proportions and
    // center-crops excess edges; Fit preserves the complete camera frame.
    arguments.append(
        m_camera.displayMode == QStringLiteral("fit")
            ? QStringLiteral("--panscan=0")
            : QStringLiteral("--panscan=1.0"));
    const double zoom = qLn(
        static_cast<double>(qBound(100, m_camera.zoomPercent, 200)) / 100.0)
        / qLn(2.0);
    arguments.append(
        QStringLiteral("--video-zoom=%1").arg(zoom, 0, 'f', 4));

    if (m_camera.latencyMs <= 0) {
        arguments.append(QStringLiteral("--cache=no"));
    } else {
        const QString seconds = QString::number(
            static_cast<double>(m_camera.latencyMs) / 1000.0, 'f', 3);
        arguments.append(QStringLiteral("--cache=yes"));
        arguments.append(QStringLiteral("--cache-pause=no"));
        arguments.append(
            QStringLiteral("--cache-secs=%1").arg(seconds));
        arguments.append(
            QStringLiteral("--demuxer-readahead-secs=%1").arg(seconds));
    }

    arguments.append(m_camera.resolvedRtspUrl());
    return arguments;
}

void VideoTile::collectPlayerOutput()
{
    if (!m_player) {
        return;
    }
    m_playerOutput.append(m_player->readAllStandardOutput());
    constexpr qsizetype MaxLogBytes = 4096;
    if (m_playerOutput.size() > MaxLogBytes) {
        m_playerOutput = m_playerOutput.right(MaxLogBytes);
    }
}

void VideoTile::stop()
{
    releaseGStreamer();
    releasePlayer();
    m_camera = Camera{};
    setStatus(tr("Drop a camera here"));
    m_videoSurface->update();
}

void VideoTile::setPlaybackBackend(const QString &backend)
{
    const QString normalized = backend == QStringLiteral("gstreamer")
        ? QStringLiteral("gstreamer")
        : QStringLiteral("mpv");
    if (normalized == m_playbackBackend) {
        return;
    }
    m_playbackBackend = normalized;
    if (hasCamera()) {
        const Camera camera = m_camera;
        play(camera);
    }
}

void VideoTile::setSelected(bool selected)
{
    setProperty("selected", selected);
    setToolTip(selected
        ? tr("Selected tile. Drag a camera here or right-click to clear.")
        : tr("Drag a camera here or right-click to clear."));
}

void VideoTile::setFullscreenMode(bool fullscreen)
{
    m_fullscreenMode = fullscreen;
}

void VideoTile::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *exitFullscreenAction = nullptr;
    if (m_fullscreenMode) {
        exitFullscreenAction = menu.addAction(tr("Exit fullscreen"));
        menu.addSeparator();
    }
    QAction *clearAction = menu.addAction(tr("Clear tile"));
    clearAction->setEnabled(hasCamera());
    QAction *selectedAction = menu.exec(event->globalPos());
    if (exitFullscreenAction &&
        selectedAction == exitFullscreenAction) {
        emit exitFullscreenRequested();
    } else if (selectedAction == clearAction) {
        stop();
        emit cleared(m_index);
    }
}

void VideoTile::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(
            QStringLiteral("application/x-cedarview-camera-id"))) {
        event->acceptProposedAction();
    }
}

void VideoTile::dropEvent(QDropEvent *event)
{
    const QByteArray id = event->mimeData()->data(
        QStringLiteral("application/x-cedarview-camera-id"));
    if (id.isEmpty()) {
        return;
    }
    emit cameraDropped(QString::fromUtf8(id), m_index);
    event->acceptProposedAction();
}

bool VideoTile::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_videoSurface) {
        if (event->type() == QEvent::DragEnter) {
            dragEnterEvent(static_cast<QDragEnterEvent *>(event));
            return event->isAccepted();
        }
        if (event->type() == QEvent::Drop) {
            dropEvent(static_cast<QDropEvent *>(event));
            return event->isAccepted();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void VideoTile::mousePressEvent(QMouseEvent *event)
{
    emit selected(m_index);
    QWidget::mousePressEvent(event);
}

void VideoTile::setStatus(const QString &text, bool error)
{
    m_statusLabel->setText(text);
    m_statusLabel->setVisible(text != tr("Live"));
    m_statusLabel->setStyleSheet(error
        ? QStringLiteral(
              "background: rgba(8, 10, 13, 190); color: #ff6b6b; "
              "padding: 8px;")
        : QStringLiteral(
              "background: rgba(8, 10, 13, 175); color: #9ca3af; "
              "padding: 8px;"));
}

void VideoTile::releasePlayer(bool immediate)
{
    if (!m_player) {
        return;
    }

    QProcess *player = m_player;
    m_player = nullptr;
    player->setProperty("cedarviewStopping", true);
    player->disconnect(this);

    if (player->state() == QProcess::NotRunning) {
        player->deleteLater();
        return;
    }

    if (immediate) {
        player->kill();
        return;
    }

    player->terminate();
    connect(player,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            player, &QObject::deleteLater);
    QTimer::singleShot(500, player, [player] {
        if (player->state() != QProcess::NotRunning) {
            player->kill();
        }
    });
}

void VideoTile::releaseGStreamer()
{
    if (m_busTimer) {
        m_busTimer->stop();
    }
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
