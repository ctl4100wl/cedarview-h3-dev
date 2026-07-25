#include "mainwindow.h"

#include "cameradialog.h"
#include "gridview.h"

#include <QApplication>
#include <QBrush>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

namespace {

constexpr auto CameraMimeType = "application/x-cedarview-camera-id";

class CameraListWidget final : public QListWidget
{
public:
    using QListWidget::QListWidget;

protected:
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override
    {
        auto *mime = new QMimeData;
        if (!items.isEmpty()) {
            mime->setData(
                QString::fromLatin1(CameraMimeType),
                items.first()->data(Qt::UserRole).toString().toUtf8());
        }
        return mime;
    }

    QStringList mimeTypes() const override
    {
        return {QString::fromLatin1(CameraMimeType)};
    }
};

QPixmap thumbnailPixmap(const QString &path)
{
    constexpr int Width = 112;
    constexpr int Height = 63;
    QPixmap source(path);
    if (source.isNull()) {
        QPixmap placeholder(Width, Height);
        placeholder.fill(QColor(QStringLiteral("#d9dde3")));
        QPainter painter(&placeholder);
        painter.setPen(QColor(QStringLiteral("#6b7280")));
        painter.drawText(placeholder.rect(), Qt::AlignCenter,
                         QObject::tr("CAM"));
        return placeholder;
    }

    source = source.scaled(Width, Height, Qt::KeepAspectRatioByExpanding,
                           Qt::SmoothTransformation);
    return source.copy((source.width() - Width) / 2,
                       (source.height() - Height) / 2,
                       Width, Height);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_state(ConfigStore::load())
{
    setWindowTitle(QStringLiteral("CedarView"));
    resize(1100, 680);
    if (QScreen *screen = QApplication::primaryScreen()) {
        resize(screen->availableGeometry().size() * 0.86);
    }

    m_sidebar = new QWidget(this);
    m_sidebar->setObjectName(QStringLiteral("sidebar"));
    m_sidebar->setMinimumWidth(245);
    m_sidebar->setMaximumWidth(330);

    auto *heading = new QLabel(tr("CAMERAS"), m_sidebar);
    heading->setObjectName(QStringLiteral("sidebarHeading"));
    heading->setStyleSheet(
        QStringLiteral("font-size: 11px; font-weight: 700; color: #4b5563;"));

    m_cameraList = new CameraListWidget(m_sidebar);
    m_cameraList->setObjectName(QStringLiteral("cameraList"));
    m_cameraList->setIconSize(QSize(112, 63));
    m_cameraList->setDragEnabled(true);
    m_cameraList->setDragDropMode(QAbstractItemView::DragOnly);
    m_cameraList->setDefaultDropAction(Qt::CopyAction);
    m_cameraList->setSpacing(3);
    connect(m_cameraList, &QListWidget::itemDoubleClicked,
            this, [this] { assignCurrentCamera(); });

    auto *addButton = new QPushButton(tr("Add"), m_sidebar);
    auto *editButton = new QPushButton(tr("Edit"), m_sidebar);
    auto *removeButton = new QPushButton(tr("Remove"), m_sidebar);
    connect(addButton, &QPushButton::clicked, this, &MainWindow::addCamera);
    connect(editButton, &QPushButton::clicked, this, &MainWindow::editCamera);
    connect(removeButton, &QPushButton::clicked, this, &MainWindow::removeCamera);

    auto *cameraButtons = new QHBoxLayout;
    cameraButtons->addWidget(addButton);
    cameraButtons->addWidget(editButton);
    cameraButtons->addWidget(removeButton);

    auto *assignButton =
        new QPushButton(tr("Show in selected tile"), m_sidebar);
    connect(assignButton, &QPushButton::clicked,
            this, &MainWindow::assignCurrentCamera);

    auto *gridLabel = new QLabel(tr("GRID PRESET"), m_sidebar);
    gridLabel->setObjectName(QStringLiteral("sidebarHeading"));
    m_gridPreset = new QComboBox(m_sidebar);
    const struct {
        const char *label;
        int rows;
        int columns;
    } presets[] = {
        {"1 camera (1 × 1)", 1, 1},
        {"2 cameras (2 × 1)", 1, 2},
        {"4 cameras (2 × 2)", 2, 2},
        {"6 cameras (3 × 2)", 2, 3},
        {"8 cameras (4 × 2)", 2, 4},
        {"9 cameras (3 × 3)", 3, 3},
        {"12 cameras (4 × 3)", 3, 4},
        {"16 cameras (4 × 4)", 4, 4},
        {"25 cameras (5 × 5)", 5, 5},
    };
    int selectedPreset = -1;
    for (const auto &preset : presets) {
        m_gridPreset->addItem(
            tr(preset.label),
            QStringLiteral("%1x%2").arg(preset.rows).arg(preset.columns));
        if (preset.rows == m_state.gridRows &&
            preset.columns == m_state.gridColumns) {
            selectedPreset = m_gridPreset->count() - 1;
        }
    }
    if (selectedPreset < 0) {
        m_gridPreset->addItem(
            tr("Current layout (%1 × %2)")
                .arg(m_state.gridColumns)
                .arg(m_state.gridRows),
            QStringLiteral("%1x%2")
                .arg(m_state.gridRows)
                .arg(m_state.gridColumns));
        selectedPreset = m_gridPreset->count() - 1;
    }
    m_gridPreset->setCurrentIndex(selectedPreset);
    connect(m_gridPreset, qOverload<int>(&QComboBox::activated),
            this, &MainWindow::applyGrid);

    m_fullscreenButton = new QPushButton(tr("Fullscreen"), m_sidebar);
    connect(m_fullscreenButton, &QPushButton::clicked,
            this, &MainWindow::toggleFullscreen);

    auto *tip = new QLabel(
        tr("Drag a camera snapshot onto any tile. Press F11 for fullscreen "
           "and Esc to exit."),
        m_sidebar);
    tip->setWordWrap(true);
    tip->setStyleSheet(QStringLiteral(
        "background: transparent; color: #6b7280; font-size: 11px;"));

    auto *sidebarLayout = new QVBoxLayout(m_sidebar);
    sidebarLayout->addWidget(heading);
    sidebarLayout->addWidget(m_cameraList, 1);
    sidebarLayout->addLayout(cameraButtons);
    sidebarLayout->addWidget(assignButton);
    sidebarLayout->addSpacing(12);
    sidebarLayout->addWidget(gridLabel);
    sidebarLayout->addWidget(m_gridPreset);
    sidebarLayout->addWidget(m_fullscreenButton);
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
    splitter->setHandleWidth(1);
    splitter->addWidget(m_sidebar);
    splitter->addWidget(m_grid);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    refreshCameraList();
    applyStyle();
    statusBar()->showMessage(tr("Ready"));

    for (const Camera &camera : m_state.cameras) {
        queueSnapshot(camera);
    }
    QTimer::singleShot(500, this, &MainWindow::startNextSnapshot);
}

MainWindow::~MainWindow()
{
    saveState();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_snapshotQueue.clear();
    if (m_snapshotProcess) {
        m_snapshotProcess->kill();
    }
    saveState();
    QMainWindow::closeEvent(event);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F11 ||
        (event->key() == Qt::Key_Escape && isFullScreen())) {
        toggleFullscreen();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
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
    queueSnapshot(camera);
    startNextSnapshot();
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
    queueSnapshot(camera);
    startNextSnapshot();
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
    const QStringList size =
        m_gridPreset->currentData().toString().split(QLatin1Char('x'));
    if (size.size() != 2) {
        return;
    }
    m_grid->setGridSize(size.at(0).toInt(), size.at(1).toInt());
    saveState();
}

void MainWindow::toggleFullscreen()
{
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
    updateFullscreenUi();
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
        auto *item = new QListWidgetItem(
            QIcon(thumbnailPixmap(snapshotPath(camera.id))),
            camera.name, m_cameraList);
        item->setData(Qt::UserRole, camera.id);
        item->setForeground(QBrush(Qt::black));
        item->setSizeHint(QSize(0, 70));
    }
}

void MainWindow::queueSnapshot(const Camera &camera)
{
    if (camera.id.isEmpty() || camera.resolvedRtspUrl().isEmpty()) {
        return;
    }
    for (const Camera &queued : std::as_const(m_snapshotQueue)) {
        if (queued.id == camera.id) {
            return;
        }
    }
    m_snapshotQueue.enqueue(camera);
}

void MainWindow::startNextSnapshot()
{
    if (m_snapshotProcess || m_snapshotQueue.isEmpty()) {
        return;
    }

    const Camera camera = m_snapshotQueue.dequeue();
    m_snapshotCameraId = camera.id;

    const QString directory =
        QFileInfo(snapshotPath(camera.id)).absolutePath();
    QDir().mkpath(directory);
    QFile::remove(snapshotPath(camera.id));

    m_snapshotProcess = new QProcess(this);
    m_snapshotProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_snapshotProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) { finishSnapshot(); });
    connect(m_snapshotProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    finishSnapshot();
                }
            });

    m_snapshotTimeout = new QTimer(this);
    m_snapshotTimeout->setSingleShot(true);
    connect(m_snapshotTimeout, &QTimer::timeout, this, [this] {
        if (m_snapshotProcess) {
            m_snapshotProcess->kill();
        }
    });
    m_snapshotTimeout->start(8000);

    const QStringList arguments{
        QStringLiteral("--no-config"),
        QStringLiteral("--no-terminal"),
        QStringLiteral("--no-audio"),
        QStringLiteral("--frames=1"),
        QStringLiteral("--hwdec=auto"),
        QStringLiteral("--vo=image"),
        QStringLiteral("--vo-image-format=jpg"),
        QStringLiteral("--vo-image-outdir=%1").arg(directory),
        QStringLiteral("--rtsp-transport=%1").arg(camera.transport),
        camera.resolvedRtspUrl(),
    };
    m_snapshotProcess->start(QStringLiteral("mpv"), arguments,
                             QIODevice::ReadOnly);
}

void MainWindow::finishSnapshot()
{
    if (!m_snapshotProcess) {
        return;
    }
    if (m_snapshotTimeout) {
        m_snapshotTimeout->stop();
        m_snapshotTimeout->deleteLater();
        m_snapshotTimeout = nullptr;
    }
    m_snapshotProcess->deleteLater();
    m_snapshotProcess = nullptr;

    const QString path = snapshotPath(m_snapshotCameraId);
    if (QFileInfo::exists(path)) {
        updateCameraThumbnail(m_snapshotCameraId, path);
    }
    m_snapshotCameraId.clear();
    QTimer::singleShot(80, this, &MainWindow::startNextSnapshot);
}

QString MainWindow::snapshotPath(const QString &cameraId) const
{
    return QDir(QStandardPaths::writableLocation(
                    QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("snapshots/%1/00000001.jpg").arg(cameraId));
}

void MainWindow::updateCameraThumbnail(const QString &cameraId,
                                       const QString &imagePath)
{
    for (int row = 0; row < m_cameraList->count(); ++row) {
        QListWidgetItem *item = m_cameraList->item(row);
        if (item->data(Qt::UserRole).toString() == cameraId) {
            item->setIcon(QIcon(thumbnailPixmap(imagePath)));
            break;
        }
    }
}

void MainWindow::updateFullscreenUi()
{
    const bool fullscreen = isFullScreen();
    m_sidebar->setVisible(!fullscreen);
    statusBar()->setVisible(!fullscreen);
    m_grid->setFullscreenMode(fullscreen);
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
        QWidget#sidebar {
            background: #f4f5f7;
        }
        QLabel#sidebarHeading {
            background: transparent;
            color: #4b5563;
        }
        QListWidget#cameraList {
            background: #ffffff;
            color: #000000;
            border: 1px solid #d1d5db;
            border-radius: 5px;
            padding: 3px;
            outline: 0;
        }
        QListWidget#cameraList::item {
            color: #000000;
            background: #ffffff;
            border-radius: 4px;
            padding: 3px;
        }
        QListWidget#cameraList::item:selected {
            color: #000000;
            background: #ffd9bd;
        }
        QLineEdit, QSpinBox, QComboBox {
            background: #1c2027;
            border: 1px solid #343a43;
            border-radius: 4px;
            padding: 5px;
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
