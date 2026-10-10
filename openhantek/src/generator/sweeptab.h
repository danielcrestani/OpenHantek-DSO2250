// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QElapsedTimer>

#include "psgwidgets.h"

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QTimer;

/// \brief The device's own sweep (frequency, amplitude or duty over time) and voltage control (VCO, Ext.IN 0..5 V).
///
/// The device sweeps by itself; this tab sets the limits and starts/stops it. For a frequency response with the
/// oscilloscope use OpenHantek Bode instead (it measures every point).
class SweepTab : public psgui::GeneratorTab {
    Q_OBJECT

  public:
    explicit SweepTab(Psg9080 *generator, QWidget *parent = nullptr);
    ~SweepTab() override;
    void refresh() override;

  private:
    void updateRows();
    void start();
    void stop();
    void showRunning(bool running);
    bool isVco() const;

    QComboBox *modeBox, *channelBox, *objectBox, *directionBox, *scaleBox;
    psgui::UnitField *startFreq, *endFreq;
    QDoubleSpinBox *startAmp, *endAmp, *startDuty, *endDuty, *timeBox;
    QPushButton *runButton;
    QLabel *elapsedLabel, *explain;
    psgui::Form *form;
    QTimer *ticker;
    QElapsedTimer clock;
    bool running = false;
};
