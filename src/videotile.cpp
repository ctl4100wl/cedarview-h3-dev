#include "videotile.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

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
}

VideoTile::~VideoTile()
{
    releasePlayer(true);
}

void VideoTile::play(const Camera &camera)
{
    releasePlayer();
    m_camera = camera;
    m_titleLabel->setText(camera.name);
    m_stopButton->setEnabled(true);
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

void VideoTile::setStatus(const QString &text, bool error)
{
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error
        ? QStringLiteral("color: #ff6b6b;")
        : QStringLiteral("color: #88909b;"));
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
