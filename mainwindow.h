#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QProcess>
#include <QStringList>
#include <QVector>
class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProcess;
class QProgressBar;
class QPushButton;
class QToolButton;
class QTimer;
class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    struct CommandResult {
        int exitCode = -1;
        QByteArray output;
        QByteArray error;
        bool started = false;
        bool timedOut = false;
    };

    struct ModuleCatalogEntry {
        QString id;
        QString zipUrl;
    };

    CommandResult runAdb(const QStringList &arguments, int timeoutMs = 30000);
    CommandResult runDevice(const QStringList &arguments, int timeoutMs = 30000);
    void buildUi();
    void refreshDevices(bool periodic = false);
    void refreshDeviceDetails();
    void refreshApps();
    void installApk(const QString &path);
    void launchScrcpy();
    void executeShellCommand();
    void startLogcat();
    void stopLogcat();
    void appendLogcatData();
    void installMagiskModule(const QString &path);
    void refreshMagiskModules();
    void setMagiskModuleEnabled(bool enabled);
    void removeMagiskModule();
    void checkRootStatus();
    void runSelinuxCommand(bool changeMode, bool enforcing);
    void applyTheme(const QString &theme);
    void loadModuleCatalog();
    void downloadSelectedModule();
    void filterModuleCatalog(const QString &text);
    void logCommand(const QString &label, const CommandResult &result);
    void showResult(const QString &successMessage, const CommandResult &result);
    bool isDeviceDisconnected(const CommandResult &result) const;
    void handleDeviceDisconnected(const CommandResult &result);
    QString selectedSerial() const;
    QString adbExecutable() const;
    QString scrcpyExecutable() const;
    QString shellQuote(const QString &value) const;
    void appendLog(const QString &text);

    QComboBox *m_devices = nullptr;
    QComboBox *m_systemApps = nullptr;
    QToolButton *m_themeToggle = nullptr;
    QProcess *m_deviceScan = nullptr;
    QTimer *m_devicePollTimer = nullptr;
    QProcess *m_deviceInfoQuery = nullptr;
    QProcess *m_appListProcess = nullptr;
    QProcess *m_appInstallProcess = nullptr;
    QProcess *m_scrcpyProcess = nullptr;
    QProcess *m_logcatProcess = nullptr;
    QProcess *m_moduleInstallProcess = nullptr;
    QProcess *m_rootCheckProcess = nullptr;
    QProcess *m_selinuxProcess = nullptr;
    QLabel *m_connectionLabel = nullptr;
    QLabel *m_deviceNameLabel = nullptr;
    QLabel *m_androidLabel = nullptr;
    QLabel *m_serialLabel = nullptr;
    QLabel *m_resolutionLabel = nullptr;
    QLabel *m_densityLabel = nullptr;
    QLabel *m_batteryLabel = nullptr;
    QLineEdit *m_appFilter = nullptr;
    QListWidget *m_appList = nullptr;
    QPushButton *m_installAppButton = nullptr;
    QComboBox *m_scrcpyMode = nullptr;
    QComboBox *m_scrcpyMaxSize = nullptr;
    QComboBox *m_scrcpyMaxFps = nullptr;
    QLineEdit *m_scrcpyVirtualSize = nullptr;
    QLineEdit *m_scrcpyRecordingPath = nullptr;
    QCheckBox *m_scrcpyRecording = nullptr;
    QCheckBox *m_scrcpyFlexDisplay = nullptr;
    QCheckBox *m_scrcpyStayAwake = nullptr;
    QCheckBox *m_scrcpyTurnScreenOff = nullptr;
    QCheckBox *m_scrcpyAudio = nullptr;
    QCheckBox *m_scrcpyAlwaysOnTop = nullptr;
    QCheckBox *m_scrcpyShowTouches = nullptr;
    QCheckBox *m_scrcpyOtgGamepad = nullptr;
    QPushButton *m_scrcpyBrowseRecording = nullptr;
    QPushButton *m_scrcpyStartButton = nullptr;
    QLabel *m_scrcpyStatusLabel = nullptr;
    QLabel *m_appInstallStatus = nullptr;
    QProgressBar *m_appInstallProgress = nullptr;
    QLineEdit *m_remotePath = nullptr;
    QLineEdit *m_shellInput = nullptr;
    QLineEdit *m_rootShellInput = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QPlainTextEdit *m_logcatOutput = nullptr;
    QPlainTextEdit *m_rootOutput = nullptr;
    QLabel *m_rootStatusLabel = nullptr;
    QLabel *m_selinuxStatusLabel = nullptr;
    QLabel *m_moduleInstallStatus = nullptr;
    QPlainTextEdit *m_moduleInstallLog = nullptr;
    QProgressBar *m_moduleInstallProgress = nullptr;
    QPushButton *m_selinuxCheckButton = nullptr;
    QPushButton *m_selinuxEnforcingButton = nullptr;
    QPushButton *m_selinuxPermissiveButton = nullptr;
    QListWidget *m_magiskModules = nullptr;
    QLineEdit *m_moduleSearch = nullptr;
    QListWidget *m_moduleCatalog = nullptr;
    QLabel *m_moduleStoreStatus = nullptr;
    QComboBox *m_moduleArchiveFormat = nullptr;
    QPushButton *m_moduleDownload = nullptr;
    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_catalogReply = nullptr;
    QProcess *m_catalogCurlProcess = nullptr;
    QNetworkReply *m_moduleDownloadReply = nullptr;
    QSaveFile *m_moduleDownloadFile = nullptr;
    QString m_moduleDownloadPath;
    QByteArray m_moduleDownloadPrefix;
    QVector<ModuleCatalogEntry> m_catalogEntries;
    void processModuleCatalogResponse(const QByteArray &data, const QUrl &finalUrl,
                                      const QString &error);
    QLineEdit *m_logcatFilter = nullptr;
    QComboBox *m_logcatLevel = nullptr;
    QPushButton *m_logcatToggle = nullptr;
    QByteArray m_logcatBuffer;
    QStringList m_pendingLogcatLines;
    QTimer *m_logcatFlushTimer = nullptr;
    int m_droppedLogcatLines = 0;
    QMap<QString, QString> m_deviceStates;
    bool m_deviceSnapshotInitialized = false;
};

#endif
