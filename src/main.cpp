#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MindLab"));
    QCoreApplication::setApplicationName(QStringLiteral("CedarView"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.5"));

    MainWindow window;
    window.show();
    return application.exec();
}
