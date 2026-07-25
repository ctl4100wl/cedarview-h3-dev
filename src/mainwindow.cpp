#include "mainwindow.h"

#include "cameradialog.h"
#include "gridview.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_state(ConfigStore::load())
{
    setWindowTitle(QStringLiteral("CedarView"));
    resize(1100, 680);
    if (QScreen *screen = QApplication::primaryScreen()) {
        resize(screen->availableGeometry().size() * 0.86);
    }

    auto *sidebar = new QWidget(this);
    sidebar->setMinimumWidth(210);
    sidebar->setMaximumWidth(300);

    auto *heading = new QLabel(tr("CAMERAS"), sidebar);
    heading->setStyleSheet(
        QStringLiteral("font-size: 11px; font-weight: 700; color: #89919d;"));

    m_cameraList = new QListWidget(sidebar);
    m_cameraList->setAlternatingRowColors(true);
    connect(m_cameraList, &QListWidget::itemDoubleClicked,
            this, [this] { assignCurrentCamera(); });

    auto *addButton = new QPushButton(tr("Add"), sidebar);
    auto *editButton = new QPushButton(tr("Edit"), sidebar);
    auto *removeButton = new QPushButton(tr("Remove"), sidebar);
    connect(addButton, &QPushButton::clicked, this, &MainWindow::addCamera);
    connect(editButton, &QPushButton::clicked, this, &MainWindow::editCamera);
    connect(removeButton, &QPushButton::clicked, this, &MainWindow::removeCamera);

    auto *cameraButtons = new QHBoxLayout;
    cameraButtons->addWidget(addButton);
    cameraButtons->addWidget(editButton);
    cameraButtons->addWidget(removeButton);

    auto *assignButton = new QPushButton(tr("Show in selected tile"), sidebar);
    connect(assignButton, &QPushButton::clicked,
            this, &MainWindow::assignCurrentCamera);

    m_rowsSpin = new QSpinBox(sidebar);
    m_rowsSpin->setRange(1, 5);
    m_rowsSpin->setValue(m_state.gridRows);
    m_columnsSpin = new QSpinBox(sidebar);
    m_columnsSpin->setRange(1, 5);
    m_columnsSpin->setValue(m_state.gridColumns);
    auto *applyGridButton = new QPushButton(tr("Apply grid"), sidebar);
    connect(applyGridButton, &QPushButton::clicked,
            this, &MainWindow::applyGrid);

    auto *gridForm = new QFormLayout;
    gridForm->addRow(tr("Rows"), m_rowsSpin);
    gridForm->addRow(tr("Columns"), m_columnsSpin);

    auto *tip = new QLabel(
        tr("H3 tip: use each camera's H.264 sub-stream for 2×2 view."),
        sidebar);
    tip->setWordWrap(true);
    tip->setStyleSheet(QStringLiteral("color: #89919d; font-size: 11px;"));

    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->addWidget(heading);
    sidebarLayout->addWidget(m_cameraList, 1);
    sidebarLayout->addLayout(cameraButtons);
    sidebarLayout->addWidget(assignButton);
    sidebarLayout->addSpacing(12);
    sidebarLayout->addLayout(gridForm);
    sidebarLayout->addWidget(applyGridButton);
    sidebarLayout->addWidget(tip);

    m_grid = new GridView(this);
    m_grid->setGridSize(m_state.gridRows, m_state.gridColumns);
    m_grid->setCameras(m_state.cameras);
    m_grid->setAssignments(m_state.assignments);
    connect(m_grid, &GridView::assignmentsChanged,
            this, &MainWindow::saveState);
    connect(m_grid, &GridView::playbackError,
            this, &MainWindow::showPlaybackError);

    auto *splitter = new QSplitter(this);
    splitter->addWidget(sidebar);
    splitter->addWidget(m_grid);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    refreshCameraList();
    applyStyle();
    statusBar()->showMessage(tr("Ready"));
}

MainWindow::~MainWindow()
{
    saveState();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveState();
    QMainWindow::closeEvent(event);
}

int MainWindow::currentCameraIndex() const
{
    return m_cameraList->currentRow();
}

void MainWindow::addCamera()
{
    CameraDialog dialog(this);
    dialog.setDefaultCredentials(m_state.defaultUsername,
                                 m_state.defaultPassword);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const Camera camera = dialog.camera();
    if (dialog.rememberCredentials()) {
        m_state.defaultUsername = camera.username;
        m_state.defaultPassword = camera.password;
    }
    m_state.cameras.append(camera);
    refreshCameraList();
    m_cameraList->setCurrentRow(m_state.cameras.size() - 1);
    m_grid->setCameras(m_state.cameras);
    saveState();
}

void MainWindow::editCamera()
{
    const int index = currentCameraIndex();
    if (index < 0 || index >= m_state.cameras.size()) {
        return;
    }
    CameraDialog dialog(this);
    dialog.setCamera(m_state.cameras.at(index));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const Camera camera = dialog.camera();
    if (dialog.rememberCredentials()) {
        m_state.defaultUsername = camera.username;
        m_state.defaultPassword = camera.password;
    }
    m_state.cameras[index] = camera;
    refreshCameraList();
    m_cameraList->setCurrentRow(index);
    m_grid->setCameras(m_state.cameras);
    saveState();
}

void MainWindow::removeCamera()
{
    const int index = currentCameraIndex();
    if (index < 0 || index >= m_state.cameras.size()) {
        return;
    }
    const Camera camera = m_state.cameras.at(index);
    if (QMessageBox::question(
            this, tr("Remove camera"),
            tr("Remove “%1” from CedarView?").arg(camera.name))
        != QMessageBox::Yes) {
        return;
    }
    m_state.cameras.removeAt(index);
    for (QString &assignment : m_state.assignments) {
        if (assignment == camera.id) {
            assignment.clear();
        }
    }
    refreshCameraList();
    m_grid->setCameras(m_state.cameras);
    saveState();
}

void MainWindow::assignCurrentCamera()
{
    const int index = currentCameraIndex();
    if (index < 0 || index >= m_state.cameras.size()) {
        statusBar()->showMessage(tr("Select a camera first"), 2500);
        return;
    }
    m_grid->assignCamera(m_state.cameras.at(index));
}

void MainWindow::applyGrid()
{
    m_grid->setGridSize(m_rowsSpin->value(), m_columnsSpin->value());
    saveState();
}

void MainWindow::showPlaybackError(const QString &camera,
                                   const QString &message)
{
    statusBar()->showMessage(tr("%1: %2").arg(camera, message), 8000);
}

void MainWindow::refreshCameraList()
{
    m_cameraList->clear();
    for (const Camera &camera : m_state.cameras) {
        new QListWidgetItem(camera.name, m_cameraList);
    }
}

void MainWindow::saveState()
{
    if (m_grid) {
        m_state.gridRows = m_grid->rows();
        m_state.gridColumns = m_grid->columns();
        m_state.assignments = m_grid->assignments();
    }
    QString error;
    if (!ConfigStore::save(m_state, &error) && statusBar()) {
        statusBar()->showMessage(tr("Could not save settings: %1").arg(error),
                                 5000);
    }
}

void MainWindow::applyStyle()
{
    qApp->setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget {
            background: #111419;
            color: #e7e9ed;
        }
        QListWidget, QLineEdit, QSpinBox {
            background: #1c2027;
            border: 1px solid #343a43;
            border-radius: 4px;
            padding: 5px;
        }
        QListWidget::item {
            padding: 7px;
        }
        QListWidget::item:selected {
            background: #85451d;
        }
        QPushButton {
            background: #2a3039;
            border: 1px solid #414956;
            border-radius: 4px;
            padding: 6px 9px;
        }
        QPushButton:hover {
            background: #353d49;
        }
        QPushButton:pressed {
            background: #ff7a1a;
            color: #111419;
        }
        QToolTip {
            background: #292f38;
            color: white;
            border: 1px solid #4b5563;
        }
    )"));
}
