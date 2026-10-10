// SPDX-License-Identifier: GPL-2.0+

#include "sequencetab.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

#include "GeneratorPanel.h"
#include "psg9080.h"
#include "style/darkstyle.h"

using namespace psg9080;

namespace {
enum Column { CHANNEL, WAVEFORM, FREQUENCY, AMPLITUDE, OFFSET, OUTPUT, DURATION, COLUMNS };

QComboBox *channelCombo() { return psgui::combo({"CH1", "CH2", QObject::tr("Ambos")}, {1, 2, 0}); }

QComboBox *waveformCombo() {
    QComboBox *b = new QComboBox;
    b->addItem(QObject::tr("(manter)"), -1);
    for (int c = 0; c < kBuiltinWaveforms; ++c) b->addItem(QString::fromUtf8(waveformName(c)), c);
    for (int s = 1; s <= 99; ++s) b->addItem(QObject::tr("Arbitrária %1").arg(s, 2, 10, QChar('0')), 100 + s);
    b->setMaxVisibleItems(20);
    return b;
}

QComboBox *outputCombo() { return psgui::combo({QObject::tr("(manter)"), QObject::tr("Ligar"), QObject::tr("Desligar")}); }

QString number(double v) { return GeneratorPanel::formatNumber(v, 6); }
} // namespace

bool SequenceTab::parseValue(const QString &text, const QString &unit, double &value) {
    QString t = text.trimmed();
    // "1 Vpp", "1 V", "2 kHz", "1,5 s": the unit is optional
    for (const QString &u : {unit, unit.startsWith('V') ? QString("V") : QString()})
        if (!u.isEmpty() && t.endsWith(u, Qt::CaseInsensitive)) {
            t.chop(u.size());
            break;
        }
    t = t.trimmed();
    double scale = 1;
    if (!t.isEmpty()) {
        const QChar last = t.back();
        const struct {
            QChar c;
            double s;
        } prefixes[] = {{'G', 1e9}, {'M', 1e6}, {'k', 1e3}, {'K', 1e3}, {'m', 1e-3}, {'u', 1e-6}, {QChar(0xb5), 1e-6},
                        {QChar(0x3bc), 1e-6}, {'n', 1e-9}};
        for (const auto &p : prefixes)
            if (last == p.c) {
                scale = p.s;
                t.chop(1);
                break;
            }
    }
    if (!GeneratorPanel::parseNumber(t, value)) return false;
    value *= scale;
    return true;
}

SequenceTab::SequenceTab(Psg9080 *generator, QWidget *parent) : GeneratorTab(generator, parent) {
    table = new QTableWidget(0, COLUMNS);
    table->setHorizontalHeaderLabels({tr("Canal"), tr("Forma de onda"), tr("Frequência"), tr("Amplitude (Vpp)"),
                                      tr("Offset (V)"), tr("Saída"), tr("Duração (s)")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(CHANNEL, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(OUTPUT, QHeaderView::ResizeToContents);
    table->verticalHeader()->setDefaultSectionSize(30);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setStyleSheet("QTableWidget { background: #11141a; color: #f0f2f5; gridline-color: #2c3139;"
                         "  border: 1px solid #3a404a; selection-background-color: #2f6fbf; }"
                         "QHeaderView::section { background: #2b3038; color: #c8ccd4; border: none;"
                         "  border-right: 1px solid #3a404a; padding: 4px; }"
                         "QTableWidget QComboBox { border-radius: 0; }");

    addButton = new QPushButton(tr("+ Passo"));
    copyButton = new QPushButton(tr("Copiar do gerador"));
    copyButton->setToolTip(tr("Novo passo com o estado atual do CH1 (ou do canal da linha selecionada)"));
    removeButton = new QPushButton(tr("Remover"));
    upButton = new QPushButton(QString::fromUtf8("↑"));
    downButton = new QPushButton(QString::fromUtf8("↓"));
    for (QPushButton *b : {upButton, downButton}) b->setFixedWidth(36);
    loadButton = new QPushButton(tr("Abrir…"));
    saveButton = new QPushButton(tr("Salvar…"));
    QHBoxLayout *edit = new QHBoxLayout;
    for (QPushButton *b : {addButton, copyButton, removeButton, upButton, downButton}) edit->addWidget(b);
    edit->addStretch(1);
    edit->addWidget(loadButton);
    edit->addWidget(saveButton);

    repeatBox = psgui::intSpin(0, 1000000, tr(" vez(es)"));
    repeatBox->setSpecialValueText(tr("sem parar"));
    repeatBox->setValue(1);
    runButton = new QPushButton;
    progress = new QLabel;
    progress->setStyleSheet("color: #9aa1ab;");
    QHBoxLayout *run = new QHBoxLayout;
    run->addWidget(new QLabel(tr("Repetir")));
    run->addWidget(repeatBox);
    run->addWidget(runButton, 1);
    run->addWidget(progress, 1);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(edit);
    layout->addWidget(table, 1);
    layout->addLayout(run);
    layout->addWidget(psgui::hint(tr("Cada passo ajusta o canal e espera a duração. Campos vazios ou \"(manter)\" não "
                                     "mudam o gerador. Frequência aceita prefixos: 1k, 2,5 MHz, 500 m. O tempo é "
                                     "contado pelo PC (precisão de ~10 ms).")));

    timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setTimerType(Qt::PreciseTimer);
    connect(timer, &QTimer::timeout, this, &SequenceTab::runStep);
    connect(addButton, &QPushButton::clicked, this, [this]() {
        QJsonObject step{{"channel", 1}, {"duration", 1.0}};
        addRow(step);
    });
    connect(copyButton, &QPushButton::clicked, this, &SequenceTab::addFromGenerator);
    connect(removeButton, &QPushButton::clicked, this, [this]() {
        const int r = table->currentRow();
        if (r >= 0) table->removeRow(r);
        updateButtons();
    });
    connect(upButton, &QPushButton::clicked, this, [this]() { moveRow(-1); });
    connect(downButton, &QPushButton::clicked, this, [this]() { moveRow(1); });
    connect(saveButton, &QPushButton::clicked, this, &SequenceTab::save);
    connect(loadButton, &QPushButton::clicked, this, &SequenceTab::load);
    connect(runButton, &QPushButton::clicked, this, [this]() { running ? stop(tr("Sequência parada.")) : start(); });
    connect(table, &QTableWidget::itemSelectionChanged, this, &SequenceTab::updateButtons);

    // the last table comes back when the program opens again
    const QJsonArray saved = QJsonDocument::fromJson(QSettings().value("Sequence/steps").toByteArray()).array();
    if (saved.isEmpty()) {
        fromJson(QJsonArray{QJsonObject{{"channel", 1}, {"waveform", 0}, {"frequency", 1000.0}, {"amplitude", 1.0},
                                        {"output", "on"}, {"duration", 2.0}},
                            QJsonObject{{"channel", 1}, {"frequency", 2000.0}, {"duration", 2.0}},
                            QJsonObject{{"channel", 1}, {"output", "off"}, {"duration", 1.0}}});
    } else {
        fromJson(saved);
    }
    repeatBox->setValue(QSettings().value("Sequence/repeat", 1).toInt());
    connectionChanged(gen->isOpen());
}

SequenceTab::~SequenceTab() {
    QSettings s;
    s.setValue("Sequence/steps", QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
    s.setValue("Sequence/repeat", repeatBox->value());
}

void SequenceTab::connectionChanged(bool open) {
    setEnabled(true); // the table can be edited offline
    if (!open && running) stop(tr("Gerador desconectado; sequência parada."));
    updateButtons();
}

void SequenceTab::addRow(const QJsonObject &step) {
    const int r = table->currentRow() >= 0 ? table->currentRow() + 1 : table->rowCount();
    table->insertRow(r);
    QComboBox *ch = channelCombo();
    ch->setCurrentIndex(std::max(0, ch->findData(step.value("channel").toInt(1))));
    QComboBox *wave = waveformCombo();
    wave->setCurrentIndex(std::max(0, wave->findData(step.contains("waveform") ? step.value("waveform").toInt() : -1)));
    QComboBox *out = outputCombo();
    const QString o = step.value("output").toString();
    out->setCurrentIndex(o == "on" ? 1 : (o == "off" ? 2 : 0));
    table->setCellWidget(r, CHANNEL, ch);
    table->setCellWidget(r, WAVEFORM, wave);
    table->setCellWidget(r, OUTPUT, out);
    auto text = [&](int column, const char *key, const QString &unit) {
        QTableWidgetItem *item = new QTableWidgetItem(
            step.contains(key) ? (unit == "Hz" ? psgui::siText(step.value(key).toDouble(), "Hz", 12)
                                               : number(step.value(key).toDouble()))
                               : QString());
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table->setItem(r, column, item);
    };
    text(FREQUENCY, "frequency", "Hz");
    text(AMPLITUDE, "amplitude", QString());
    text(OFFSET, "offset", QString());
    text(DURATION, "duration", QString());
    table->selectRow(r);
    updateButtons();
}

QJsonArray SequenceTab::toJson() const {
    QJsonArray out;
    for (int r = 0; r < table->rowCount(); ++r) {
        QJsonObject s;
        s["channel"] = static_cast<QComboBox *>(table->cellWidget(r, CHANNEL))->currentData().toInt();
        const int w = static_cast<QComboBox *>(table->cellWidget(r, WAVEFORM))->currentData().toInt();
        if (w >= 0) s["waveform"] = w;
        const int o = static_cast<QComboBox *>(table->cellWidget(r, OUTPUT))->currentIndex();
        if (o) s["output"] = o == 1 ? "on" : "off";
        auto value = [&](int column, const char *key, const QString &unit) {
            double v = 0;
            const QTableWidgetItem *item = table->item(r, column);
            if (item && parseValue(item->text(), unit, v)) s[key] = v;
        };
        value(FREQUENCY, "frequency", "Hz");
        value(AMPLITUDE, "amplitude", "Vpp");
        value(OFFSET, "offset", "V");
        value(DURATION, "duration", "s");
        out.append(s);
    }
    return out;
}

void SequenceTab::fromJson(const QJsonArray &steps) {
    table->setRowCount(0);
    table->setCurrentCell(-1, -1);
    for (const QJsonValue &v : steps) {
        table->setCurrentCell(table->rowCount() - 1, 0);
        addRow(v.toObject());
    }
    table->clearSelection();
    updateButtons();
}

void SequenceTab::addFromGenerator() {
    int ch = 1;
    if (table->currentRow() >= 0)
        ch = std::max(1, static_cast<QComboBox *>(table->cellWidget(table->currentRow(), CHANNEL))->currentData().toInt());
    Psg9080::ChannelState s;
    if (!check(gen->readChannel(ch, s))) return;
    addRow(QJsonObject{{"channel", ch}, {"waveform", s.waveform}, {"frequency", s.frequency}, {"amplitude", s.amplitude},
                       {"offset", s.offset}, {"output", s.enabled ? "on" : "off"}, {"duration", 1.0}});
    say(tr("Passo com o estado atual do CH%1.").arg(ch));
}

void SequenceTab::moveRow(int delta) {
    const int r = table->currentRow(), to = r + delta;
    if (r < 0 || to < 0 || to >= table->rowCount()) return;
    QJsonArray steps = toJson();
    const QJsonValue v = steps[r];
    steps.removeAt(r);
    steps.insert(to, v);
    fromJson(steps);
    table->selectRow(to);
}

void SequenceTab::updateButtons() {
    const bool selected = table->currentRow() >= 0;
    for (QPushButton *b : {removeButton, upButton, downButton}) b->setEnabled(selected && !running);
    addButton->setEnabled(!running);
    loadButton->setEnabled(!running);
    copyButton->setEnabled(gen->isOpen() && !running);
    table->setEnabled(!running);
    repeatBox->setEnabled(!running);
    runButton->setEnabled(gen->isOpen() && table->rowCount() > 0);
    darkstyle::styleRunButton(runButton, running);
    runButton->setText(running ? QString::fromUtf8("■  ") + tr("Parar") : QString::fromUtf8("▶  ") + tr("Executar"));
}

bool SequenceTab::applyStep(int row, QString &error) {
    const QJsonObject s = toJson()[row].toObject();
    const int sel = s.value("channel").toInt(1);
    const QVector<int> channels = sel == 0 ? QVector<int>{1, 2} : QVector<int>{sel};
    auto fail = [&](const QString &what) {
        error = tr("Passo %1: %2").arg(row + 1).arg(what);
        return false;
    };
    if (!s.contains("duration") || s.value("duration").toDouble() < 0)
        return fail(tr("duração inválida (em segundos, ex.: 1,5)."));
    for (int ch : channels) {
        if (s.contains("waveform") && !gen->setWaveform(ch, s.value("waveform").toInt())) return fail(gen->lastError());
        if (s.contains("frequency") && !gen->setFrequency(ch, s.value("frequency").toDouble()))
            return fail(gen->lastError());
        if (s.contains("amplitude") && !gen->setAmplitude(ch, s.value("amplitude").toDouble()))
            return fail(gen->lastError());
        if (s.contains("offset") && !gen->setOffset(ch, s.value("offset").toDouble())) return fail(gen->lastError());
    }
    if (s.contains("output")) {
        const bool on = s.value("output").toString() == "on";
        bool a = false, b = false;
        if (!gen->readOutputs(a, b)) return fail(gen->lastError());
        for (int ch : channels) (ch == 1 ? a : b) = on;
        if (!gen->setOutputs(a, b)) return fail(gen->lastError());
    }
    return true;
}

void SequenceTab::start() {
    // check every step before touching the generator
    for (int r = 0; r < table->rowCount(); ++r) {
        double v = 0;
        const QTableWidgetItem *d = table->item(r, DURATION);
        if (!d || !parseValue(d->text(), "s", v) || v < 0) {
            say(tr("Passo %1: duração inválida (em segundos, ex.: 1,5).").arg(r + 1), true);
            return;
        }
        if (v > 2e6) { // the timer counts milliseconds in an int (about 24 days)
            say(tr("Passo %1: duração máxima de 2 000 000 s (23 dias).").arg(r + 1), true);
            return;
        }
        for (int c : {FREQUENCY, AMPLITUDE, OFFSET}) {
            const QTableWidgetItem *item = table->item(r, c);
            if (item && !item->text().trimmed().isEmpty() && !parseValue(item->text(), c == FREQUENCY ? "Hz" : "V", v)) {
                say(tr("Passo %1: valor inválido \"%2\".").arg(r + 1).arg(item->text()), true);
                return;
            }
        }
    }
    running = true;
    current = -1;
    cycle = 1;
    updateButtons();
    runStep();
}

void SequenceTab::stop(const QString &why) {
    timer->stop();
    running = false;
    current = -1;
    progress->clear();
    table->clearSelection();
    updateButtons();
    if (!why.isEmpty()) say(why);
}

void SequenceTab::runStep() {
    if (!running) return;
    ++current;
    if (current >= table->rowCount()) {
        if (repeatBox->value() != 0 && cycle >= repeatBox->value()) {
            stop(tr("Sequência concluída (%1 ciclo(s)).").arg(cycle));
            return;
        }
        ++cycle;
        current = 0;
    }
    table->selectRow(current);
    QString error;
    if (!applyStep(current, error)) {
        stop();
        say(error, true);
        return;
    }
    const double seconds = toJson()[current].toObject().value("duration").toDouble();
    progress->setText(repeatBox->value() == 0 ? tr("Passo %1 de %2 · ciclo %3").arg(current + 1).arg(table->rowCount()).arg(cycle)
                                              : tr("Passo %1 de %2 · ciclo %3 de %4")
                                                    .arg(current + 1)
                                                    .arg(table->rowCount())
                                                    .arg(cycle)
                                                    .arg(repeatBox->value()));
    timer->start((int)std::lround(std::max(0.0, seconds) * 1000.0));
}

void SequenceTab::save() {
    QSettings s;
    const QString dir = s.value("Sequence/dir", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    const QString name = QFileDialog::getSaveFileName(this, tr("Salvar sequência"), dir + "/sequencia.json",
                                                      tr("Sequência do PSG9080 (*.json)"));
    if (name.isEmpty()) return;
    s.setValue("Sequence/dir", QFileInfo(name).absolutePath());
    QJsonObject doc{{"psg9080_sequence", 1}, {"repeat", repeatBox->value()}, {"steps", toJson()}};
    QSaveFile f(name);
    const QByteArray bytes = QJsonDocument(doc).toJson();
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        say(tr("Não foi possível salvar %1: %2").arg(name, f.errorString()), true);
        return;
    }
    say(tr("Sequência salva em %1.").arg(name));
}

void SequenceTab::load() {
    QSettings s;
    const QString dir = s.value("Sequence/dir", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    const QString name =
        QFileDialog::getOpenFileName(this, tr("Abrir sequência"), dir, tr("Sequência do PSG9080 (*.json)"));
    if (name.isEmpty()) return;
    s.setValue("Sequence/dir", QFileInfo(name).absolutePath());
    QFile f(name);
    if (!f.open(QIODevice::ReadOnly)) {
        say(tr("Não foi possível abrir %1: %2").arg(name, f.errorString()), true);
        return;
    }
    const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
    if (!doc.contains("steps")) {
        say(tr("%1 não é uma sequência do PSG9080.").arg(QFileInfo(name).fileName()), true);
        return;
    }
    fromJson(doc.value("steps").toArray());
    repeatBox->setValue(doc.value("repeat").toInt(1));
    say(tr("Sequência %1 aberta (%2 passos).").arg(QFileInfo(name).fileName()).arg(table->rowCount()));
}
