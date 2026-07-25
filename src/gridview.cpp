#include "gridview.h"

#include "videotile.h"

#include <QGridLayout>

GridView::GridView(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QGridLayout(this);
    m_layout->setContentsMargins(2, 2, 2, 2);
    m_layout->setSpacing(2);
    rebuild();
}

void GridView::setGridSize(int rows, int columns)
{
    rows = qBound(1, rows, 5);
    columns = qBound(1, columns, 5);
    if (rows == m_rows && columns == m_columns) {
        return;
    }
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
    rebuild();
}

QStringList GridView::assignments() const
{
    QStringList result = m_assignments;
    result.resize(m_rows * m_columns);
    return result;
}

void GridView::assignCamera(const Camera &camera)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= m_tiles.size()) {
        return;
    }
    m_assignments.resize(m_tiles.size());
    m_assignments[m_selectedIndex] = camera.id;
    m_tiles.at(m_selectedIndex)->play(camera);
    emit assignmentsChanged();
}

void GridView::assignCameraToIndex(const QString &cameraId, int index)
{
    const Camera camera = findCamera(cameraId);
    if (camera.id.isEmpty() || index < 0 || index >= m_tiles.size()) {
        return;
    }
    selectTile(index);
    m_assignments.resize(m_tiles.size());
    m_assignments[index] = camera.id;
    m_tiles.at(index)->play(camera);
    emit assignmentsChanged();
}

void GridView::setFullscreenMode(bool fullscreen)
{
    const int margin = fullscreen ? 0 : 2;
    const int spacing = fullscreen ? 1 : 2;
    m_layout->setContentsMargins(margin, margin, margin, margin);
    m_layout->setSpacing(spacing);
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
    retained.resize(m_rows * m_columns);
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

    const int count = m_rows * m_columns;
    for (int index = 0; index < count; ++index) {
        auto *tile = new VideoTile(index, this);
        connect(tile, &VideoTile::selected,
                this, &GridView::selectTile);
        connect(tile, &VideoTile::cleared, this, [this](int tileIndex) {
            m_assignments.resize(m_tiles.size());
            m_assignments[tileIndex].clear();
            emit assignmentsChanged();
        });
        connect(tile, &VideoTile::cameraDropped,
                this, &GridView::assignCameraToIndex);
        connect(tile, &VideoTile::playbackError, this,
                [this, tile](int, const QString &message) {
                    emit playbackError(tile->hasCamera()
                        ? findCamera(tile->cameraId()).name
                        : tr("Camera"),
                        message);
                });
        m_layout->addWidget(tile, index / m_columns, index % m_columns);
        m_tiles.append(tile);

        const Camera camera = findCamera(m_assignments.value(index));
        if (!camera.id.isEmpty()) {
            tile->play(camera);
        }
    }
    selectTile(qBound(0, m_selectedIndex, count - 1));
}
