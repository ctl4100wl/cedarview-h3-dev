#include "gridview.h"

#include "videotile.h"

#include <QApplication>
#include <QEvent>
#include <QGridLayout>
#include <QTimer>
#include <QtMath>

GridView::GridView(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QGridLayout(this);
    m_layout->setContentsMargins(2, 2, 2, 2);
    m_layout->setSpacing(2);

    setMouseTracking(true);
    qApp->installEventFilter(this);
    m_cursorTimer = new QTimer(this);
    m_cursorTimer->setSingleShot(true);
    m_cursorTimer->setInterval(2500);
    connect(m_cursorTimer, &QTimer::timeout, this, [this] {
        if (!m_fullscreenMode) {
            return;
        }
        if (!m_cursorHidden) {
            qApp->setOverrideCursor(Qt::BlankCursor);
            m_cursorHidden = true;
        }
        for (VideoTile *tile : m_tiles) {
            tile->hideControls();
        }
    });
    rebuild();
}

GridView::~GridView()
{
    if (m_cursorHidden) {
        qApp->restoreOverrideCursor();
    }
}

void GridView::setGridSize(int rows, int columns)
{
    rows = qBound(1, rows, 5);
    columns = qBound(1, columns, 5);
    const int count = rows * columns;
    if (rows == m_rows && columns == m_columns &&
        count == m_tileCount) {
        return;
    }
    m_rows = rows;
    m_columns = columns;
    m_tileCount = count;
    rebuild();
    emit assignmentsChanged();
}

void GridView::setTileCount(int count)
{
    count = qBound(1, count, 25);

    int rows = 1;
    if (count == 3) {
        rows = 2;
    } else if (count >= 4) {
        rows = qMax(1, qFloor(qSqrt(static_cast<double>(count))));
    }
    const int columns = qCeil(static_cast<double>(count) / rows);
    rows = qCeil(static_cast<double>(count) / columns);

    if (count == m_tileCount && rows == m_rows &&
        columns == m_columns) {
        return;
    }
    m_tileCount = count;
    m_rows = rows;
    m_columns = columns;
    rebuild();
    emit assignmentsChanged();
}

void GridView::setCameras(const QList<Camera> &cameras)
{
    m_cameras = cameras;
    for (int i = 0; i < m_assignments.size(); ++i) {
        if (!m_assignments.at(i).isEmpty() &&
            findCamera(m_assignments.at(i)).id.isEmpty()) {
            m_assignments[i].clear();
        }
    }
    rebuild();
}

void GridView::setAssignments(const QStringList &assignments)
{
    m_assignments = assignments;
    QStringList occupiedCameraIds;
    for (QString &cameraId : m_assignments) {
        if (cameraId.isEmpty()) {
            continue;
        }
        if (occupiedCameraIds.contains(cameraId)) {
            cameraId.clear();
        } else {
            occupiedCameraIds.append(cameraId);
        }
    }
    rebuild();
}

QStringList GridView::assignments() const
{
    QStringList result = m_assignments;
    result.resize(m_tileCount);
    return result;
}

void GridView::assignCamera(const Camera &camera)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= m_tiles.size()) {
        return;
    }
    assignCameraToIndex(camera.id, m_selectedIndex);
}

void GridView::assignCameraToIndex(const QString &cameraId, int index)
{
    const Camera camera = findCamera(cameraId);
    if (camera.id.isEmpty() || index < 0 || index >= m_tiles.size()) {
        return;
    }
    selectTile(index);
    m_assignments.resize(m_tiles.size());

    if (m_assignments.at(index) == camera.id) {
        return;
    }

    // A camera may occupy only one tile. When it is dragged from the sidebar
    // onto another tile, exchange that tile's assignment with its old one.
    // Dropping onto an empty tile therefore behaves like a move.
    const int previousIndex = m_assignments.indexOf(camera.id);
    const QString displacedCameraId = m_assignments.at(index);
    if (previousIndex >= 0 && previousIndex != index) {
        m_assignments[previousIndex] = displacedCameraId;
    }
    m_assignments[index] = camera.id;

    if (previousIndex >= 0 && previousIndex != index) {
        const Camera displacedCamera = findCamera(displacedCameraId);
        if (displacedCamera.id.isEmpty()) {
            m_tiles.at(previousIndex)->stop();
        } else {
            m_tiles.at(previousIndex)->play(displacedCamera);
        }
    }
    m_tiles.at(index)->play(camera);
    emit assignmentsChanged();
}

void GridView::setFullscreenMode(bool fullscreen)
{
    m_fullscreenMode = fullscreen;
    const int margin = fullscreen ? 0 : 2;
    const int spacing = fullscreen ? 1 : 2;
    m_layout->setContentsMargins(margin, margin, margin, margin);
    m_layout->setSpacing(spacing);
    for (VideoTile *tile : m_tiles) {
        tile->setFullscreenMode(fullscreen);
    }
    if (fullscreen) {
        if (m_cursorHidden) {
            qApp->restoreOverrideCursor();
            m_cursorHidden = false;
        }
        m_cursorTimer->start();
    } else {
        m_cursorTimer->stop();
        if (m_cursorHidden) {
            qApp->restoreOverrideCursor();
            m_cursorHidden = false;
        }
    }
}

void GridView::setPlaybackBackend(const QString &backend)
{
    const QString normalized = backend == QStringLiteral("gstreamer")
        ? QStringLiteral("gstreamer")
        : QStringLiteral("mpv");
    if (normalized == m_playbackBackend) {
        return;
    }
    m_playbackBackend = normalized;
    for (VideoTile *tile : m_tiles) {
        tile->setPlaybackBackend(m_playbackBackend);
    }
}

void GridView::clearSelected()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= m_tiles.size()) {
        return;
    }
    m_assignments.resize(m_tiles.size());
    m_assignments[m_selectedIndex].clear();
    m_tiles.at(m_selectedIndex)->stop();
    emit assignmentsChanged();
}

bool GridView::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched)
    if (m_fullscreenMode &&
        (event->type() == QEvent::MouseMove ||
         event->type() == QEvent::MouseButtonPress ||
         event->type() == QEvent::Wheel)) {
        if (m_cursorHidden) {
            qApp->restoreOverrideCursor();
            m_cursorHidden = false;
        }
        m_cursorTimer->start();
    }
    return QWidget::eventFilter(watched, event);
}

Camera GridView::findCamera(const QString &id) const
{
    for (const Camera &camera : m_cameras) {
        if (camera.id == id) {
            return camera;
        }
    }
    return {};
}

void GridView::selectTile(int index)
{
    m_selectedIndex = index;
    for (VideoTile *tile : m_tiles) {
        tile->setSelected(tile->index() == index);
    }
}

void GridView::rebuild()
{
    QStringList retained = m_assignments;
    retained.resize(m_tileCount);
    m_assignments = retained;

    while (QLayoutItem *item = m_layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            if (auto *tile = qobject_cast<VideoTile *>(widget)) {
                tile->stop();
            }
            widget->deleteLater();
        }
        delete item;
    }
    m_tiles.clear();

    for (int column = 0; column < 10; ++column) {
        m_layout->setColumnStretch(column, 0);
    }
    for (int column = 0; column < m_columns * 2; ++column) {
        m_layout->setColumnStretch(column, 1);
    }

    const int count = m_tileCount;
    for (int index = 0; index < count; ++index) {
        auto *tile = new VideoTile(index, this);
        tile->setPlaybackBackend(m_playbackBackend);
        connect(tile, &VideoTile::selected,
                this, &GridView::selectTile);
        connect(tile, &VideoTile::cleared, this, [this](int tileIndex) {
            m_assignments.resize(m_tiles.size());
            m_assignments[tileIndex].clear();
            emit assignmentsChanged();
        });
        connect(tile, &VideoTile::cameraDropped,
                this, &GridView::assignCameraToIndex);
        connect(tile, &VideoTile::streamSubtypeChanged,
                this, [this](const QString &cameraId, int subtype) {
                    for (Camera &camera : m_cameras) {
                        if (camera.id == cameraId) {
                            camera.subtype = subtype;
                            break;
                        }
                    }
                    emit cameraStreamChanged(cameraId, subtype);
                });
        connect(tile, &VideoTile::exitFullscreenRequested,
                this, &GridView::exitFullscreenRequested);
        connect(tile, &VideoTile::playbackError, this,
                [this, tile](int, const QString &message) {
                    emit playbackError(tile->hasCamera()
                        ? findCamera(tile->cameraId()).name
                        : tr("Camera"),
                        message);
                });
        const int row = index / m_columns;
        const int firstIndexInRow = row * m_columns;
        const int itemsInRow =
            qMin(m_columns, count - firstIndexInRow);
        const int centeredOffset = m_columns - itemsInRow;
        const int virtualColumn =
            centeredOffset + (index - firstIndexInRow) * 2;
        m_layout->addWidget(tile, row, virtualColumn, 1, 2);
        m_tiles.append(tile);

        const Camera camera = findCamera(m_assignments.value(index));
        if (!camera.id.isEmpty()) {
            tile->play(camera);
        }
    }
    selectTile(qBound(0, m_selectedIndex, count - 1));
}
