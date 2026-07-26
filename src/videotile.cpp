#include "videotile.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEnterEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QProcess>
#include <QStackedLayout>
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

    m_controls = new QWidget(this);
    m_controls->setObjectName(QStringLiteral("tileControls"));
    m_controls->setMouseTracking(true);
    m_controls->setAcceptDrops(true);
    m_controls->installEventFilter(this);
    m_controls->setStyleSheet(QStringLiteral(
        "QWidget#tileControls { background: transparent; }"));

    m_cameraNameLabel = new QLabel(m_controls);
    m_cameraNameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_cameraNameLabel->setStyleSheet(QStringLiteral(
        "color: white; background: rgba(8,10,13,175); border-radius: 4px; "
        "padding: 4px 7px; font-weight: 700;"));
    m_connectionLabel = new QLabel(m_controls);
    m_connectionLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_connectionLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_connectionLabel->setStyleSheet(QStringLiteral(
        "color: #e5e7eb; background: rgba(8,10,13,155); "
        "border-radius: 4px; padding: 4px 7px;"));

    m_pauseButton = makeOverlayButton(
        QStringLiteral("Ⅱ"), tr("Pause this feed"), m_controls);
    m_streamButton = makeOverlayButton(
        tr("SUB"), tr("Switch between Main and Sub stream"), m_controls);
    m_retryButton = makeOverlayButton(
        QStringLiteral("↻"), tr("Reconnect now"), m_controls);
    m_closeButton = makeOverlayButton(
        QStringLiteral("×"), tr("Close this feed"), m_controls);

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
    connect(m_retryButton, &QToolButton::clicked,
            this, &VideoTile::reconnectNow);
    connect(m_closeButton, &QToolButton::clicked, this, [this] {
        stop();
        emit cleared(m_index);
    });

    auto *topRow = new QHBoxLayout;
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->addWidget(m_cameraNameLabel);
    topRow->addStretch(1);
    topRow->addWidget(m_connectionLabel);
    topRow->addWidget(m_closeButton);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->setContentsMargins(0, 0, 0, 0);
    bottomRow->addStretch(1);
    bottomRow->addWidget(m_pauseButton);
    bottomRow->addWidget(m_streamButton);
    bottomRow->addWidget(m_retryButton);

    auto *controlsLayout = new QVBoxLayout(m_controls);
    controlsLayout->setContentsMargins(8, 8, 8, 8);
    controlsLayout->setSpacing(4);
    controlsLayout->addLayout(topRow);
    controlsLayout->addStretch(1);
    controlsLayout->addLayout(bottomRow);

    auto *layout = new QStackedLayout(this);
    layout->setStackingMode(QStackedLayout::StackAll);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_videoSurface);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_controls);

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
    auto *player = new QProcess(this);
    m_player = player;
    player->setProcessChannelMode(QProcess::MergedChannels);

    connect(player, &QProcess::started, this, [this, player] {
        if (m_player != player) {
            return;
        }
        m_connectionLabel->setText(
            tr("%1 • %2")
                .arg(m_camera.transport.toUpper(),
                     m_camera.subtype == 0 ? tr("Main") : tr("Sub")));
        QTimer::singleShot(1800, this, [this, player] {
            if (m_player == player &&
                player->state() == QProcess::Running) {
                markLive();
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
                scheduleReconnect(detail);
            });

    player->start(QStringLiteral("mpv"), playerArguments(),
                  QIODevice::ReadOnly);
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
        QStringLiteral("--vo=xv,x11"),
        QStringLiteral("--wid=%1").arg(m_windowHandle),
        QStringLiteral("--rtsp-transport=%1").arg(m_camera.transport),
        QStringLiteral("--msg-level=all=warn"),
    };

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

void VideoTile::pause()
{
    if (!hasCamera() || m_paused) {
        return;
    }
    m_paused = true;
    m_stopping = true;
    m_reconnectTimer->stop();
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
    m_retryAttempt = 0;
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
    if (watched == m_videoSurface || watched == m_controls) {
        if (event->type() == QEvent::DragEnter) {
            dragEnterEvent(static_cast<QDragEnterEvent *>(event));
            return event->isAccepted();
        }
        if (event->type() == QEvent::Drop) {
            dropEvent(static_cast<QDropEvent *>(event));
            return event->isAccepted();
        }
    }
    if (watched == m_controls) {
        if (event->type() == QEvent::MouseButtonPress) {
            mousePressEvent(static_cast<QMouseEvent *>(event));
            return true;
        }
        if (event->type() == QEvent::MouseMove) {
            mouseMoveEvent(static_cast<QMouseEvent *>(event));
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            mouseReleaseEvent(static_cast<QMouseEvent *>(event));
            return true;
        }
    }
    if (watched == m_videoSurface || watched == m_controls) {
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
    m_controls->show();
    m_controls->raise();
    m_controlsTimer->start();
}

void VideoTile::hideControls()
{
    if (m_controls) {
        m_controls->hide();
    }
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
        m_controls->hide();
    }
}

void VideoTile::setStatus(const QString &text, bool error)
{
    m_statusLabel->setText(text);
    const bool shouldShow =
        text != tr("Live") && !text.isEmpty();
    m_statusLabel->setVisible(shouldShow);
    m_statusLabel->setStyleSheet(error
        ? QStringLiteral(
              "background: rgba(8, 10, 13, 190); color: #ff8a8a; "
              "padding: 8px;")
        : QStringLiteral(
              "background: rgba(8, 10, 13, 175); color: #d1d5db; "
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
