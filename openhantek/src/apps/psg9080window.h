// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QDialog>
#include <QMainWindow>

#include "generator/psg9080presets.h"

class GeneratorPanel;
class Psg9080;
class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

/// \brief Asks for a preset name and what to save: both channels, CH1 or CH2.
class PresetDialog : public QDialog {
    Q_OBJECT

  public:
    PresetDialog(QWidget *parent, const QString &name, int scope, const QStringList &existing);
    QString presetName() const;
    /// 0 = both channels, 1 or 2 = that channel
    int scope() const;

  private:
    void accepted();
    QLineEdit *nameEdit;
    QButtonGroup *scopeGroup;
    QPushButton *saveButton;
    QStringList existingNames;
};

/// \brief Main window of the PSG9080 program: generator panel and presets.
class Psg9080Window : public QMainWindow {
    Q_OBJECT

  public:
    explicit Psg9080Window(QWidget *parent = nullptr);

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    void reloadPresets(const QString &select = QString());
    void presetSelected();
    void savePreset();
    void applyPreset();
    void deletePreset();
    void updatePresetButtons();
    void status(const QString &text, bool error = false);

    Psg9080 *gen;
    GeneratorPanel *panel;
    Psg9080PresetStore store;
    QComboBox *presetBox;
    QLabel *targetLabel;
    QComboBox *targetBox;
    QPushButton *applyButton, *saveButton, *deleteButton;
};
