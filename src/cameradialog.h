#pragma once

#include "camera.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

class CameraDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit CameraDialog(QWidget *parent = nullptr);

    void setCamera(const Camera &camera);
    Camera camera() const;

private slots:
    void updatePreview();
    void validateAndAccept();

private:
    Camera m_camera;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_hostEdit = nullptr;
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QComboBox *m_streamCombo = nullptr;
    QLabel *m_previewLabel = nullptr;
    QSpinBox *m_latencySpin = nullptr;
    QCheckBox *m_tcpCheck = nullptr;
};
