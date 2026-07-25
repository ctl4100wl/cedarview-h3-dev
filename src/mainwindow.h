#pragma once

#include "configstore.h"

#include <QMainWindow>
#include <QQueue>

class GridView;
class QComboBox;
class QKeyEvent;
class QListWidget;
class QProcess;
class QPushButton;
class QTimer;
class QWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void addCamera();
    void editCamera();
    void removeCamera();
    void assignCurrentCamera();
    void applyGrid();
    void toggleFullscreen();
    void toggleTheme();
    void showPlaybackError(const QString &camera, const QString &message);

private:
    int currentCameraIndex() const;
    void refreshCameraList();
    void queueSnapshot(const Camera &camera);
    void startNextSnapshot();
    void finishSnapshot();
    QString snapshotPath(const QString &cameraId) const;
    void updateCameraThumbnail(const QString &cameraId,
                               const QString &imagePath);
    void updateAssignmentIndicators();
    void updateFullscreenUi();
    void saveState();
    void applyStyle();

    AppState m_state;
    GridView *m_grid = nullptr;
    QWidget *m_sidebar = nullptr;
    QListWidget *m_cameraList = nullptr;
    QComboBox *m_gridPreset = nullptr;
    QPushButton *m_fullscreenButton = nullptr;
    QPushButton *m_themeButton = nullptr;
    QQueue<Camera> m_snapshotQueue;
    QProcess *m_snapshotProcess = nullptr;
    QTimer *m_snapshotTimeout = nullptr;
    QString m_snapshotCameraId;
};
