#pragma once

#include "configstore.h"

#include <QMainWindow>

class GridView;
class QListWidget;
class QSpinBox;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void addCamera();
    void editCamera();
    void removeCamera();
    void assignCurrentCamera();
    void applyGrid();
    void showPlaybackError(const QString &camera, const QString &message);

private:
    int currentCameraIndex() const;
    void refreshCameraList();
    void saveState();
    void applyStyle();

    AppState m_state;
    GridView *m_grid = nullptr;
    QListWidget *m_cameraList = nullptr;
    QSpinBox *m_rowsSpin = nullptr;
    QSpinBox *m_columnsSpin = nullptr;
};

