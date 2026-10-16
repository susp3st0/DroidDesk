#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QSslSocket>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName("DroidDesk");
#if defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
    const QStringList tlsBackends = QSslSocket::availableBackends();
    if (tlsBackends.contains(QStringLiteral("schannel"))) {
        if (!QSslSocket::setActiveBackend(QStringLiteral("schannel")))
            qWarning("Could not activate Qt's Windows Schannel TLS backend.");
    } else {
        qWarning() << "Qt's Schannel TLS backend is unavailable. Available backends:"
                   << tlsBackends;
    }
#endif
    QCoreApplication::setApplicationName("adbtools");
    MainWindow w;
    w.show();
    return QApplication::exec();
}
