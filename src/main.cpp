#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>

#include <gst/gst.h>

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MindLab"));
    QCoreApplication::setApplicationName(QStringLiteral("CedarView"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.2"));

    MainWindow window;
    window.show();
    return application.exec();
}
