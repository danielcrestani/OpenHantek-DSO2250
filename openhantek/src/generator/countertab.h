// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include "psgwidgets.h"

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QTimer;

/// \brief Frequency meter and pulse counter of the Ext.IN input (1 Hz to 100 MHz, 2 to 20 Vpp), read live.
class CounterTab : public psgui::GeneratorTab {
    Q_OBJECT

  public:
    explicit CounterTab(Psg9080 *generator, QWidget *parent = nullptr);
    void refresh() override;

  protected:
    void hideEvent(QHideEvent *event) override;
    void connectionChanged(bool open) override;

  private:
    void start();
    void stop(bool restoreScreen = true);
    void poll();
    void updateRows();
    bool isCounter() const;

    QComboBox *functionBox, *couplingBox, *rangeBox;
    QDoubleSpinBox *gateBox;
    QFormLayout *form;
    QPushButton *runButton;
    QLabel *mainValue, *mainCaption, *period, *positive, *negative, *duty;
    QWidget *details;
    QTimer *timer;
    bool running = false;
};
