// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QJsonArray>

#include "psgwidgets.h"

class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

/// \brief Programmed sequence: steps that set a channel (waveform, frequency, amplitude, offset, output) and wait.
///
/// Empty cells keep what the generator has. The table is kept between runs of the program and can be saved to a
/// JSON file. The PC times the steps (about 10 ms precision), so it suits tests of seconds to hours.
class SequenceTab : public psgui::GeneratorTab {
    Q_OBJECT

  public:
    explicit SequenceTab(Psg9080 *generator, QWidget *parent = nullptr);
    ~SequenceTab() override;
    void refresh() override {}

    QJsonArray toJson() const;
    void fromJson(const QJsonArray &steps);
    /// Parse "1,5 k", "2 MHz", "500 mV"... with SI prefixes; the unit text is optional. False if not a number.
    static bool parseValue(const QString &text, const QString &unit, double &value);

  protected:
    void connectionChanged(bool open) override;

  private:
    void addRow(const QJsonObject &step);
    void addFromGenerator();
    void moveRow(int delta);
    void start();
    void stop(const QString &why = QString());
    void runStep();
    bool applyStep(int row, QString &error);
    void updateButtons();
    void save();
    void load();

    QTableWidget *table;
    QSpinBox *repeatBox;
    QPushButton *runButton, *addButton, *copyButton, *removeButton, *upButton, *downButton, *saveButton, *loadButton;
    QLabel *progress;
    QTimer *timer;
    int current = -1, cycle = 0;
    bool running = false;
};
