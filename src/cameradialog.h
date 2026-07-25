#pragma once

#include "camera.h"

#include <QDialog>

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class RtspScanner;

class CameraDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit CameraDialog(QWidget *parent = nullptr);

    void setDefaultCredentials(const QString &username,
                               const QString &password);
    void setCamera(const Camera &camera);
    Camera camera() const;
    bool rememberCredentials() const;

private slots:
    void startScan();
    void addScannedAddress(const QString &address);
    void updatePreview();
    void validateAndAccept();

private:
    Camera m_camera;
    QLineEdit *m_nameEdit = nullptr;
    QComboBox *m_hostCombo = nullptr;
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QComboBox *m_streamCombo = nullptr;
    QComboBox *m_transportCombo = nullptr;
    QComboBox *m_bufferCombo = nullptr;
    QComboBox *m_displayModeCombo = nullptr;
    QSpinBox *m_zoomSpin = nullptr;
    QCheckBox *m_showPasswordCheck = nullptr;
    QCheckBox *m_rememberCredentialsCheck = nullptr;
    QSpinBox *m_channelSpin = nullptr;
    QPushButton *m_scanButton = nullptr;
    QLabel *m_scanStatus = nullptr;
    QLabel *m_previewLabel = nullptr;
    RtspScanner *m_scanner = nullptr;
    int m_scanFound = 0;
};
