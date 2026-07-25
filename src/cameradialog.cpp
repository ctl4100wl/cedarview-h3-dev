#include "cameradialog.h"

#include "rtspscanner.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
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
    m_usernameEdit->setText(QStringLiteral("admin"));

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(
        tr("Enter once, then reuse for new cameras"));

    m_showPasswordCheck = new QCheckBox(tr("Show password"), this);
    connect(m_showPasswordCheck, &QCheckBox::toggled,
            m_passwordEdit, [this](bool checked) {
                m_passwordEdit->setEchoMode(
                    checked ? QLineEdit::Normal : QLineEdit::Password);
            });

    m_rememberCredentialsCheck =
        new QCheckBox(tr("Use this login for new cameras"), this);
    m_rememberCredentialsCheck->setChecked(true);

    m_streamCombo = new QComboBox(this);
    m_streamCombo->addItem(tr("Sub-stream — recommended for H3 grids"), 1);
    m_streamCombo->addItem(tr("Main stream — highest quality"), 0);

    m_channelSpin = new QSpinBox(this);
    m_channelSpin->setRange(1, 16);
    m_channelSpin->setValue(1);
    m_channelSpin->setToolTip(
        tr("For a dual-lens camera, add it twice and select channel 1 "
           "for one lens and channel 2 for the other."));

    m_transportCombo = new QComboBox(this);
    m_transportCombo->addItem(tr("TCP — reliable"), QStringLiteral("tcp"));
    m_transportCombo->addItem(tr("UDP — lower latency"), QStringLiteral("udp"));

    m_bufferCombo = new QComboBox(this);
    m_bufferCombo->addItem(tr("Disabled — minimum delay"), 0);
    m_bufferCombo->addItem(tr("150 ms — low latency"), 150);
    m_bufferCombo->addItem(tr("300 ms — balanced"), 300);
    m_bufferCombo->addItem(tr("750 ms — stable"), 750);
    m_bufferCombo->addItem(tr("1500 ms — very stable"), 1500);
    m_bufferCombo->setCurrentIndex(m_bufferCombo->findData(300));

    m_displayModeCombo = new QComboBox(this);
    m_displayModeCombo->addItem(
        tr("Fill 16:9 — crop edges, largest picture"),
        QStringLiteral("fill"));
    m_displayModeCombo->addItem(
        tr("Fit 16:9 — show everything with black bars"),
        QStringLiteral("fit"));

    m_zoomSpin = new QSpinBox(this);
    m_zoomSpin->setRange(100, 200);
    m_zoomSpin->setSingleStep(10);
    m_zoomSpin->setSuffix(QStringLiteral("%"));
    m_zoomSpin->setValue(100);
    m_zoomSpin->setToolTip(
        tr("Digital zoom crops further into the center of the picture."));

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
    form->addRow(QString(), m_showPasswordCheck);
    form->addRow(QString(), m_rememberCredentialsCheck);
    form->addRow(tr("Channel / lens"), m_channelSpin);
    form->addRow(tr("Video stream"), m_streamCombo);
    form->addRow(tr("Transport"), m_transportCombo);
    form->addRow(tr("Network buffer"), m_bufferCombo);
    form->addRow(tr("Picture sizing"), m_displayModeCombo);
    form->addRow(tr("Digital zoom"), m_zoomSpin);
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
    connect(m_channelSpin, &QSpinBox::valueChanged,
            this, &CameraDialog::updatePreview);
    updatePreview();
}

void CameraDialog::setDefaultCredentials(const QString &username,
                                         const QString &password)
{
    if (m_usernameEdit->text().isEmpty() ||
        m_usernameEdit->text() == QStringLiteral("admin")) {
        m_usernameEdit->setText(
            username.isEmpty() ? QStringLiteral("admin") : username);
    }
    if (m_passwordEdit->text().isEmpty()) {
        m_passwordEdit->setText(password);
    }
    m_rememberCredentialsCheck->setChecked(true);
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
    m_channelSpin->setValue(qMax(1, camera.channel));
    m_streamCombo->setCurrentIndex(
        qMax(0, m_streamCombo->findData(camera.subtype)));
    m_transportCombo->setCurrentIndex(
        qMax(0, m_transportCombo->findData(camera.transport)));
    int bufferIndex = m_bufferCombo->findData(camera.latencyMs);
    if (bufferIndex < 0) {
        m_bufferCombo->addItem(
            tr("%1 ms — custom").arg(camera.latencyMs),
            camera.latencyMs);
        bufferIndex = m_bufferCombo->count() - 1;
    }
    m_bufferCombo->setCurrentIndex(bufferIndex);
    m_displayModeCombo->setCurrentIndex(
        qMax(0, m_displayModeCombo->findData(camera.displayMode)));
    m_zoomSpin->setValue(qBound(100, camera.zoomPercent, 200));
    m_rememberCredentialsCheck->setChecked(false);
    updatePreview();
}

Camera CameraDialog::camera() const
{
    Camera result = m_camera;
    result.name = m_nameEdit->text().trimmed();
    result.host = m_hostCombo->currentText().trimmed();
    result.username = m_usernameEdit->text();
    result.password = m_passwordEdit->text();
    result.channel = m_channelSpin->value();
    result.subtype = m_streamCombo->currentData().toInt();
    result.transport = m_transportCombo->currentData().toString();
    result.latencyMs = m_bufferCombo->currentData().toInt();
    result.displayMode = m_displayModeCombo->currentData().toString();
    result.zoomPercent = m_zoomSpin->value();
    if (result.name.isEmpty()) {
        result.name = result.host;
    }
    return result;
}

bool CameraDialog::rememberCredentials() const
{
    return m_rememberCredentialsCheck->isChecked();
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
