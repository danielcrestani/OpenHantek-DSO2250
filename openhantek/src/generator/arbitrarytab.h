// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QString>
#include <QVector>

#include <vector>

#include "arbwave.h"
#include "psgwidgets.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

/// \brief Black screen with one period of the waveform (what will be sent: 8192 points, 14 bits).
class WavePreview : public QWidget {
  public:
    explicit WavePreview(QWidget *parent = nullptr);
    void setWave(const std::vector<double> &values, const QString &caption);

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    std::vector<double> wave; ///< -1..+1
    QString text;
};

/// \brief Arbitrary waveforms: create from a formula or a file (also OpenHantek captures), read from and write to
/// the 99 slots of the generator, use a slot on a channel.
class ArbitraryTab : public psgui::GeneratorTab {
    Q_OBJECT

  public:
    explicit ArbitraryTab(Psg9080 *generator, QWidget *parent = nullptr);
    void refresh() override {}
    /// Load values (any length, any unit) as the current waveform; used by the formula, files and the tests.
    void setSource(const std::vector<double> &values, const QString &name, bool exactCodes = false);

  protected:
    void connectionChanged(bool open) override;

  private:
    void generate();
    void openFile();
    void pickFromTable();
    void saveFile();
    void readSlot();
    void writeSlot();
    void useOn(int channel);
    std::vector<int> codes() const;
    void updatePreview();
    void say(const QString &text, bool error = false) { emit statusMessage(text, error); }

    QComboBox *exampleBox, *columnBox, *scalingBox;
    QLineEdit *formulaEdit;
    QSpinBox *acquisitionBox, *slotBox;
    QCheckBox *invertBox;
    QFormLayout *fileForm;
    QLabel *fileLabel;
    WavePreview *preview;
    QPushButton *readButton, *writeButton, *useButtons[2], *saveButton;

    std::vector<double> source; ///< 8192 points, any unit
    QString sourceName;
    arbwave::Table table;
};
