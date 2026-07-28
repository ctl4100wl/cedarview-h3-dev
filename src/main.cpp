#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>

#include <gst/gst.h>

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MindLab"));
    QCoreApplication::setApplicationName(QStringLiteral("CedarView"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.3.5"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Lightweight RTSP camera wall"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption fullscreenOption(
        {QStringLiteral("f"), QStringLiteral("fullscreen")},
        QStringLiteral("Start directly in fullscreen mode."));
    parser.addOption(fullscreenOption);
    parser.process(application);

    MainWindow window(parser.isSet(fullscreenOption));
    window.show();
    return application.exec();
}
