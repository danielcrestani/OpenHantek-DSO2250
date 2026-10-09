// SPDX-License-Identifier: GPL-2.0+
// PSG9080: control of the Joy-IT / JunTek PSG9080 function generator.

#include <QApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

#include "psg9080window.h"
#include "style/darkstyle.h"

int main(int argc, char *argv[]) {
    QCoreApplication::setOrganizationName("psg9080-gui");
    QCoreApplication::setApplicationName("PSG9080");
    QCoreApplication::setApplicationVersion(VERSION);
    QApplication app(argc, argv);
    QGuiApplication::setDesktopFileName("psg9080"); // taskbar icon from psg9080.desktop
    app.setWindowIcon(QIcon(":/icons/psg9080.png"));
    darkstyle::applyApplicationLook(app);

    QTranslator qtTranslator; // Portuguese for the Qt standard dialogs
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QString path = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
#else
    const QString path = QLibraryInfo::location(QLibraryInfo::TranslationsPath);
#endif
    if (qtTranslator.load("qt_" + QLocale::system().name(), path)) app.installTranslator(&qtTranslator);

    Psg9080Window window;
    window.show();
    return app.exec();
}
