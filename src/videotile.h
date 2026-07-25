#pragma once

#include "camera.h"

#include <QByteArray>
#include <QFrame>
#include <QStringList>

class QLabel;
class QProcess;
class QPushButton;
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
    void play(const Camera &camera);
    void stop();
    void setSelected(bool selected);

signals:
    void selected(int index);
    void cleared(int index);
    void playbackError(int index, const QString &message);

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    QStringList playerArguments() const;
    void collectPlayerOutput();
    void setStatus(const QString &text, bool error = false);
    void releasePlayer(bool immediate = false);

    int m_index = 0;
    Camera m_camera;
    QProcess *m_player = nullptr;
    QByteArray m_playerOutput;
    QWidget *m_videoSurface = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_stopButton = nullptr;
    quintptr m_windowHandle = 0;
};
