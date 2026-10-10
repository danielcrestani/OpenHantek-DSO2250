// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <map>
#include <string>
#include <vector>

#include "psgwidgets.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

/// \brief Device information, CH1→CH2 synchronization, memory slots, display/sound settings and a register table
/// for diagnosis (reads registers 0..90 at once and marks what changed since the last reading).
class SystemTab : public psgui::GeneratorTab {
    Q_OBJECT

  public:
    explicit SystemTab(Psg9080 *generator, QWidget *parent = nullptr);
    void refresh() override;

  private:
    void writeSync();
    void memory(int op);
    void readRegisters();

    QLabel *model, *serial, *versions;
    QCheckBox *sync[6];
    QSpinBox *trimBox, *slotBox, *brightnessBox;
    QCheckBox *soundBox;
    QComboBox *languageBox, *loadingBox;
    QTableWidget *registers;
    std::map<int, std::vector<std::string>> lastRegisters;
};
