// SPDX-License-Identifier: GPL-2.0+

#include "psg9080window.h"

#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QVBoxLayout>

#include <algorithm>

#include "generator/GeneratorPanel.h"
#include "generator/psg9080.h"
#include "style/darkstyle.h"

// ------------------------------------------------------------------------------------------------ PresetDialog
PresetDialog::PresetDialog(QWidget *parent, const QString &name, int scope, const QStringList &existing)
    : QDialog(parent), existingNames(existing) {
    setWindowTitle(tr("Salvar preset"));
    setMinimumWidth(440);
    setAutoFillBackground(true); // some themes leave dialogs see-through otherwise
    setStyleSheet(darkstyle::panelSheet());

    nameEdit = new QLineEdit(name);
    nameEdit->setPlaceholderText(tr("ex.: quadrada 1 kHz 5 V"));
    nameEdit->setClearButtonEnabled(true);
    nameEdit->setMinimumHeight(30);
    nameEdit->selectAll();

    scopeGroup = new QButtonGroup(this);
    QHBoxLayout *scopeRow = new QHBoxLayout;
    scopeRow->setSpacing(18);
    const struct {
        int id;
        const char *label;
    } options[] = {{0, "Ambos"}, {1, "CH1"}, {2, "CH2"}};
    for (const auto &o : options) {
        QRadioButton *b = new QRadioButton(tr(o.label));
        b->setChecked(o.id == scope);
        scopeGroup->addButton(b, o.id);
        scopeRow->addWidget(b);
    }
    scopeRow->addStretch(1);

    // One line that follows the selection (a wrapped label inside a form gets clipped)
    QLabel *hint = new QLabel;
    QFont small = hint->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    hint->setFont(small);
    const QColor fg = palette().color(QPalette::WindowText), bg = palette().color(QPalette::Window);
    hint->setStyleSheet(QString("color: rgb(%1, %2, %3);")
                            .arg(qRound(0.65 * fg.red() + 0.35 * bg.red()))
                            .arg(qRound(0.65 * fg.green() + 0.35 * bg.green()))
                            .arg(qRound(0.65 * fg.blue() + 0.35 * bg.blue())));
    auto updateHint = [this, hint]() {
        hint->setText(scopeGroup->checkedId() == 0 ? tr("Salva os dois canais juntos.")
                                                   : tr("Pode ser aplicado depois no CH1 ou no CH2."));
    };
    connect(scopeGroup, &QButtonGroup::idToggled, this, [updateHint](int, bool on) {
        if (on) updateHint();
    });
    updateHint();

    QFormLayout *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(12);
    form->addRow(tr("Nome"), nameEdit);
    form->addRow(tr("Canais"), scopeRow);
    form->addRow(QString(), hint);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    saveButton = buttons->button(QDialogButtonBox::Save);
    saveButton->setText(tr("Salvar"));
    saveButton->setDefault(true);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    connect(buttons, &QDialogButtonBox::accepted, this, &PresetDialog::accepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(nameEdit, &QLineEdit::textChanged, this,
            [this](const QString &t) { saveButton->setEnabled(!t.trimmed().isEmpty()); });
    saveButton->setEnabled(!name.trimmed().isEmpty());

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(16);
    layout->addLayout(form);
    layout->addWidget(buttons);
    nameEdit->setFocus();
}

void PresetDialog::accepted() {
    const QString name = presetName();
    if (existingNames.contains(name) &&
        QMessageBox::question(this, tr("Salvar preset"), tr("Já existe um preset \"%1\". Substituir?").arg(name)) !=
            QMessageBox::Yes)
        return;
    accept();
}

QString PresetDialog::presetName() const { return nameEdit->text().trimmed(); }

int PresetDialog::scope() const { return scopeGroup->checkedId(); }

// ------------------------------------------------------------------------------------------------ Psg9080Window
Psg9080Window::Psg9080Window(QWidget *parent) : QMainWindow(parent), gen(new Psg9080(this)) {
    setWindowTitle(tr("PSG9080 — gerador de funções"));

    panel = new GeneratorPanel(gen, Qt::Horizontal);

    presetBox = new QComboBox;
    presetBox->setMinimumWidth(220);
    targetLabel = new QLabel(tr("em"));
    targetBox = new QComboBox;
    targetBox->addItem("CH1", 1);
    targetBox->addItem("CH2", 2);
    targetBox->setToolTip(tr("Canal que recebe um preset de um canal só"));
    applyButton = new QPushButton(tr("Aplicar"));
    saveButton = new QPushButton(tr("Salvar atual…"));
    deleteButton = new QPushButton(tr("Excluir"));
    QGroupBox *presetGroup = new QGroupBox(tr("Presets"));
    darkstyle::colorSection(presetGroup, darkstyle::violet());
    QHBoxLayout *presets = new QHBoxLayout(presetGroup);
    presets->addWidget(presetBox, 1);
    presets->addWidget(targetLabel);
    presets->addWidget(targetBox);
    presets->addWidget(applyButton);
    presets->addWidget(saveButton);
    presets->addWidget(deleteButton);

    QWidget *root = new QWidget;
    root->setStyleSheet(darkstyle::panelSheet());
    QVBoxLayout *layout = new QVBoxLayout(root);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->addWidget(panel, 1);
    layout->addWidget(presetGroup);
    setCentralWidget(root);

    connect(presetBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { presetSelected(); });
    connect(applyButton, &QPushButton::clicked, this, &Psg9080Window::applyPreset);
    connect(saveButton, &QPushButton::clicked, this, &Psg9080Window::savePreset);
    connect(deleteButton, &QPushButton::clicked, this, &Psg9080Window::deletePreset);
    connect(gen, &Psg9080::connectionChanged, this, [this](bool) { updatePresetButtons(); });

    resize(820, 440); // first run; a saved size replaces it below
    QSettings s;
    restoreGeometry(s.value("window/geometry").toByteArray());
    reloadPresets(s.value("window/preset").toString());
    updatePresetButtons();
}

void Psg9080Window::closeEvent(QCloseEvent *event) {
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/preset", presetBox->currentData().toString());
    QMainWindow::closeEvent(event);
}

void Psg9080Window::status(const QString &text, bool error) { panel->message(text, error); }

void Psg9080Window::reloadPresets(const QString &select) {
    QString error;
    const QMap<QString, Psg9080Preset> all = store.load(&error);
    {
        QSignalBlocker block(presetBox);
        presetBox->clear();
        QStringList names = all.keys();
        std::sort(names.begin(), names.end(),
                  [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
        for (const QString &name : names) presetBox->addItem(QString("%1   [%2]").arg(name, all[name].label()), name);
        const int index = presetBox->findData(select);
        presetBox->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (!error.isEmpty()) status(error, true);
    presetSelected();
}

void Psg9080Window::presetSelected() {
    const QMap<QString, Psg9080Preset> all = store.load();
    const QString name = presetBox->currentData().toString();
    const bool single = all.contains(name) && all[name].isSingle();
    targetLabel->setVisible(single);
    targetBox->setVisible(single);
    if (single) targetBox->setCurrentIndex(targetBox->findData(all[name].source));
    updatePresetButtons();
}

void Psg9080Window::updatePresetButtons() {
    const bool connected = gen->isOpen(), selected = presetBox->count() > 0;
    applyButton->setEnabled(connected && selected);
    saveButton->setEnabled(connected);
    deleteButton->setEnabled(selected); // works offline
    targetBox->setEnabled(connected);
}

void Psg9080Window::savePreset() {
    const QMap<QString, Psg9080Preset> all = store.load();
    const QString current = presetBox->currentData().toString();
    const int scope = all.contains(current) && all[current].isSingle() ? all[current].source : 0;
    PresetDialog dialog(this, current, scope, all.keys());
    if (dialog.exec() != QDialog::Accepted) return;

    Psg9080Preset preset;
    const int channels = dialog.scope();
    for (int ch = 1; ch <= 2; ++ch) {
        if (channels != 0 && ch != channels) continue;
        Psg9080::ChannelState s;
        if (!gen->readChannel(ch, s)) {
            status(gen->lastError(), true);
            return;
        }
        preset.states.push_back(s);
    }
    preset.source = channels == 0 ? 1 : channels;
    QString error;
    if (!store.put(dialog.presetName(), preset, &error)) {
        status(error, true);
        return;
    }
    reloadPresets(dialog.presetName());
    status(tr("Preset \"%1\" salvo (%2).").arg(dialog.presetName(), preset.label()));
}

void Psg9080Window::applyPreset() {
    const QMap<QString, Psg9080Preset> all = store.load();
    const QString name = presetBox->currentData().toString();
    if (!all.contains(name)) return;
    const Psg9080Preset &p = all[name];
    const int target = p.isSingle() ? targetBox->currentData().toInt() : 0;
    const bool ok = p.apply(gen, target);
    panel->refresh();
    if (ok)
        status(p.isSingle() ? tr("Preset \"%1\" aplicado no CH%2.").arg(name).arg(target)
                            : tr("Preset \"%1\" aplicado.").arg(name));
    else
        status(gen->lastError(), true);
}

void Psg9080Window::deletePreset() {
    const QString name = presetBox->currentData().toString();
    if (name.isEmpty() ||
        QMessageBox::question(this, tr("Excluir preset"), tr("Excluir o preset \"%1\"?").arg(name)) != QMessageBox::Yes)
        return;
    QString error;
    if (!store.remove(name, &error)) status(error, true);
    reloadPresets();
}
