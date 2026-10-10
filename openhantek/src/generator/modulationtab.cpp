// SPDX-License-Identifier: GPL-2.0+

#include "modulationtab.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include "psg9080.h"
#include "style/darkstyle.h"

using namespace psg9080;
using psgui::UnitField;

namespace {
enum Type { AM, FM, PM, ASK, FSK, PSK, PULSE, BURST };
} // namespace

ModulationTab::ModulationTab(Psg9080 *generator, QWidget *parent) : GeneratorTab(generator, parent) {
    channelBox = psgui::combo({"CH1", "CH2"}, {1, 2});
    onButton = new QPushButton(tr("MODULAÇÃO DESLIGADA"));
    onButton->setCheckable(true);
    QHBoxLayout *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Canal")));
    top->addWidget(channelBox);
    top->addSpacing(12);
    top->addWidget(onButton, 1);

    typeBox = psgui::combo({tr("AM — amplitude"), tr("FM — frequência"), tr("PM — fase"), tr("ASK — chaveamento de amplitude"),
                            tr("FSK — chaveamento de frequência"), tr("PSK — chaveamento de fase"),
                            tr("Pulso (PWM)"), tr("Burst — rajada de ciclos")});
    sourceBox = psgui::combo({tr("Interna"), tr("Externa (entrada Ext.IN, 0 a 3 Vpp, até 20 kHz)")});
    waveBox = psgui::combo({tr("Senoidal"), tr("Quadrada"), tr("Triangular"), tr("Rampa de subida"), tr("Rampa de descida"),
                            tr("Arbitrária 01"), tr("Arbitrária 02"), tr("Arbitrária 03"), tr("Arbitrária 04"),
                            tr("Arbitrária 05")});
    rate = new UnitField(UnitField::frequencyUnits());
    rate->setRange(0.001, 1e6);
    depth = psgui::spin(1, 0, 200, 1, " %");
    fmDeviation = new UnitField(UnitField::frequencyUnits());
    fmDeviation->setRange(0.1, 10e3);
    fskFrequency = new UnitField(UnitField::frequencyUnits());
    fskFrequency->setRange(0.1, 80e6);
    phase = psgui::spin(1, 0, 359.9, 1, QString::fromUtf8(" °"));
    polarityBox = psgui::combo({tr("Positiva"), tr("Negativa")});
    pulseWidth = new UnitField(UnitField::timeUnits());
    pulseWidth->setRange(1e-9, 4);
    pulsePeriod = new UnitField(UnitField::timeUnits());
    pulsePeriod->setRange(10e-9, 40);
    invertBox = psgui::combo({tr("Normal"), tr("Invertido")});
    cycles = psgui::intSpin(1, 1000000000, tr(" ciclos"));
    idleBox = psgui::combo({tr("Zero"), tr("Máximo positivo"), tr("Máximo negativo")});
    triggerBox = psgui::combo({tr("Manual (botão Disparar)"), tr("Interno (pelo CH2)"), tr("Externo AC (Ext.IN)"),
                               tr("Externo DC (Ext.IN)")});
    fireButton = new QPushButton(tr("Disparar agora"));

    QGroupBox *box = new QGroupBox(tr("Modulação"));
    darkstyle::colorSection(box, darkstyle::orange());
    form = new QFormLayout(box);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->addRow(tr("Tipo"), typeBox);
    form->addRow(tr("Fonte"), sourceBox);
    form->addRow(tr("Onda moduladora"), waveBox);
    form->addRow(tr("Frequência moduladora"), rate);
    form->addRow(tr("Profundidade"), depth);
    form->addRow(tr("Desvio de frequência"), fmDeviation);
    form->addRow(tr("Frequência de salto"), fskFrequency);
    form->addRow(tr("Desvio de fase"), phase);
    form->addRow(tr("Polaridade"), polarityBox);
    form->addRow(tr("Largura do pulso"), pulseWidth);
    form->addRow(tr("Período do pulso"), pulsePeriod);
    form->addRow(tr("Saída"), invertBox);
    form->addRow(tr("Ciclos por disparo"), cycles);
    form->addRow(tr("Repouso"), idleBox);
    form->addRow(tr("Disparo"), triggerBox);
    form->addRow(QString(), fireButton);

    explain = psgui::hint(QString());
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(top);
    layout->addWidget(box);
    layout->addWidget(explain);
    layout->addStretch(1);

    // writes
    connect(channelBox, QOverload<int>::of(&QComboBox::activated), this, [this](int) { refresh(); });
    connect(onButton, &QPushButton::clicked, this, &ModulationTab::toggle);
    connect(typeBox, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        writePair(REG_MODULATION, i);
        updateRows();
    });
    connect(sourceBox, QOverload<int>::of(&QComboBox::activated), this, [this](int i) { writePair(REG_MOD_SOURCE, i); });
    connect(waveBox, QOverload<int>::of(&QComboBox::activated), this, [this](int i) { writePair(REG_MOD_WAVEFORM, i); });
    connect(polarityBox, QOverload<int>::of(&QComboBox::activated), this, [this](int i) { writePair(REG_POLARITY, i); });
    connect(invertBox, QOverload<int>::of(&QComboBox::activated), this, [this](int i) { writePair(REG_PULSE_INVERT, i); });
    connect(idleBox, QOverload<int>::of(&QComboBox::activated), this, [this](int i) { writePair(REG_BURST_IDLE, i); });
    connect(triggerBox, QOverload<int>::of(&QComboBox::activated), this,
            [this](int i) { writePair(REG_TRIGGER_SOURCE, i); });
    connect(cycles, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int n) { writePair(REG_BURST_COUNT, n); });
    connect(rate, &UnitField::edited, this, [this](double hz) { writeScaled(REG_MOD_FREQUENCY, hz, 1000); });
    connect(depth, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this]() { writeScaled(REG_AM_DEPTH, psgui::spinValue(depth), 10); });
    connect(fmDeviation, &UnitField::edited, this, [this](double hz) { writeScaled(REG_FM_DEVIATION, hz, 10); });
    connect(fskFrequency, &UnitField::edited, this, [this](double hz) { writeScaled(REG_FSK_FREQUENCY, hz, 10); });
    connect(phase, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this]() { writeScaled(REG_PM_DEVIATION, psgui::spinValue(phase), 10); });
    connect(pulseWidth, &UnitField::edited, this, [this](double s) { writeScaled(REG_PULSE_WIDTH, s, 1e9); });
    connect(pulsePeriod, &UnitField::edited, this, [this](double s) { writeScaled(REG_PULSE_PERIOD, s, 1e8); });
    for (UnitField *f : {rate, fmDeviation, fskFrequency, pulseWidth, pulsePeriod})
        connect(f, &UnitField::invalid, this,
                [this](const QString &t) { emit statusMessage(tr("Valor fora da faixa: %1").arg(t), true); });
    connect(fireButton, &QPushButton::clicked, this, [this]() {
        if (check(gen->writeRaw(REG_TRIGGER, encodePair(channel() == 1, channel() == 2))))
            say(tr("Disparo enviado ao CH%1.").arg(channel()));
    });
    updateRows();
}

int ModulationTab::channel() const { return channelBox->currentData().toInt(); }

void ModulationTab::updateRows() {
    const int t = typeBox->currentIndex();
    auto show = [this](QWidget *field, bool visible) { psgui::setRowVisible(form, field, visible); };
    const bool analog = t == AM || t == FM || t == PM;
    const bool keyed = t == ASK || t == FSK || t == PSK;
    show(sourceBox, analog || keyed);
    show(waveBox, analog);
    show(rate, analog || keyed);
    show(depth, t == AM || t == ASK);
    show(fmDeviation, t == FM);
    show(fskFrequency, t == FSK);
    show(phase, t == PM || t == PSK);
    show(polarityBox, keyed);
    show(pulseWidth, t == PULSE);
    show(pulsePeriod, t == PULSE);
    show(invertBox, t == PULSE);
    show(cycles, t == BURST);
    show(idleBox, t == BURST);
    show(triggerBox, t == BURST);
    show(fireButton, t == BURST);
    form->labelForField(rate)->setProperty("text", keyed ? tr("Taxa") : tr("Frequência moduladora"));
    form->labelForField(depth)->setProperty("text", t == ASK ? tr("Amplitude ASK") : tr("Profundidade"));
    form->labelForField(phase)->setProperty("text", t == PSK ? tr("Fase PSK") : tr("Desvio de fase"));

    QString text;
    switch (t) {
    case AM: text = tr("A amplitude da portadora segue a onda moduladora; 100 % vai de zero ao dobro."); break;
    case FM: text = tr("A frequência varia ± o desvio em torno da frequência do canal (até 10 kHz)."); break;
    case PM: text = tr("A fase varia ± o desvio, no ritmo da onda moduladora."); break;
    case ASK: text = tr("Alterna entre a amplitude do canal e a amplitude ASK na taxa escolhida (ou pela Ext.IN)."); break;
    case FSK: text = tr("Alterna entre a frequência do canal e a frequência de salto."); break;
    case PSK: text = tr("Alterna entre a fase do canal e a fase PSK."); break;
    case PULSE: text = tr("O canal precisa estar com a forma Pulso; largura de 1 ns a 4 s, período de 10 ns a 40 s."); break;
    case BURST:
        text = tr("A cada disparo saem N ciclos da portadora. O período da rajada precisa ser menor que o do disparo.");
        break;
    }
    explain->setText(text + "\n" + tr("Portadora: forma de onda, frequência e amplitude do canal na aba Básico "
                                      "(senoidal, quadrada, rampa ou arbitrária; não DC)."));
}

void ModulationTab::showActive(bool on) {
    const QString color = darkstyle::generatorChannel(channel() - 1);
    onButton->setStyleSheet(darkstyle::channelOnButtonSheet(color));
    QSignalBlocker b(onButton);
    onButton->setChecked(on);
    onButton->setText(on ? tr("MODULAÇÃO LIGADA") : tr("MODULAÇÃO DESLIGADA"));
}

void ModulationTab::toggle(bool on) {
    const int ch = channel();
    if (!check(gen->setInterface(on ? modulationInterface(ch) : waveformInterface(ch)))) {
        refresh();
        return;
    }
    showActive(on);
    say(on ? tr("Modulação ligada no CH%1 (o gerador mostra a tela de modulação).").arg(ch)
           : tr("Modulação desligada no CH%1.").arg(ch));
}

void ModulationTab::writePair(int code, int value) {
    if (!check(gen->setPairValue(code, channel(), value))) refresh();
}

void ModulationTab::writeScaled(int baseCode, double value, double scale) {
    if (!check(gen->setChannelScaled(baseCode, channel(), value, scale))) refresh();
}

void ModulationTab::refresh() {
    if (!gen->isOpen()) return;
    const int ch = channel();
    long long v = 0;
    double d = 0;
    bool ok = true;
    QSignalBlocker b1(typeBox), b2(sourceBox), b3(waveBox), b4(polarityBox), b5(invertBox), b6(idleBox),
        b7(triggerBox), b8(cycles), b9(depth), b10(phase);
    auto pair = [&](int code, QComboBox *box) {
        if (ok && (ok = gen->readPairValue(code, ch, v))) box->setCurrentIndex(std::max(0, box->findData((int)v)));
    };
    pair(REG_MODULATION, typeBox);
    pair(REG_MOD_SOURCE, sourceBox);
    pair(REG_MOD_WAVEFORM, waveBox);
    pair(REG_POLARITY, polarityBox);
    pair(REG_PULSE_INVERT, invertBox);
    pair(REG_BURST_IDLE, idleBox);
    pair(REG_TRIGGER_SOURCE, triggerBox);
    if (ok && (ok = gen->readPairValue(REG_BURST_COUNT, ch, v))) cycles->setValue((int)std::max(1LL, v));
    auto scaled = [&](int code, double scale, auto set) {
        if (ok && (ok = gen->readChannelScaled(code, ch, scale, d))) set(d);
    };
    scaled(REG_MOD_FREQUENCY, 1000, [&](double x) { rate->setValue(x); });
    scaled(REG_AM_DEPTH, 10, [&](double x) { depth->setValue(x); });
    scaled(REG_FM_DEVIATION, 10, [&](double x) { fmDeviation->setValue(x); });
    scaled(REG_FSK_FREQUENCY, 10, [&](double x) { fskFrequency->setValue(x); });
    scaled(REG_PM_DEVIATION, 10, [&](double x) { phase->setValue(x); });
    scaled(REG_PULSE_WIDTH, 1e9, [&](double x) { pulseWidth->setValue(x); });
    scaled(REG_PULSE_PERIOD, 1e8, [&](double x) { pulsePeriod->setValue(x); });
    Interface screen;
    if (ok && (ok = gen->readInterface(screen))) showActive(screen.page == ch && screen.sub == 7);
    check(ok);
    updateRows();
}
