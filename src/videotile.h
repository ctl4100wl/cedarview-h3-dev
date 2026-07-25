#pragma once

#include "camera.h"

#include <QByteArray>
#include <QWidget>
#include <QStringList>

class QLabel;
class QProcess;

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
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;

signals:
    void selected(int index);
    void cleared(int index);
    void cameraDropped(const QString &cameraId, int index);
    void playbackError(int index, const QString &message);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
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
    QLabel *m_statusLabel = nullptr;
    quintptr m_windowHandle = 0;
};
