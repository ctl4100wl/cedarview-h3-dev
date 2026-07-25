#include "cameradialog.h"

#include "rtspscanner.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

CameraDialog::CameraDialog(QWidget *parent)
    : QDialog(parent),
      m_camera(Camera::create())
{
    setWindowTitle(tr("Camera"));
    setMinimumWidth(520);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("Optional — defaults to IP address"));

    m_hostCombo = new QComboBox(this);
    m_hostCombo->setEditable(true);
    m_hostCombo->setInsertPolicy(QComboBox::NoInsert);
    m_hostCombo->lineEdit()->setPlaceholderText(
        QStringLiteral("192.168.1.10"));

    m_scanButton = new QPushButton(tr("Scan LAN"), this);
    m_scanStatus = new QLabel(this);
    m_scanStatus->setStyleSheet(
        QStringLiteral("color: #89919d; font-size: 11px;"));

    auto *hostRow = new QHBoxLayout;
    hostRow->setContentsMargins(0, 0, 0, 0);
    hostRow->addWidget(m_hostCombo, 1);
    hostRow->addWidget(m_scanButton);

    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setPlaceholderText(QStringLiteral("admin"));

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);

    m_streamCombo = new QComboBox(this);
    m_streamCombo->addItem(tr("Sub-stream — recommended for H3 grids"), 1);
    m_streamCombo->addItem(tr("Main stream — highest quality"), 0);

    m_transportCombo = new QComboBox(this);
    m_transportCombo->addItem(tr("TCP — reliable"), QStringLiteral("tcp"));
    m_transportCombo->addItem(tr("UDP — lower latency"), QStringLiteral("udp"));

    m_previewLabel = new QLabel(this);
    m_previewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_previewLabel->setWordWrap(true);
    m_previewLabel->setStyleSheet(
        QStringLiteral("color: #89919d; font-family: monospace;"));

    auto *form = new QFormLayout;
    form->addRow(tr("Name"), m_nameEdit);
    form->addRow(tr("Camera IP"), hostRow);
    form->addRow(QString(), m_scanStatus);
    form->addRow(tr("Username"), m_usernameEdit);
    form->addRow(tr("Password"), m_passwordEdit);
    form->addRow(tr("Video stream"), m_streamCombo);
    form->addRow(tr("Transport"), m_transportCombo);
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

    m_scanner = new RtspScanner(this);
    connect(m_scanButton, &QPushButton::clicked,
            this, &CameraDialog::startScan);
    connect(m_scanner, &RtspScanner::cameraFound,
            this, &CameraDialog::addScannedAddress);
    connect(m_scanner, &RtspScanner::progressChanged, this,
            [this](int completed, int total) {
                m_scanStatus->setText(
                    tr("Scanning port 554… %1/%2").arg(completed).arg(total));
            });
    connect(m_scanner, &RtspScanner::finished, this, [this] {
        m_scanButton->setEnabled(true);
        m_scanStatus->setText(
            tr("Scan complete — %1 RTSP device(s)")
                .arg(m_scanFound));
    });

    connect(m_hostCombo->lineEdit(), &QLineEdit::textChanged,
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
    if (m_hostCombo->findText(camera.host) < 0) {
        m_hostCombo->addItem(camera.host);
    }
    m_hostCombo->setCurrentText(camera.host);
    m_usernameEdit->setText(camera.username);
    m_passwordEdit->setText(camera.password);
    m_streamCombo->setCurrentIndex(
        qMax(0, m_streamCombo->findData(camera.subtype)));
    m_transportCombo->setCurrentIndex(
        qMax(0, m_transportCombo->findData(camera.transport)));
    updatePreview();
}

Camera CameraDialog::camera() const
{
    Camera result = m_camera;
    result.name = m_nameEdit->text().trimmed();
    result.host = m_hostCombo->currentText().trimmed();
    result.username = m_usernameEdit->text();
    result.password = m_passwordEdit->text();
    result.channel = 1;
    result.subtype = m_streamCombo->currentData().toInt();
    result.transport = m_transportCombo->currentData().toString();
    if (result.name.isEmpty()) {
        result.name = result.host;
    }
    return result;
}

void CameraDialog::startScan()
{
    m_scanButton->setEnabled(false);
    m_scanFound = 0;
    m_scanStatus->setText(tr("Finding local networks…"));
    m_scanner->start();
}

void CameraDialog::addScannedAddress(const QString &address)
{
    ++m_scanFound;
    if (m_hostCombo->findText(address) < 0) {
        m_hostCombo->addItem(address);
    }
    if (m_hostCombo->currentText().trimmed().isEmpty()) {
        m_hostCombo->setCurrentText(address);
    }
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
