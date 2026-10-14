#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName("DroidDesk");
    QCoreApplication::setApplicationName("adbtools");
    MainWindow w;
    w.show();
    return QApplication::exec();
}
