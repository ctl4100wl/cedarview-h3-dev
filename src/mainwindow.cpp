#include "mainwindow.h"

#include "cameradialog.h"
#include "gridview.h"
#include "onvifclockmonitor.h"

#include <QApplication>
#include <QBrush>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
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
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

#include <gst/gst.h>

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

bool gstFactoryAvailable(const char *name)
{
    GstElementFactory *factory = gst_element_factory_find(name);
    if (!factory) {
        return false;
    }
    gst_object_unref(factory);
    return true;
}

} // namespace

MainWindow::MainWindow(bool startFullscreen, QWidget *parent)
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
    m_gridPreset->addItem(tr("Auto — use camera count"),
                          QStringLiteral("auto"));
    m_gridPreset->insertSeparator(m_gridPreset->count());
    int selectedPreset = -1;
    QList<int> presets;
    for (int count = 1; count <= 16; ++count) {
        presets.append(count);
    }
    presets.append(20);
    presets.append(25);
    for (const int count : std::as_const(presets)) {
        int rows = 1;
        if (count == 3) {
            rows = 2;
        } else if (count >= 4) {
            rows = qMax(1, qFloor(qSqrt(static_cast<double>(count))));
        }
        const int columns =
            qCeil(static_cast<double>(count) / rows);
        rows = qCeil(static_cast<double>(count) / columns);

        QStringList rowSizes;
        for (int row = 0; row < rows; ++row) {
            rowSizes.append(QString::number(
                qMin(columns, count - row * columns)));
        }
        const bool rectangular =
            count == rows * columns && rows > 1;
        const QString shape = rectangular
            ? QStringLiteral("%1 × %2").arg(columns).arg(rows)
            : rowSizes.join(QStringLiteral(" + "));
        m_gridPreset->addItem(
            count == 1
                ? tr("1 camera (1 × 1)")
                : tr("%1 cameras (%2)").arg(count).arg(shape),
            QStringLiteral("count:%1").arg(count));
        if (m_state.gridMode == QStringLiteral("count:%1").arg(count)) {
            selectedPreset = m_gridPreset->count() - 1;
        }
    }
    if (m_state.gridMode == QStringLiteral("auto")) {
        selectedPreset = 0;
    }
    if (selectedPreset < 0) {
        m_gridPreset->addItem(
            tr("Current layout (%1 cameras)")
                .arg(m_state.gridRows * m_state.gridColumns),
            QStringLiteral("count:%1")
                .arg(m_state.gridRows * m_state.gridColumns));
        selectedPreset = m_gridPreset->count() - 1;
    }
    m_gridPreset->setCurrentIndex(selectedPreset);
    connect(m_gridPreset, qOverload<int>(&QComboBox::activated),
            this, &MainWindow::applyGrid);

    m_fullscreenButton = new QPushButton(tr("Fullscreen"), m_sidebar);
    connect(m_fullscreenButton, &QPushButton::clicked,
            this, &MainWindow::toggleFullscreen);

    m_themeButton = new QPushButton(m_sidebar);
    connect(m_themeButton, &QPushButton::clicked,
            this, &MainWindow::toggleTheme);

    auto *developerButton =
        new QPushButton(tr("Developer settings…"), m_sidebar);
    connect(developerButton, &QPushButton::clicked,
            this, &MainWindow::showDeveloperSettings);

    auto *tip = new QLabel(
        tr("Drag a camera snapshot onto any tile. Press F11 for fullscreen "
           "and Esc to exit."),
        m_sidebar);
    tip->setObjectName(QStringLiteral("sidebarTip"));
    tip->setWordWrap(true);

    auto *sidebarLayout = new QVBoxLayout(m_sidebar);
    sidebarLayout->addWidget(heading);
    sidebarLayout->addWidget(m_cameraList, 1);
    sidebarLayout->addLayout(cameraButtons);
    sidebarLayout->addWidget(assignButton);
    sidebarLayout->addSpacing(12);
    sidebarLayout->addWidget(gridLabel);
    sidebarLayout->addWidget(m_gridPreset);
    sidebarLayout->addWidget(m_fullscreenButton);
    sidebarLayout->addWidget(m_themeButton);
    sidebarLayout->addWidget(developerButton);
    sidebarLayout->addWidget(tip);

    m_grid = new GridView(this);
    m_grid->setPlaybackBackend(m_state.playbackBackend);
    m_grid->setPlaybackSync(
        m_state.playbackSyncEnabled,
        m_state.playbackSyncThresholdMs);
    m_grid->setCameras(m_state.cameras);
    for (auto it = m_cameraClockOffsets.cbegin();
         it != m_cameraClockOffsets.cend(); ++it) {
        m_grid->setCameraClockOffset(it.key(), it.value());
    }
    m_autoGrid = m_state.gridMode == QStringLiteral("auto");
    if (m_autoGrid) {
        m_grid->setTileCount(qMax(1, m_state.cameras.size()));
    } else {
        const QString countText =
            m_state.gridMode.section(QLatin1Char(':'), 1, 1);
        bool validCount = false;
        int count = countText.toInt(&validCount);
        if (!validCount) {
            count = m_state.gridRows * m_state.gridColumns;
        }
        m_grid->setTileCount(qBound(
            1, count, 25));
    }
    m_grid->setAssignments(m_state.assignments);
    connect(m_grid, &GridView::assignmentsChanged,
            this, [this] {
                saveState();
                updateAssignmentIndicators();
            });
    connect(m_grid, &GridView::playbackError,
            this, &MainWindow::showPlaybackError);
    connect(m_grid, &GridView::cameraStreamChanged,
            this, &MainWindow::saveCameraStreamPreference);
    connect(m_grid, &GridView::exitFullscreenRequested,
            this, [this] {
                if (isFullScreen()) {
                    toggleFullscreen();
                }
            });
    connect(m_grid, &GridView::feedResynced, this,
            [this](const QString &cameraName, double lagSeconds) {
                statusBar()->showMessage(
                    tr("%1 refreshed after drifting %2 s behind")
                        .arg(cameraName)
                        .arg(lagSeconds, 0, 'f', 1),
                    7000);
            });

    auto *splitter = new QSplitter(this);
    splitter->setHandleWidth(1);
    splitter->addWidget(m_sidebar);
    splitter->addWidget(m_grid);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    m_clockMonitor = new OnvifClockMonitor(this);
    m_clockMonitor->setCameras(m_state.cameras);
    m_clockMonitor->setIntervalSeconds(
        m_state.onvifClockCheckIntervalSeconds);
    m_clockMonitor->setEnabled(m_state.onvifClockCheckEnabled);
    connect(m_clockMonitor, &OnvifClockMonitor::clockMeasured,
            this, [this](const QString &cameraId, qint64 offsetMs) {
                m_cameraClockOffsets.insert(cameraId, offsetMs);
                m_grid->setCameraClockOffset(cameraId, offsetMs);
                if (qAbs(offsetMs) >=
                    m_state.playbackSyncThresholdMs) {
                    QString cameraName = tr("Camera");
                    for (const Camera &camera : m_state.cameras) {
                        if (camera.id == cameraId) {
                            cameraName = camera.name;
                            break;
                        }
                    }
                    statusBar()->showMessage(
                        tr("%1 camera clock differs from Linux by %2 s")
                            .arg(cameraName)
                            .arg(static_cast<double>(offsetMs) / 1000.0,
                                 0, 'f', 1),
                        7000);
                }
            });
    m_clockMonitor->start();

    refreshCameraList();
    applyStyle();
    statusBar()->showMessage(tr("Ready"));

    for (const Camera &camera : m_state.cameras) {
        queueSnapshot(camera);
    }
    QTimer::singleShot(500, this, &MainWindow::startNextSnapshot);
    if (startFullscreen) {
        QTimer::singleShot(0, this, [this] {
            showFullScreen();
            updateFullscreenUi();
        });
    }
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
    m_clockMonitor->setCameras(m_state.cameras);
    refreshCameraList();
    m_cameraList->setCurrentRow(m_state.cameras.size() - 1);
    m_grid->setCameras(m_state.cameras);
    for (auto it = m_cameraClockOffsets.cbegin();
         it != m_cameraClockOffsets.cend(); ++it) {
        m_grid->setCameraClockOffset(it.key(), it.value());
    }
    if (m_autoGrid) {
        m_grid->setTileCount(qMax(1, m_state.cameras.size()));
    }
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
    m_clockMonitor->setCameras(m_state.cameras);
    refreshCameraList();
    m_cameraList->setCurrentRow(index);
    m_grid->setCameras(m_state.cameras);
    for (auto it = m_cameraClockOffsets.cbegin();
         it != m_cameraClockOffsets.cend(); ++it) {
        m_grid->setCameraClockOffset(it.key(), it.value());
    }
    if (m_autoGrid) {
        m_grid->setTileCount(qMax(1, m_state.cameras.size()));
    }
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
    m_cameraClockOffsets.remove(camera.id);
    m_clockMonitor->setCameras(m_state.cameras);
    for (QString &assignment : m_state.assignments) {
        if (assignment == camera.id) {
            assignment.clear();
        }
    }
    refreshCameraList();
    m_grid->setCameras(m_state.cameras);
    for (auto it = m_cameraClockOffsets.cbegin();
         it != m_cameraClockOffsets.cend(); ++it) {
        m_grid->setCameraClockOffset(it.key(), it.value());
    }
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
    applySelectedLayout();
    saveState();
}

void MainWindow::applySelectedLayout()
{
    const QString mode = m_gridPreset->currentData().toString();
    m_autoGrid = mode == QStringLiteral("auto");
    m_state.gridMode = mode;
    const int count = m_autoGrid
        ? qMax(1, m_state.cameras.size())
        : qBound(1, mode.section(QLatin1Char(':'), 1, 1).toInt(), 25);
    m_grid->setTileCount(count);
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

void MainWindow::toggleTheme()
{
    m_state.theme = m_state.theme == QStringLiteral("dark")
        ? QStringLiteral("light")
        : QStringLiteral("dark");
    applyStyle();
    updateAssignmentIndicators();
    saveState();
}

void MainWindow::showDeveloperSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Developer settings"));
    dialog.setMinimumWidth(430);

    auto *backend = new QComboBox(&dialog);
    backend->addItem(tr("MPV / FFmpeg (stable default)"),
                     QStringLiteral("mpv"));
    backend->addItem(tr("GStreamer / Cedrus (experimental)"),
                     QStringLiteral("gstreamer"));
    const int current =
        backend->findData(m_state.playbackBackend);
    backend->setCurrentIndex(qMax(0, current));

    auto *syncEnabled = new QCheckBox(
        tr("Refresh only a feed that falls behind"), &dialog);
    syncEnabled->setChecked(m_state.playbackSyncEnabled);

    auto *syncThreshold = new QSpinBox(&dialog);
    syncThreshold->setRange(1000, 15000);
    syncThreshold->setSingleStep(500);
    syncThreshold->setSuffix(tr(" ms"));
    syncThreshold->setValue(m_state.playbackSyncThresholdMs);
    syncThreshold->setToolTip(
        tr("Two consecutive drift readings must exceed this value."));
    syncThreshold->setEnabled(syncEnabled->isChecked());
    connect(syncEnabled, &QCheckBox::toggled,
            syncThreshold, &QWidget::setEnabled);

    auto *clockCheck = new QCheckBox(
        tr("Read camera clocks through ONVIF port 80"), &dialog);
    clockCheck->setChecked(m_state.onvifClockCheckEnabled);

    auto *clockInterval = new QSpinBox(&dialog);
    clockInterval->setRange(15, 3600);
    clockInterval->setSuffix(tr(" s"));
    clockInterval->setValue(
        m_state.onvifClockCheckIntervalSeconds);
    clockInterval->setEnabled(clockCheck->isChecked());
    connect(clockCheck, &QCheckBox::toggled,
            clockInterval, &QWidget::setEnabled);

    const bool h264Available = gstFactoryAvailable("v4l2slh264dec");
    const bool h265Available = gstFactoryAvailable("v4l2slh265dec");
    auto *status = new QLabel(
        tr("Cedrus decoders: H.264 %1 • H.265 %2")
            .arg(h264Available ? tr("ready") : tr("missing"),
                 h265Available ? tr("ready") : tr("missing")),
        &dialog);
    status->setWordWrap(true);

    auto *note = new QLabel(
        tr("Changing backend restarts every active tile. MPV sync uses its "
           "local IPC timestamps and the Linux clock; only a lagging tile is "
           "refreshed. ONVIF clock checks are informational and use "
           "http://CAMERA:80/onvif/device_service. GStreamer mode "
           "prefers the stateless V4L2 H.264/H.265 decoders and embeds "
           "XVideo directly in each tile. MPV mode preserves CedarView's "
           "full crop and digital-zoom controls."),
        &dialog);
    note->setWordWrap(true);

    auto *form = new QFormLayout;
    form->addRow(tr("Playback backend"), backend);
    form->addRow(tr("Live feed sync"), syncEnabled);
    form->addRow(tr("Refresh threshold"), syncThreshold);
    form->addRow(tr("Camera clock check"), clockCheck);
    form->addRow(tr("Clock-check interval"), clockInterval);
    form->addRow(QString(), status);
    form->addRow(QString(), note);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted,
            &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            &dialog, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString selected = backend->currentData().toString();
    const bool backendChanged =
        selected != m_state.playbackBackend;
    m_state.playbackBackend = selected;
    m_state.playbackSyncEnabled = syncEnabled->isChecked();
    m_state.playbackSyncThresholdMs = syncThreshold->value();
    m_state.onvifClockCheckEnabled = clockCheck->isChecked();
    m_state.onvifClockCheckIntervalSeconds = clockInterval->value();
    if (backendChanged) {
        m_grid->setPlaybackBackend(selected);
    }
    m_grid->setPlaybackSync(
        m_state.playbackSyncEnabled,
        m_state.playbackSyncThresholdMs);
    m_clockMonitor->setIntervalSeconds(
        m_state.onvifClockCheckIntervalSeconds);
    m_clockMonitor->setEnabled(m_state.onvifClockCheckEnabled);
    if (m_state.onvifClockCheckEnabled) {
        m_clockMonitor->checkNow();
    }
    saveState();
    statusBar()->showMessage(
        tr("Developer playback and sync settings saved"),
        5000);
}

void MainWindow::showPlaybackError(const QString &camera,
                                   const QString &message)
{
    statusBar()->showMessage(tr("%1: %2").arg(camera, message), 8000);
}

void MainWindow::saveCameraStreamPreference(const QString &cameraId,
                                            int subtype)
{
    for (Camera &camera : m_state.cameras) {
        if (camera.id == cameraId) {
            camera.subtype = qBound(0, subtype, 1);
            saveState();
            updateAssignmentIndicators();
            return;
        }
    }
}

void MainWindow::refreshCameraList()
{
    m_cameraList->clear();
    for (const Camera &camera : m_state.cameras) {
        auto *item = new QListWidgetItem(
            QIcon(thumbnailPixmap(snapshotPath(camera.id))),
            camera.name, m_cameraList);
        item->setData(Qt::UserRole, camera.id);
        item->setData(Qt::UserRole + 1, camera.name);
        item->setSizeHint(QSize(0, 70));
    }
    updateAssignmentIndicators();
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

void MainWindow::updateAssignmentIndicators()
{
    if (!m_cameraList || !m_grid) {
        return;
    }

    const QStringList currentAssignments = m_grid->assignments();
    const QColor textColor(
        m_state.theme == QStringLiteral("dark")
            ? QStringLiteral("#f3f4f6")
            : QStringLiteral("#111827"));
    for (int row = 0; row < m_cameraList->count(); ++row) {
        QListWidgetItem *item = m_cameraList->item(row);
        const QString cameraId = item->data(Qt::UserRole).toString();
        const QString cameraName = item->data(Qt::UserRole + 1).toString();
        QStringList positions;
        for (int tileIndex = 0; tileIndex < currentAssignments.size();
             ++tileIndex) {
            if (currentAssignments.at(tileIndex) == cameraId) {
                positions.append(QString::number(tileIndex + 1));
            }
        }
        item->setText(positions.isEmpty()
                          ? tr("▷  %1").arg(cameraName)
                          : tr("●  %1  •  Tile %2")
                                .arg(cameraName, positions.join(
                                    QStringLiteral(", "))));
        item->setToolTip(positions.isEmpty()
                             ? tr("Double-click to show in the selected tile")
                             : tr("Currently shown in tile %1. Drag it or "
                                  "double-click to move it.")
                                   .arg(positions.join(QStringLiteral(", "))));
        item->setForeground(QBrush(textColor));
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
    const bool dark = m_state.theme == QStringLiteral("dark");
    m_themeButton->setText(dark ? tr("☀  Light mode")
                                : tr("☾  Dark mode"));

    if (dark) {
        qApp->setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget {
            background: #111419;
            color: #e7e9ed;
        }
        QWidget#sidebar {
            background: #171b21;
        }
        QWidget#videoTile {
            background: #080a0d;
            border: 1px solid #252a32;
        }
        QWidget#videoTile[selected="true"] {
            border: 2px solid #ff8a1f;
        }
        QLabel#sidebarHeading {
            background: transparent;
            color: #aeb5c0;
            font-size: 11px;
            font-weight: 700;
        }
        QLabel#sidebarTip {
            background: transparent;
            color: #9ca3af;
            font-size: 11px;
        }
        QListWidget#cameraList {
            background: #101318;
            color: #f3f4f6;
            border: 1px solid #343a43;
            border-radius: 5px;
            padding: 3px;
            outline: 0;
        }
        QListWidget#cameraList::item {
            color: #f3f4f6;
            background: #171b21;
            border-radius: 4px;
            padding: 3px;
        }
        QListWidget#cameraList::item:selected {
            color: #ffffff;
            background: #a9470b;
        }
        QLineEdit, QSpinBox, QComboBox {
            background: #1c2027;
            color: #e7e9ed;
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
        return;
    }

    qApp->setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget {
            background: #eef1f5;
            color: #111827;
        }
        QWidget#sidebar {
            background: #f8fafc;
        }
        QWidget#videoTile {
            background: #080a0d;
            border: 1px solid #d4d9e1;
        }
        QWidget#videoTile[selected="true"] {
            border: 2px solid #ff7a1a;
        }
        QLabel#sidebarHeading {
            background: transparent;
            color: #4b5563;
            font-size: 11px;
            font-weight: 700;
        }
        QLabel#sidebarTip {
            background: transparent;
            color: #6b7280;
            font-size: 11px;
        }
        QListWidget#cameraList {
            background: #ffffff;
            color: #111827;
            border: 1px solid #cbd5e1;
            border-radius: 5px;
            padding: 3px;
            outline: 0;
        }
        QListWidget#cameraList::item {
            color: #111827;
            background: #ffffff;
            border-radius: 4px;
            padding: 3px;
        }
        QListWidget#cameraList::item:selected {
            color: #111827;
            background: #ffd9bd;
        }
        QLineEdit, QSpinBox, QComboBox {
            background: #ffffff;
            color: #111827;
            border: 1px solid #cbd5e1;
            border-radius: 4px;
            padding: 5px;
        }
        QPushButton {
            background: #ffffff;
            color: #111827;
            border: 1px solid #cbd5e1;
            border-radius: 4px;
            padding: 6px 9px;
        }
        QPushButton:hover {
            background: #f1f5f9;
        }
        QPushButton:pressed {
            background: #ff7a1a;
            color: #111419;
        }
        QToolTip {
            background: #ffffff;
            color: #111827;
            border: 1px solid #94a3b8;
        }
    )"));
}
