// SPDX-License-Identifier: GPL-2.0+
// OpenHantek Bode: frequency response with the PSG9080 function generator and the Hantek DSO-2250.

#include <QApplication>
#include <QIcon>
#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QThread>
#include <QTranslator>

#include <memory>

#include "bode/bodewindow.h"
#include "bode/sampletap.h"
#include "hantekdso/dsomodel.h"
#include "hantekdso/hantekdsocontrol.h"
#include "selectdevice/selectsupporteddevice.h"
#include "style/darkstyle.h"
#include "usb/libusbexit.h"
#include "usb/usbdevice.h"

int main(int argc, char *argv[]) {
    QCoreApplication::setOrganizationName("OpenHantek");
    QCoreApplication::setOrganizationDomain("www.openhantek.org");
    QCoreApplication::setApplicationName("OpenHantek Bode");
    QCoreApplication::setApplicationVersion(VERSION);
    QApplication app(argc, argv);
    QGuiApplication::setDesktopFileName("openhantek-bode"); // taskbar icon from openhantek-bode.desktop
    app.setWindowIcon(QIcon(":/icons/openhantek-bode.png"));
    darkstyle::applyApplicationLook(app);

    QTranslator qtTranslator; // Portuguese for the Qt standard dialogs
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QString path = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
#else
    const QString path = QLibraryInfo::location(QLibraryInfo::TranslationsPath);
#endif
    if (qtTranslator.load("qt_" + QLocale::system().name(), path)) app.installTranslator(&qtTranslator);

    // Oscilloscope: same selection and firmware upload as OpenHantek (only one program can use it at a time)
    libusb_context *context = nullptr;
    const int error = libusb_init(&context);
    if (error) {
        SelectSupportedDevice().showLibUSBFailedDialogModel(error);
        return -1;
    }
    std::unique_ptr<USBDevice> device = SelectSupportedDevice().showSelectDeviceModal(context);
    QString errorMessage;
    if (device == nullptr || !device->connectDevice(errorMessage)) {
        device.reset();
        exitLibUsb(context);
        return -1;
    }

    QThread dsoThread;
    dsoThread.setObjectName("dsoControlThread");
    HantekDsoControl dsoControl(device.get());
    dsoControl.moveToThread(&dsoThread);
    QObject::connect(&dsoThread, &QThread::started, &dsoControl, &HantekDsoControl::run);
    QObject::connect(&dsoControl, &HantekDsoControl::communicationError, &app, &QCoreApplication::quit);
    QObject::connect(device.get(), &USBDevice::deviceDisconnected, &app, &QCoreApplication::quit);

    // Every acquisition is copied in the DSO thread and handed to the window
    SampleTap tap;
    QObject::connect(&dsoControl, &HantekDsoControl::samplesAvailable, &tap, &SampleTap::capture,
                     Qt::DirectConnection);

    BodeWindow window(&dsoControl, device->getModel()->spec(), &tap);
    window.show();

    dsoControl.enableSampling(true);
    dsoThread.start();
    const int result = app.exec();

    dsoThread.quit();
    dsoThread.wait(10000);
    device.reset(); // libusb_close() before libusb_exit()
    exitLibUsb(context);
    return result;
}
