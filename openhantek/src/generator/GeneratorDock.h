// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QDockWidget>

#include <vector>

#include "psg9080.h"

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;

/// \brief Front panel for the PSG9080 function generator: connection and both channels.
///
/// Values are sent on Enter / focus out / arrow steps and read back from the device, so the panel always
/// shows what the generator accepted. While a Bode sweep runs the panel is locked (setLocked).
class GeneratorDock : public QDockWidget {
    Q_OBJECT

  public:
    GeneratorDock(Psg9080 *generator, QWidget *parent);

    /// Lock the controls (e.g. during a frequency sweep); unlocking reads the device again.
    void setLocked(bool locked, const QString &reason = QString());
    /// Read both channels from the device.
    void refresh();

    /// Parse a number typed by the user, accepting a decimal comma ("1,5") or point.
    static bool parseNumber(const QString &text, double &value);
    /// Format a number without trailing zeros, with a decimal comma.
    static QString formatNumber(double value, int maxDecimals = 9);

  private:
    struct ChannelUi {
        QGroupBox *box = nullptr;
        QPushButton *output = nullptr;
        QComboBox *waveform = nullptr;
        QLineEdit *frequency = nullptr;
        QComboBox *unit = nullptr;
        QDoubleSpinBox *amplitude = nullptr;
        QDoubleSpinBox *offset = nullptr;
        QDoubleSpinBox *duty = nullptr;
        QDoubleSpinBox *phase = nullptr;
        double frequencyHz = 0.0; ///< last value read from the device
    };

    QGroupBox *makeChannel(int channel, const QString &color);
    void refreshPorts();
    void toggleConnection();
    void updateConnectionUi();
    void readChannel(int channel);
    void showChannel(int channel, const Psg9080::ChannelState &s);
    void showOutputs(bool ch1, bool ch2);
    /// Run a write; on failure show the message and read the device again.
    void apply(int channel, bool ok);
    void message(const QString &text, bool error = false);

    Psg9080 *gen;
    std::vector<ChannelUi> ui; ///< index 0 = CH1
    QComboBox *portBox = nullptr;
    QPushButton *refreshButton = nullptr;
    QPushButton *connectButton = nullptr;
    QPushButton *readButton = nullptr;
    QPushButton *allOffButton = nullptr;
    QLabel *status = nullptr;
    bool locked = false;
};
