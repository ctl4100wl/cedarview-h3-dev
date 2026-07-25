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
}

VideoTile::~VideoTile()
{
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
    releasePlayer();
    m_camera = camera;
    m_playerOutput.clear();
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
    releasePlayer();
    m_camera = Camera{};
    setStatus(tr("Drop a camera here"));
    m_videoSurface->update();
}

void VideoTile::setSelected(bool selected)
{
    setProperty("selected", selected);
    setToolTip(selected
        ? tr("Selected tile. Drag a camera here or right-click to clear.")
        : tr("Drag a camera here or right-click to clear."));
}

void VideoTile::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *clearAction = menu.addAction(tr("Clear tile"));
    clearAction->setEnabled(hasCamera());
    if (menu.exec(event->globalPos()) == clearAction) {
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
