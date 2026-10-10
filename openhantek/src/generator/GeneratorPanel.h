// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QWidget>

#include <vector>

#include "psg9080.h"

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;

/// \brief Panel for the PSG9080 function generator: connection, both channels, read/all-off buttons.
///
/// Values are sent on Enter / focus out / arrow steps and read back from the device, so the panel always
/// shows what the generator accepted. setLocked() disables it while another part drives the generator.
class GeneratorPanel : public QWidget {
    Q_OBJECT

  public:
    /// `channels`: Qt::Horizontal puts CH1 and CH2 side by side, Qt::Vertical stacks them.
    GeneratorPanel(Psg9080 *generator, Qt::Orientation channels, QWidget *parent = nullptr);

    /// Lock the controls (e.g. during a frequency sweep); unlocking reads the device again.
    void setLocked(bool locked, const QString &reason = QString());
    /// Read both channels from the device.
    void refresh();
    /// Show a message in the panel status line.
    void message(const QString &text, bool error = false);
    /// Port selector and Connect button; a window may put it elsewhere (it is reparented out of the panel).
    QWidget *connectionBar() const { return connectionRow; }
    /// Status line; a window may put it elsewhere.
    QLabel *statusLine() const { return status; }

    /// Parse a number typed by the user, accepting a decimal comma ("1,5") or point.
    static bool parseNumber(const QString &text, double &value);
    /// Format a number without trailing zeros, with a decimal comma.
    static QString formatNumber(double value, int maxDecimals = 9);

  signals:
    /// Emitted with every status message (for a status bar).
    void statusMessage(const QString &text, bool error);

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

    Psg9080 *gen;
    std::vector<ChannelUi> ui; ///< index 0 = CH1
    QWidget *connectionRow = nullptr;
    QComboBox *portBox = nullptr;
    QPushButton *refreshButton = nullptr;
    QPushButton *connectButton = nullptr;
    QPushButton *readButton = nullptr;
    QPushButton *allOffButton = nullptr;
    QLabel *status = nullptr;
    bool locked = false;
};
