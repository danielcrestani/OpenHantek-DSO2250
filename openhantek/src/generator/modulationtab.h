// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include "psgwidgets.h"

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QSpinBox;

/// \brief AM, FM, PM, ASK, FSK, PSK, pulse (PWM) and burst of one channel; the carrier is the channel itself.
///
/// Every field is written as soon as it is edited, like the Básico tab. Modulation is active while the device
/// shows the modulation screen of the channel (register 24), so the ON button switches that screen.
class ModulationTab : public psgui::GeneratorTab {
    Q_OBJECT

  public:
    explicit ModulationTab(Psg9080 *generator, QWidget *parent = nullptr);
    void refresh() override;

  private:
    int channel() const;
    void updateRows();
    void showActive(bool on);
    void toggle(bool on);
    /// Write a pair register for the selected channel and read the tab again on failure.
    void writePair(int code, int value);
    void writeScaled(int baseCode, double value, double scale);

    QComboBox *channelBox, *typeBox, *sourceBox, *waveBox, *polarityBox, *invertBox, *idleBox, *triggerBox;
    psgui::UnitField *rate, *fmDeviation, *fskFrequency, *pulseWidth, *pulsePeriod;
    QDoubleSpinBox *depth, *phase;
    QSpinBox *cycles;
    QPushButton *onButton, *fireButton;
    QFormLayout *form;
    QLabel *explain;
};
