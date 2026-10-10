// SPDX-License-Identifier: GPL-2.0+

#include "psgwidgets.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>

#include <cmath>

#include "GeneratorPanel.h"
#include "psg9080.h"
#include "style/darkstyle.h"

namespace psgui {

QDoubleSpinBox *spin(int decimals, double min, double max, double step, const QString &suffix) {
    QDoubleSpinBox *box = new QDoubleSpinBox;
    box->setDecimals(decimals);
    box->setRange(min, max);
    box->setSingleStep(step);
    box->setSuffix(suffix);
    box->setKeyboardTracking(false);
    box->setAccelerated(true);
    darkstyle::plainSpin(box);
    return box;
}

QSpinBox *intSpin(int min, int max, const QString &suffix) {
    QSpinBox *box = new QSpinBox;
    box->setRange(min, max);
    box->setSuffix(suffix);
    box->setKeyboardTracking(false);
    box->setAccelerated(true);
    darkstyle::plainSpin(box);
    return box;
}

double spinValue(const QDoubleSpinBox *box) {
    const double scale = std::pow(10.0, box->decimals());
    return std::round(box->value() * scale) / scale;
}

QComboBox *combo(const QStringList &items, const QVector<int> &codes) {
    QComboBox *box = new QComboBox;
    for (int i = 0; i < items.size(); ++i) box->addItem(items[i], i < codes.size() ? codes[i] : i);
    return box;
}

QLabel *hint(const QString &text) {
    QLabel *l = new QLabel(text);
    l->setWordWrap(true);
    l->setProperty("role", "hint");
    l->setStyleSheet("color: #9aa1ab; font-size: 8.5pt;");
    return l;
}

void setRowVisible(QFormLayout *form, QWidget *field, bool visible) {
    if (QWidget *label = form->labelForField(field)) label->setVisible(visible);
    field->setVisible(visible);
}

Form::Form(QWidget *parent) : layout(new QGridLayout(parent)) {
    layout->setHorizontalSpacing(10);
    layout->setVerticalSpacing(6);
    layout->setColumnStretch(1, 1);
}

QLabel *Form::addRow(const QString &label, QWidget *field) {
    const int row = layout->rowCount();
    QLabel *l = label.isEmpty() ? nullptr : new QLabel(label);
    if (l) layout->addWidget(l, row, 0, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(field, row, 1); // fields line up also when the row has no label
    rows.append({field, l});
    return l;
}

QLabel *Form::addRow(const QString &label, QLayout *field) {
    QWidget *holder = new QWidget;
    field->setContentsMargins(0, 0, 0, 0);
    holder->setLayout(field);
    return addRow(label, holder);
}

void Form::setRowVisible(QWidget *field, bool visible) {
    for (const auto &r : rows)
        if (r.first == field) {
            if (r.second) r.second->setVisible(visible);
            field->setVisible(visible);
        }
}

QLabel *Form::labelFor(QWidget *field) const {
    for (const auto &r : rows)
        if (r.first == field) return r.second;
    return nullptr;
}

QString siText(double value, const QString &unit, int digits) {
    const struct {
        double scale;
        const char *prefix;
    } p[] = {{1e9, "G"}, {1e6, "M"}, {1e3, "k"}, {1, ""}, {1e-3, "m"}, {1e-6, "µ"}, {1e-9, "n"}};
    if (value == 0 || !std::isfinite(value)) return "0 " + unit;
    for (const auto &x : p)
        if (std::fabs(value) >= x.scale * 0.9999999)
            return QString::number(value / x.scale, 'g', digits).replace('.', ',') + " " + QString::fromUtf8(x.prefix) +
                   unit;
    return QString::number(value / 1e-9, 'g', digits).replace('.', ',') + " n" + unit;
}

// ------------------------------------------------------------------------------------------------ UnitField
QVector<UnitField::Unit> UnitField::frequencyUnits() {
    return {{"mHz", 1e-3}, {"Hz", 1}, {"kHz", 1e3}, {"MHz", 1e6}};
}

QVector<UnitField::Unit> UnitField::timeUnits() {
    return {{"ns", 1e-9}, {QString::fromUtf8("µs"), 1e-6}, {"ms", 1e-3}, {"s", 1}};
}

UnitField::UnitField(const QVector<Unit> &u, QWidget *parent) : QWidget(parent), units(u) {
    edit = new QLineEdit;
    edit->setAlignment(Qt::AlignRight);
    unitBox = new QComboBox;
    for (const Unit &x : units) unitBox->addItem(x.name);
    QHBoxLayout *l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    l->addWidget(edit, 1);
    l->addWidget(unitBox);
    connect(edit, &QLineEdit::editingFinished, this, [this]() {
        if (edit->isModified()) commit();
    });
    connect(unitBox, QOverload<int>::of(&QComboBox::activated), this, [this](int) { commit(); });
}

void UnitField::setRange(double min, double max) {
    minimum = min;
    maximum = max;
}

void UnitField::setValue(double base) {
    current = base;
    int best = 0;
    for (int i = 0; i < units.size(); ++i)
        if (std::fabs(base) >= units[i].factor * 0.9999999) best = i;
    QSignalBlocker b(unitBox);
    unitBox->setCurrentIndex(best);
    edit->setText(GeneratorPanel::formatNumber(base / units[best].factor, 6));
    edit->setModified(false);
}

void UnitField::commit() {
    edit->setModified(false);
    double v = 0;
    if (!GeneratorPanel::parseNumber(edit->text(), v)) {
        emit invalid(edit->text());
        setValue(current);
        return;
    }
    const double base = v * units[unitBox->currentIndex()].factor;
    if (base < minimum * 0.9999999 || base > maximum * 1.0000001) {
        emit invalid(edit->text() + " " + unitBox->currentText());
        setValue(current);
        return;
    }
    current = base;
    emit edited(base);
}

// ------------------------------------------------------------------------------------------------ GeneratorTab
GeneratorTab::GeneratorTab(Psg9080 *generator, QWidget *parent) : QWidget(parent), gen(generator) {
    connect(gen, &Psg9080::connectionChanged, this, [this](bool open) { connectionChanged(open); });
    setEnabled(false);
}

void GeneratorTab::connectionChanged(bool open) {
    setEnabled(open);
    needsRefresh = true;
    if (open && isVisible()) {
        needsRefresh = false;
        refresh();
    }
}

void GeneratorTab::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (gen->isOpen()) { // read again every time the tab is opened: the knobs may have changed things
        needsRefresh = false;
        refresh();
    }
}

bool GeneratorTab::check(bool ok) {
    if (!ok) emit statusMessage(gen->lastError(), true);
    return ok;
}

} // namespace psgui
