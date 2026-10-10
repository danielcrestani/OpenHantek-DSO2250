// SPDX-License-Identifier: GPL-2.0+

#include "sweeptab.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

#include "psg9080.h"
#include "style/darkstyle.h"

using namespace psg9080;
using psgui::UnitField;

namespace {
enum Object { FREQUENCY, AMPLITUDE, DUTY };
} // namespace

SweepTab::SweepTab(Psg9080 *generator, QWidget *parent) : GeneratorTab(generator, parent) {
    modeBox = psgui::combo({tr("Varredura no tempo"), tr("Controle por tensão (VCO): entrada Ext.IN, 0 a 5 V")});
    channelBox = psgui::combo({"CH1", "CH2"}, {1, 2});
    objectBox = psgui::combo({tr("Frequência"), tr("Amplitude"), tr("Duty cycle")});
    startFreq = new UnitField(UnitField::frequencyUnits());
    endFreq = new UnitField(UnitField::frequencyUnits());
    for (UnitField *f : {startFreq, endFreq}) f->setRange(0.1, kMaxFrequency);
    startAmp = psgui::spin(3, 0.002, kMaxAmplitude, 0.1, " Vpp");
    endAmp = psgui::spin(3, 0.002, kMaxAmplitude, 0.1, " Vpp");
    startDuty = psgui::spin(2, 0.01, 99.99, 1, " %");
    endDuty = psgui::spin(2, 0.01, 99.99, 1, " %");
    timeBox = psgui::spin(2, 0.01, 640, 1, " s");
    directionBox = psgui::combo({tr("Subindo"), tr("Descendo"), tr("Ida e volta")});
    scaleBox = psgui::combo({tr("Linear"), tr("Logarítmica")});
    startFreq->setValue(100);
    endFreq->setValue(10e3);
    startAmp->setValue(1);
    endAmp->setValue(5);
    startDuty->setValue(10);
    endDuty->setValue(90);
    timeBox->setValue(10);

    QGroupBox *box = new QGroupBox(tr("Varredura"));
    darkstyle::colorSection(box, darkstyle::blue());
    form = new QFormLayout(box);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->addRow(tr("Modo"), modeBox);
    form->addRow(tr("Canal"), channelBox);
    form->addRow(tr("Varrer"), objectBox);
    form->addRow(tr("Frequência inicial"), startFreq);
    form->addRow(tr("Frequência final"), endFreq);
    form->addRow(tr("Amplitude inicial"), startAmp);
    form->addRow(tr("Amplitude final"), endAmp);
    form->addRow(tr("Duty inicial"), startDuty);
    form->addRow(tr("Duty final"), endDuty);
    form->addRow(tr("Tempo de varredura"), timeBox);
    form->addRow(tr("Sentido"), directionBox);
    form->addRow(tr("Escala"), scaleBox);

    runButton = new QPushButton;
    elapsedLabel = new QLabel;
    elapsedLabel->setAlignment(Qt::AlignCenter);
    explain = psgui::hint(QString());

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(box);
    layout->addWidget(runButton);
    layout->addWidget(elapsedLabel);
    layout->addWidget(explain);
    layout->addStretch(1);

    ticker = new QTimer(this);
    ticker->setInterval(200);
    connect(ticker, &QTimer::timeout, this, [this]() {
        if (isVco()) {
            elapsedLabel->setText(tr("VCO ligado há %1 s").arg(clock.elapsed() / 1000));
            return;
        }
        const double t = clock.elapsed() / 1000.0, period = std::max(0.01, timeBox->value());
        elapsedLabel->setText(tr("Varrendo há %1 s — ciclo %2, %3 %")
                                  .arg((int)t)
                                  .arg((int)(t / period) + 1)
                                  .arg((int)(100 * std::fmod(t, period) / period)));
    });
    connect(modeBox, QOverload<int>::of(&QComboBox::activated), this, [this](int) { updateRows(); });
    connect(objectBox, QOverload<int>::of(&QComboBox::activated), this, [this](int) { updateRows(); });
    connect(runButton, &QPushButton::clicked, this, [this]() { running ? stop() : start(); });
    for (UnitField *f : {startFreq, endFreq})
        connect(f, &UnitField::invalid, this,
                [this](const QString &t) { emit statusMessage(tr("Frequência fora da faixa: %1").arg(t), true); });
    showRunning(false);
    updateRows();
}

bool SweepTab::isVco() const { return modeBox->currentIndex() == 1; }

void SweepTab::updateRows() {
    const int o = objectBox->currentIndex();
    auto show = [this](QWidget *w, bool v) { psgui::setRowVisible(form, w, v); };
    show(startFreq, o == FREQUENCY);
    show(endFreq, o == FREQUENCY);
    show(startAmp, o == AMPLITUDE);
    show(endAmp, o == AMPLITUDE);
    show(startDuty, o == DUTY);
    show(endDuty, o == DUTY);
    show(timeBox, !isVco());
    show(directionBox, !isVco());
    form->labelForField(startFreq)->setProperty("text", isVco() ? tr("Frequência em 0 V") : tr("Frequência inicial"));
    form->labelForField(endFreq)->setProperty("text", isVco() ? tr("Frequência em 5 V") : tr("Frequência final"));
    form->labelForField(startAmp)->setProperty("text", isVco() ? tr("Amplitude em 0 V") : tr("Amplitude inicial"));
    form->labelForField(endAmp)->setProperty("text", isVco() ? tr("Amplitude em 5 V") : tr("Amplitude final"));
    form->labelForField(startDuty)->setProperty("text", isVco() ? tr("Duty em 0 V") : tr("Duty inicial"));
    form->labelForField(endDuty)->setProperty("text", isVco() ? tr("Duty em 5 V") : tr("Duty final"));
    explain->setText(
        (isVco() ? tr("A tensão na entrada Ext.IN (0 a 5 V) leva o parâmetro do valor em 0 V ao valor em 5 V.")
                 : tr("O próprio gerador varre o parâmetro, do valor inicial ao final, no tempo escolhido.")) +
        "\n" +
        tr("A forma de onda é a do canal (aba Básico). Se o gerador varrer outro parâmetro que não o escolhido, "
           "selecione-o também na tela do aparelho (tecla FUNC). Para medir resposta em frequência com o "
           "osciloscópio, use o OpenHantek Bode."));
}

void SweepTab::showRunning(bool on) {
    running = on;
    darkstyle::styleRunButton(runButton, on);
    runButton->setText(on ? QString::fromUtf8("■  ") + (isVco() ? tr("Parar VCO") : tr("Parar varredura"))
                          : QString::fromUtf8("▶  ") + (isVco() ? tr("Ligar VCO") : tr("Iniciar varredura")));
    for (QWidget *w : {(QWidget *)modeBox, (QWidget *)channelBox, (QWidget *)objectBox}) w->setEnabled(!on);
    if (on) {
        clock.start();
        ticker->start();
    } else {
        ticker->stop();
        elapsedLabel->clear();
    }
}

void SweepTab::start() {
    const int ch = channelBox->currentData().toInt(), o = objectBox->currentIndex();
    std::vector<std::string> setup = {std::to_string(ch - 1)};
    std::vector<std::string> f;
    encodeScaled(timeBox->value(), 100, f);
    setup.push_back(f[0]);
    setup.push_back(std::to_string(directionBox->currentIndex()));
    setup.push_back(std::to_string(scaleBox->currentIndex()));
    bool ok = gen->writeRaw(REG_SWEEP_SETUP, setup);
    if (o == FREQUENCY) {
        ok = ok && gen->setScaled(REG_SWEEP_FREQ_START, startFreq->value(), 10) &&
             gen->setScaled(REG_SWEEP_FREQ_END, endFreq->value(), 10);
    } else if (o == AMPLITUDE) {
        ok = ok && gen->setScaled(REG_SWEEP_AMP_START, psgui::spinValue(startAmp), 1000) &&
             gen->setScaled(REG_SWEEP_AMP_END, psgui::spinValue(endAmp), 1000);
    } else {
        ok = ok && gen->setScaled(REG_SWEEP_DUTY_START, psgui::spinValue(startDuty), 100) &&
             gen->setScaled(REG_SWEEP_DUTY_END, psgui::spinValue(endDuty), 100);
    }
    // screen of the sweep / voltage control; the third selector picks frequency, amplitude or duty
    Interface screen = isVco() ? vcoInterface() : sweepInterface();
    screen.sub = o;
    ok = ok && gen->setInterface(screen) && gen->writeRaw(REG_SWEEP_ENABLE, encodePair(!isVco(), isVco()));
    if (!check(ok)) return;
    showRunning(true);
    say(isVco() ? tr("Controle por tensão ligado no CH%1.").arg(ch) : tr("Varredura iniciada no CH%1.").arg(ch));
}

void SweepTab::stop() {
    const int ch = channelBox->currentData().toInt();
    const bool ok = gen->writeRaw(REG_SWEEP_ENABLE, encodePair(0, 0)) && gen->setInterface(waveformInterface(ch));
    showRunning(false);
    if (check(ok)) say(tr("Varredura parada."));
}

void SweepTab::refresh() {
    if (!gen->isOpen()) return;
    std::vector<std::string> f;
    double d = 0;
    bool ok = gen->readRaw(REG_SWEEP_SETUP, f);
    if (ok && f.size() >= 4) {
        long long ch = 0, t = 0, dir = 0, log = 0;
        if (decodeInteger({f[0]}, ch) && decodeInteger({f[1]}, t) && decodeInteger({f[2]}, dir) &&
            decodeInteger({f[3]}, log)) {
            QSignalBlocker b1(channelBox), b2(directionBox), b3(scaleBox), b4(timeBox);
            channelBox->setCurrentIndex(ch == 1 ? 1 : 0);
            timeBox->setValue(std::max(0.01, t / 100.0));
            directionBox->setCurrentIndex((int)std::min(2LL, std::max(0LL, dir)));
            scaleBox->setCurrentIndex(log ? 1 : 0);
        }
    }
    auto get = [&](int code, double scale, auto set) {
        if (ok && (ok = gen->readScaled(code, scale, d))) set(d);
    };
    QSignalBlocker b5(startAmp), b6(endAmp), b7(startDuty), b8(endDuty);
    get(REG_SWEEP_FREQ_START, 10, [&](double x) { startFreq->setValue(x); });
    get(REG_SWEEP_FREQ_END, 10, [&](double x) { endFreq->setValue(x); });
    get(REG_SWEEP_AMP_START, 1000, [&](double x) { startAmp->setValue(x); });
    get(REG_SWEEP_AMP_END, 1000, [&](double x) { endAmp->setValue(x); });
    get(REG_SWEEP_DUTY_START, 100, [&](double x) { startDuty->setValue(x); });
    get(REG_SWEEP_DUTY_END, 100, [&](double x) { endDuty->setValue(x); });
    long long sweep = 0, vco = 0;
    if (ok && (ok = gen->readPair(REG_SWEEP_ENABLE, sweep, vco))) {
        if ((sweep || vco) && !running) {
            QSignalBlocker b(modeBox);
            modeBox->setCurrentIndex(vco ? 1 : 0);
            updateRows();
            showRunning(true);
        } else if (!sweep && !vco && running) {
            showRunning(false);
        }
    }
    check(ok);
}
