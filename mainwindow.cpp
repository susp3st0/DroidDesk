#include "mainwindow.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSaveFile>
#include <QSettings>
#include <QSslSocket>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QTimer>
#include <QSet>
#include <algorithm>

namespace {
QPushButton *makeButton(const QString &text, const QString &objectName = {})
{
    auto *button = new QPushButton(text);
    if (!objectName.isEmpty())
        button->setObjectName(objectName);
    button->setMinimumHeight(34);
    return button;
}

QWidget *makeSection(const QString &title, QVBoxLayout *&layout)
{
    auto *section = new QWidget;
    layout = new QVBoxLayout(section);
    layout->setContentsMargins(14, 12, 14, 14);
    layout->setSpacing(10);
    auto *heading = new QLabel(title);
    heading->setObjectName("sectionHeading");
    layout->addWidget(heading);
    return section;
}

QString propertyValue(const QString &text, const QString &key)
{
    const QString prefix = key + QLatin1Char(':');
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.startsWith(prefix)) {
            QString value = line.mid(prefix.size()).trimmed();
            if (value.size() >= 2 && value.startsWith('[') && value.endsWith(']'))
                value = value.mid(1, value.size() - 2);
            return value;
        }
    }
    return {};
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    setWindowTitle("DroidDesk — Android Device Manager");
    setWindowIcon(QIcon(":/icons/android.png"));
    resize(1160, 790);
    setMinimumSize(920, 640);
    refreshDevices();
    m_devicePollTimer = new QTimer(this);
    m_devicePollTimer->setInterval(3000);
    connect(m_devicePollTimer, &QTimer::timeout, this, [this] {
        refreshDevices(true);
    });
    m_devicePollTimer->start();
    QTimer::singleShot(10000, this, [this] {
        if (m_devices->count() == 0)
            refreshDevices();
    });
}

MainWindow::~MainWindow() = default;

QString MainWindow::adbExecutable() const
{
    QSettings settings;
    const QString configured = settings.value("adb/path").toString();
    if (!configured.isEmpty() && QFileInfo::exists(configured))
        return configured;
    const QString fromPath = QStandardPaths::findExecutable("adb.exe");
    if (!fromPath.isEmpty())
        return fromPath;
    QStringList sdkRoots{QCoreApplication::applicationDirPath(), QDir::currentPath(),
                         qEnvironmentVariable("ANDROID_SDK_ROOT"),
                         qEnvironmentVariable("ANDROID_HOME")};
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty())
        sdkRoots << localAppData + "/Android/Sdk";
    for (const QString &root : sdkRoots) {
        if (root.isEmpty())
            continue;
        QDir directory(root);
        for (int depth = 0; depth < 6; ++depth) {
            const QString candidate = directory.filePath("platform-tools/adb.exe");
            if (QFileInfo::exists(candidate))
                return candidate;
            const QString alongsideApp = directory.filePath("adb.exe");
            if (QFileInfo::exists(alongsideApp))
                return alongsideApp;
            if (!directory.cdUp())
                break;
        }
    }
    return "adb";
}

QString MainWindow::scrcpyExecutable() const
{
    QStringList searchRoots{QCoreApplication::applicationDirPath(), QDir::currentPath()};
    const QString appDir = QCoreApplication::applicationDirPath();
    QDir appDirectory(appDir);
    for (int depth = 0; depth < 6; ++depth) {
        searchRoots.append(appDirectory.absolutePath());
        if (!appDirectory.cdUp())
            break;
    }
    QSet<QString> checked;
    for (const QString &root : searchRoots) {
        if (root.isEmpty())
            continue;
        const QString normalizedRoot = QDir::cleanPath(root);
        if (checked.contains(normalizedRoot))
            continue;
        checked.insert(normalizedRoot);
        const QString candidate = QDir(normalizedRoot).filePath("scrcpy/scrcpy.exe");
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();
    }
    return {};
}

QString MainWindow::selectedSerial() const
{
    const QString value = m_devices->currentData().toString();
    if (value != "auto")
        return value.startsWith("__") ? QString() : value;
    for (int i = 1; i < m_devices->count(); ++i) {
        const QString candidate = m_devices->itemData(i).toString();
        if (!candidate.startsWith("__"))
            return candidate;
    }
    return {};
}

QString MainWindow::shellQuote(const QString &value) const
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("'") + escaped + QStringLiteral("'");
}

MainWindow::CommandResult MainWindow::runAdb(const QStringList &arguments, int timeoutMs)
{
    CommandResult result;
    QProcess process;
    process.setProgram(adbExecutable());
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    result.started = process.waitForStarted(5000);
    if (!result.started) {
        result.error = process.errorString().toUtf8();
        return result;
    }
    if (!process.waitForFinished(timeoutMs)) {
        result.timedOut = true;
        process.kill();
        process.waitForFinished(3000);
    }
    result.exitCode = process.exitCode();
    result.output = process.readAllStandardOutput();
    result.error = process.readAllStandardError();
    return result;
}

MainWindow::CommandResult MainWindow::runDevice(const QStringList &arguments, int timeoutMs)
{
    QStringList command;
    if (!selectedSerial().isEmpty())
        command << "-s" << selectedSerial();
    command << arguments;
    const CommandResult result = runAdb(command, timeoutMs);
    if (isDeviceDisconnected(result))
        handleDeviceDisconnected(result);
    return result;
}

void MainWindow::appendLog(const QString &text)
{
    m_output->appendPlainText(QStringLiteral("[%1] %2")
                                  .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), text));
}

void MainWindow::logCommand(const QString &label, const CommandResult &result)
{
    QString message = label + QStringLiteral(" — ");
    if (!result.started) {
        message += QStringLiteral("could not start ADB: ") + QString::fromLocal8Bit(result.error);
    } else if (result.timedOut) {
        message += QStringLiteral("timed out");
    } else {
        message += QStringLiteral("exit ") + QString::number(result.exitCode);
    }
    appendLog(message);
    const QString output = QString::fromLocal8Bit(result.output).trimmed();
    const QString error = QString::fromLocal8Bit(result.error).trimmed();
    if (!output.isEmpty())
        appendLog(output);
    if (!error.isEmpty())
        appendLog(QStringLiteral("stderr: ") + error);
}

void MainWindow::showResult(const QString &successMessage, const CommandResult &result)
{
    if (!result.started || result.timedOut || result.exitCode != 0) {
        const QString details = QString::fromLocal8Bit(
            result.error.isEmpty() ? result.output : result.error).trimmed();
        if (isDeviceDisconnected(result)) {
            statusBar()->showMessage("Device disconnected. Reconnect it and approve USB debugging; refreshing devices…", 8000);
            return;
        }
        QMessageBox::warning(this, "ADB command failed",
                             (result.timedOut ? "The command timed out." :
                              !result.started ? "ADB could not be started. Install Android SDK Platform-Tools or select adb.exe in Settings." :
                              "The device reported an error.") +
                                 (details.isEmpty() ? QString() : QStringLiteral("\n\n") + details));
        return;
    }
    statusBar()->showMessage(successMessage, 5000);
}

bool MainWindow::isDeviceDisconnected(const CommandResult &result) const
{
    const QString message = QString::fromLocal8Bit(result.error + '\n' + result.output).toLower();
    static const QStringList indicators{
        "device offline",
        "device not found",
        "no devices/emulators found",
        "device unauthorized",
        "device disconnected",
        "transport error",
        "error: closed"
    };
    for (const QString &indicator : indicators) {
        if (message.contains(indicator))
            return true;
    }
    return false;
}

void MainWindow::handleDeviceDisconnected(const CommandResult &result)
{
    m_connectionLabel->setText("Device disconnected or unauthorized. Reconnect it and approve USB debugging.");
    statusBar()->showMessage("Device connection lost; refreshing the device list…", 8000);
    const QString details = QString::fromLocal8Bit(result.error + '\n' + result.output).trimmed();
    if (!details.isEmpty())
        appendLog("Device connection lost: " + details);
    QTimer::singleShot(500, this, [this] { refreshDevices(); });
}

void MainWindow::buildUi()
{
    setStyleSheet(R"(
        QMainWindow { background: #f3f5f9; }
        QWidget { color: #202737; font-family: "Segoe UI"; font-size: 10pt; }
        QTabWidget::pane { border: 1px solid #e0e5ed; background: #ffffff; }
        QTabBar::tab { background: #e9edf4; padding: 11px 17px; margin-right: 3px; }
        QTabBar::tab:selected { background: #ffffff; color: #315fe8; font-weight: 600; }
        QLabel#sectionHeading { color: #283652; font-size: 15pt; font-weight: 650; }
        QLabel#muted { color: #78839a; }
        QLabel#metric { background: #f4f7fc; padding: 12px; font-size: 11pt; }
        QLineEdit, QComboBox, QPlainTextEdit, QListWidget { background: #ffffff; border: 1px solid #dce2ec; padding: 7px; selection-background-color: #5278ec; }
        QPushButton { background: #f5f7fb; border: 1px solid #dce2ec; padding: 7px 12px; }
        QPushButton:hover { background: #eaf0ff; border-color: #9eb4fa; }
        QPushButton#primary { background: #4169e1; color: white; border-color: #4169e1; font-weight: 600; }
        QPushButton#primary:hover { background: #3158cf; }
        QPushButton#danger { color: #bd3e45; }
        QStatusBar { background: #ffffff; color: #65718a; }
        QComboBox QAbstractItemView { border: 1px solid #dce2ec; }
        QScrollBar:vertical { width: 12px; background: #f3f5f9; }
        QScrollBar::handle:vertical { background: #c5cedc; min-height: 24px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
        QScrollBar:horizontal { height: 12px; background: #f3f5f9; }
        QScrollBar::handle:horizontal { background: #c5cedc; min-width: 24px; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }
    )");

    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(20, 18, 20, 14);
    root->setSpacing(14);

    auto *header = new QHBoxLayout;
    auto *brand = new QHBoxLayout;
    auto *brandIcon = new QLabel;
    brandIcon->setPixmap(QIcon(":/icons/android.png").pixmap(44, 44));
    brandIcon->setMinimumSize(44, 44);
    brandIcon->setAlignment(Qt::AlignCenter);
    brand->addWidget(brandIcon, 0, Qt::AlignVCenter);
    auto *brandText = new QVBoxLayout;
    auto *title = new QLabel("DroidDesk");
    title->setStyleSheet("font-size: 22pt; font-weight: 700; color: #243452;");
    auto *subtitle = new QLabel("Your Android device, managed from Windows.");
    subtitle->setObjectName("muted");
    brandText->addWidget(title);
    brandText->addWidget(subtitle);
    brand->addLayout(brandText);
    header->addLayout(brand);
    header->addStretch();
    m_devices = new QComboBox;
    m_devices->setMinimumWidth(250);
    m_devices->setMinimumHeight(38);
    auto *refreshButton = makeButton("Refresh devices");
    auto *settingsButton = makeButton("ADB settings");
    header->addWidget(m_devices);
    header->addWidget(refreshButton);
    header->addWidget(settingsButton);
    root->addLayout(header);

    auto *tabs = new QTabWidget;
    root->addWidget(tabs, 1);

    QVBoxLayout *homeLayout = nullptr;
    auto *home = makeSection("Device overview", homeLayout);
    m_connectionLabel = new QLabel("Checking ADB connection…");
    m_connectionLabel->setObjectName("metric");
    homeLayout->addWidget(m_connectionLabel);
    auto *infoGrid = new QFormLayout;
    infoGrid->setLabelAlignment(Qt::AlignLeft);
    m_deviceNameLabel = new QLabel("—");
    m_androidLabel = new QLabel("—");
    m_serialLabel = new QLabel("—");
    m_resolutionLabel = new QLabel("—");
    m_densityLabel = new QLabel("—");
    m_batteryLabel = new QLabel("—");
    infoGrid->addRow("Device", m_deviceNameLabel);
    infoGrid->addRow("Android version", m_androidLabel);
    infoGrid->addRow("Serial", m_serialLabel);
    infoGrid->addRow("Display", m_resolutionLabel);
    infoGrid->addRow("Density", m_densityLabel);
    infoGrid->addRow("Battery", m_batteryLabel);
    homeLayout->addLayout(infoGrid);

    auto *quick = new QLabel("QUICK ACTIONS");
    quick->setStyleSheet("font-size: 9pt; font-weight: 700; color: #78839a; margin-top: 8px;");
    homeLayout->addWidget(quick);
    auto *quickGrid = new QHBoxLayout;
    auto *quickScreenshot = makeButton("▣  Screenshot");
    auto *quickReboot = makeButton("↻  Reboot");
    auto *quickRecovery = makeButton("Recovery");
    auto *quickBootloader = makeButton("Bootloader");
    auto *quickClipboard = makeButton("Copy device info");
    quickGrid->addWidget(quickScreenshot);
    quickGrid->addWidget(quickReboot);
    quickGrid->addWidget(quickRecovery);
    quickGrid->addWidget(quickBootloader);
    quickGrid->addWidget(quickClipboard);
    homeLayout->addLayout(quickGrid);
    homeLayout->addStretch();
    tabs->addTab(home, "  Overview  ");

    QVBoxLayout *scrcpyLayout = nullptr;
    auto *scrcpyPage = makeSection("Screen mirror and control", scrcpyLayout);
    auto *scrcpyDescription = new QLabel(
        "Mirror and control Android with scrcpy. Choose a mode, quality and recording options, then start the separate scrcpy window.");
    scrcpyDescription->setObjectName("muted");
    scrcpyDescription->setWordWrap(true);
    scrcpyLayout->addWidget(scrcpyDescription);

    auto *scrcpyModeRow = new QHBoxLayout;
    scrcpyModeRow->addWidget(new QLabel("Mode"));
    m_scrcpyMode = new QComboBox;
    m_scrcpyMode->addItem("Mirror device screen", "mirror");
    m_scrcpyMode->addItem("Virtual display", "virtual");
    m_scrcpyMode->addItem("OTG control (USB)", "otg");
    scrcpyModeRow->addWidget(m_scrcpyMode, 1);
    scrcpyLayout->addLayout(scrcpyModeRow);

    auto *scrcpyVirtualRow = new QHBoxLayout;
    scrcpyVirtualRow->addWidget(new QLabel("Virtual display size"));
    m_scrcpyVirtualSize = new QLineEdit("1280x720");
    m_scrcpyVirtualSize->setValidator(
        new QRegularExpressionValidator(QRegularExpression("^[0-9]{3,4}x[0-9]{3,4}$"),
                                        m_scrcpyVirtualSize));
    m_scrcpyVirtualSize->setMaximumWidth(130);
    m_scrcpyFlexDisplay = new QCheckBox("Flex display (resize to window)");
    scrcpyVirtualRow->addWidget(m_scrcpyVirtualSize);
    scrcpyVirtualRow->addWidget(m_scrcpyFlexDisplay);
    scrcpyVirtualRow->addStretch();
    scrcpyLayout->addLayout(scrcpyVirtualRow);

    auto *scrcpyQualityRow = new QHBoxLayout;
    scrcpyQualityRow->addWidget(new QLabel("Maximum video size"));
    m_scrcpyMaxSize = new QComboBox;
    m_scrcpyMaxSize->addItem("Device default", "");
    m_scrcpyMaxSize->addItem("720p", "720");
    m_scrcpyMaxSize->addItem("1080p", "1080");
    m_scrcpyMaxSize->addItem("1440p", "1440");
    scrcpyQualityRow->addWidget(m_scrcpyMaxSize);
    scrcpyQualityRow->addWidget(new QLabel("Frame rate"));
    m_scrcpyMaxFps = new QComboBox;
    m_scrcpyMaxFps->addItem("Device default", "");
    m_scrcpyMaxFps->addItem("30 FPS", "30");
    m_scrcpyMaxFps->addItem("60 FPS", "60");
    m_scrcpyMaxFps->addItem("90 FPS", "90");
    m_scrcpyMaxFps->addItem("120 FPS", "120");
    scrcpyQualityRow->addWidget(m_scrcpyMaxFps);
    scrcpyQualityRow->addStretch();
    scrcpyLayout->addLayout(scrcpyQualityRow);

    auto *scrcpyOptionsRow = new QHBoxLayout;
    m_scrcpyAudio = new QCheckBox("Audio");
    m_scrcpyAudio->setChecked(true);
    m_scrcpyStayAwake = new QCheckBox("Keep device awake");
    m_scrcpyTurnScreenOff = new QCheckBox("Turn device screen off");
    m_scrcpyAlwaysOnTop = new QCheckBox("Always on top");
    m_scrcpyShowTouches = new QCheckBox("Show touches");
    scrcpyOptionsRow->addWidget(m_scrcpyAudio);
    scrcpyOptionsRow->addWidget(m_scrcpyStayAwake);
    scrcpyOptionsRow->addWidget(m_scrcpyTurnScreenOff);
    scrcpyOptionsRow->addWidget(m_scrcpyAlwaysOnTop);
    scrcpyOptionsRow->addWidget(m_scrcpyShowTouches);
    scrcpyOptionsRow->addStretch();
    scrcpyLayout->addLayout(scrcpyOptionsRow);

    auto *scrcpyOtgRow = new QHBoxLayout;
    m_scrcpyOtgGamepad = new QCheckBox("Enable gamepad in OTG mode");
    scrcpyOtgRow->addWidget(m_scrcpyOtgGamepad);
    scrcpyOtgRow->addStretch();
    scrcpyLayout->addLayout(scrcpyOtgRow);

    auto *scrcpyRecordRow = new QHBoxLayout;
    m_scrcpyRecording = new QCheckBox("Record session");
    m_scrcpyRecordingPath = new QLineEdit;
    m_scrcpyRecordingPath->setPlaceholderText("Choose an MP4 or MKV recording file…");
    m_scrcpyBrowseRecording = makeButton("Choose file…");
    scrcpyRecordRow->addWidget(m_scrcpyRecording);
    scrcpyRecordRow->addWidget(m_scrcpyRecordingPath, 1);
    scrcpyRecordRow->addWidget(m_scrcpyBrowseRecording);
    scrcpyLayout->addLayout(scrcpyRecordRow);

    auto *scrcpyModeInfo = new QLabel;
    scrcpyModeInfo->setObjectName("muted");
    scrcpyModeInfo->setWordWrap(true);
    scrcpyLayout->addWidget(scrcpyModeInfo);

    auto *scrcpyActionsRow = new QHBoxLayout;
    m_scrcpyStatusLabel = new QLabel("Ready.");
    m_scrcpyStatusLabel->setObjectName("metric");
    m_scrcpyStatusLabel->setWordWrap(true);
    m_scrcpyStartButton = makeButton("Start scrcpy", "primary");
    scrcpyActionsRow->addWidget(m_scrcpyStatusLabel, 1);
    scrcpyActionsRow->addWidget(m_scrcpyStartButton);
    scrcpyLayout->addLayout(scrcpyActionsRow);
    scrcpyLayout->addStretch();
    tabs->addTab(scrcpyPage, "  Screen mirror  ");

    QVBoxLayout *appsLayout = nullptr;
    auto *appsPage = makeSection("Apps", appsLayout);
    auto *appActions = new QHBoxLayout;
    m_appFilter = new QLineEdit;
    m_appFilter->setPlaceholderText("Filter by package name…");
    m_systemApps = new QComboBox;
    m_systemApps->addItems({"User apps", "All apps"});
    auto *listApps = makeButton("↻  List apps");
    m_installAppButton = makeButton("＋  Install APK", "primary");
    appActions->addWidget(m_appFilter, 1);
    appActions->addWidget(m_systemApps);
    appActions->addWidget(listApps);
    appActions->addWidget(m_installAppButton);
    appsLayout->addLayout(appActions);
    auto *installProgressRow = new QHBoxLayout;
    m_appInstallStatus = new QLabel;
    m_appInstallStatus->setObjectName("muted");
    m_appInstallStatus->setVisible(false);
    m_appInstallProgress = new QProgressBar;
    m_appInstallProgress->setRange(0, 0);
    m_appInstallProgress->setTextVisible(false);
    m_appInstallProgress->setMaximumWidth(150);
    m_appInstallProgress->setVisible(false);
    installProgressRow->addWidget(m_appInstallStatus, 1);
    installProgressRow->addWidget(m_appInstallProgress);
    appsLayout->addLayout(installProgressRow);
    m_appList = new QListWidget;
    m_appList->setSelectionMode(QAbstractItemView::SingleSelection);
    appsLayout->addWidget(m_appList, 1);
    auto *appManage = new QHBoxLayout;
    auto *launchApp = makeButton("▶  Launch");
    auto *forceStop = makeButton("■  Force stop");
    auto *clearData = makeButton("Clear app data");
    auto *uninstallApp = makeButton("Uninstall", "danger");
    appManage->addWidget(launchApp);
    appManage->addWidget(forceStop);
    appManage->addWidget(clearData);
    appManage->addWidget(uninstallApp);
    appManage->addStretch();
    appsLayout->addLayout(appManage);
    tabs->addTab(appsPage, "  Apps  ");

    QVBoxLayout *personalizeLayout = nullptr;
    auto *personalize = makeSection("Personalize", personalizeLayout);
    personalizeLayout->addWidget(new QLabel("Set an image on the connected device as its home or lock screen wallpaper."));
    auto *wallpaperRow = new QHBoxLayout;
    auto *wallpaperPath = new QLineEdit;
    wallpaperPath->setPlaceholderText("Choose a wallpaper image…");
    auto *browseWallpaper = makeButton("Browse");
    auto *setWallpaper = makeButton("Choose wallpaper on device", "primary");
    wallpaperRow->addWidget(wallpaperPath, 1);
    wallpaperRow->addWidget(browseWallpaper);
    personalizeLayout->addLayout(wallpaperRow);
    auto *wallpaperButtons = new QHBoxLayout;
    wallpaperButtons->addWidget(setWallpaper);
    wallpaperButtons->addStretch();
    personalizeLayout->addLayout(wallpaperButtons);
    auto *wallpaperTip = new QLabel("Android will open its wallpaper picker. Choose home screen, lock screen, or both there. Some manufacturers restrict lock-screen wallpaper changes.");
    wallpaperTip->setObjectName("muted");
    wallpaperTip->setWordWrap(true);
    personalizeLayout->addWidget(wallpaperTip);
    personalizeLayout->addSpacing(16);
    personalizeLayout->addWidget(new QLabel("Screen density (DPI)"));
    auto *densityRow = new QHBoxLayout;
    auto *densityInput = new QLineEdit;
    densityInput->setPlaceholderText("For example: 420");
    auto *setDensity = makeButton("Apply density");
    auto *resetDensity = makeButton("Reset to device default");
    densityRow->addWidget(densityInput);
    densityRow->addWidget(setDensity);
    densityRow->addWidget(resetDensity);
    densityRow->addStretch();
    personalizeLayout->addLayout(densityRow);
    personalizeLayout->addStretch();
    tabs->addTab(personalize, "  Personalize  ");

    QVBoxLayout *filesLayout = nullptr;
    auto *filesPage = makeSection("Files & screenshots", filesLayout);
    filesLayout->addWidget(new QLabel("Transfer files between your PC and the device. Remote paths use Android paths, such as /sdcard/Download."));
    auto *remoteRow = new QHBoxLayout;
    m_remotePath = new QLineEdit("/sdcard/Download");
    auto *remoteBrowse = makeButton("Browse remote");
    remoteRow->addWidget(new QLabel("Remote folder"));
    remoteRow->addWidget(m_remotePath, 1);
    remoteRow->addWidget(remoteBrowse);
    filesLayout->addLayout(remoteRow);
    auto *transferRow = new QHBoxLayout;
    auto *pushFile = makeButton("↑  PC → device", "primary");
    auto *pullFile = makeButton("↓  Device → PC");
    auto *captureScreen = makeButton("▣  Save screenshot");
    transferRow->addWidget(pushFile);
    transferRow->addWidget(pullFile);
    transferRow->addWidget(captureScreen);
    transferRow->addStretch();
    filesLayout->addLayout(transferRow);
    auto *fileTip = new QLabel("Pushing and pulling files uses ADB's original file-transfer commands; the device must be connected and authorized.");
    fileTip->setObjectName("muted");
    fileTip->setWordWrap(true);
    filesLayout->addWidget(fileTip);
    filesLayout->addStretch();
    tabs->addTab(filesPage, "  Files  ");

    QVBoxLayout *deviceLayout = nullptr;
    auto *devicePage = makeSection("Device controls", deviceLayout);
    auto *controlDesc = new QLabel("Perform standard device controls. Rebooting to the bootloader or recovery requires a compatible device and may not be available on every model.");
    controlDesc->setWordWrap(true);
    controlDesc->setObjectName("muted");
    deviceLayout->addWidget(controlDesc);
    auto *powerRow = new QHBoxLayout;
    auto *reboot = makeButton("↻  Reboot");
    auto *rebootRecovery = makeButton("Reboot recovery");
    auto *rebootBootloader = makeButton("Reboot bootloader");
    auto *powerOff = makeButton("Power off");
    powerRow->addWidget(reboot);
    powerRow->addWidget(rebootRecovery);
    powerRow->addWidget(rebootBootloader);
    powerRow->addWidget(powerOff);
    powerRow->addStretch();
    deviceLayout->addLayout(powerRow);
    auto *actionsRow = new QHBoxLayout;
    auto *sideload = makeButton("Install OTA (ADB sideload)");
    auto *adbTcpip = makeButton("Enable ADB over Wi-Fi");
    auto *connectWifi = makeButton("Connect to Wi-Fi device");
    auto *usbMode = makeButton("Return to USB mode");
    actionsRow->addWidget(sideload);
    actionsRow->addWidget(adbTcpip);
    actionsRow->addWidget(connectWifi);
    actionsRow->addWidget(usbMode);
    actionsRow->addStretch();
    deviceLayout->addLayout(actionsRow);
    auto *rootNote = new QLabel("Root-only commands and Magisk module management are in the Root & Magisk tab.");
    rootNote->setObjectName("muted");
    rootNote->setWordWrap(true);
    deviceLayout->addWidget(rootNote);
    deviceLayout->addStretch();
    tabs->addTab(devicePage, "  Device  ");

    QVBoxLayout *rootLayout = nullptr;
    auto *rootPage = makeSection("Root & Magisk", rootLayout);
    auto *rootInfo = new QLabel(
        "Root features require an already-rooted device, Magisk installed, and an approved superuser request. DroidDesk does not unlock bootloaders or root devices. Review each module before installing; changes may prevent the device from booting.");
    rootInfo->setObjectName("muted");
    rootInfo->setWordWrap(true);
    rootLayout->addWidget(rootInfo);

    auto *rootCheckRow = new QHBoxLayout;
    m_rootStatusLabel = new QLabel("Root status not checked.");
    m_rootStatusLabel->setObjectName("metric");
    m_rootStatusLabel->setWordWrap(true);
    auto *checkRoot = makeButton("Check root", "primary");
    rootCheckRow->addWidget(m_rootStatusLabel, 1);
    rootCheckRow->addWidget(checkRoot);
    rootLayout->addLayout(rootCheckRow);

    auto *rootTabs = new QTabWidget;
    rootLayout->addWidget(rootTabs, 1);
    QVBoxLayout *installedLayout = nullptr;
    auto *installedPage = makeSection("Installed Magisk modules", installedLayout);
    auto *moduleHeader = new QHBoxLayout;
    auto *refreshModules = makeButton("Refresh modules");
    auto *installModule = makeButton("Install module ZIP…", "primary");
    moduleHeader->addStretch();
    moduleHeader->addWidget(refreshModules);
    moduleHeader->addWidget(installModule);
    installedLayout->addLayout(moduleHeader);

    auto *moduleColumns = new QHBoxLayout;
    moduleColumns->setSpacing(0);
    auto *moduleListPane = new QWidget;
    auto *moduleListLayout = new QVBoxLayout(moduleListPane);
    moduleListLayout->setContentsMargins(0, 0, 12, 0);
    moduleListLayout->setSpacing(14);
    m_magiskModules = new QListWidget;
    m_magiskModules->setSelectionMode(QAbstractItemView::SingleSelection);
    auto *modulePlaceholder = new QListWidgetItem("Refresh to list installed modules.", m_magiskModules);
    modulePlaceholder->setFlags(Qt::NoItemFlags);
    moduleListLayout->addWidget(m_magiskModules, 1);

    auto *moduleActions = new QHBoxLayout;
    auto *enableModule = makeButton("Enable selected");
    auto *disableModule = makeButton("Disable selected");
    auto *removeModule = makeButton("Remove selected", "danger");
    moduleActions->addWidget(enableModule);
    moduleActions->addWidget(disableModule);
    moduleActions->addWidget(removeModule);
    moduleActions->addStretch();
    moduleListLayout->addLayout(moduleActions);

    auto *moduleLogPane = new QWidget;
    auto *moduleLogLayout = new QVBoxLayout(moduleLogPane);
    moduleLogLayout->setContentsMargins(12, 0, 0, 0);
    moduleLogLayout->setSpacing(14);
    auto *moduleInstallHeader = new QHBoxLayout;
    auto *moduleInstallLabel = new QLabel("Installation log");
    moduleInstallLabel->setStyleSheet("font-size: 11pt; font-weight: 600;");
    m_moduleInstallStatus = new QLabel("Ready.");
    m_moduleInstallStatus->setObjectName("muted");
    m_moduleInstallProgress = new QProgressBar;
    m_moduleInstallProgress->setRange(0, 0);
    m_moduleInstallProgress->setTextVisible(false);
    m_moduleInstallProgress->setMaximumWidth(130);
    m_moduleInstallProgress->setVisible(false);
    auto *clearModuleInstallLog = makeButton("Clear");
    moduleInstallHeader->addWidget(moduleInstallLabel);
    moduleInstallHeader->addStretch();
    moduleInstallHeader->addWidget(clearModuleInstallLog);
    moduleLogLayout->addLayout(moduleInstallHeader);
    auto *moduleInstallStatusRow = new QHBoxLayout;
    moduleInstallStatusRow->addWidget(m_moduleInstallStatus, 1);
    moduleInstallStatusRow->addWidget(m_moduleInstallProgress);
    moduleLogLayout->addLayout(moduleInstallStatusRow);
    m_moduleInstallLog = new QPlainTextEdit;
    m_moduleInstallLog->setReadOnly(true);
    m_moduleInstallLog->setMaximumBlockCount(250);
    m_moduleInstallLog->setPlaceholderText("Magisk module copy and installer output will appear here.");
    moduleLogLayout->addWidget(m_moduleInstallLog, 1);

    moduleColumns->addWidget(moduleListPane, 1);
    moduleColumns->addWidget(moduleLogPane, 1);
    installedLayout->addLayout(moduleColumns, 1);
    rootTabs->addTab(installedPage, "Installed");

    QVBoxLayout *selinuxLayout = nullptr;
    auto *selinuxPage = makeSection("SELinux status and mode", selinuxLayout);
    auto *selinuxInfo = new QLabel(
        "SELinux protects Android system services. Permissive mode weakens device security and may be reset at reboot. Use only when you understand the impact.");
    selinuxInfo->setObjectName("muted");
    selinuxInfo->setWordWrap(true);
    selinuxLayout->addWidget(selinuxInfo);
    m_selinuxStatusLabel = new QLabel("SELinux status not checked.");
    m_selinuxStatusLabel->setObjectName("metric");
    m_selinuxStatusLabel->setWordWrap(true);
    selinuxLayout->addWidget(m_selinuxStatusLabel);
    auto *selinuxActions = new QHBoxLayout;
    m_selinuxCheckButton = makeButton("Check status");
    m_selinuxEnforcingButton = makeButton("Set Enforcing");
    m_selinuxPermissiveButton = makeButton("Set Permissive", "danger");
    selinuxActions->addWidget(m_selinuxCheckButton);
    selinuxActions->addWidget(m_selinuxEnforcingButton);
    selinuxActions->addWidget(m_selinuxPermissiveButton);
    selinuxActions->addStretch();
    selinuxLayout->addLayout(selinuxActions);
    auto *selinuxNote = new QLabel(
        "Changing SELinux mode requires working root access. DroidDesk verifies the reported mode after each request; a device policy may reject or override it.");
    selinuxNote->setObjectName("muted");
    selinuxNote->setWordWrap(true);
    selinuxLayout->addWidget(selinuxNote);
    selinuxLayout->addStretch();
    rootTabs->addTab(selinuxPage, "SELinux");

    QVBoxLayout *storeLayout = nullptr;
    auto *storePage = makeSection("GitHub module catalog", storeLayout);
    auto *storeInfo = new QLabel(
        "Browse the Magisk-Modules-Alt-Repo catalog. Its ZIP links are GitHub source archives, not verified Magisk installer packages; downloading them does not install a module.");
    storeInfo->setObjectName("muted");
    storeInfo->setWordWrap(true);
    storeLayout->addWidget(storeInfo);
    auto *catalogControls = new QHBoxLayout;
    m_moduleSearch = new QLineEdit;
    m_moduleSearch->setPlaceholderText("Filter modules by ID, name, author, or description");
    auto *refreshCatalog = makeButton("Load / refresh catalog", "primary");
    catalogControls->addWidget(m_moduleSearch, 1);
    catalogControls->addWidget(refreshCatalog);
    storeLayout->addLayout(catalogControls);
    m_moduleStoreStatus = new QLabel("Catalog not loaded.");
    m_moduleStoreStatus->setObjectName("muted");
    storeLayout->addWidget(m_moduleStoreStatus);
    auto *storeContent = new QHBoxLayout;
    m_moduleCatalog = new QListWidget;
    m_moduleCatalog->setSelectionMode(QAbstractItemView::SingleSelection);
    m_moduleDetails = new QPlainTextEdit;
    m_moduleDetails->setReadOnly(true);
    m_moduleDetails->setPlaceholderText("Select a catalog entry to load its module metadata.");
    auto *storeDetailsLayout = new QVBoxLayout;
    storeDetailsLayout->addWidget(m_moduleDetails, 1);
    m_moduleDownload = makeButton("Download source ZIP…");
    m_moduleDownload->setEnabled(false);
    storeDetailsLayout->addWidget(m_moduleDownload, 0, Qt::AlignRight);
    storeContent->addWidget(m_moduleCatalog, 1);
    storeContent->addLayout(storeDetailsLayout, 1);
    storeLayout->addLayout(storeContent, 1);
    rootTabs->addTab(storePage, "Module store");

    QVBoxLayout *rootShellLayout = nullptr;
    auto *rootShellPage = makeSection("Root shell", rootShellLayout);
    auto *rootShellInfo = new QLabel("Commands run through `su -c`. Use only commands you understand.");
    rootShellInfo->setObjectName("muted");
    rootShellLayout->addWidget(rootShellInfo);
    auto *rootShellRow = new QHBoxLayout;
    m_rootShellInput = new QLineEdit;
    m_rootShellInput->setPlaceholderText("For example: id");
    auto *runRootShell = makeButton("Run as root", "primary");
    rootShellRow->addWidget(m_rootShellInput, 1);
    rootShellRow->addWidget(runRootShell);
    rootShellLayout->addLayout(rootShellRow);
    m_rootOutput = new QPlainTextEdit;
    m_rootOutput->setReadOnly(true);
    m_rootOutput->setMaximumBlockCount(1000);
    m_rootOutput->setPlaceholderText("Root command output appears here.");
    m_rootOutput->setMaximumHeight(150);
    rootShellLayout->addWidget(m_rootOutput, 1);
    rootShellLayout->addStretch();
    rootTabs->addTab(rootShellPage, "Root shell");
    tabs->addTab(rootPage, "  Root & Magisk  ");

    m_networkManager = new QNetworkAccessManager(this);

    QVBoxLayout *shellLayout = nullptr;
    auto *shellPage = makeSection("Shell & log", shellLayout);
    auto *shellInfo = new QLabel("Run ADB shell commands on your selected device. Commands are sent as text, not through a local command interpreter. Use only commands you understand.");
    shellInfo->setObjectName("muted");
    shellInfo->setWordWrap(true);
    shellLayout->addWidget(shellInfo);
    auto *shellRow = new QHBoxLayout;
    m_shellInput = new QLineEdit;
    m_shellInput->setPlaceholderText("For example: pm list packages");
    auto *runShell = makeButton("Run command", "primary");
    shellRow->addWidget(m_shellInput, 1);
    shellRow->addWidget(runShell);
    shellLayout->addLayout(shellRow);
    m_output = new QPlainTextEdit;
    m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(2000);
    m_output->setPlaceholderText("ADB actions and device output appear here.");
    shellLayout->addWidget(m_output, 1);
    auto *logActions = new QHBoxLayout;
    auto *clearLog = makeButton("Clear log");
    auto *copyLog = makeButton("Copy log");
    auto *captureLogcat = makeButton("Capture recent logcat");
    logActions->addWidget(clearLog);
    logActions->addWidget(copyLog);
    logActions->addWidget(captureLogcat);
    logActions->addStretch();
    shellLayout->addLayout(logActions);
    tabs->addTab(shellPage, "  Shell & log  ");

    QVBoxLayout *logcatLayout = nullptr;
    auto *logcatPage = makeSection("Live Logcat", logcatLayout);
    auto *logcatInfo = new QLabel("Stream Android system logs from the selected device. Filtering happens on your PC; sensitive app and device data may appear in logs.");
    logcatInfo->setObjectName("muted");
    logcatInfo->setWordWrap(true);
    logcatLayout->addWidget(logcatInfo);
    auto *logcatControls = new QHBoxLayout;
    m_logcatToggle = makeButton("Start logcat", "primary");
    m_logcatLevel = new QComboBox;
    m_logcatLevel->addItems({"Verbose", "Debug", "Info", "Warning", "Error", "Fatal"});
    m_logcatFilter = new QLineEdit;
    m_logcatFilter->setPlaceholderText("Filter log lines (optional)…");
    auto *clearLogcat = makeButton("Clear");
    auto *saveLogcat = makeButton("Save log…");
    logcatControls->addWidget(m_logcatToggle);
    logcatControls->addWidget(m_logcatLevel);
    logcatControls->addWidget(m_logcatFilter, 1);
    logcatControls->addWidget(clearLogcat);
    logcatControls->addWidget(saveLogcat);
    logcatLayout->addLayout(logcatControls);
    m_logcatOutput = new QPlainTextEdit;
    m_logcatOutput->setReadOnly(true);
    m_logcatOutput->setMaximumBlockCount(3000);
    m_logcatOutput->setPlaceholderText("Start logcat to stream device messages here.");
    logcatLayout->addWidget(m_logcatOutput, 1);
    m_logcatFlushTimer = new QTimer(this);
    m_logcatFlushTimer->setSingleShot(true);
    m_logcatFlushTimer->setInterval(100);
    connect(m_logcatFlushTimer, &QTimer::timeout, this, [this] {
        if (m_pendingLogcatLines.isEmpty() && m_droppedLogcatLines == 0)
            return;
        QStringList batch;
        if (m_droppedLogcatLines > 0) {
            batch << QStringLiteral("[DroidDesk dropped %1 log lines because the display could not keep up]")
                         .arg(m_droppedLogcatLines);
            m_droppedLogcatLines = 0;
        }
        batch.append(m_pendingLogcatLines);
        m_pendingLogcatLines.clear();
        m_logcatOutput->appendPlainText(batch.join(QLatin1Char('\n')));
    });
    tabs->addTab(logcatPage, "  Logcat  ");

    setCentralWidget(central);
    statusBar()->showMessage("Ready");

    const auto updateScrcpyOptions = [this, scrcpyModeInfo] {
        const QString mode = m_scrcpyMode->currentData().toString();
        const bool otg = mode == QStringLiteral("otg");
        const bool virtualDisplay = mode == QStringLiteral("virtual");
        m_scrcpyVirtualSize->setEnabled(virtualDisplay);
        m_scrcpyFlexDisplay->setEnabled(virtualDisplay);
        m_scrcpyMaxSize->setEnabled(!otg);
        m_scrcpyMaxFps->setEnabled(!otg);
        m_scrcpyAudio->setEnabled(!otg);
        m_scrcpyStayAwake->setEnabled(!otg);
        m_scrcpyTurnScreenOff->setEnabled(!otg);
        m_scrcpyAlwaysOnTop->setEnabled(!otg);
        m_scrcpyShowTouches->setEnabled(!otg);
        m_scrcpyOtgGamepad->setEnabled(otg);
        m_scrcpyRecording->setEnabled(!otg);
        const bool recording = !otg && m_scrcpyRecording->isChecked();
        m_scrcpyRecordingPath->setEnabled(recording);
        m_scrcpyBrowseRecording->setEnabled(recording);
        if (otg) {
            scrcpyModeInfo->setText(
                "OTG simulates a physical USB keyboard and mouse (and optional gamepad). It works over USB only, does not require USB debugging, and has no video, audio, virtual display, or recording.");
        } else if (virtualDisplay) {
            scrcpyModeInfo->setText(
                "Virtual display creates a separate Android display and destroys it when scrcpy closes. Flex mode resizes the virtual display with its window; Android 10 or later is generally required.");
        } else {
            scrcpyModeInfo->setText(
                "Mirror the selected device screen with audio and control. Recording captures the scrcpy stream to an MP4 or MKV file on this PC.");
        }
    };
    connect(m_scrcpyMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [updateScrcpyOptions] { updateScrcpyOptions(); });
    connect(m_scrcpyRecording, &QCheckBox::toggled, this,
            [updateScrcpyOptions] { updateScrcpyOptions(); });
    connect(m_scrcpyBrowseRecording, &QPushButton::clicked, this, [this] {
        QString initialPath = m_scrcpyRecordingPath->text().trimmed();
        if (initialPath.isEmpty())
            initialPath = QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
                              .filePath("DroidDesk-recording.mp4");
        const QString path = QFileDialog::getSaveFileName(
            this, "Save scrcpy recording", initialPath,
            "MP4 video (*.mp4);;Matroska video (*.mkv)");
        if (!path.isEmpty())
            m_scrcpyRecordingPath->setText(path);
    });
    connect(m_scrcpyStartButton, &QPushButton::clicked, this, &MainWindow::launchScrcpy);
    updateScrcpyOptions();

    connect(refreshButton, &QPushButton::clicked, this, [this] { refreshDevices(); });
    connect(clearModuleInstallLog, &QPushButton::clicked, m_moduleInstallLog,
            &QPlainTextEdit::clear);
    connect(m_devices, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        if (m_selinuxProcess) {
            QProcess *process = m_selinuxProcess;
            m_selinuxProcess = nullptr;
            disconnect(process, nullptr, this, nullptr);
            if (process->state() != QProcess::NotRunning)
                process->kill();
            process->deleteLater();
            m_selinuxCheckButton->setEnabled(true);
            m_selinuxEnforcingButton->setEnabled(true);
            m_selinuxPermissiveButton->setEnabled(true);
        }
        m_selinuxStatusLabel->setText("SELinux status not checked for the selected device.");
        if (m_rootCheckProcess) {
            QProcess *process = m_rootCheckProcess;
            m_rootCheckProcess = nullptr;
            disconnect(process, nullptr, this, nullptr);
            if (process->state() != QProcess::NotRunning)
                process->kill();
            process->deleteLater();
        }
        m_rootStatusLabel->setText("Root status not checked for the selected device.");
        if (m_logcatProcess && m_logcatProcess->state() != QProcess::NotRunning)
            stopLogcat();
        refreshDeviceDetails();
        if (m_appListProcess) {
            QProcess *process = m_appListProcess;
            m_appListProcess = nullptr;
            disconnect(process, nullptr, this, nullptr);
            if (process->state() != QProcess::NotRunning)
                process->kill();
            process->deleteLater();
        }
        m_appList->clear();
        m_magiskModules->clear();
        auto *modulePlaceholder = new QListWidgetItem("Refresh modules for the selected device.", m_magiskModules);
        modulePlaceholder->setFlags(Qt::NoItemFlags);
    });
    connect(settingsButton, &QPushButton::clicked, this, [this] {
        QSettings settings;
        const QString path = QFileDialog::getOpenFileName(
            this, "Select Android Debug Bridge", settings.value("adb/path").toString(),
            "ADB executable (adb.exe);;All files (*.*)");
        if (path.isEmpty())
            return;
        settings.setValue("adb/path", path);
        refreshDevices();
    });
    connect(m_systemApps, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refreshApps(); });
    connect(listApps, &QPushButton::clicked, this, &MainWindow::refreshApps);
    connect(m_appFilter, &QLineEdit::textChanged, m_appList, [this] {
        const QString filter = m_appFilter->text().trimmed();
        for (int i = 0; i < m_appList->count(); ++i)
            m_appList->item(i)->setHidden(!m_appList->item(i)->text().contains(filter, Qt::CaseInsensitive));
    });
    connect(m_installAppButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "Install Android package", {}, "Android packages (*.apk);;All files (*.*)");
        if (path.isEmpty())
            return;
        installApk(path);
    });

    const auto selectedPackage = [this]() -> QString {
        if (m_appList->selectedItems().isEmpty() ||
            m_appList->selectedItems().first()->text().startsWith('('))
            return {};
        return m_appList->selectedItems().first()->text();
    };
    connect(launchApp, &QPushButton::clicked, this, [this, selectedPackage] {
        const QString package = selectedPackage();
        if (package.isEmpty()) {
            QMessageBox::information(this, "Select an app", "Choose an app first.");
            return;
        }
        const auto result = runDevice({"shell", "monkey", "-p", package, "-c", "android.intent.category.LAUNCHER", "1"});
        logCommand("Launch " + package, result);
        showResult("Launch request sent.", result);
    });
    connect(forceStop, &QPushButton::clicked, this, [this, selectedPackage] {
        const QString package = selectedPackage();
        if (package.isEmpty())
            return;
        const auto result = runDevice({"shell", "am", "force-stop", package});
        logCommand("Force stop " + package, result);
        showResult("App stopped.", result);
    });
    connect(clearData, &QPushButton::clicked, this, [this, selectedPackage] {
        const QString package = selectedPackage();
        if (package.isEmpty())
            return;
        if (QMessageBox::question(this, "Clear app data", "Delete all local data for " + package + "? This cannot be undone.") != QMessageBox::Yes)
            return;
        const auto result = runDevice({"shell", "pm", "clear", package});
        logCommand("Clear data " + package, result);
        showResult("App data cleared.", result);
    });
    connect(uninstallApp, &QPushButton::clicked, this, [this, selectedPackage] {
        const QString package = selectedPackage();
        if (package.isEmpty())
            return;
        if (QMessageBox::question(this, "Uninstall app", "Uninstall " + package + " for the current device user?") != QMessageBox::Yes)
            return;
        QStringList args{"shell", "pm", "uninstall", "-k", "--user", "0", package};
        if (m_systemApps->currentIndex() == 1) {
            if (QMessageBox::warning(this, "System app", "This is the all-apps list. Removing a system app may break device features. Continue?", QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
                return;
        }
        const auto result = runDevice(args);
        logCommand("Uninstall " + package, result);
        showResult("Package removal request completed.", result);
        if (result.exitCode == 0)
            refreshApps();
    });

    connect(browseWallpaper, &QPushButton::clicked, this, [wallpaperPath, this] {
        const QString path = QFileDialog::getOpenFileName(this, "Choose wallpaper", {}, "Images (*.png *.jpg *.jpeg *.webp);;All files (*.*)");
        if (!path.isEmpty())
            wallpaperPath->setText(path);
    });
    const auto applyWallpaper = [this, wallpaperPath] {
        const QString path = wallpaperPath->text().trimmed();
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            QMessageBox::warning(this, "Wallpaper", "Choose an existing image file first.");
            return;
        }
        const QString suffix = QFileInfo(path).suffix();
        const QString remote = "/sdcard/Pictures/droiddesk_wallpaper" +
                               (suffix.isEmpty() ? QStringLiteral(".png") : QStringLiteral(".") + suffix);
        const auto makeDirectory = runDevice({"shell", "mkdir", "-p", "/sdcard/Pictures"});
        if (!makeDirectory.started || makeDirectory.timedOut || makeDirectory.exitCode != 0) {
            logCommand("Prepare wallpaper folder", makeDirectory);
            showResult({}, makeDirectory);
            return;
        }
        auto pushResult = runDevice({"push", path, remote}, 120000);
        logCommand("Transfer wallpaper", pushResult);
        if (!pushResult.started || pushResult.timedOut || pushResult.exitCode != 0) {
            showResult({}, pushResult);
            return;
        }
        const QString extension = suffix.toLower();
        const QString mimeType = extension == "jpg" || extension == "jpeg"
                                     ? QStringLiteral("image/jpeg")
                                     : extension == "webp"
                                           ? QStringLiteral("image/webp")
                                           : QStringLiteral("image/png");
        const QString imageUri = QStringLiteral("file://") + remote;
        const auto result = runDevice({"shell", "am", "start",
                                       "-a", "android.intent.action.ATTACH_DATA",
                                       "-d", imageUri, "-t", mimeType,
                                       "--es", "mimeType", mimeType});
        logCommand("Open Android wallpaper picker", result);
        if (!result.started || result.timedOut || result.exitCode != 0) {
            showResult({}, result);
            return;
        }
        showResult("Choose the wallpaper target and confirm in the Android screen that opened.",
                   {0, {}, {}, true, false});
    };
    connect(setWallpaper, &QPushButton::clicked, this, applyWallpaper);

    connect(setDensity, &QPushButton::clicked, this, [this, densityInput] {
        bool ok = false;
        const int dpi = densityInput->text().toInt(&ok);
        if (!ok || dpi < 80 || dpi > 1000) {
            QMessageBox::warning(this, "Screen density", "Enter a DPI value between 80 and 1000.");
            return;
        }
        const auto result = runDevice({"shell", "wm", "density", QString::number(dpi)});
        logCommand("Set screen density", result);
        showResult("Screen density updated.", result);
        refreshDeviceDetails();
    });
    connect(connectWifi, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString address = QInputDialog::getText(this, "Connect to ADB over Wi-Fi",
                                                       "Device IP address and port (for example 192.168.1.25:5555):",
                                                       QLineEdit::Normal, {}, &ok).trimmed();
        static const QRegularExpression endpoint(R"(^[A-Za-z0-9.-]+:\d{1,5}$)");
        if (!ok || address.isEmpty())
            return;
        if (!endpoint.match(address).hasMatch()) {
            QMessageBox::warning(this, "Invalid address", "Enter an address in the form host:port.");
            return;
        }
        const auto result = runAdb({"connect", address});
        logCommand("Connect to Wi-Fi device", result);
        showResult("ADB connection request sent.", result);
        refreshDevices();
    });
    connect(resetDensity, &QPushButton::clicked, this, [this] {
        const auto result = runDevice({"shell", "wm", "density", "reset"});
        logCommand("Reset screen density", result);
        showResult("Screen density reset.", result);
        refreshDeviceDetails();
    });

    connect(pushFile, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "Choose file to send");
        if (path.isEmpty())
            return;
        const auto result = runDevice({"push", path, m_remotePath->text().trimmed()}, 120000);
        logCommand("Push file", result);
        showResult("File transferred to device.", result);
    });
    connect(pullFile, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString remote = QInputDialog::getText(this, "Pull file", "Device file path:", QLineEdit::Normal,
                                                      m_remotePath->text().trimmed() + "/", &ok);
        if (!ok || remote.trimmed().isEmpty())
            return;
        const QString path = QFileDialog::getSaveFileName(this, "Save file on PC", QFileInfo(remote).fileName());
        if (path.isEmpty())
            return;
        const auto result = runDevice({"pull", remote.trimmed(), path}, 120000);
        logCommand("Pull file", result);
        showResult("File transferred to PC.", result);
    });
    connect(captureScreen, &QPushButton::clicked, this, [this] {
        QString path = QFileDialog::getSaveFileName(this, "Save device screenshot",
                                                     QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) +
                                                         "/android-screenshot.png",
                                                     "PNG images (*.png)");
        if (path.isEmpty())
            return;
        if (!path.endsWith(".png", Qt::CaseInsensitive))
            path += ".png";
        const QString remote = "/sdcard/droiddesk_screenshot.png";
        auto capture = runDevice({"shell", "screencap", "-p", remote});
        logCommand("Capture screenshot", capture);
        if (!capture.started || capture.timedOut || capture.exitCode != 0) {
            showResult({}, capture);
            return;
        }
        const auto result = runDevice({"pull", remote, path}, 30000);
        logCommand("Save screenshot", result);
        showResult("Screenshot saved to " + path, result);
    });
    connect(quickScreenshot, &QPushButton::clicked, captureScreen, &QPushButton::click);

    connect(remoteBrowse, &QPushButton::clicked, this, [this] {
        const auto result = runDevice({"shell", "ls", "-1", m_remotePath->text().trimmed()});
        logCommand("List remote folder", result);
        if (!result.started || result.timedOut || result.exitCode != 0) {
            showResult({}, result);
            return;
        }
        QMessageBox::information(this, "Remote folder contents",
                                 QString::fromLocal8Bit(result.output).trimmed().isEmpty()
                                     ? "(empty)"
                                     : QString::fromLocal8Bit(result.output).trimmed());
    });

    const auto rebootDevice = [this](const QString &mode) {
        if (QMessageBox::question(this, "Confirm reboot", "Reboot the selected device" +
                                    (mode.isEmpty() ? QString() : " to " + mode) + "?") != QMessageBox::Yes)
            return;
        QStringList args{"reboot"};
        if (!mode.isEmpty())
            args << mode;
        const auto result = runDevice(args);
        logCommand("Reboot " + (mode.isEmpty() ? QStringLiteral("system") : mode), result);
        showResult("Reboot command sent.", result);
    };
    connect(reboot, &QPushButton::clicked, this, [rebootDevice] { rebootDevice({}); });
    connect(rebootRecovery, &QPushButton::clicked, this, [rebootDevice] { rebootDevice("recovery"); });
    connect(rebootBootloader, &QPushButton::clicked, this, [rebootDevice] { rebootDevice("bootloader"); });
    connect(quickReboot, &QPushButton::clicked, reboot, &QPushButton::click);
    connect(quickRecovery, &QPushButton::clicked, rebootRecovery, &QPushButton::click);
    connect(quickBootloader, &QPushButton::clicked, rebootBootloader, &QPushButton::click);
    connect(powerOff, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, "Power off", "Power off the selected device?") != QMessageBox::Yes)
            return;
        const auto result = runDevice({"shell", "reboot", "-p"});
        logCommand("Power off", result);
        showResult("Power-off command sent.", result);
    });
    connect(sideload, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "Choose OTA package", {}, "ZIP archives (*.zip);;All files (*.*)");
        if (path.isEmpty())
            return;
        if (QMessageBox::warning(this, "ADB sideload", "Your device must already be in recovery sideload mode. Only install an OTA intended for this exact device model.",
                                 QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok)
            return;
        const auto result = runDevice({"sideload", path}, 600000);
        logCommand("ADB sideload", result);
        showResult("Sideload command completed.", result);
    });
    connect(adbTcpip, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const int port = QInputDialog::getInt(this, "ADB over Wi-Fi", "TCP port:", 5555, 1024, 65535, 1, &ok);
        if (!ok)
            return;
        const auto result = runDevice({"tcpip", QString::number(port)});
        logCommand("Enable ADB over TCP/IP", result);
        showResult("ADB over Wi-Fi enabled on port " + QString::number(port) + ".", result);
        refreshDeviceDetails();
    });
    connect(usbMode, &QPushButton::clicked, this, [this] {
        const auto result = runDevice({"usb"});
        logCommand("Return to USB mode", result);
        showResult("ADB returned to USB mode.", result);
    });
    connect(runShell, &QPushButton::clicked, this, &MainWindow::executeShellCommand);
    connect(m_shellInput, &QLineEdit::returnPressed, this, &MainWindow::executeShellCommand);
    connect(runRootShell, &QPushButton::clicked, this, [this] {
        const QString command = m_rootShellInput->text().trimmed();
        if (command.isEmpty())
            return;
        if (selectedSerial().isEmpty()) {
            QMessageBox::information(this, "Root shell", "Connect and select an authorized device first.");
            return;
        }
        m_rootOutput->appendPlainText(QStringLiteral("$ ") + command);
        const auto result = runDevice({"shell", "su", "-c", shellQuote(command)}, 120000);
        m_rootOutput->appendPlainText(QString::fromLocal8Bit(result.output).trimmed());
        if (!result.error.isEmpty())
            m_rootOutput->appendPlainText(QStringLiteral("stderr: ") + QString::fromLocal8Bit(result.error).trimmed());
        logCommand("Root shell command", result);
        if (!result.started || result.timedOut || result.exitCode != 0)
            showResult({}, result);
        m_rootShellInput->clear();
    });
    connect(m_rootShellInput, &QLineEdit::returnPressed, runRootShell, &QPushButton::click);
    connect(checkRoot, &QPushButton::clicked, this, &MainWindow::checkRootStatus);
    connect(refreshModules, &QPushButton::clicked, this, &MainWindow::refreshMagiskModules);
    connect(installModule, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, "Choose Magisk module", {}, "Magisk module archives (*.zip)");
        if (!path.isEmpty())
            installMagiskModule(path);
    });
    connect(enableModule, &QPushButton::clicked, this, [this] { setMagiskModuleEnabled(true); });
    connect(disableModule, &QPushButton::clicked, this, [this] { setMagiskModuleEnabled(false); });
    connect(removeModule, &QPushButton::clicked, this, &MainWindow::removeMagiskModule);
    connect(refreshCatalog, &QPushButton::clicked, this, &MainWindow::loadModuleCatalog);
    connect(m_selinuxCheckButton, &QPushButton::clicked, this, [this] {
        runSelinuxCommand(false, true);
    });
    connect(m_selinuxEnforcingButton, &QPushButton::clicked, this, [this] {
        runSelinuxCommand(true, true);
    });
    connect(m_selinuxPermissiveButton, &QPushButton::clicked, this, [this] {
        runSelinuxCommand(true, false);
    });
    connect(rootTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1)
            runSelinuxCommand(false, true);
        else if (index == 2 && m_catalogEntries.isEmpty())
            loadModuleCatalog();
    });
    connect(m_moduleSearch, &QLineEdit::textChanged, this, &MainWindow::filterModuleCatalog);
    connect(m_moduleCatalog, &QListWidget::itemSelectionChanged, this, [this] {
        if (m_moduleCatalog->currentItem())
            showModuleDetails(m_moduleCatalog->currentItem());
    });
    connect(m_moduleDownload, &QPushButton::clicked, this, &MainWindow::downloadSelectedModule);
    connect(clearLog, &QPushButton::clicked, m_output, &QPlainTextEdit::clear);
    connect(copyLog, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_output->toPlainText());
        statusBar()->showMessage("Log copied.", 3000);
    });
    connect(captureLogcat, &QPushButton::clicked, this, [this] {
        if (selectedSerial().isEmpty()) {
            QMessageBox::information(this, "Logcat", "Connect and select an authorized device first.");
            return;
        }
        auto *process = new QProcess(this);
        process->setProgram(adbExecutable());
        process->setArguments({"-s", selectedSerial(), "logcat", "-d", "-t", "500"});
        process->setProcessChannelMode(QProcess::SeparateChannels);
        auto *timeout = new QTimer(process);
        timeout->setSingleShot(true);
        timeout->start(60000);
        connect(timeout, &QTimer::timeout, process, [process] {
            if (process->state() != QProcess::NotRunning) {
                process->setProperty("timedOut", true);
                process->kill();
            }
        });
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
            const QByteArray output = process->readAllStandardOutput();
            const QByteArray error = process->readAllStandardError();
            const bool started = process->error() != QProcess::FailedToStart;
            const bool timedOut = process->property("timedOut").toBool();
            logCommand("Recent device logcat", {exitCode, output, error, started, timedOut});
            process->deleteLater();
            if (!started || timedOut || exitCode != 0 || exitStatus != QProcess::NormalExit) {
                const CommandResult result{exitCode, output, error, started, timedOut};
                if (isDeviceDisconnected(result))
                    handleDeviceDisconnected(result);
                showResult({}, {exitCode, output, error, started, timedOut});
                return;
            }
            m_pendingLogcatLines.clear();
            m_droppedLogcatLines = 0;
            m_logcatBuffer.clear();
            m_logcatOutput->setPlainText(QString::fromLocal8Bit(output));
            statusBar()->showMessage("Recent device logcat captured in the Logcat tab.", 5000);
        });
        connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
                return;
            const QString message = process->errorString();
            process->deleteLater();
            QMessageBox::warning(this, "Logcat", "Could not start ADB:\n" + message);
        });
        process->start();
        statusBar()->showMessage("Capturing recent device logcat…");
    });
    connect(m_logcatToggle, &QPushButton::clicked, this, [this] {
        if (m_logcatProcess && m_logcatProcess->state() != QProcess::NotRunning) {
            stopLogcat();
        } else {
            startLogcat();
        }
    });
    connect(clearLogcat, &QPushButton::clicked, this, [this] {
        m_logcatOutput->clear();
        m_pendingLogcatLines.clear();
        m_droppedLogcatLines = 0;
    });
    connect(saveLogcat, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, "Save Logcat output",
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/android-logcat.txt",
            "Text files (*.txt);;Log files (*.log);;All files (*.*)");
        if (path.isEmpty())
            return;
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text) ||
            file.write(m_logcatOutput->toPlainText().toUtf8()) < 0 ||
            !file.commit()) {
            QMessageBox::warning(this, "Save Logcat", "Could not save the log file:\n" + file.errorString());
            return;
        }
        statusBar()->showMessage("Logcat saved to " + path, 5000);
    });
    connect(quickClipboard, &QPushButton::clicked, this, [this] {
        const QString text = QStringLiteral("Device: %1\nAndroid: %2\nSerial: %3\nDisplay: %4\nBattery: %5")
                                 .arg(m_deviceNameLabel->text(), m_androidLabel->text(), m_serialLabel->text(),
                                      m_resolutionLabel->text(), m_batteryLabel->text());
        QApplication::clipboard()->setText(text);
        statusBar()->showMessage("Device info copied.", 3000);
    });
}

void MainWindow::refreshDevices(bool periodic)
{
    if (m_deviceScan && m_deviceScan->state() != QProcess::NotRunning)
        return;

    const QString previous = selectedSerial();
    if (!periodic)
        m_connectionLabel->setText("Searching for ADB devices…");
    auto *scan = new QProcess(this);
    m_deviceScan = scan;
    scan->setProgram(adbExecutable());
    scan->setArguments({"devices", "-l"});
    connect(scan, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, scan, previous, periodic](int exitCode, QProcess::ExitStatus exitStatus) {
        const QByteArray output = scan->readAllStandardOutput();
        const QByteArray error = scan->readAllStandardError();
        const bool started = scan->error() != QProcess::FailedToStart;
        if (!periodic)
            logCommand("Refresh devices", {exitCode, output, error, started,
                                           exitStatus == QProcess::CrashExit});
        scan->deleteLater();
        if (m_deviceScan == scan)
            m_deviceScan = nullptr;
        if (!started) {
            if (!periodic || !m_deviceSnapshotInitialized)
                m_connectionLabel->setText("ADB not found — install Android SDK Platform-Tools or use ADB settings to select adb.exe.");
            return;
        }
        if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
            if (!periodic || !m_deviceSnapshotInitialized)
                m_connectionLabel->setText("ADB could not complete device discovery. Check the ADB log for details.");
            if (periodic)
                logCommand("Poll ADB device status", {exitCode, output, error, started, false});
            return;
        }

        int connectedCount = 0;
        QMap<QString, QString> currentStates;
        QVector<QStringList> deviceRows;
        const QString outputText = QString::fromLocal8Bit(output);
        for (const QString &line : outputText.split(QLatin1Char('\n'))) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith("List of devices"))
                continue;
            const QStringList parts = trimmed.split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
            if (parts.size() < 2)
                continue;
            const QString serial = parts.at(0);
            const QString state = parts.at(1);
            currentStates.insert(serial, state);
            QString label = serial;
            const int modelAt = trimmed.indexOf("model:");
            if (modelAt >= 0) {
                const QString model = trimmed.mid(modelAt + 6).split(QRegularExpression("\\s+")).first();
                label = model + "  ·  " + serial;
            }
            if (state == "device") {
                ++connectedCount;
            }
            deviceRows.append({label, serial, state});
        }
        QStringList addedDevices;
        QStringList changedDevices;
        QStringList removedDevices;
        if (m_deviceSnapshotInitialized) {
            for (auto it = currentStates.cbegin(); it != currentStates.cend(); ++it) {
                if (!m_deviceStates.contains(it.key())) {
                    addedDevices.append(it.value() == QStringLiteral("device")
                                            ? QStringLiteral("connected: ") + it.key()
                                            : QStringLiteral("detected (%1): %2").arg(it.value(), it.key()));
                } else if (m_deviceStates.value(it.key()) != it.value()) {
                    changedDevices.append(QStringLiteral("%1 (%2 → %3)")
                        .arg(it.key(), m_deviceStates.value(it.key()), it.value()));
                }
            }
            for (auto it = m_deviceStates.cbegin(); it != m_deviceStates.cend(); ++it) {
                if (!currentStates.contains(it.key()))
                    removedDevices.append(it.key());
            }
        }
        const bool deviceStateChanged = !addedDevices.isEmpty() ||
                                        !changedDevices.isEmpty() ||
                                        !removedDevices.isEmpty();
        if (periodic && m_deviceSnapshotInitialized && !deviceStateChanged)
            return;

        m_deviceStates = currentStates;
        m_deviceSnapshotInitialized = true;
        m_devices->blockSignals(true);
        m_devices->clear();
        bool any = false;
        for (const QStringList &row : deviceRows) {
            const QString &label = row.at(0);
            const QString &serial = row.at(1);
            const QString &state = row.at(2);
            if (state == QStringLiteral("device")) {
                m_devices->addItem(label, serial);
                any = true;
            } else {
                m_devices->addItem(label + "  (" + state + ")",
                                   QStringLiteral("__") + state + QLatin1Char(':') + serial);
            }
        }
        m_devices->insertItem(0, "Auto-select connected device", "auto");
        bool previousSelected = false;
        if (!previous.isEmpty()) {
            for (int i = 1; i < m_devices->count(); ++i) {
                if (m_devices->itemData(i).toString() == previous) {
                    m_devices->setCurrentIndex(i);
                    previousSelected = true;
                    break;
                }
            }
        }
        if (!previousSelected) {
            m_devices->setCurrentIndex(0);
        }
        m_devices->blockSignals(false);

        if (!any) {
            const int unavailableCount = currentStates.size();
            m_connectionLabel->setText(unavailableCount == 0
                ? QStringLiteral("No Android devices found. Connect a device with USB debugging enabled, or use USB OTG mode.")
                : QStringLiteral("No authorized Android devices. %1 device(s) are offline or awaiting authorization.")
                      .arg(unavailableCount));
        } else {
            m_connectionLabel->setText(QString("%1 device(s) connected and ready%2")
                .arg(connectedCount)
                .arg(deviceStateChanged ? QStringLiteral(" — device status updated")
                                        : QString()));
        }
        if (deviceStateChanged) {
            for (const QString &device : addedDevices)
                appendLog("ADB device " + device);
            for (const QString &device : changedDevices)
                appendLog("ADB device state changed: " + device);
            for (const QString &device : removedDevices)
                appendLog("ADB device disconnected: " + device);
            statusBar()->showMessage("ADB device connection status changed.", 5000);
        }
        if (!periodic || previous != selectedSerial())
            refreshDeviceDetails();
    });
    connect(scan, &QProcess::errorOccurred, this, [this, scan](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_connectionLabel->setText("ADB not found — install Android SDK Platform-Tools or use ADB settings to select adb.exe.");
        if (m_deviceScan == scan)
            m_deviceScan = nullptr;
        scan->deleteLater();
    });
    scan->start();
}

void MainWindow::refreshDeviceDetails()
{
    if (m_deviceInfoQuery && m_deviceInfoQuery->state() != QProcess::NotRunning) {
        disconnect(m_deviceInfoQuery, nullptr, this, nullptr);
        m_deviceInfoQuery->kill();
        m_deviceInfoQuery->deleteLater();
        m_deviceInfoQuery = nullptr;
    }
    if (selectedSerial().isEmpty()) {
        m_deviceNameLabel->setText("No device selected");
        m_androidLabel->setText("—");
        m_serialLabel->setText("—");
        m_resolutionLabel->setText("—");
        m_densityLabel->setText("—");
        m_batteryLabel->setText("—");
        return;
    }
    const QString serial = selectedSerial();
    const QString command = QStringLiteral(
        "getprop; printf '\\n--DROIDDESK-SIZE--\\n'; wm size; "
        "printf '\\n--DROIDDESK-DENSITY--\\n'; wm density; "
        "printf '\\n--DROIDDESK-BATTERY--\\n'; dumpsys battery");
    QStringList arguments;
    if (!serial.isEmpty())
        arguments << "-s" << serial;
    arguments << "shell" << "sh" << "-c" << command;

    m_deviceNameLabel->setText("Loading…");
    m_androidLabel->setText("Loading…");
    m_serialLabel->setText(serial);
    m_resolutionLabel->setText("Loading…");
    m_densityLabel->setText("Loading…");
    m_batteryLabel->setText("Loading…");

    auto *query = new QProcess(this);
    m_deviceInfoQuery = query;
    query->setProgram(adbExecutable());
    query->setArguments(arguments);
    connect(query, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, query, serial](int exitCode, QProcess::ExitStatus exitStatus) {
        const QByteArray output = query->readAllStandardOutput();
        const QByteArray error = query->readAllStandardError();
        const bool started = query->error() != QProcess::FailedToStart;
        const CommandResult result{exitCode, output, error, started,
                                  exitStatus == QProcess::CrashExit};
        logCommand("Read device information", result);
        query->deleteLater();
        if (m_deviceInfoQuery == query)
            m_deviceInfoQuery = nullptr;
        if (serial != selectedSerial())
            return;
        if (!started || exitCode != 0 || exitStatus != QProcess::NormalExit) {
            m_deviceNameLabel->setText("Unavailable");
            m_androidLabel->setText("Unavailable");
            m_resolutionLabel->setText("Unavailable");
            m_densityLabel->setText("Unavailable");
            m_batteryLabel->setText("Unavailable");
            if (isDeviceDisconnected(result))
                handleDeviceDisconnected(result);
            return;
        }

        const QString text = QString::fromLocal8Bit(output);
        const QString sizeMarker = "\n--DROIDDESK-SIZE--\n";
        const QString densityMarker = "\n--DROIDDESK-DENSITY--\n";
        const QString batteryMarker = "\n--DROIDDESK-BATTERY--\n";
        const int sizeAt = text.indexOf(sizeMarker);
        const int densityAt = text.indexOf(densityMarker);
        const int batteryAt = text.indexOf(batteryMarker);
        const QString propertyText = sizeAt < 0 ? text : text.left(sizeAt);
        const QString sizeText = sizeAt < 0 || densityAt < 0
                                     ? QString()
                                     : text.mid(sizeAt + sizeMarker.size(),
                                                densityAt - sizeAt - sizeMarker.size()).trimmed();
        const QString densityText = densityAt < 0 || batteryAt < 0
                                        ? QString()
                                        : text.mid(densityAt + densityMarker.size(),
                                                   batteryAt - densityAt - densityMarker.size()).trimmed();
        const QString batteryText = batteryAt < 0 ? QString() : text.mid(batteryAt + batteryMarker.size());
        const QString manufacturer = propertyValue(propertyText, "[ro.product.manufacturer]");
        const QString model = propertyValue(propertyText, "[ro.product.model]");
        const QString version = propertyValue(propertyText, "[ro.build.version.release]");
        const QString sdk = propertyValue(propertyText, "[ro.build.version.sdk]");
        const QString deviceName = (manufacturer + " " + model).trimmed();
        m_deviceNameLabel->setText(deviceName.isEmpty() ? m_devices->currentText() : deviceName);
        m_androidLabel->setText(version.isEmpty() ? "Unknown" : "Android " + version +
                                  (sdk.isEmpty() ? "" : " (API " + sdk + ")"));
        QString resolution = sizeText.section(QLatin1Char('\n'), -1).section(QLatin1Char(':'), -1).trimmed();
        m_resolutionLabel->setText(resolution.isEmpty() ? "Unknown" : resolution + " px");
        QString dpi = densityText.section(QLatin1Char('\n'), -1).section(QLatin1Char(':'), -1).trimmed();
        m_densityLabel->setText(dpi.isEmpty() ? "Unknown" : dpi + " dpi");
        const auto match = QRegularExpression(R"(level:\s*(\d+))").match(batteryText);
        m_batteryLabel->setText(match.hasMatch() ? match.captured(1) + "%" : "Unknown");
    });
    connect(query, &QProcess::errorOccurred, this, [this, query, serial](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        query->deleteLater();
        if (m_deviceInfoQuery == query)
            m_deviceInfoQuery = nullptr;
        if (serial == selectedSerial()) {
            m_deviceNameLabel->setText("Unavailable");
            m_androidLabel->setText("Unavailable");
            m_resolutionLabel->setText("Unavailable");
            m_densityLabel->setText("Unavailable");
            m_batteryLabel->setText("Unavailable");
        }
    });
    query->start();
}

void MainWindow::refreshApps()
{
    if (m_appListProcess) {
        statusBar()->showMessage("The app list is already loading.", 3000);
        return;
    }
    if (selectedSerial().isEmpty()) {
        QMessageBox::information(this, "Apps", "Connect and select an authorized device first.");
        return;
    }
    const QString serial = selectedSerial();
    const bool includeSystem = m_systemApps->currentIndex() == 1;
    QStringList args{"-s", serial, "shell", "pm", "list", "packages"};
    if (!includeSystem)
        args << "-3";

    auto *process = new QProcess(this);
    m_appListProcess = process;
    process->setProgram(adbExecutable());
    process->setArguments(args);
    m_appList->clear();
    m_appList->addItem("Loading apps…");
    m_appList->item(0)->setFlags(Qt::NoItemFlags);
    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, process, [process] {
        if (process->state() != QProcess::NotRunning) {
            process->setProperty("timedOut", true);
            process->kill();
        }
    });
    timeout->start(60000);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, timeout, serial, includeSystem](int exitCode, QProcess::ExitStatus exitStatus) {
        timeout->stop();
        const QByteArray output = process->readAllStandardOutput();
        const QByteArray error = process->readAllStandardError();
        const bool started = process->error() != QProcess::FailedToStart;
        const bool timedOut = process->property("timedOut").toBool();
        if (m_appListProcess != process)
            return;
        m_appListProcess = nullptr;
        process->deleteLater();

        if (serial != selectedSerial())
            return;
        const CommandResult result{exitCode, output, error, started, timedOut};
        logCommand(includeSystem ? "List all packages" : "List user apps", result);
        if (isDeviceDisconnected(result)) {
            m_appList->clear();
            m_appList->addItem("Device disconnected while loading apps.");
            m_appList->item(0)->setFlags(Qt::NoItemFlags);
            handleDeviceDisconnected(result);
            return;
        }
        if (!started || timedOut || exitStatus != QProcess::NormalExit || exitCode != 0) {
            m_appList->clear();
            auto *item = new QListWidgetItem(
                timedOut ? "Loading apps timed out." : "Could not load apps. See the ADB log for details.",
                m_appList);
            item->setFlags(Qt::NoItemFlags);
            showResult({}, result);
            return;
        }
        m_appList->clear();
        const QString outputText = QString::fromLocal8Bit(output);
        for (QString line : outputText.split(QLatin1Char('\n'))) {
            line = line.trimmed();
            if (line.startsWith("package:"))
                line = line.mid(8);
            if (!line.isEmpty())
                m_appList->addItem(line);
        }
        if (m_appList->count() == 0) {
            auto *emptyItem = new QListWidgetItem("(No packages found)", m_appList);
            emptyItem->setFlags(Qt::NoItemFlags);
        }
        appendLog(QString("Found %1 packages").arg(m_appList->count()));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_appListProcess != process)
            return;
        m_appListProcess = nullptr;
        const QString errorText = process->errorString();
        process->deleteLater();
        m_appList->clear();
        m_appList->addItem("Could not start ADB.");
        m_appList->item(0)->setFlags(Qt::NoItemFlags);
        showResult({}, {-1, {}, errorText.toUtf8(), false, false});
    });
    process->start();
}

void MainWindow::installApk(const QString &path)
{
    if (m_appInstallProcess) {
        statusBar()->showMessage("An APK installation is already running.", 4000);
        return;
    }
    const QFileInfo apk(path);
    if (!apk.isFile() || apk.suffix().compare("apk", Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(this, "Install APK", "Choose a valid .apk file.");
        return;
    }
    const QString serial = selectedSerial();
    if (serial.isEmpty()) {
        QMessageBox::information(this, "Install APK", "Connect and select an authorized device first.");
        return;
    }

    auto *process = new QProcess(this);
    m_appInstallProcess = process;
    process->setProgram(adbExecutable());
    process->setArguments({"-s", serial, "install", "-r", path});
    m_installAppButton->setEnabled(false);
    m_appInstallStatus->setText(QString("Installing %1 on %2…").arg(apk.fileName(), serial));
    m_appInstallStatus->setVisible(true);
    m_appInstallProgress->setVisible(true);

    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, process, [process] {
        if (process->state() != QProcess::NotRunning) {
            process->setProperty("timedOut", true);
            process->kill();
        }
    });
    timeout->start(10 * 60 * 1000);

    const auto updateInstallOutput = [this, process] {
        QByteArray newOutput = process->readAllStandardOutput();
        newOutput += process->readAllStandardError();
        if (newOutput.isEmpty())
            return;
        QByteArray accumulated = process->property("installOutput").toByteArray();
        accumulated += newOutput;
        constexpr int maxOutputBytes = 64 * 1024;
        if (accumulated.size() > maxOutputBytes)
            accumulated = accumulated.right(maxOutputBytes);
        process->setProperty("installOutput", accumulated);
        const QStringList lines = QString::fromLocal8Bit(newOutput).split(
            QRegularExpression("[\\r\\n]+"), QString::SkipEmptyParts);
        if (!lines.isEmpty()) {
            m_appInstallStatus->setText(
                QString("Installing %1: %2").arg(QFileInfo(process->arguments().last()).fileName(),
                                                  lines.last().trimmed()));
        }
    };
    connect(process, &QProcess::readyReadStandardOutput, this, updateInstallOutput);
    connect(process, &QProcess::readyReadStandardError, this, updateInstallOutput);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, timeout, serial, fileName = apk.fileName(), updateInstallOutput]
            (int exitCode, QProcess::ExitStatus exitStatus) {
        timeout->stop();
        updateInstallOutput();
        const QByteArray output = process->property("installOutput").toByteArray();
        const bool started = process->error() != QProcess::FailedToStart;
        const bool timedOut = process->property("timedOut").toBool();
        if (m_appInstallProcess != process)
            return;
        m_appInstallProcess = nullptr;
        process->deleteLater();
        m_installAppButton->setEnabled(true);
        m_appInstallProgress->setVisible(false);
        m_appInstallStatus->setVisible(true);

        const CommandResult result{exitCode, output, {}, started, timedOut};
        logCommand("Install APK " + fileName, result);
        if (isDeviceDisconnected(result)) {
            m_appInstallStatus->setText("Installation stopped: device disconnected.");
            handleDeviceDisconnected(result);
            return;
        }
        if (!started || timedOut || exitStatus != QProcess::NormalExit || exitCode != 0) {
            const QString details = QString::fromLocal8Bit(output).trimmed();
            const QString reason = timedOut ? "Installation timed out after 10 minutes." :
                !started ? "ADB could not be started." :
                details.isEmpty() ? "ADB could not install the APK." : details;
            m_appInstallStatus->setText("Install failed: " + reason.left(300));
            QMessageBox::warning(this, "Install APK", reason +
                (details.isEmpty() ? QString() : QStringLiteral("\n\n") + details));
            return;
        }
        m_appInstallStatus->setText(fileName + " installed successfully.");
        statusBar()->showMessage(fileName + " installed successfully.", 6000);
        if (serial == selectedSerial())
            refreshApps();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_appInstallProcess != process)
            return;
        m_appInstallProcess = nullptr;
        m_installAppButton->setEnabled(true);
        m_appInstallProgress->setVisible(false);
        m_appInstallStatus->setText("Could not start ADB: " + process->errorString());
        QMessageBox::warning(this, "Install APK", "Could not start ADB:\n" + process->errorString());
        process->deleteLater();
    });
    statusBar()->showMessage("Installing APK on the selected device…");
    process->start();
}

void MainWindow::launchScrcpy()
{
    if (m_scrcpyProcess) {
        m_scrcpyStartButton->setEnabled(false);
        m_scrcpyStartButton->setText("Stopping scrcpy…");
        m_scrcpyStatusLabel->setText("Stopping scrcpy…");
        statusBar()->showMessage("Closing scrcpy…");
        QProcess *process = m_scrcpyProcess;
        process->terminate();
        QTimer::singleShot(2000, process, [process] {
            if (process->state() != QProcess::NotRunning)
                process->kill();
        });
        return;
    }

    const QString mode = m_scrcpyMode->currentData().toString();
    const bool otg = mode == QStringLiteral("otg");
    const bool virtualDisplay = mode == QStringLiteral("virtual");
    const QString serial = selectedSerial();
    if (!otg && serial.isEmpty()) {
        QMessageBox::information(this, "Screen mirror", "Connect and select an authorized Android device first.");
        return;
    }
    if (otg && m_scrcpyRecording->isChecked()) {
        QMessageBox::information(this, "OTG mode", "scrcpy OTG mode has no video or audio stream, so recording is unavailable. Turn off Record session or choose a different mode.");
        return;
    }
    if (virtualDisplay) {
        static const QRegularExpression resolutionPattern(QStringLiteral("^([0-9]{3,4})x([0-9]{3,4})$"));
        const auto match = resolutionPattern.match(m_scrcpyVirtualSize->text().trimmed());
        bool widthOk = false;
        bool heightOk = false;
        const int width = match.hasMatch() ? match.captured(1).toInt(&widthOk) : 0;
        const int height = match.hasMatch() ? match.captured(2).toInt(&heightOk) : 0;
        if (!widthOk || !heightOk || width < 320 || height < 320 ||
            width > 8192 || height > 8192) {
            QMessageBox::warning(this, "Virtual display",
                                 "Enter a virtual display size between 320 and 8192 pixels per side, such as 1280x720.");
            return;
        }
    }
    if (otg && !serial.isEmpty() && serial.contains(QLatin1Char(':'))) {
        QMessageBox::information(this, "OTG mode",
                                 "OTG works over a direct USB connection, not an ADB-over-Wi-Fi device. Select the USB device or disconnect its Wi-Fi ADB connection.");
        return;
    }
    const QString executable = scrcpyExecutable();
    if (executable.isEmpty()) {
        const QString expectedPath = QDir(QCoreApplication::applicationDirPath())
                                         .filePath("scrcpy/scrcpy.exe");
        QMessageBox::warning(
            this, "scrcpy not found",
            QString("The bundled scrcpy executable was not found.\n\nExpected it at:\n%1\n\n"
                    "Extract the official Windows release into the scrcpy folder next to DroidDesk.")
                .arg(expectedPath));
        return;
    }

    auto *process = new QProcess(this);
    m_scrcpyProcess = process;
    process->setProgram(executable);
    QStringList arguments;
    if (!serial.isEmpty())
        arguments << "-s" << serial;
    if (otg) {
        arguments << "--otg";
        if (m_scrcpyOtgGamepad->isChecked())
            arguments << "--gamepad=aoa";
    } else {
        arguments << "--window-title"
                  << QString("DroidDesk - %1").arg(serial);
        if (virtualDisplay) {
            arguments << "--new-display=" + m_scrcpyVirtualSize->text().trimmed();
            if (m_scrcpyFlexDisplay->isChecked())
                arguments << "--flex-display";
        }
        const QString maxSize = m_scrcpyMaxSize->currentData().toString();
        if (!maxSize.isEmpty())
            arguments << "--max-size=" + maxSize;
        const QString maxFps = m_scrcpyMaxFps->currentData().toString();
        if (!maxFps.isEmpty())
            arguments << "--max-fps=" + maxFps;
        if (!m_scrcpyAudio->isChecked())
            arguments << "--no-audio";
        if (m_scrcpyStayAwake->isChecked())
            arguments << "--stay-awake";
        if (m_scrcpyTurnScreenOff->isChecked())
            arguments << "--turn-screen-off";
        if (m_scrcpyAlwaysOnTop->isChecked())
            arguments << "--always-on-top";
        if (m_scrcpyShowTouches->isChecked())
            arguments << "--show-touches";
        if (m_scrcpyRecording->isChecked()) {
            QString recordingPath = m_scrcpyRecordingPath->text().trimmed();
            if (recordingPath.isEmpty())
                recordingPath = QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
                                    .filePath("DroidDesk-recording.mp4");
            recordingPath = QFileDialog::getSaveFileName(
                this, "Save scrcpy recording", recordingPath,
                "MP4 video (*.mp4);;Matroska video (*.mkv)");
            if (recordingPath.isEmpty()) {
                m_scrcpyProcess = nullptr;
                process->deleteLater();
                return;
            }
            const QString extension = QFileInfo(recordingPath).suffix().toLower();
            if (extension != QStringLiteral("mp4") && extension != QStringLiteral("mkv")) {
                m_scrcpyProcess = nullptr;
                process->deleteLater();
                QMessageBox::warning(this, "Scrcpy recording",
                                     "Choose an MP4 (.mp4) or Matroska (.mkv) recording file.");
                return;
            }
            m_scrcpyRecordingPath->setText(recordingPath);
            arguments << "--record=" + QFileInfo(recordingPath).absoluteFilePath();
        }
    }
    process->setArguments(arguments);
    process->setWorkingDirectory(QFileInfo(executable).absolutePath());
    process->setProcessChannelMode(QProcess::MergedChannels);
    m_scrcpyStartButton->setText("Stop scrcpy");
    m_scrcpyStartButton->setEnabled(true);
    const QString targetLabel = serial.isEmpty() ? QStringLiteral("USB device")
                                                  : serial;
    m_scrcpyStatusLabel->setText("Starting " + (otg ? QStringLiteral("OTG control") :
        virtualDisplay ? QStringLiteral("virtual display") : QStringLiteral("screen mirror")) +
        " for " + targetLabel + "…");
    statusBar()->showMessage("Starting scrcpy for " + targetLabel + "…");
    const auto captureOutput = [process] {
        QByteArray output = process->property("scrcpyOutput").toByteArray();
        output += process->readAll();
        constexpr int maxOutputBytes = 32 * 1024;
        if (output.size() > maxOutputBytes)
            output = output.right(maxOutputBytes);
        process->setProperty("scrcpyOutput", output);
    };
    connect(process, &QProcess::readyRead, this, captureOutput);

    connect(process, &QProcess::started, this, [this, serial, process, otg, virtualDisplay] {
        if (m_scrcpyProcess != process)
            return;
        const QString modeName = otg ? QStringLiteral("OTG control") :
            virtualDisplay ? QStringLiteral("Virtual display") : QStringLiteral("Screen mirror");
        m_scrcpyStatusLabel->setText(modeName + " is running for " +
                                     (serial.isEmpty() ? QStringLiteral("the USB device") : serial) +
                                     ". Close scrcpy or choose Stop scrcpy.");
        statusBar()->showMessage(modeName + " started.", 8000);
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, serial, captureOutput](int exitCode, QProcess::ExitStatus exitStatus) {
        captureOutput();
        const QByteArray output = process->property("scrcpyOutput").toByteArray();
        const bool started = process->error() != QProcess::FailedToStart;
        if (m_scrcpyProcess != process)
            return;
        m_scrcpyProcess = nullptr;
        process->deleteLater();
        m_scrcpyStartButton->setText("Start scrcpy");
        m_scrcpyStartButton->setEnabled(true);
        const CommandResult result{exitCode, output, {}, started, false};
        logCommand("scrcpy screen mirror", result);
        if (isDeviceDisconnected(result)) {
            m_scrcpyStatusLabel->setText("scrcpy closed because the Android device disconnected.");
            statusBar()->showMessage("scrcpy closed because the Android device disconnected.", 8000);
            handleDeviceDisconnected(result);
            return;
        }
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            const QString details = QString::fromLocal8Bit(output).trimmed();
            const QString reason = details.isEmpty()
                ? QStringLiteral("scrcpy exited with code %1. Check USB debugging and device authorization.").arg(exitCode)
                : details.left(1000);
            m_scrcpyStatusLabel->setText("scrcpy exited with an error. See the ADB log for details.");
            statusBar()->showMessage("scrcpy failed to start or stopped with an error.", 8000);
            QMessageBox::warning(this, "scrcpy", reason);
            return;
        }
        m_scrcpyStatusLabel->setText("scrcpy stopped.");
        statusBar()->showMessage("scrcpy closed for " +
                                 (serial.isEmpty() ? QStringLiteral("the USB device") : serial) + ".", 5000);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_scrcpyProcess != process)
            return;
        m_scrcpyProcess = nullptr;
        const QString errorText = process->errorString();
        process->deleteLater();
        m_scrcpyStartButton->setText("Start scrcpy");
        m_scrcpyStartButton->setEnabled(true);
        m_scrcpyStatusLabel->setText("Could not start scrcpy.");
        statusBar()->showMessage("Could not start scrcpy.", 6000);
        QMessageBox::warning(this, "scrcpy", "Could not start scrcpy:\n" + errorText);
    });
    process->start();
}

void MainWindow::executeShellCommand()
{
    const QString command = m_shellInput->text().trimmed();
    if (command.isEmpty())
        return;
    if (selectedSerial().isEmpty()) {
        QMessageBox::information(this, "Shell", "Connect and select an authorized device first.");
        return;
    }
    const QStringList args{"shell", "sh", "-c", shellQuote(command)};
    appendLog(QStringLiteral("> ") + command);
    const auto result = runDevice(args, 120000);
    logCommand("Shell command", result);
    m_shellInput->clear();
}

void MainWindow::loadModuleCatalog()
{
    if (m_catalogReply || m_catalogCurlProcess) {
        statusBar()->showMessage("The module catalog is already loading.", 3000);
        return;
    }

    const QUrl catalogUrl(QStringLiteral(
        "https://raw.githubusercontent.com/Magisk-Modules-Alt-Repo/json/main/modules.json"));
    if (!QSslSocket::supportsSsl()) {
        const QString curl = QStandardPaths::findExecutable("curl.exe");
        if (curl.isEmpty()) {
            m_moduleStoreStatus->setText(
                "HTTPS is unavailable: this Qt build has no SSL runtime and curl.exe was not found. "
                "Install the Qt OpenSSL runtime or add Windows curl.exe to PATH.");
            return;
        }
        auto *process = new QProcess(this);
        m_catalogCurlProcess = process;
        process->setProgram(curl);
        process->setArguments({
            "--location", "--fail", "--silent", "--show-error",
            "--max-time", "30", "--max-filesize", "10485760",
            "--proto", "=https", "--proto-redir", "=https",
            "--write-out", "\nDROIDDESK_EFFECTIVE_URL:%{url_effective}",
            catalogUrl.toString()
        });
        m_moduleStoreStatus->setText("Loading catalog using Windows curl…");
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
            QByteArray output = process->readAllStandardOutput();
            const QString error = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
            if (m_catalogCurlProcess != process)
                return;
            m_catalogCurlProcess = nullptr;
            process->deleteLater();
            const QByteArray marker("\nDROIDDESK_EFFECTIVE_URL:");
            const qsizetype markerIndex = output.lastIndexOf(marker);
            QUrl finalUrl;
            if (markerIndex >= 0) {
                finalUrl = QUrl(QString::fromUtf8(output.mid(markerIndex + marker.size())).trimmed());
                output.truncate(markerIndex);
            }
            if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                processModuleCatalogResponse({}, {}, error.isEmpty()
                    ? QString("curl exited with code %1").arg(exitCode) : error);
                return;
            }
            processModuleCatalogResponse(output, finalUrl, {});
        });
        connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
                return;
            if (m_catalogCurlProcess == process)
                m_catalogCurlProcess = nullptr;
            m_moduleStoreStatus->setText(
                "Could not start curl.exe. Add Windows curl.exe to PATH or install the Qt OpenSSL runtime.");
            process->deleteLater();
        });
        process->start();
        return;
    }

    QNetworkRequest request(catalogUrl);
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    request.setRawHeader("User-Agent", "DroidDesk/1.0");
    m_catalogReply = m_networkManager->get(request);
    auto *timeout = new QTimer(m_catalogReply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, m_catalogReply, &QNetworkReply::abort);
    timeout->start(30000);
    connect(m_catalogReply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64) {
        if (received > 10 * 1024 * 1024 && m_catalogReply)
            m_catalogReply->abort();
    });
    m_moduleStoreStatus->setText("Loading the public GitHub catalog…");
    connect(m_catalogReply, &QNetworkReply::finished, this, [this, timeout] {
        QNetworkReply *reply = m_catalogReply;
        m_catalogReply = nullptr;
        timeout->stop();
        const QByteArray data = reply->readAll();
        const QUrl finalUrl = reply->url();
        const auto networkError = reply->error();
        const QString networkErrorText = reply->errorString();
        reply->deleteLater();
        processModuleCatalogResponse(
            data, finalUrl,
            networkError == QNetworkReply::NoError ? QString() : networkErrorText);
    });
}

void MainWindow::processModuleCatalogResponse(const QByteArray &data, const QUrl &finalUrl,
                                             const QString &error)
{
    if (!error.isEmpty()) {
        m_moduleStoreStatus->setText("Catalog request failed: " + error);
        return;
    }
    if (finalUrl.scheme() != QStringLiteral("https") ||
        finalUrl.host() != QStringLiteral("raw.githubusercontent.com")) {
        m_moduleStoreStatus->setText("Catalog response came from an unexpected host.");
        return;
    }
    if (data.size() > 10 * 1024 * 1024) {
        m_moduleStoreStatus->setText("Catalog response was too large to process safely.");
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    const QJsonArray modules = document.object().value("modules").toArray();
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        modules.isEmpty()) {
        m_moduleStoreStatus->setText(
            "The catalog response was invalid or contained no module entries.");
        return;
    }

    static const QRegularExpression safeId(QStringLiteral("^[A-Za-z0-9._-]{1,120}$"));
    QSet<QString> seenIds;
    QVector<ModuleCatalogEntry> entries;
    entries.reserve(modules.size());
    for (const QJsonValue &value : modules) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        ModuleCatalogEntry entry;
        entry.id = object.value("id").toString();
        entry.propUrl = object.value("prop_url").toString();
        entry.notesUrl = object.value("notes_url").toString();
        entry.zipUrl = object.value("zip_url").toString();
        entry.stars = object.value("stars").toInt(-1);
        entry.lastUpdated = qint64(object.value("last_update").toDouble());
        const QUrl propUrl(entry.propUrl);
        const QUrl notesUrl(entry.notesUrl);
        const QUrl zipUrl(entry.zipUrl);
        const QString repositoryPath =
            QStringLiteral("/Magisk-Modules-Alt-Repo/") + entry.id + QLatin1Char('/');
        if (!safeId.match(entry.id).hasMatch() || entry.id == QStringLiteral(".") ||
            entry.id == QStringLiteral("..") || seenIds.contains(entry.id) ||
            entry.stars < 0 || !propUrl.isValid() || !notesUrl.isValid() ||
            !zipUrl.isValid() || propUrl.scheme() != QStringLiteral("https") ||
            notesUrl.scheme() != QStringLiteral("https") ||
            zipUrl.scheme() != QStringLiteral("https") ||
            propUrl.host() != QStringLiteral("raw.githubusercontent.com") ||
            notesUrl.host() != QStringLiteral("raw.githubusercontent.com") ||
            zipUrl.host() != QStringLiteral("github.com") ||
            !propUrl.path().startsWith(repositoryPath) ||
            !notesUrl.path().startsWith(repositoryPath) ||
            !zipUrl.path().startsWith(repositoryPath + QStringLiteral("archive/")) ||
            !zipUrl.path().endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))
            continue;
        seenIds.insert(entry.id);
        entries.append(entry);
    }

    if (entries.isEmpty()) {
        m_moduleStoreStatus->setText("No valid GitHub module entries were found.");
        return;
    }
    std::sort(entries.begin(), entries.end(), [](const ModuleCatalogEntry &left,
                                                 const ModuleCatalogEntry &right) {
        if (left.stars != right.stars)
            return left.stars > right.stars;
        return left.id.compare(right.id, Qt::CaseInsensitive) < 0;
    });
    m_catalogEntries = entries;
    m_moduleCatalog->clear();
    for (const ModuleCatalogEntry &entry : m_catalogEntries) {
        auto *item = new QListWidgetItem(
            QString("%1  ★ %2").arg(entry.id).arg(entry.stars), m_moduleCatalog);
        item->setData(Qt::UserRole, entry.id);
    }
    m_moduleStoreStatus->setText(
        QString("Loaded %1 validated entries from Magisk-Modules-Alt-Repo.")
            .arg(m_catalogEntries.size()));
    filterModuleCatalog(m_moduleSearch->text());
    m_moduleDetails->clear();
    m_moduleDownload->setEnabled(false);
}

void MainWindow::showModuleDetails(QListWidgetItem *item)
{
    if (!item)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    auto entryIt = std::find_if(m_catalogEntries.cbegin(), m_catalogEntries.cend(),
                                [&id](const ModuleCatalogEntry &entry) { return entry.id == id; });
    if (entryIt == m_catalogEntries.cend())
        return;

    if (m_moduleMetadataReply) {
        m_moduleMetadataReply->abort();
        m_moduleMetadataReply = nullptr;
    }
    m_moduleDownload->setEnabled(true);
    m_moduleDetails->setPlainText(
        QString("Loading metadata for %1…\n\nCatalog stars: %2\nUpdated: %3\n\n"
                "The catalog ZIP is a GitHub source archive, not a verified Magisk installer ZIP.")
            .arg(id)
            .arg(entryIt->stars)
            .arg(entryIt->lastUpdated > 0
                     ? QDateTime::fromMSecsSinceEpoch(entryIt->lastUpdated).toLocalTime()
                           .toString(Qt::DefaultLocaleShortDate)
                     : QStringLiteral("unknown")));

    QNetworkRequest request(QUrl(entryIt->propUrl));
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    request.setRawHeader("User-Agent", "DroidDesk/1.0");
    QNetworkReply *reply = m_networkManager->get(request);
    m_moduleMetadataReply = reply;
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout->start(20000);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64) {
        if (received > 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, timeout, id] {
        timeout->stop();
        if (m_moduleMetadataReply != reply) {
            reply->deleteLater();
            return;
        }
        m_moduleMetadataReply = nullptr;
        const QByteArray data = reply->readAll();
        const QString errorText = reply->errorString();
        const bool failed = reply->error() != QNetworkReply::NoError ||
                            reply->url().scheme() != QStringLiteral("https") ||
                            reply->url().host() != QStringLiteral("raw.githubusercontent.com");
        reply->deleteLater();

        auto entryIt = std::find_if(m_catalogEntries.begin(), m_catalogEntries.end(),
                                    [&id](const ModuleCatalogEntry &entry) { return entry.id == id; });
        if (entryIt == m_catalogEntries.end())
            return;
        if (failed) {
            m_moduleDetails->setPlainText(
                QString("Could not load metadata for %1: %2\n\n"
                        "Stars: %3\nUpdated: %4\n\n"
                        "The catalog download is an unverified GitHub source archive, not a Magisk installer ZIP.")
                    .arg(id, errorText)
                    .arg(entryIt->stars)
                    .arg(entryIt->lastUpdated > 0
                             ? QDateTime::fromMSecsSinceEpoch(entryIt->lastUpdated).toLocalTime()
                                   .toString(Qt::DefaultLocaleShortDate)
                             : QStringLiteral("unknown")));
            return;
        }

        const QString properties = QString::fromUtf8(data);
        auto property = [&properties](const QString &key) {
            const QString prefix = key + QLatin1Char('=');
            for (const QString &line : properties.split(QLatin1Char('\n'))) {
                if (line.startsWith(prefix))
                    return line.mid(prefix.size()).trimmed();
            }
            return QString();
        };
        entryIt->name = property("name");
        entryIt->description = property("description");
        entryIt->version = property("version");
        entryIt->author = property("author");
        m_moduleDetails->setPlainText(
            QString("Name: %1\nID: %2\nVersion: %3\nAuthor: %4\n\nDescription:\n%5\n\n"
                    "Stars: %6\nUpdated: %7\n\n"
                    "Source: Magisk-Modules-Alt-Repo/%2\n"
                    "The catalog ZIP is a repository source archive. DroidDesk does not treat it as an installable module package.")
                .arg(entryIt->name.isEmpty() ? id : entryIt->name, id,
                     entryIt->version.isEmpty() ? QStringLiteral("unknown") : entryIt->version,
                     entryIt->author.isEmpty() ? QStringLiteral("unknown") : entryIt->author,
                     entryIt->description.isEmpty() ? QStringLiteral("No description provided.")
                                                    : entryIt->description)
                .arg(entryIt->stars)
                .arg(entryIt->lastUpdated > 0
                         ? QDateTime::fromMSecsSinceEpoch(entryIt->lastUpdated).toLocalTime()
                               .toString(Qt::DefaultLocaleShortDate)
                         : QStringLiteral("unknown")));
        filterModuleCatalog(m_moduleSearch->text());
    });
}

void MainWindow::downloadSelectedModule()
{
    if (m_moduleDownloadReply) {
        QMessageBox::information(this, "Module store", "A module download is already in progress.");
        return;
    }
    QListWidgetItem *item = m_moduleCatalog->currentItem();
    if (!item)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    const auto entryIt = std::find_if(m_catalogEntries.cbegin(), m_catalogEntries.cend(),
                                      [&id](const ModuleCatalogEntry &entry) { return entry.id == id; });
    if (entryIt == m_catalogEntries.cend())
        return;
    if (QMessageBox::warning(
            this, "Download module source archive",
            QString("Download the GitHub source archive for %1?\n\n"
                    "This is not a verified Magisk installer ZIP. Review the source and package it using the module author's instructions before installation.")
                .arg(entryIt->name.isEmpty() ? id : entryIt->name),
            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Ok)
        return;

    QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (downloads.isEmpty())
        downloads = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(
        this, "Save module source archive",
        QDir(downloads).filePath(id + "-source.zip"),
        "ZIP archives (*.zip)");
    if (path.isEmpty())
        return;
    m_moduleDownloadFile = new QSaveFile(path);
    if (!m_moduleDownloadFile->open(QIODevice::WriteOnly)) {
        delete m_moduleDownloadFile;
        m_moduleDownloadFile = nullptr;
        QMessageBox::warning(this, "Module store", "Could not create the download file:\n" + path);
        return;
    }
    m_moduleDownloadPath = path;
    m_moduleDownloadPrefix.clear();

    QNetworkRequest request(QUrl(entryIt->zipUrl));
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    request.setRawHeader("User-Agent", "DroidDesk/1.0");
    QNetworkReply *reply = m_networkManager->get(request);
    m_moduleDownloadReply = reply;
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout->start(5 * 60 * 1000);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        const QByteArray chunk = reply->readAll();
        if (m_moduleDownloadPrefix.size() < 4)
            m_moduleDownloadPrefix.append(chunk.left(4 - m_moduleDownloadPrefix.size()));
        if (!m_moduleDownloadFile ||
            m_moduleDownloadFile->write(chunk) != chunk.size()) {
            reply->setProperty("writeFailed", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, reply](qint64 received, qint64 total) {
        constexpr qint64 maxDownloadSize = 512LL * 1024 * 1024;
        if (received > maxDownloadSize) {
            reply->setProperty("sizeExceeded", true);
            reply->abort();
            return;
        }
        m_moduleStoreStatus->setText(
            total > 0 ? QString("Downloading source archive: %1 / %2 MB")
                            .arg(received / (1024 * 1024))
                            .arg(total / (1024 * 1024))
                      : QString("Downloading source archive: %1 MB")
                            .arg(received / (1024 * 1024)));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, timeout, id] {
        timeout->stop();
        const QByteArray trailing = reply->readAll();
        if (!trailing.isEmpty() && m_moduleDownloadFile) {
            if (m_moduleDownloadPrefix.size() < 4)
                m_moduleDownloadPrefix.append(trailing.left(4 - m_moduleDownloadPrefix.size()));
            if (m_moduleDownloadFile->write(trailing) != trailing.size())
                reply->setProperty("writeFailed", true);
        }
        const QUrl finalUrl = reply->url();
        const bool validHost =
            finalUrl.scheme() == QStringLiteral("https") &&
            (finalUrl.host() == QStringLiteral("github.com") ||
             finalUrl.host() == QStringLiteral("codeload.github.com"));
        const bool validZip = m_moduleDownloadPrefix.startsWith("PK\x03\x04") ||
                              m_moduleDownloadPrefix.startsWith("PK\x05\x06") ||
                              m_moduleDownloadPrefix.startsWith("PK\x07\x08");
        const bool success = reply->error() == QNetworkReply::NoError && validHost &&
                             validZip && !reply->property("writeFailed").toBool() &&
                             !reply->property("sizeExceeded").toBool() && m_moduleDownloadFile &&
                             m_moduleDownloadFile->commit();
        const QString errorText = reply->errorString();
        const bool tooLarge = reply->property("sizeExceeded").toBool();
        const bool writeFailed = reply->property("writeFailed").toBool();
        reply->deleteLater();
        m_moduleDownloadReply = nullptr;
        if (m_moduleDownloadFile) {
            if (!success)
                m_moduleDownloadFile->cancelWriting();
            delete m_moduleDownloadFile;
            m_moduleDownloadFile = nullptr;
        }

        if (!success) {
            m_moduleStoreStatus->setText("Module download failed.");
            QMessageBox::warning(
                this, "Module store",
                tooLarge ? "The archive exceeded the 512 MB safety limit." :
                writeFailed ? "Could not write the download file." :
                validHost && validZip ? "Download failed: " + errorText :
                "The response was not a ZIP archive from an approved GitHub download host.");
            return;
        }
        m_moduleStoreStatus->setText(QString("Downloaded source archive for %1.").arg(id));
        QMessageBox::information(
            this, "Module source downloaded",
            QString("Saved to:\n%1\n\nThis is a source archive, not a verified Magisk installer ZIP. Review the repository and follow its packaging instructions before using Install module ZIP.")
                .arg(m_moduleDownloadPath));
    });
}

void MainWindow::filterModuleCatalog(const QString &text)
{
    const QString needle = text.trimmed();
    QListWidgetItem *selectedItem = m_moduleCatalog->currentItem();
    for (int i = 0; i < m_moduleCatalog->count(); ++i) {
        QListWidgetItem *item = m_moduleCatalog->item(i);
        const QString id = item->data(Qt::UserRole).toString();
        auto entryIt = std::find_if(m_catalogEntries.cbegin(), m_catalogEntries.cend(),
                                    [&id](const ModuleCatalogEntry &entry) { return entry.id == id; });
        if (entryIt == m_catalogEntries.cend()) {
            item->setHidden(true);
            continue;
        }
        const QString searchable = id + QLatin1Char(' ') + entryIt->name + QLatin1Char(' ') +
                                   entryIt->author + QLatin1Char(' ') + entryIt->description;
        item->setHidden(!searchable.contains(needle, Qt::CaseInsensitive));
    }
    if (selectedItem && selectedItem->isHidden()) {
        m_moduleCatalog->clearSelection();
        m_moduleCatalog->setCurrentItem(nullptr);
        if (m_moduleMetadataReply) {
            m_moduleMetadataReply->abort();
            m_moduleMetadataReply = nullptr;
        }
        m_moduleDownload->setEnabled(false);
        m_moduleDetails->clear();
    }
}

void MainWindow::checkRootStatus()
{
    if (m_rootCheckProcess) {
        statusBar()->showMessage("A root check is already in progress.", 3000);
        return;
    }
    const QString serial = selectedSerial();
    if (serial.isEmpty()) {
        m_rootStatusLabel->setText("Connect and select an authorized device before checking root.");
        return;
    }

    const QString script = QStringLiteral(
        "uid=$(id -u 2>/dev/null) || exit 1; "
        "printf 'DROIDDESK_UID=%s\\n' \"$uid\"; "
        "version=$(magisk -v 2>/dev/null); "
        "if [ -n \"$version\" ]; then printf 'DROIDDESK_MAGISK_VERSION=%s\\n' \"$version\"; "
        "elif [ -d /data/adb/modules ] || [ -e /data/adb/magisk.db ]; then "
        "printf 'DROIDDESK_MAGISK_PRESENT=1\\n'; "
        "else printf 'DROIDDESK_MAGISK_PRESENT=0\\n'; fi");
    auto *process = new QProcess(this);
    m_rootCheckProcess = process;
    process->setProgram(adbExecutable());
    process->setArguments({"-s", serial, "shell", "su", "-c", shellQuote(script)});
    process->setProcessChannelMode(QProcess::SeparateChannels);
    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, process, [process] {
        if (process->state() != QProcess::NotRunning) {
            process->setProperty("timedOut", true);
            process->kill();
        }
    });
    timeout->start(30000);
    m_rootStatusLabel->setText("Checking root access… Approve the superuser request on your device if prompted.");

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, timeout, serial](int exitCode, QProcess::ExitStatus exitStatus) {
        timeout->stop();
        const QByteArray output = process->readAllStandardOutput();
        const QByteArray error = process->readAllStandardError();
        const bool started = process->error() != QProcess::FailedToStart;
        const bool timedOut = process->property("timedOut").toBool();
        if (m_rootCheckProcess != process)
            return;
        m_rootCheckProcess = nullptr;
        process->deleteLater();

        if (serial != selectedSerial()) {
            m_rootStatusLabel->setText("Root check discarded because the selected device changed.");
            return;
        }
        const CommandResult result{exitCode, output, error, started, timedOut};
        logCommand("Check root access", result);
        if (isDeviceDisconnected(result)) {
            m_rootStatusLabel->setText("Device disconnected during the root check.");
            handleDeviceDisconnected(result);
            return;
        }
        if (timedOut) {
            m_rootStatusLabel->setText(
                "Root check timed out. Approve or deny the superuser request on the device, then try again.");
            return;
        }
        if (!started) {
            m_rootStatusLabel->setText("Could not start ADB: " + QString::fromLocal8Bit(error).trimmed());
            return;
        }

        QString uid;
        QString magiskVersion;
        bool magiskPresent = false;
        const QString outputText = QString::fromLocal8Bit(output);
        for (const QString &line : outputText.split(QLatin1Char('\n'))) {
            const QString trimmedLine = line.trimmed();
            if (trimmedLine.startsWith(QStringLiteral("DROIDDESK_UID=")))
                uid = trimmedLine.mid(QStringLiteral("DROIDDESK_UID=").size());
            else if (trimmedLine.startsWith(QStringLiteral("DROIDDESK_MAGISK_VERSION="))) {
                magiskVersion = trimmedLine.mid(QStringLiteral("DROIDDESK_MAGISK_VERSION=").size());
                magiskPresent = !magiskVersion.isEmpty();
            } else if (trimmedLine == QStringLiteral("DROIDDESK_MAGISK_PRESENT=1")) {
                magiskPresent = true;
            }
        }

        if (uid != QStringLiteral("0")) {
            QString details = QString::fromLocal8Bit(error.isEmpty() ? output : error).trimmed();
            if (details.isEmpty())
                details = QStringLiteral("su did not return a root user ID. It may be unavailable or its request was denied.");
            m_rootStatusLabel->setText("Root access unavailable: " + details.left(500));
            return;
        }

        QString status = QStringLiteral("Root access granted (UID 0). ");
        if (!magiskVersion.isEmpty())
            status += QStringLiteral("Magisk detected: ") + magiskVersion;
        else if (magiskPresent)
            status += QStringLiteral("Magisk files detected; version could not be read.");
        else
            status += QStringLiteral("Magisk was not detected.");
        m_rootStatusLabel->setText(status);
        m_rootStatusLabel->setToolTip(status);
        statusBar()->showMessage(status, 8000);
        return;
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        if (m_rootCheckProcess != process)
            return;
        m_rootCheckProcess = nullptr;
        m_rootStatusLabel->setText("Could not start ADB: " + process->errorString());
        process->deleteLater();
    });
    process->start();
}

void MainWindow::runSelinuxCommand(bool changeMode, bool enforcing)
{
    if (m_selinuxProcess) {
        statusBar()->showMessage("A SELinux check or change is already running.", 3000);
        return;
    }
    const QString serial = selectedSerial();
    if (serial.isEmpty()) {
        m_selinuxStatusLabel->setText("Connect and select an authorized device first.");
        return;
    }
    if (changeMode) {
        const QString targetMode = enforcing ? QStringLiteral("Enforcing")
                                             : QStringLiteral("Permissive");
        const QString message = enforcing
            ? QStringLiteral("Set SELinux to Enforcing on %1? This can restrict modifications made by root tools or modules.")
                  .arg(serial)
            : QStringLiteral("Set SELinux to Permissive on %1?\n\n"
                             "This weakens Android's security enforcement and can expose the device to increased risk. "
                             "Only continue if you understand why this is needed.")
                  .arg(serial);
        const QMessageBox::StandardButton answer = enforcing
            ? QMessageBox::question(this, "Change SELinux mode", message,
                                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
            : QMessageBox::warning(this, "Security warning: SELinux Permissive", message,
                                   QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes)
            return;
        m_selinuxStatusLabel->setText("Requesting SELinux " + targetMode + " mode…");
    } else {
        m_selinuxStatusLabel->setText("Checking SELinux status… Approve the superuser request if prompted.");
    }

    const QString script = changeMode
        ? QStringLiteral("setenforce %1 && getenforce").arg(enforcing ? 1 : 0)
        : QStringLiteral("getenforce");
    auto *process = new QProcess(this);
    m_selinuxProcess = process;
    process->setProgram(adbExecutable());
    process->setArguments({"-s", serial, "shell", "su", "-c", shellQuote(script)});
    process->setProcessChannelMode(QProcess::SeparateChannels);
    m_selinuxCheckButton->setEnabled(false);
    m_selinuxEnforcingButton->setEnabled(false);
    m_selinuxPermissiveButton->setEnabled(false);
    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, process, [process] {
        if (process->state() != QProcess::NotRunning) {
            process->setProperty("timedOut", true);
            process->kill();
        }
    });
    timeout->start(30000);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, timeout, serial, changeMode, enforcing]
            (int exitCode, QProcess::ExitStatus exitStatus) {
        timeout->stop();
        const QByteArray output = process->readAllStandardOutput();
        const QByteArray error = process->readAllStandardError();
        const bool started = process->error() != QProcess::FailedToStart;
        const bool timedOut = process->property("timedOut").toBool();
        if (m_selinuxProcess != process)
            return;
        m_selinuxProcess = nullptr;
        process->deleteLater();
        m_selinuxCheckButton->setEnabled(true);
        m_selinuxEnforcingButton->setEnabled(true);
        m_selinuxPermissiveButton->setEnabled(true);

        if (serial != selectedSerial()) {
            m_selinuxStatusLabel->setText("SELinux result discarded because the selected device changed.");
            return;
        }
        const CommandResult result{exitCode, output, error, started, timedOut};
        logCommand(changeMode ? "Change SELinux mode" : "Check SELinux status", result);
        if (isDeviceDisconnected(result)) {
            m_selinuxStatusLabel->setText("Device disconnected during the SELinux operation.");
            handleDeviceDisconnected(result);
            return;
        }
        if (timedOut) {
            m_selinuxStatusLabel->setText(
                "SELinux operation timed out. Approve the superuser request on the device, then try again.");
            return;
        }
        if (!started || exitStatus != QProcess::NormalExit || exitCode != 0) {
            const QString details = QString::fromLocal8Bit(error.isEmpty() ? output : error).trimmed();
            m_selinuxStatusLabel->setText(
                (changeMode ? "Could not change SELinux mode: " : "Could not read SELinux status: ") +
                (details.isEmpty() ? QStringLiteral("root access may be unavailable or Android may have denied the operation.")
                                   : details.left(400)));
            return;
        }

        QString reportedMode;
        const QString outputText = QString::fromLocal8Bit(output);
        for (const QString &line : outputText.split(QRegularExpression("[\\r\\n]+"), QString::SkipEmptyParts)) {
            const QString mode = line.trimmed();
            if (mode.compare(QStringLiteral("Enforcing"), Qt::CaseInsensitive) == 0 ||
                mode.compare(QStringLiteral("Permissive"), Qt::CaseInsensitive) == 0) {
                reportedMode = mode;
            }
        }
        if (reportedMode.isEmpty()) {
            m_selinuxStatusLabel->setText(
                "SELinux status was not recognized. Device output: " + outputText.trimmed().left(400));
            return;
        }
        const QString actualMode = reportedMode.compare(QStringLiteral("Enforcing"), Qt::CaseInsensitive) == 0
            ? QStringLiteral("Enforcing") : QStringLiteral("Permissive");
        m_selinuxStatusLabel->setText(
            (changeMode && (enforcing != (actualMode == QStringLiteral("Enforcing")))
                 ? QStringLiteral("Device did not apply the requested mode. Current mode: ")
                 : QStringLiteral("Current SELinux mode: ")) +
            actualMode);
        statusBar()->showMessage("SELinux mode: " + actualMode, 6000);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_selinuxProcess != process)
            return;
        m_selinuxProcess = nullptr;
        m_selinuxCheckButton->setEnabled(true);
        m_selinuxEnforcingButton->setEnabled(true);
        m_selinuxPermissiveButton->setEnabled(true);
        m_selinuxStatusLabel->setText("Could not start ADB: " + process->errorString());
        process->deleteLater();
    });
    process->start();
}

void MainWindow::refreshMagiskModules()
{
    if (selectedSerial().isEmpty()) {
        QMessageBox::information(this, "Magisk modules", "Connect and select an authorized device first.");
        return;
    }

    const QString script = QStringLiteral(
        "if [ ! -d /data/adb/modules ]; then echo 'Magisk module directory not found'; exit 2; fi; "
        "for d in /data/adb/modules/*; do "
        "[ -d \"$d\" ] || continue; id=${d##*/}; "
        "name=$(sed -n 's/^name=//p' \"$d/module.prop\" 2>/dev/null | head -n 1); "
        "version=$(sed -n 's/^version=//p' \"$d/module.prop\" 2>/dev/null | head -n 1); "
        "author=$(sed -n 's/^author=//p' \"$d/module.prop\" 2>/dev/null | head -n 1); "
        "state=Enabled; [ -f \"$d/disable\" ] && state=Disabled; "
        "[ -f \"$d/remove\" ] && state='Pending removal'; "
        "printf 'DROIDDESK_MODULE\\t%s\\t%s\\t%s\\t%s\\t%s\\n' "
        "\"$id\" \"$name\" \"$version\" \"$author\" \"$state\"; done");
    const auto result = runDevice({"shell", "su", "-c", shellQuote(script)}, 60000);
    logCommand("List Magisk modules", result);
    if (!result.started || result.timedOut || result.exitCode != 0) {
        showResult({}, result);
        return;
    }

    m_magiskModules->clear();
    const QString output = QString::fromLocal8Bit(result.output);
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        const QStringList fields = line.trimmed().split(QLatin1Char('\t'));
        if (fields.size() < 6 || fields.at(0) != "DROIDDESK_MODULE")
            continue;
        const QString id = fields.at(1);
        static const QRegularExpression safeId(R"(^[A-Za-z0-9._-]+$)");
        if (!safeId.match(id).hasMatch())
            continue;
        const QString name = fields.at(2).isEmpty() ? id : fields.at(2);
        const QString version = fields.at(3).isEmpty() ? "Unknown version" : fields.at(3);
        const QString author = fields.at(4).isEmpty() ? "Unknown author" : fields.at(4);
        const QString state = fields.at(5);
        auto *item = new QListWidgetItem(
            QString("%1  ·  %2  ·  %3  ·  %4").arg(name, version, author, state),
            m_magiskModules);
        item->setData(Qt::UserRole, id);
        item->setData(Qt::UserRole + 1, state);
        item->setToolTip("Module ID: " + id);
    }
    if (m_magiskModules->count() == 0) {
        auto *item = new QListWidgetItem("No installed modules found.", m_magiskModules);
        item->setFlags(Qt::NoItemFlags);
    }
    statusBar()->showMessage(QString("Magisk modules refreshed (%1 found).").arg(m_magiskModules->count()), 5000);
}

void MainWindow::setMagiskModuleEnabled(bool enabled)
{
    QListWidgetItem *item = m_magiskModules->currentItem();
    if (!item || !item->flags().testFlag(Qt::ItemIsSelectable)) {
        QMessageBox::information(this, "Magisk modules", "Select an installed module first.");
        return;
    }
    const QString id = item->data(Qt::UserRole).toString();
    static const QRegularExpression safeId(R"(^[A-Za-z0-9._-]+$)");
    if (!safeId.match(id).hasMatch()) {
        QMessageBox::warning(this, "Magisk modules", "The selected module ID is invalid.");
        return;
    }
    const QString state = item->data(Qt::UserRole + 1).toString();
    if ((enabled && state == "Enabled") || (!enabled && state == "Disabled")) {
        statusBar()->showMessage(enabled ? "Module is already enabled." : "Module is already disabled.", 4000);
        return;
    }
    const QString action = enabled ? "Enable" : "Disable";
    if (QMessageBox::question(this, action + " Magisk module",
                              action + " \"" + item->text() + "\"? A reboot is required for the change to take effect.") != QMessageBox::Yes)
        return;

    const QString directory = "/data/adb/modules/" + id;
    const QString script = enabled
                               ? "rm -f " + shellQuote(directory + "/disable") + " " + shellQuote(directory + "/remove")
                               : "touch " + shellQuote(directory + "/disable");
    const auto result = runDevice({"shell", "su", "-c", shellQuote(script)}, 60000);
    logCommand(action + " Magisk module " + id, result);
    if (!result.started || result.timedOut || result.exitCode != 0) {
        showResult({}, result);
        return;
    }
    refreshMagiskModules();
    statusBar()->showMessage(action + " request recorded. Reboot to apply it.", 6000);
}

void MainWindow::removeMagiskModule()
{
    QListWidgetItem *item = m_magiskModules->currentItem();
    if (!item || !item->flags().testFlag(Qt::ItemIsSelectable)) {
        QMessageBox::information(this, "Magisk modules", "Select an installed module first.");
        return;
    }
    const QString id = item->data(Qt::UserRole).toString();
    static const QRegularExpression safeId(R"(^[A-Za-z0-9._-]+$)");
    if (!safeId.match(id).hasMatch()) {
        QMessageBox::warning(this, "Magisk modules", "The selected module ID is invalid.");
        return;
    }
    if (QMessageBox::warning(
            this, "Schedule Magisk module removal",
            "Mark \"" + item->text() +
                "\" for removal? Magisk will remove it on the next reboot. This cannot be undone from DroidDesk.",
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;

    const QString marker = "/data/adb/modules/" + id + "/remove";
    const auto result = runDevice({"shell", "su", "-c", shellQuote("touch " + shellQuote(marker))}, 60000);
    logCommand("Schedule module removal " + id, result);
    if (!result.started || result.timedOut || result.exitCode != 0) {
        showResult({}, result);
        return;
    }
    refreshMagiskModules();
    statusBar()->showMessage("Module scheduled for removal at next reboot.", 6000);
}

void MainWindow::startLogcat()
{
    if (selectedSerial().isEmpty()) {
        QMessageBox::information(this, "Logcat", "Connect and select an authorized device first.");
        return;
    }
    static const QStringList levels{"V", "D", "I", "W", "E", "F"};
    const int levelIndex = qBound(0, m_logcatLevel->currentIndex(), levels.size() - 1);
    auto *process = new QProcess(this);
    m_logcatProcess = process;
    m_logcatBuffer.clear();
    process->setProgram(adbExecutable());
    process->setArguments({"-s", selectedSerial(), "logcat", "-v", "threadtime",
                           "*:" + levels.at(levelIndex)});
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process, &QProcess::readyReadStandardOutput, this, &MainWindow::appendLogcatData);
    connect(process, &QProcess::readyReadStandardError, this, [this, process] {
        const QByteArray errorBytes = process->readAllStandardError();
        const QString error = QString::fromLocal8Bit(errorBytes).trimmed();
        if (error.isEmpty())
            return;
        appendLog(QStringLiteral("Logcat stderr: ") + error);
        const CommandResult result{-1, {}, errorBytes, true, false};
        if (isDeviceDisconnected(result))
            handleDeviceDisconnected(result);
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        appendLogcatData();
        if (!m_logcatBuffer.isEmpty()) {
            const QString tail = QString::fromLocal8Bit(m_logcatBuffer).trimmed();
            if (!tail.isEmpty() && (m_logcatFilter->text().isEmpty() ||
                                    tail.contains(m_logcatFilter->text(), Qt::CaseInsensitive))) {
                if (m_pendingLogcatLines.size() >= 2000) {
                    m_pendingLogcatLines.removeFirst();
                    ++m_droppedLogcatLines;
                }
                m_pendingLogcatLines.append(tail);
            }
            m_logcatBuffer.clear();
        }
        if (m_logcatFlushTimer->isActive())
            m_logcatFlushTimer->stop();
        if (!m_pendingLogcatLines.isEmpty() || m_droppedLogcatLines > 0)
            m_logcatFlushTimer->start();
        const bool wasStopping = m_logcatToggle->text() != "Stop logcat";
        const bool started = process->error() != QProcess::FailedToStart;
        logCommand("Live logcat", {exitCode, {}, process->readAllStandardError(), started,
                                   exitStatus == QProcess::CrashExit});
        if (m_logcatProcess == process)
            m_logcatProcess = nullptr;
        process->deleteLater();
        m_logcatToggle->setText("Start logcat");
        m_logcatToggle->setEnabled(true);
        if (wasStopping) {
            statusBar()->showMessage("Logcat stopped.", 3000);
        } else if (!started || exitCode != 0 || exitStatus != QProcess::NormalExit) {
            statusBar()->showMessage("Logcat ended unexpectedly; see the shell log for details.", 6000);
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        appendLog("Could not start logcat: " + process->errorString());
        if (m_logcatProcess == process)
            m_logcatProcess = nullptr;
        process->deleteLater();
        m_logcatToggle->setText("Start logcat");
        m_logcatToggle->setEnabled(true);
        QMessageBox::warning(this, "Logcat", "Could not start ADB:\n" + process->errorString());
    });
    m_logcatToggle->setText("Stop logcat");
    statusBar()->showMessage("Starting live logcat…");
    process->start();
}

void MainWindow::stopLogcat()
{
    if (!m_logcatProcess || m_logcatProcess->state() == QProcess::NotRunning)
        return;
    m_logcatToggle->setText("Stopping…");
    m_logcatToggle->setEnabled(false);
    QProcess *process = m_logcatProcess;
    process->terminate();
    QTimer::singleShot(1500, process, [process] {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
}

void MainWindow::appendLogcatData()
{
    if (!m_logcatProcess)
        return;
    m_logcatBuffer += m_logcatProcess->readAllStandardOutput();
    int newline = -1;
    while ((newline = m_logcatBuffer.indexOf('\n')) >= 0) {
        const QByteArray bytes = m_logcatBuffer.left(newline);
        m_logcatBuffer.remove(0, newline + 1);
        const QString line = QString::fromLocal8Bit(bytes).trimmed();
        if (line.isEmpty())
            continue;
        const QString filter = m_logcatFilter->text();
        if (!filter.isEmpty() && !line.contains(filter, Qt::CaseInsensitive))
            continue;
        if (m_pendingLogcatLines.size() >= 2000) {
            m_pendingLogcatLines.removeFirst();
            ++m_droppedLogcatLines;
        }
        m_pendingLogcatLines.append(line);
    }
    constexpr int MaxPartialLineBytes = 64 * 1024;
    if (m_logcatBuffer.size() > MaxPartialLineBytes) {
        m_logcatBuffer = m_logcatBuffer.right(MaxPartialLineBytes);
        ++m_droppedLogcatLines;
    }
    if ((!m_pendingLogcatLines.isEmpty() || m_droppedLogcatLines > 0) &&
        !m_logcatFlushTimer->isActive())
        m_logcatFlushTimer->start();
}

void MainWindow::installMagiskModule(const QString &path)
{
    if (selectedSerial().isEmpty()) {
        QMessageBox::information(this, "Install Magisk module", "Connect and select an authorized device first.");
        return;
    }
    const QFileInfo module(path);
    if (!module.isFile() || module.suffix().compare("zip", Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(this, "Install Magisk module", "Choose a valid Magisk module .zip file.");
        return;
    }
    if (m_moduleInstallProcess && m_moduleInstallProcess->state() != QProcess::NotRunning) {
        QMessageBox::information(this, "Install Magisk module", "A module installation is already in progress.");
        return;
    }
    const QString serial = selectedSerial();
    if (QMessageBox::warning(
            this, "Install Magisk module",
            QString("Install %1 on device %2?\n\nThe device must already have Magisk installed. Review the module's source and compatibility before installing. A reboot is usually required to activate it.")
                .arg(module.fileName(), serial),
            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Ok)
        return;

    constexpr auto remotePath = "/data/local/tmp/droiddesk-magisk-module.zip";
    auto *process = new QProcess(this);
    m_moduleInstallProcess = process;
    process->setProgram(adbExecutable());
    process->setArguments({"-s", serial, "push", path, remotePath});
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setProperty("installPhase", "Copying module ZIP");
    process->setProperty("installOutput", QByteArray());
    process->setProperty("installLineBuffer", QByteArray());
    m_moduleInstallLog->clear();
    m_moduleInstallProgress->setVisible(true);
    m_moduleInstallStatus->setText("Copying " + module.fileName() + " to the device…");
    m_moduleInstallLog->appendPlainText(
        QString("[%1] Starting installation of %2 on %3.")
            .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), module.fileName(), serial));
    appendLog("Magisk module installation started: " + module.fileName() + " on " + serial);

    const auto captureOutput = [this, process](bool flushPartialLine = false) {
        QByteArray chunk = process->readAllStandardOutput();
        if (!chunk.isEmpty()) {
            QByteArray output = process->property("installOutput").toByteArray();
            output += chunk;
            constexpr int maxOutputBytes = 128 * 1024;
            if (output.size() > maxOutputBytes)
                output = output.right(maxOutputBytes);
            process->setProperty("installOutput", output);
        }

        QByteArray lineBuffer = process->property("installLineBuffer").toByteArray();
        lineBuffer += chunk;
        QStringList lines = QString::fromLocal8Bit(lineBuffer).split(
            QRegularExpression("[\\r\\n]+"), QString::KeepEmptyParts);
        const bool endsWithDelimiter =
            lineBuffer.endsWith('\n') || lineBuffer.endsWith('\r');
        if (!flushPartialLine && !endsWithDelimiter && !lines.isEmpty()) {
            lineBuffer = lines.takeLast().toLocal8Bit();
        } else {
            lineBuffer.clear();
        }
        process->setProperty("installLineBuffer", lineBuffer);
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            const QString phase = process->property("installPhase").toString();
            m_moduleInstallStatus->setText(phase + ": " + trimmed.left(220));
            m_moduleInstallLog->appendPlainText(
                QString("[%1] %2")
                    .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), trimmed));
            appendLog("Magisk module [" + phase + "]: " + trimmed);
        }
        if (flushPartialLine && !lineBuffer.isEmpty()) {
            const QString finalLine = QString::fromLocal8Bit(lineBuffer).trimmed();
            if (!finalLine.isEmpty()) {
                const QString phase = process->property("installPhase").toString();
                m_moduleInstallStatus->setText(phase + ": " + finalLine.left(220));
                m_moduleInstallLog->appendPlainText(
                    QString("[%1] %2")
                        .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), finalLine));
                appendLog("Magisk module [" + phase + "]: " + finalLine);
            }
            process->setProperty("installLineBuffer", QByteArray());
        }
    };
    connect(process, &QProcess::readyReadStandardOutput, this,
            [captureOutput] { captureOutput(); });

    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, process, [process] {
        if (process->state() != QProcess::NotRunning) {
            process->setProperty("timedOut", true);
            process->kill();
        }
    });
    timeout->start(5 * 60 * 1000);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, serial, remotePath, timeout, captureOutput, moduleName = module.fileName()]
            (int exitCode, QProcess::ExitStatus exitStatus) {
        captureOutput(true);
        const QByteArray output = process->property("installOutput").toByteArray();
        const bool started = process->error() != QProcess::FailedToStart;
        const bool timedOut = process->property("timedOut").toBool();
        if (m_moduleInstallProcess != process)
            return;
        if (!started || timedOut || exitCode != 0 || exitStatus != QProcess::NormalExit) {
            const bool pushFinished = process->property("pushFinished").toBool();
            const CommandResult result{exitCode, output, {}, started, timedOut};
            logCommand(pushFinished ? "Run Magisk module installer" : "Copy Magisk module", result);
            m_moduleInstallProcess = nullptr;
            process->deleteLater();
            m_moduleInstallProgress->setVisible(false);
            if (isDeviceDisconnected(result)) {
                m_moduleInstallStatus->setText("Installation interrupted: device disconnected.");
                m_moduleInstallLog->appendPlainText(
                    "[" + QDateTime::currentDateTime().toString("HH:mm:ss") +
                    "] Installation interrupted: device disconnected.");
                handleDeviceDisconnected(result);
                return;
            }
            const QString details = QString::fromLocal8Bit(output).trimmed();
            const QString reason = timedOut ? QStringLiteral("The operation timed out.") :
                !started ? QStringLiteral("ADB could not be started.") :
                pushFinished ? QStringLiteral("Magisk could not install the module. Check that Magisk and root access are available.") :
                QStringLiteral("Could not copy the module to the device.");
            m_moduleInstallStatus->setText("Failed: " + reason);
            m_moduleInstallLog->appendPlainText(
                "[" + QDateTime::currentDateTime().toString("HH:mm:ss") +
                "] Installation failed: " + reason);
            appendLog("Magisk module installation failed: " + reason);
            QMessageBox::warning(this, "Magisk module install",
                                 reason + (details.isEmpty() ? QString() :
                                     QStringLiteral("\n\n") + details));
            return;
        }

        const bool pushFinished = process->property("pushFinished").toBool();
        logCommand(pushFinished ? "Run Magisk module installer" : "Copy Magisk module",
                   {exitCode, output, {}, true, false});
        if (!pushFinished) {
            process->setProperty("pushFinished", true);
            process->setProperty("installPhase", "Magisk installer");
            process->setProgram(adbExecutable());
            process->setArguments({"-s", serial, "shell", "su", "-c",
                                   QString("magisk --install-module %1").arg(shellQuote(remotePath))});
            m_moduleInstallStatus->setText("Starting Magisk installer… Approve the root request if prompted.");
            m_moduleInstallLog->appendPlainText(
                "[" + QDateTime::currentDateTime().toString("HH:mm:ss") +
                "] ZIP copied. Starting Magisk installer; approve the superuser request if prompted.");
            appendLog("Magisk module ZIP copied; starting Magisk installer.");
            statusBar()->showMessage("Installing Magisk module on the device…");
            process->start();
            return;
        }

        m_moduleInstallProcess = nullptr;
        process->deleteLater();
        m_moduleInstallProgress->setVisible(false);
        m_moduleInstallStatus->setText(moduleName + " installer completed. Reboot to activate it.");
        m_moduleInstallLog->appendPlainText(
            "[" + QDateTime::currentDateTime().toString("HH:mm:ss") +
            "] Magisk installer completed successfully. Reboot to activate the module.");
        appendLog("Magisk module installer completed successfully: " + moduleName);
        QMessageBox::information(
            this, "Magisk module",
            "The module installer completed successfully. Reboot the device to activate the module.");
        statusBar()->showMessage("Magisk module installed; reboot the device to activate it.", 8000);
        refreshMagiskModules();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        if (m_moduleInstallProcess == process)
            m_moduleInstallProcess = nullptr;
        m_moduleInstallProgress->setVisible(false);
        m_moduleInstallStatus->setText("Could not start ADB: " + process->errorString());
        m_moduleInstallLog->appendPlainText(
            "[" + QDateTime::currentDateTime().toString("HH:mm:ss") +
            "] Could not start ADB: " + process->errorString());
        process->deleteLater();
        QMessageBox::warning(this, "Magisk module install",
                             "Could not start ADB:\n" + process->errorString());
    });
    statusBar()->showMessage("Copying Magisk module to the selected device…");
    process->start();
}
