#include "cameradialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

CameraDialog::CameraDialog(QWidget *parent)
    : QDialog(parent),
      m_camera(Camera::create())
{
    setWindowTitle(tr("Camera"));
    setMinimumWidth(520);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("Optional — defaults to IP address"));

    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setPlaceholderText(QStringLiteral("192.168.1.10"));

    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setPlaceholderText(QStringLiteral("admin"));

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);

    m_streamCombo = new QComboBox(this);
    m_streamCombo->addItem(tr("Sub-stream — recommended for H3 grids"), 1);
    m_streamCombo->addItem(tr("Main stream — highest quality"), 0);

    m_previewLabel = new QLabel(this);
    m_previewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_previewLabel->setWordWrap(true);
    m_previewLabel->setStyleSheet(
        QStringLiteral("color: #89919d; font-family: monospace;"));

    m_latencySpin = new QSpinBox(this);
    m_latencySpin->setRange(50, 5000);
    m_latencySpin->setValue(300);
    m_latencySpin->setSuffix(tr(" ms"));

    m_tcpCheck = new QCheckBox(tr("Force RTSP over TCP"), this);
    m_tcpCheck->setChecked(true);

    auto *form = new QFormLayout;
    form->addRow(tr("Name"), m_nameEdit);
    form->addRow(tr("Camera IP"), m_hostEdit);
    form->addRow(tr("Username"), m_usernameEdit);
    form->addRow(tr("Password"), m_passwordEdit);
    form->addRow(tr("Video stream"), m_streamCombo);
    form->addRow(tr("Network latency"), m_latencySpin);
    form->addRow(QString(), m_tcpCheck);
    form->addRow(tr("Generated URL"), m_previewLabel);

    auto *warning = new QLabel(
        tr("Credentials inside the URL are saved in a private configuration "
           "file readable only by your Linux user."),
        this);
    warning->setWordWrap(true);
    warning->setStyleSheet(
        QStringLiteral("color: palette(mid); font-size: 11px;"));

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &CameraDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(warning);
    layout->addWidget(buttons);

    connect(m_hostEdit, &QLineEdit::textChanged,
            this, &CameraDialog::updatePreview);
    connect(m_usernameEdit, &QLineEdit::textChanged,
            this, &CameraDialog::updatePreview);
    connect(m_passwordEdit, &QLineEdit::textChanged,
            this, &CameraDialog::updatePreview);
    connect(m_streamCombo, &QComboBox::currentIndexChanged,
            this, &CameraDialog::updatePreview);
    updatePreview();
}

void CameraDialog::setCamera(const Camera &camera)
{
    m_camera = camera;
    m_nameEdit->setText(camera.name);
    m_hostEdit->setText(camera.host);
    m_usernameEdit->setText(camera.username);
    m_passwordEdit->setText(camera.password);
    m_streamCombo->setCurrentIndex(
        qMax(0, m_streamCombo->findData(camera.subtype)));
    m_latencySpin->setValue(camera.latencyMs);
    m_tcpCheck->setChecked(camera.forceTcp);
    updatePreview();
}

Camera CameraDialog::camera() const
{
    Camera result = m_camera;
    result.name = m_nameEdit->text().trimmed();
    result.host = m_hostEdit->text().trimmed();
    result.username = m_usernameEdit->text();
    result.password = m_passwordEdit->text();
    result.channel = 1;
    result.subtype = m_streamCombo->currentData().toInt();
    result.latencyMs = m_latencySpin->value();
    result.forceTcp = m_tcpCheck->isChecked();
    if (result.name.isEmpty()) {
        result.name = result.host;
    }
    return result;
}

void CameraDialog::updatePreview()
{
    const Camera result = camera();
    QString preview = result.resolvedRtspUrl();
    if (!result.password.isEmpty()) {
        preview.replace(
            QString::fromUtf8(QUrl::toPercentEncoding(result.password)),
            QStringLiteral("••••••"));
        preview.replace(result.password, QStringLiteral("••••••"));
    }
    m_previewLabel->setText(preview.isEmpty()
        ? tr("Enter the camera IP")
        : preview);
}

void CameraDialog::validateAndAccept()
{
    const Camera result = camera();
    if (result.host.isEmpty()) {
        QMessageBox::warning(this, tr("Missing camera IP"),
                             tr("Enter the camera's local IP address."));
        return;
    }
    if (result.username.isEmpty()) {
        QMessageBox::warning(this, tr("Missing username"),
                             tr("Enter the camera username, usually admin."));
        return;
    }
    if (result.password.isEmpty()) {
        QMessageBox::warning(this, tr("Missing password"),
                             tr("Enter the camera password."));
        return;
    }
    accept();
}
