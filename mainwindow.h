#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QStringList>
#include <QVector>
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QTimer;
class QListWidgetItem;
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
        QString propUrl;
        QString notesUrl;
        QString zipUrl;
        QString name;
        QString description;
        QString version;
        QString author;
        int stars = 0;
        qint64 lastUpdated = 0;
    };

    CommandResult runAdb(const QStringList &arguments, int timeoutMs = 30000);
    CommandResult runDevice(const QStringList &arguments, int timeoutMs = 30000);
    void buildUi();
    void refreshDevices();
    void refreshDeviceDetails();
    void refreshApps();
    void executeShellCommand();
    void startLogcat();
    void stopLogcat();
    void appendLogcatData();
    void installMagiskModule(const QString &path);
    void refreshMagiskModules();
    void setMagiskModuleEnabled(bool enabled);
    void removeMagiskModule();
    void loadModuleCatalog();
    void showModuleDetails(QListWidgetItem *item);
    void downloadSelectedModule();
    void filterModuleCatalog(const QString &text);
    void logCommand(const QString &label, const CommandResult &result);
    void showResult(const QString &successMessage, const CommandResult &result);
    bool isDeviceDisconnected(const CommandResult &result) const;
    void handleDeviceDisconnected(const CommandResult &result);
    QString selectedSerial() const;
    QString adbExecutable() const;
    QString shellQuote(const QString &value) const;
    void appendLog(const QString &text);

    QComboBox *m_devices = nullptr;
    QComboBox *m_systemApps = nullptr;
    QProcess *m_deviceScan = nullptr;
    QProcess *m_deviceInfoQuery = nullptr;
    QProcess *m_logcatProcess = nullptr;
    QProcess *m_moduleInstallProcess = nullptr;
    QLabel *m_connectionLabel = nullptr;
    QLabel *m_deviceNameLabel = nullptr;
    QLabel *m_androidLabel = nullptr;
    QLabel *m_serialLabel = nullptr;
    QLabel *m_resolutionLabel = nullptr;
    QLabel *m_densityLabel = nullptr;
    QLabel *m_batteryLabel = nullptr;
    QLineEdit *m_appFilter = nullptr;
    QListWidget *m_appList = nullptr;
    QLineEdit *m_remotePath = nullptr;
    QLineEdit *m_shellInput = nullptr;
    QLineEdit *m_rootShellInput = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QPlainTextEdit *m_logcatOutput = nullptr;
    QPlainTextEdit *m_rootOutput = nullptr;
    QListWidget *m_magiskModules = nullptr;
    QLineEdit *m_moduleSearch = nullptr;
    QListWidget *m_moduleCatalog = nullptr;
    QPlainTextEdit *m_moduleDetails = nullptr;
    QLabel *m_moduleStoreStatus = nullptr;
    QPushButton *m_moduleDownload = nullptr;
    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_catalogReply = nullptr;
    QProcess *m_catalogCurlProcess = nullptr;
    QNetworkReply *m_moduleMetadataReply = nullptr;
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
};

#endif
