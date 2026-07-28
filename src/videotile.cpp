#include "videotile.h"

#include <QApplication>
#include <QCoreApplication>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEnterEvent>
#include <QEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocalSocket>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QProcess>
#include <QResizeEvent>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

#include <gst/video/videooverlay.h>

namespace {

constexpr auto CameraMimeType = "application/x-cedarview-camera-id";
constexpr int ControlsTimeoutMs = 2200;
constexpr int MaximumReconnectDelayMs = 15000;
constexpr int StablePlaybackMs = 60000;
constexpr int LiveEdgePollIntervalMs = 1000;
constexpr int LiveEdgeWarmupMs = 8000;
constexpr int LiveEdgeBadSamplesRequired = 3;
constexpr int LiveEdgeStallMs = 4000;
constexpr int LiveEdgeFlushRecoveryMs = 6000;
constexpr int LiveEdgeCooldownMs = 60000;
constexpr qint64 MaximumPlayerLogBytes = 2 * 1024 * 1024;

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

QToolButton *makeOverlayButton(const QString &text, const QString &tooltip,
                               QWidget *parent)
{
    auto *button = new QToolButton(parent);
    // mpv/xv renders into a native X11 child window. Overlay controls must
    // also be native siblings or the video window will cover them.
    button->setAttribute(Qt::WA_NativeWindow);
    button->setText(text);
    button->setToolTip(tooltip);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setStyleSheet(QStringLiteral(
        "QToolButton { color: white; background: rgba(20, 24, 30, 185); "
        "border: 1px solid rgba(255,255,255,45); border-radius: 4px; "
        "padding: 4px 7px; font-weight: 600; }"
        "QToolButton:hover { background: rgba(255, 122, 26, 220); "
        "color: #111419; }"));
    return button;
}

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
    setMouseTracking(true);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setSelected(false);

    m_videoSurface = new AspectRatioSurface(this);
    m_videoSurface->setAttribute(Qt::WA_NativeWindow);
    m_videoSurface->setAttribute(Qt::WA_OpaquePaintEvent);
    m_videoSurface->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_videoSurface->setAcceptDrops(true);
    m_videoSurface->setMouseTracking(true);
    m_videoSurface->installEventFilter(this);
    m_videoSurface->setAutoFillBackground(true);
    m_videoSurface->setStyleSheet(QStringLiteral("background: #080a0d;"));
    m_videoSurface->setMinimumHeight(90);
    m_windowHandle = static_cast<quintptr>(m_videoSurface->winId());

    m_statusLabel = new QLabel(tr("Drop a camera here"), this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_statusLabel->setAttribute(Qt::WA_NativeWindow);

    // Do not place a transparent widget across the whole video. A native
    // mpv/XVideo child window cannot be alpha-composited through a regular
    // Qt widget on X11, and the "transparent" center becomes a black box.
    // Every badge/button is instead a small native sibling over the video.
    m_cameraNameLabel = new QLabel(this);
    m_cameraNameLabel->setAttribute(Qt::WA_NativeWindow);
    m_cameraNameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_cameraNameLabel->setStyleSheet(QStringLiteral(
        "color: white; background: #20242b; border-radius: 4px; "
        "padding: 4px 7px; font-weight: 700;"));
    m_connectionLabel = new QLabel(this);
    m_connectionLabel->setAttribute(Qt::WA_NativeWindow);
    m_connectionLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_connectionLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_connectionLabel->setStyleSheet(QStringLiteral(
        "color: #e5e7eb; background: #20242b; "
        "border-radius: 4px; padding: 4px 7px;"));

    m_pauseButton = makeOverlayButton(
        QStringLiteral("Ⅱ"), tr("Pause this feed"), this);
    m_streamButton = makeOverlayButton(
        tr("SUB"), tr("Switch between Main and Sub stream"), this);
    m_liveButton = makeOverlayButton(
        tr("LIVE"), tr("Discard queued frames and jump to live"), this);
    m_retryButton = makeOverlayButton(
        QStringLiteral("↻"), tr("Reconnect now"), this);
    m_closeButton = makeOverlayButton(
        QStringLiteral("×"), tr("Close this feed"), this);

    connect(m_pauseButton, &QToolButton::clicked, this, [this] {
        if (m_paused) {
            resume();
        } else {
            pause();
        }
        showControls();
    });
    connect(m_streamButton, &QToolButton::clicked, this, [this] {
        setStreamSubtype(m_camera.subtype == 0 ? 1 : 0);
        showControls();
    });
    connect(m_liveButton, &QToolButton::clicked, this, [this] {
        flushToLiveEdge(false);
        showControls();
    });
    connect(m_retryButton, &QToolButton::clicked,
            this, &VideoTile::reconnectNow);
    connect(m_closeButton, &QToolButton::clicked, this, [this] {
        stop();
        emit cleared(m_index);
    });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_videoSurface);

    m_controlsTimer = new QTimer(this);
    m_controlsTimer->setSingleShot(true);
    m_controlsTimer->setInterval(ControlsTimeoutMs);
    connect(m_controlsTimer, &QTimer::timeout,
            this, &VideoTile::hideControls);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this] {
        if (hasCamera() && !m_paused && !m_stopping) {
            startPlayback();
        }
    });

    m_busTimer = new QTimer(this);
    m_busTimer->setInterval(120);
    connect(m_busTimer, &QTimer::timeout,
            this, &VideoTile::pollGStreamerBus);

    m_liveEdgeTimer = new QTimer(this);
    m_liveEdgeTimer->setInterval(LiveEdgePollIntervalMs);
    connect(m_liveEdgeTimer, &QTimer::timeout,
            this, &VideoTile::pollLiveEdge);
    m_monotonicClock.start();

    updateOverlay();
    hideControls();
}

VideoTile::~VideoTile()
{
    m_stopping = true;
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
    m_stopping = true;
    m_reconnectTimer->stop();
    releaseGStreamer();
    releasePlayer();
    m_camera = camera;
    m_paused = false;
    m_live = false;
    m_retryAttempt = 0;
    resetLiveEdgeTracking(true);
    m_playerOutput.clear();
    m_stopping = false;
    updateOverlay();
    startPlayback();
}

void VideoTile::startPlayback()
{
    if (!hasCamera() || m_paused || m_stopping) {
        return;
    }
    m_stopping = true;
    releaseGStreamer();
    releasePlayer();
    m_stopping = false;
    m_live = false;
    m_playerOutput.clear();
    updateOverlay();
    if (m_playbackBackend == QStringLiteral("gstreamer")) {
        playGStreamer();
    } else {
        playMpv();
    }
}

void VideoTile::playMpv()
{
    setStatus(tr("Connecting…"));
    prepareMpvIpc();
    auto *player = new QProcess(this);
    m_player = player;
    player->setProcessChannelMode(QProcess::MergedChannels);

    connect(player, &QProcess::started, this, [this, player] {
        if (m_player != player) {
            return;
        }
        connectMpvIpc(player);
        m_connectionLabel->setText(
            tr("%1 • %2")
                .arg(m_camera.transport.toUpper(),
                     m_camera.subtype == 0 ? tr("Main") : tr("Sub")));
        QTimer::singleShot(StablePlaybackMs, this, [this, player] {
            if (m_player == player &&
                player->state() == QProcess::Running) {
                m_retryAttempt = 0;
            }
        });
    });
    connect(player, &QProcess::readyReadStandardOutput,
            this, &VideoTile::collectPlayerOutput);
    connect(player, &QProcess::errorOccurred, this,
            [this, player](QProcess::ProcessError error) {
                if (m_player != player ||
                    player->property("cedarviewStopping").toBool()) {
                    return;
                }
                if (error == QProcess::FailedToStart) {
                    appendPlayerLog(
                        QByteArrayLiteral(
                            "\n[cedarview] mpv failed to start\n"));
                    scheduleReconnect(
                        tr("Could not launch mpv. Install the mpv package."));
                }
            });
    connect(player,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this, player](int exitCode, QProcess::ExitStatus status) {
                if (m_player == player) {
                    collectPlayerOutput();
                    m_player = nullptr;
                }
                const bool intentional =
                    player->property("cedarviewStopping").toBool() ||
                    m_stopping || m_paused || !hasCamera();
                player->deleteLater();
                if (intentional) {
                    return;
                }

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
                appendPlayerLog(
                    QStringLiteral(
                        "\n[cedarview] mpv exit status=%1 code=%2\n")
                        .arg(status == QProcess::CrashExit
                                 ? QStringLiteral("crash")
                                 : QStringLiteral("normal"))
                        .arg(exitCode)
                        .toUtf8());
                scheduleReconnect(
                    tr("%1\nLog: %2").arg(detail, m_playerLogPath));
            });

    const QStringList arguments = playerArguments();
    preparePlayerLog(arguments);
    player->start(QStringLiteral("mpv"), arguments, QIODevice::ReadOnly);
}

void VideoTile::playGStreamer()
{
    setStatus(tr("Connecting…"));
    QString error;
    if (!createGStreamerPipeline(&error)) {
        scheduleReconnect(error);
        return;
    }
    m_busTimer->start();
    if (gst_element_set_state(m_pipeline, GST_STATE_PLAYING)
        == GST_STATE_CHANGE_FAILURE) {
        scheduleReconnect(tr("GStreamer rejected the RTSP stream."));
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
    g_object_set(m_pipeline, "flags", 0x00000001, nullptr);
    g_signal_connect(m_pipeline, "source-setup",
                     G_CALLBACK(VideoTile::sourceSetupHandler), this);

    GstBus *bus = gst_element_get_bus(m_pipeline);
    gst_bus_set_sync_handler(bus, VideoTile::busSyncHandler, this, nullptr);
    gst_object_unref(bus);
    m_connectionLabel->setText(
        tr("GStreamer • %1").arg(
            m_camera.subtype == 0 ? tr("Main") : tr("Sub")));
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
    bool reconnect = false;
    QString reconnectDetail;
    while (GstMessage *message = gst_bus_pop(bus)) {
        switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *gstError = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_error(message, &gstError, &debug);
            reconnectDetail = gstError
                ? QString::fromUtf8(gstError->message)
                : tr("Unknown GStreamer playback error");
            reconnect = true;
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
                    markLive();
                }
            }
            break;
        case GST_MESSAGE_EOS:
            reconnectDetail = tr("Stream ended");
            reconnect = true;
            break;
        default:
            break;
        }
        gst_message_unref(message);
        if (reconnect) {
            break;
        }
    }
    gst_object_unref(bus);
    if (reconnect && !m_stopping && !m_paused && hasCamera()) {
        releaseGStreamer();
        scheduleReconnect(reconnectDetail);
    }
}

QStringList VideoTile::playerArguments() const
{
    QStringList arguments{
        QStringLiteral("--no-config"),
        // Keep diagnostic output connected to QProcess. --no-terminal
        // silences stdout/stderr completely, which previously left only a
        // generic exit code in the per-camera log.
        QStringLiteral("--terminal=yes"),
        QStringLiteral("--input-terminal=no"),
        QStringLiteral("--quiet"),
        QStringLiteral("--msg-color=no"),
        QStringLiteral("--msg-module"),
        QStringLiteral("--msg-time"),
        QStringLiteral("--no-audio"),
        QStringLiteral("--no-osc"),
        QStringLiteral("--osd-level=0"),
        QStringLiteral("--input-default-bindings=no"),
        QStringLiteral("--input-cursor=no"),
        QStringLiteral("--input-ipc-server=%1").arg(m_ipcSocketPath),
        QStringLiteral("--profile=low-latency"),
        QStringLiteral("--framedrop=vo"),
        QStringLiteral("--hwdec=auto"),
        QStringLiteral("--hwdec-codecs=h264,hevc"),
        QStringLiteral("--vo=xv,x11"),
        QStringLiteral("--wid=%1").arg(m_windowHandle),
        QStringLiteral("--rtsp-transport=%1").arg(m_camera.transport),
        QStringLiteral("--msg-level=all=warn"),
    };
    if (m_ultraLiveMode) {
        arguments.append(QStringLiteral("--untimed=yes"));
    }

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
    const QByteArray output = m_player->readAllStandardOutput();
    m_playerOutput.append(output);
    appendPlayerLog(output);
    constexpr qsizetype MaxLogBytes = 4096;
    if (m_playerOutput.size() > MaxLogBytes) {
        m_playerOutput = m_playerOutput.right(MaxLogBytes);
    }
}

void VideoTile::prepareMpvIpc()
{
    releaseMpvIpc();
    resetLiveEdgeTracking(false);
    m_ipcSocketPath = QStringLiteral("/tmp/cedarview-%1-tile-%2.sock")
                          .arg(QCoreApplication::applicationPid())
                          .arg(m_index);
    QFile::remove(m_ipcSocketPath);
}

void VideoTile::connectMpvIpc(QProcess *player, int attempt)
{
    if (m_player != player ||
        player->state() != QProcess::Running ||
        m_ipcSocketPath.isEmpty()) {
        return;
    }
    if (attempt >= 30) {
        appendPlayerLog(
            QByteArrayLiteral(
                "[cedarview] MPV IPC socket was not available after 6 s\n"));
        updateOverlay();
        return;
    }

    auto *socket = new QLocalSocket(this);
    m_ipcSocket = socket;
    connect(socket, &QLocalSocket::readyRead,
            this, &VideoTile::readMpvIpc);
    connect(socket, &QLocalSocket::connected, this, [this, socket] {
        if (m_ipcSocket != socket) {
            return;
        }
        socket->setProperty("cedarviewEverConnected", true);
        appendPlayerLog(
            QByteArrayLiteral("[cedarview] MPV IPC connected\n"));
        updateOverlay();
        if (m_liveEdgeCorrectionEnabled) {
            m_liveEdgeTimer->start();
            pollLiveEdge();
        }
    });
    connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
        if (m_ipcSocket != socket) {
            return;
        }
        m_liveEdgeTimer->stop();
        m_ipcSocket = nullptr;
        socket->deleteLater();
        updateOverlay();
    });
    connect(socket, &QLocalSocket::errorOccurred, this,
            [this, player, socket, attempt](QLocalSocket::LocalSocketError) {
                if (m_ipcSocket != socket ||
                    socket->property("cedarviewEverConnected").toBool()) {
                    return;
                }
                socket->abort();
                m_ipcSocket = nullptr;
                socket->deleteLater();
                QTimer::singleShot(
                    200, this, [this, player, attempt] {
                        connectMpvIpc(player, attempt + 1);
                    });
            });
    socket->connectToServer(m_ipcSocketPath, QIODevice::ReadWrite);

    QTimer::singleShot(200, this, [this, player, socket, attempt] {
        if (m_player != player || m_ipcSocket != socket ||
            socket->state() == QLocalSocket::ConnectedState) {
            return;
        }
        socket->abort();
        m_ipcSocket = nullptr;
        socket->deleteLater();
        connectMpvIpc(player, attempt + 1);
    });
}

void VideoTile::readMpvIpc()
{
    if (!m_ipcSocket) {
        return;
    }
    m_ipcBuffer.append(m_ipcSocket->readAll());
    while (true) {
        const qsizetype lineEnd = m_ipcBuffer.indexOf('\n');
        if (lineEnd < 0) {
            break;
        }
        const QByteArray line = m_ipcBuffer.left(lineEnd).trimmed();
        m_ipcBuffer.remove(0, lineEnd + 1);
        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError ||
            !document.isObject()) {
            continue;
        }
        const QJsonObject response = document.object();
        const QString event =
            response.value(QStringLiteral("event")).toString();
        if (event == QStringLiteral("file-loaded")) {
            appendPlayerLog(
                QByteArrayLiteral("[cedarview] MPV file-loaded\n"));
            markLive();
        } else if (event == QStringLiteral("end-file")) {
            const QString reason =
                response.value(QStringLiteral("reason")).toString(
                    QStringLiteral("unknown"));
            QString fileError =
                response.value(QStringLiteral("file_error")).toString();
            if (fileError.isEmpty()) {
                fileError =
                    response.value(QStringLiteral("error")).toString();
            }
            appendPlayerLog(
                QStringLiteral(
                    "[cedarview] MPV end-file reason=%1 error=%2\n")
                    .arg(reason,
                         fileError.isEmpty()
                             ? QStringLiteral("none")
                             : fileError)
                    .toUtf8());
        }
        const int requestId =
            response.value(QStringLiteral("request_id")).toInt();
        const QJsonValue data = response.value(QStringLiteral("data"));
        if (requestId == 100 && data.isDouble()) {
            evaluateLiveEdgeSample(data.toDouble());
        } else if (requestId == 101 && data.isBool()) {
            m_coreIdle = data.toBool();
        } else if (requestId == 102 && data.isBool()) {
            m_pausedForCache = data.toBool();
        } else if (requestId == 103 && data.isDouble()) {
            m_frameDropCount =
                static_cast<qint64>(data.toDouble());
        } else if (requestId == 104 && data.isDouble()) {
            m_decoderFrameDropCount =
                static_cast<qint64>(data.toDouble());
        } else if (requestId == 200 &&
                   response.value(QStringLiteral("error")).toString()
                       != QStringLiteral("success")) {
            appendPlayerLog(
                QStringLiteral("[cedarview] drop-buffers failed: %1\n")
                    .arg(response.value(QStringLiteral("error")).toString())
                    .toUtf8());
        }
    }
}

void VideoTile::pollLiveEdge()
{
    if (!m_liveEdgeCorrectionEnabled || !m_ipcSocket ||
        m_ipcSocket->state() != QLocalSocket::ConnectedState ||
        !m_player || m_player->state() != QProcess::Running ||
        m_paused || m_stopping) {
        return;
    }
    const qint64 nowMs = m_monotonicClock.elapsed();
    if (m_waitingAfterFlush && m_flushWasAutomatic &&
        !m_progressSeenAfterFlush &&
        nowMs - m_flushStartedMs >= LiveEdgeFlushRecoveryMs) {
        appendPlayerLog(
            QStringLiteral(
                "[cedarview] no media-time response %1 ms after "
                "live-edge flush; reopening this feed\n")
                .arg(LiveEdgeFlushRecoveryMs)
                .toUtf8());
        m_waitingAfterFlush = false;
        m_flushWasAutomatic = false;
        m_autoCooldownUntilMs = nowMs + LiveEdgeCooldownMs;
        setStatus(tr("Feed stalled; reopening…"), true);
        QTimer::singleShot(0, this, [this] {
            if (hasCamera() && !m_paused && !m_stopping) {
                startPlayback();
            }
        });
        return;
    }
    sendMpvCommand(
        QJsonArray{QStringLiteral("get_property"),
                   QStringLiteral("time-pos")},
        100);
    sendMpvCommand(
        QJsonArray{QStringLiteral("get_property"),
                   QStringLiteral("core-idle")},
        101);
    sendMpvCommand(
        QJsonArray{QStringLiteral("get_property"),
                   QStringLiteral("paused-for-cache")},
        102);
    sendMpvCommand(
        QJsonArray{QStringLiteral("get_property"),
                   QStringLiteral("frame-drop-count")},
        103);
    sendMpvCommand(
        QJsonArray{QStringLiteral("get_property"),
                   QStringLiteral("decoder-frame-drop-count")},
        104);
}

void VideoTile::evaluateLiveEdgeSample(double timePosition)
{
    if (!qIsFinite(timePosition) || !m_liveEdgeCorrectionEnabled) {
        return;
    }
    const qint64 nowMs = m_monotonicClock.elapsed();
    if (m_lastSampleMs < 0 || m_lastTimePosition < 0.0) {
        m_lastSampleMs = nowMs;
        m_lastProgressMs = nowMs;
        m_lastTimePosition = timePosition;
        m_monitorReadyAtMs = nowMs + LiveEdgeWarmupMs;
        return;
    }

    const double elapsedSeconds =
        qMax<qint64>(1, nowMs - m_lastSampleMs) / 1000.0;
    const double mediaDelta = timePosition - m_lastTimePosition;
    const bool discontinuity =
        mediaDelta < -1.0 || mediaDelta > elapsedSeconds + 5.0;
    const bool advanced =
        mediaDelta > 0.04 || discontinuity;

    if (advanced) {
        m_lastProgressMs = nowMs;
        if (m_waitingAfterFlush &&
            nowMs > m_flushStartedMs) {
            m_progressSeenAfterFlush = true;
        }
    }

    if (discontinuity) {
        m_accumulatedDelaySeconds = 0.0;
        m_delayBadSamples = 0;
    } else {
        const double shortfall = elapsedSeconds - qMax(0.0, mediaDelta);
        m_accumulatedDelaySeconds = qBound(
            0.0,
            m_accumulatedDelaySeconds + shortfall,
            10.0);
    }

    m_lastSampleMs = nowMs;
    m_lastTimePosition = timePosition;

    if (m_waitingAfterFlush) {
        if (m_progressSeenAfterFlush &&
            nowMs - m_flushStartedMs >= 2000) {
            m_waitingAfterFlush = false;
            m_flushWasAutomatic = false;
            m_accumulatedDelaySeconds = 0.0;
            m_delayBadSamples = 0;
            markLive();
            return;
        }
        if (m_flushWasAutomatic && !m_progressSeenAfterFlush &&
            nowMs - m_flushStartedMs >= LiveEdgeFlushRecoveryMs) {
            appendPlayerLog(
                QStringLiteral(
                    "[cedarview] no frame progress %1 ms after live-edge "
                    "flush; reopening this feed\n")
                    .arg(LiveEdgeFlushRecoveryMs)
                    .toUtf8());
            m_waitingAfterFlush = false;
            m_flushWasAutomatic = false;
            m_autoCooldownUntilMs =
                nowMs + LiveEdgeCooldownMs;
            setStatus(tr("Feed stalled; reopening…"), true);
            QTimer::singleShot(0, this, [this] {
                if (hasCamera() && !m_paused && !m_stopping) {
                    startPlayback();
                }
            });
            return;
        }
        return;
    }

    if (nowMs < m_monitorReadyAtMs) {
        m_accumulatedDelaySeconds = 0.0;
        m_delayBadSamples = 0;
        return;
    }

    const bool beyondDelayThreshold =
        m_accumulatedDelaySeconds * 1000.0 >=
        m_liveEdgeDelayThresholdMs;
    if (beyondDelayThreshold) {
        ++m_delayBadSamples;
    } else {
        m_delayBadSamples = 0;
    }

    const bool stalled =
        m_lastProgressMs >= 0 &&
        nowMs - m_lastProgressMs >= LiveEdgeStallMs;
    if (nowMs >= m_autoCooldownUntilMs &&
        (stalled ||
         m_delayBadSamples >= LiveEdgeBadSamplesRequired)) {
        appendPlayerLog(
            QStringLiteral(
                "[cedarview] live-edge correction: delay=%1 ms, "
                "stalled=%2, core-idle=%3, paused-for-cache=%4, "
                "vo-drops=%5, decoder-drops=%6\n")
                .arg(qRound(m_accumulatedDelaySeconds * 1000.0))
                .arg(stalled ? 1 : 0)
                .arg(m_coreIdle ? 1 : 0)
                .arg(m_pausedForCache ? 1 : 0)
                .arg(m_frameDropCount)
                .arg(m_decoderFrameDropCount)
                .toUtf8());
        flushToLiveEdge(true);
    }
}

void VideoTile::flushToLiveEdge(bool automatic)
{
    if (!hasCamera() || m_playbackBackend != QStringLiteral("mpv") ||
        !m_ipcSocket ||
        m_ipcSocket->state() != QLocalSocket::ConnectedState) {
        return;
    }

    const qint64 nowMs = m_monotonicClock.elapsed();
    sendMpvCommand(
        QJsonArray{QStringLiteral("drop-buffers")}, 200);
    appendPlayerLog(
        QStringLiteral("[cedarview] %1 GO LIVE: drop-buffers sent\n")
            .arg(automatic
                     ? QStringLiteral("automatic")
                     : QStringLiteral("manual"))
            .toUtf8());

    m_accumulatedDelaySeconds = 0.0;
    m_delayBadSamples = 0;
    m_lastSampleMs = -1;
    m_lastTimePosition = -1.0;
    m_lastProgressMs = nowMs;
    if (automatic) {
        m_waitingAfterFlush = true;
        m_flushWasAutomatic = true;
        m_progressSeenAfterFlush = false;
        m_flushStartedMs = nowMs;
        m_autoCooldownUntilMs = nowMs + LiveEdgeCooldownMs;
    }

    setStatus(automatic ? tr("Catching up to live…")
                        : tr("Going live…"));
    QProcess *const player = m_player;
    QTimer::singleShot(1600, this, [this, player] {
        if (m_player == player && player &&
            player->state() == QProcess::Running &&
            !m_waitingAfterFlush) {
            markLive();
        }
    });
}

void VideoTile::resetLiveEdgeTracking(bool resetCooldown)
{
    m_coreIdle = false;
    m_pausedForCache = false;
    m_waitingAfterFlush = false;
    m_flushWasAutomatic = false;
    m_progressSeenAfterFlush = false;
    m_delayBadSamples = 0;
    m_lastTimePosition = -1.0;
    m_accumulatedDelaySeconds = 0.0;
    m_lastSampleMs = -1;
    m_lastProgressMs = -1;
    m_frameDropCount = 0;
    m_decoderFrameDropCount = 0;
    m_monitorReadyAtMs =
        m_monotonicClock.isValid()
            ? m_monotonicClock.elapsed() + LiveEdgeWarmupMs
            : LiveEdgeWarmupMs;
    m_flushStartedMs = -1;
    if (resetCooldown) {
        m_autoCooldownUntilMs = 0;
    }
}

void VideoTile::releaseMpvIpc()
{
    if (m_liveEdgeTimer) {
        m_liveEdgeTimer->stop();
    }
    if (m_ipcSocket) {
        QLocalSocket *socket = m_ipcSocket;
        m_ipcSocket = nullptr;
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    m_ipcBuffer.clear();
    if (!m_ipcSocketPath.isEmpty()) {
        QFile::remove(m_ipcSocketPath);
        m_ipcSocketPath.clear();
    }
    resetLiveEdgeTracking(false);
}

void VideoTile::sendMpvCommand(const QJsonArray &command, int requestId)
{
    if (!m_ipcSocket ||
        m_ipcSocket->state() != QLocalSocket::ConnectedState) {
        return;
    }
    QJsonObject request{
        {QStringLiteral("command"), command},
    };
    if (requestId > 0) {
        request.insert(QStringLiteral("request_id"), requestId);
    }
    QByteArray payload =
        QJsonDocument(request).toJson(QJsonDocument::Compact);
    payload.append('\n');
    m_ipcSocket->write(payload);
}

void VideoTile::preparePlayerLog(const QStringList &arguments)
{
    QString stateRoot = qEnvironmentVariable("XDG_STATE_HOME");
    if (stateRoot.isEmpty()) {
        stateRoot = QDir::home().filePath(QStringLiteral(".local/state"));
    }

    QDir directory(stateRoot);
    if (!directory.mkpath(QStringLiteral("cedarview/mpv"))) {
        m_playerLogPath.clear();
        return;
    }
    directory.cd(QStringLiteral("cedarview"));
    directory.cd(QStringLiteral("mpv"));

    QString host = m_camera.host.trimmed();
    if (host.isEmpty()) {
        host = QStringLiteral("tile-%1").arg(m_index + 1);
    }
    host.replace(QLatin1Char('/'), QLatin1Char('_'));
    host.replace(QLatin1Char('\\'), QLatin1Char('_'));
    host.replace(QLatin1Char(':'), QLatin1Char('_'));
    m_playerLogPath =
        directory.filePath(QStringLiteral("camera-%1.log").arg(host));

    QFile current(m_playerLogPath);
    if (current.exists() && current.size() > MaximumPlayerLogBytes) {
        const QString previous = m_playerLogPath + QStringLiteral(".previous");
        QFile::remove(previous);
        current.rename(previous);
    }

    QStringList safeArguments = arguments;
    if (!safeArguments.isEmpty()) {
        safeArguments.removeLast();
    }
    const QString header =
        QStringLiteral(
            "\n\n=== %1 | %2 | %3 | %4 | attempt %5 ===\n"
            "mpv %6 [RTSP URL]\n")
            .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
                 m_camera.name,
                 m_camera.host,
                 m_camera.subtype == 0
                     ? QStringLiteral("main")
                     : QStringLiteral("sub"))
            .arg(m_retryAttempt + 1)
            .arg(safeArguments.join(QLatin1Char(' ')));
    appendPlayerLog(header.toUtf8());
}

void VideoTile::appendPlayerLog(const QByteArray &data) const
{
    if (m_playerLogPath.isEmpty() || data.isEmpty()) {
        return;
    }
    QFile log(m_playerLogPath);
    if (!log.open(QIODevice::WriteOnly | QIODevice::Append)) {
        return;
    }
    log.write(data);
}

void VideoTile::pause()
{
    if (!hasCamera() || m_paused) {
        return;
    }
    m_paused = true;
    m_stopping = true;
    m_reconnectTimer->stop();
    resetLiveEdgeTracking(false);
    releaseGStreamer();
    releasePlayer();
    m_stopping = false;
    m_live = false;
    setStatus(tr("Paused"));
    updateOverlay();
}

void VideoTile::resume()
{
    if (!hasCamera() || !m_paused) {
        return;
    }
    m_paused = false;
    m_retryAttempt = 0;
    updateOverlay();
    startPlayback();
}

void VideoTile::reconnectNow()
{
    if (!hasCamera()) {
        return;
    }
    m_paused = false;
    m_retryAttempt = 0;
    m_autoCooldownUntilMs = 0;
    m_reconnectTimer->stop();
    updateOverlay();
    startPlayback();
    showControls();
}

void VideoTile::setStreamSubtype(int subtype)
{
    if (!hasCamera()) {
        return;
    }
    subtype = qBound(0, subtype, 1);
    if (m_camera.subtype == subtype) {
        return;
    }
    m_camera.subtype = subtype;
    m_retryAttempt = 0;
    m_autoCooldownUntilMs = 0;
    updateOverlay();
    emit streamSubtypeChanged(m_camera.id, subtype);
    if (!m_paused) {
        startPlayback();
    }
}

void VideoTile::stop()
{
    m_stopping = true;
    m_reconnectTimer->stop();
    m_controlsTimer->stop();
    releaseGStreamer();
    releasePlayer();
    m_camera = Camera{};
    m_paused = false;
    m_live = false;
    m_retryAttempt = 0;
    resetLiveEdgeTracking(true);
    setStatus(tr("Drop a camera here"));
    updateOverlay();
    hideControls();
    m_videoSurface->update();
    m_stopping = false;
}

void VideoTile::scheduleReconnect(const QString &detail)
{
    if (!hasCamera() || m_paused || m_stopping) {
        return;
    }
    // One backend failure can surface through more than one signal (for
    // example QProcess error + finished, or GStreamer error + EOS). Keep one
    // retry deadline so repeated notifications cannot postpone it forever.
    if (m_reconnectTimer->isActive()) {
        return;
    }
    m_live = false;
    ++m_retryAttempt;
    const int delayMs = qMin(
        MaximumReconnectDelayMs,
        1000 * (1 << qMin(m_retryAttempt, 4)));
    const int delaySeconds = qMax(1, (delayMs + 999) / 1000);
    setStatus(tr("Reconnecting in %1 s…").arg(delaySeconds), true);
    m_connectionLabel->setText(
        tr("Retry %1 • %2 s").arg(m_retryAttempt).arg(delaySeconds));
    updateOverlay();
    m_reconnectTimer->start(delayMs);
    emit playbackError(m_index, detail);
}

void VideoTile::markLive()
{
    m_live = true;
    setStatus(tr("Live"));
    updateOverlay();
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
    if (hasCamera() && !m_paused) {
        m_retryAttempt = 0;
        startPlayback();
    }
}

void VideoTile::setLiveEdgeSettings(bool enabled, int delayThresholdMs,
                                    bool ultraLiveMode)
{
    const bool timingModeChanged =
        m_ultraLiveMode != ultraLiveMode;
    m_liveEdgeCorrectionEnabled = enabled;
    m_liveEdgeDelayThresholdMs =
        qBound(750, delayThresholdMs, 5000);
    m_ultraLiveMode = ultraLiveMode;
    resetLiveEdgeTracking(false);
    if (timingModeChanged && hasCamera() && !m_paused &&
        m_playbackBackend == QStringLiteral("mpv")) {
        startPlayback();
        return;
    }
    if (m_ipcSocket &&
        m_ipcSocket->state() == QLocalSocket::ConnectedState &&
        m_liveEdgeCorrectionEnabled) {
        m_liveEdgeTimer->start();
        pollLiveEdge();
    } else if (m_liveEdgeTimer) {
        m_liveEdgeTimer->stop();
    }
    updateOverlay();
}

void VideoTile::setSelected(bool selected)
{
    setProperty("selected", selected);
    style()->unpolish(this);
    style()->polish(this);
    setToolTip(selected
        ? tr("Selected tile. Drag it to reorder, or hover for controls.")
        : tr("Click to select. Drag it to reorder, or hover for controls."));
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
    QAction *pauseAction = menu.addAction(
        m_paused ? tr("Resume video") : tr("Pause video"));
    pauseAction->setEnabled(hasCamera());
    QAction *mainAction = menu.addAction(tr("Main stream"));
    QAction *subAction = menu.addAction(tr("Sub stream"));
    mainAction->setCheckable(true);
    subAction->setCheckable(true);
    mainAction->setChecked(hasCamera() && m_camera.subtype == 0);
    subAction->setChecked(hasCamera() && m_camera.subtype == 1);
    mainAction->setEnabled(hasCamera());
    subAction->setEnabled(hasCamera());
    QAction *liveAction = menu.addAction(tr("GO LIVE"));
    liveAction->setEnabled(
        hasCamera() &&
        m_playbackBackend == QStringLiteral("mpv") &&
        m_ipcSocket &&
        m_ipcSocket->state() == QLocalSocket::ConnectedState);
    QAction *retryAction = menu.addAction(tr("Reconnect now"));
    retryAction->setEnabled(hasCamera());
    menu.addSeparator();
    QAction *clearAction = menu.addAction(tr("Close video"));
    clearAction->setEnabled(hasCamera());

    QAction *selectedAction = menu.exec(event->globalPos());
    if (exitFullscreenAction &&
        selectedAction == exitFullscreenAction) {
        emit exitFullscreenRequested();
    } else if (selectedAction == pauseAction) {
        m_paused ? resume() : pause();
    } else if (selectedAction == mainAction) {
        setStreamSubtype(0);
    } else if (selectedAction == subAction) {
        setStreamSubtype(1);
    } else if (selectedAction == liveAction) {
        flushToLiveEdge(false);
    } else if (selectedAction == retryAction) {
        reconnectNow();
    } else if (selectedAction == clearAction) {
        stop();
        emit cleared(m_index);
    }
}

void VideoTile::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(
            QString::fromLatin1(CameraMimeType))) {
        event->acceptProposedAction();
    }
}

void VideoTile::dropEvent(QDropEvent *event)
{
    const QByteArray id = event->mimeData()->data(
        QString::fromLatin1(CameraMimeType));
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
    if (watched == m_videoSurface) {
        if (event->type() == QEvent::Enter ||
            event->type() == QEvent::MouseMove) {
            showControls();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void VideoTile::mousePressEvent(QMouseEvent *event)
{
    emit selected(m_index);
    if (event->button() == Qt::LeftButton && hasCamera()) {
        m_dragStartPosition = event->position().toPoint();
        m_dragStarted = false;
    }
    showControls();
    QWidget::mousePressEvent(event);
}

void VideoTile::mouseMoveEvent(QMouseEvent *event)
{
    showControls();
    if ((event->buttons() & Qt::LeftButton) && hasCamera() &&
        !m_dragStarted &&
        (event->position().toPoint() - m_dragStartPosition)
                .manhattanLength() >= QApplication::startDragDistance()) {
        m_dragStarted = true;
        auto *mime = new QMimeData;
        mime->setData(QString::fromLatin1(CameraMimeType),
                      m_camera.id.toUtf8());
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::MoveAction);
    }
    QWidget::mouseMoveEvent(event);
}

void VideoTile::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragStarted = false;
    QWidget::mouseReleaseEvent(event);
}

void VideoTile::enterEvent(QEnterEvent *event)
{
    showControls();
    QWidget::enterEvent(event);
}

void VideoTile::leaveEvent(QEvent *event)
{
    if (m_controlsTimer) {
        m_controlsTimer->start(300);
    }
    QWidget::leaveEvent(event);
}

void VideoTile::showControls()
{
    if (!hasCamera()) {
        return;
    }
    setControlsVisible(true);
    positionOverlayWidgets();
    m_controlsTimer->start();
}

void VideoTile::hideControls()
{
    setControlsVisible(false);
}

void VideoTile::updateOverlay()
{
    const bool occupied = hasCamera();
    m_cameraNameLabel->setText(occupied ? m_camera.name : QString());
    m_pauseButton->setText(m_paused
        ? QStringLiteral("▶")
        : QStringLiteral("Ⅱ"));
    m_pauseButton->setToolTip(m_paused
        ? tr("Resume this feed")
        : tr("Pause this feed"));
    m_streamButton->setText(
        occupied && m_camera.subtype == 0 ? tr("MAIN") : tr("SUB"));
    m_streamButton->setEnabled(occupied);
    m_pauseButton->setEnabled(occupied);
    m_liveButton->setEnabled(
        occupied &&
        m_playbackBackend == QStringLiteral("mpv") &&
        m_ipcSocket &&
        m_ipcSocket->state() == QLocalSocket::ConnectedState);
    m_retryButton->setEnabled(occupied);
    m_closeButton->setEnabled(occupied);
    if (m_paused) {
        m_connectionLabel->setText(tr("Paused"));
    } else if (m_live) {
        m_connectionLabel->setText(
            tr("Live • %1").arg(
                m_camera.subtype == 0 ? tr("Main") : tr("Sub")));
    } else if (occupied && !m_reconnectTimer->isActive()) {
        m_connectionLabel->setText(
            tr("Connecting • %1").arg(
                m_camera.subtype == 0 ? tr("Main") : tr("Sub")));
    }
    if (!occupied) {
        setControlsVisible(false);
    }
    positionOverlayWidgets();
}

void VideoTile::setStatus(const QString &text, bool error)
{
    m_statusLabel->setText(text);
    const bool shouldShow =
        text != tr("Live") && !text.isEmpty();
    m_statusLabel->setVisible(shouldShow);
    m_statusLabel->setStyleSheet(error
        ? QStringLiteral(
              "background: #24191c; color: #ff8a8a; "
              "border-radius: 5px; padding: 8px 12px;")
        : QStringLiteral(
              "background: #20242b; color: #d1d5db; "
              "border-radius: 5px; padding: 8px 12px;"));
    positionOverlayWidgets();
}

void VideoTile::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    positionOverlayWidgets();
}

void VideoTile::setControlsVisible(bool visible)
{
    for (QWidget *widget : {
             static_cast<QWidget *>(m_cameraNameLabel),
             static_cast<QWidget *>(m_connectionLabel),
             static_cast<QWidget *>(m_pauseButton),
             static_cast<QWidget *>(m_streamButton),
             static_cast<QWidget *>(m_liveButton),
             static_cast<QWidget *>(m_retryButton),
             static_cast<QWidget *>(m_closeButton)}) {
        if (widget) {
            widget->setVisible(visible);
            if (visible) {
                widget->raise();
            }
        }
    }
    if (m_statusLabel && m_statusLabel->isVisible()) {
        m_statusLabel->raise();
    }
}

void VideoTile::positionOverlayWidgets()
{
    constexpr int margin = 8;
    constexpr int gap = 4;

    for (QWidget *widget : {
             static_cast<QWidget *>(m_cameraNameLabel),
             static_cast<QWidget *>(m_connectionLabel),
             static_cast<QWidget *>(m_pauseButton),
             static_cast<QWidget *>(m_streamButton),
             static_cast<QWidget *>(m_liveButton),
             static_cast<QWidget *>(m_retryButton),
             static_cast<QWidget *>(m_closeButton),
             static_cast<QWidget *>(m_statusLabel)}) {
        if (widget) {
            widget->adjustSize();
        }
    }

    if (m_cameraNameLabel) {
        m_cameraNameLabel->move(margin, margin);
    }

    int topRight = width() - margin;
    if (m_closeButton) {
        topRight -= m_closeButton->width();
        m_closeButton->move(topRight, margin);
        topRight -= gap;
    }
    if (m_connectionLabel) {
        topRight -= m_connectionLabel->width();
        m_connectionLabel->move(qMax(margin, topRight), margin);
    }

    int bottomRight = width() - margin;
    const int bottom = height() - margin;
    for (QWidget *widget : {
             static_cast<QWidget *>(m_retryButton),
             static_cast<QWidget *>(m_streamButton),
             static_cast<QWidget *>(m_liveButton),
             static_cast<QWidget *>(m_pauseButton)}) {
        if (!widget) {
            continue;
        }
        bottomRight -= widget->width();
        widget->move(bottomRight, bottom - widget->height());
        bottomRight -= gap;
    }

    if (m_statusLabel) {
        const int statusX = qMax(
            margin, (width() - m_statusLabel->width()) / 2);
        const int statusY = qMax(
            margin, (height() - m_statusLabel->height()) / 2);
        m_statusLabel->move(statusX, statusY);
        if (m_statusLabel->isVisible()) {
            m_statusLabel->raise();
        }
    }
}

void VideoTile::releasePlayer(bool immediate)
{
    releaseMpvIpc();
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

    connect(player,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            player, &QObject::deleteLater);
    if (immediate) {
        player->kill();
        return;
    }

    player->terminate();
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
