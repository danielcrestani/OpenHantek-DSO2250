// SPDX-License-Identifier: GPL-2.0+

#include "countertab.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include "psg9080.h"
#include "style/darkstyle.h"

using namespace psg9080;

CounterTab::CounterTab(Psg9080 *generator, QWidget *parent) : GeneratorTab(generator, parent) {
    functionBox = psgui::combo({tr("Frequencímetro (frequência, período, larguras, duty)"), tr("Contador de pulsos")});
    couplingBox = psgui::combo({tr("AC"), tr("DC")});
    gateBox = psgui::spin(3, 0.001, 10, 0.1, " s");
    gateBox->setValue(1);
    rangeBox = psgui::combo({tr("Alta frequência (acima de 2 kHz)"), tr("Baixa frequência (abaixo de 2 kHz)")});

    QGroupBox *setup = new QGroupBox(tr("Entrada Ext.IN"));
    darkstyle::colorSection(setup, darkstyle::violet());
    form = new psgui::Form(setup);
    form->addRow(tr("Função"), functionBox);
    form->addRow(tr("Acoplamento"), couplingBox);
    form->addRow(tr("Tempo de porta"), gateBox);
    form->addRow(tr("Faixa"), rangeBox);

    // Readout: black screen like the oscilloscope, big digits
    QWidget *screen = new QWidget;
    screen->setObjectName("counterScreen");
    screen->setStyleSheet("#counterScreen { background: #000; border: 1px solid #3a404a; border-radius: 6px; }"
                          "QLabel { background: transparent; }");
    QFont big("monospace");
    big.setStyleHint(QFont::Monospace);
    big.setPointSizeF(30);
    big.setBold(true);
    mainValue = new QLabel(QString::fromUtf8("—"));
    mainValue->setFont(big);
    mainValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    mainValue->setStyleSheet(QString("color: %1;").arg(darkstyle::violet()));
    mainCaption = new QLabel;
    mainCaption->setStyleSheet("color: #9aa1ab;");
    QFont mid("monospace");
    mid.setStyleHint(QFont::Monospace);
    mid.setPointSizeF(13);
    QGridLayout *grid = new QGridLayout(screen);
    grid->setContentsMargins(14, 10, 14, 10);
    grid->addWidget(mainCaption, 0, 0, 1, 2);
    grid->addWidget(mainValue, 1, 0, 1, 2);
    details = new QWidget;
    QGridLayout *detailGrid = new QGridLayout(details);
    detailGrid->setContentsMargins(0, 6, 0, 0);
    const char *names[] = {"Período", "Largura +", "Largura −", "Duty cycle"};
    int row = 0;
    for (QLabel **l : {&period, &positive, &negative, &duty}) {
        QLabel *name = new QLabel(tr(names[row]));
        name->setStyleSheet("color: #9aa1ab;");
        *l = new QLabel(QString::fromUtf8("—"));
        (*l)->setFont(mid);
        (*l)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        (*l)->setStyleSheet("color: #e6e9ee;");
        detailGrid->addWidget(name, row, 0);
        detailGrid->addWidget(*l, row, 1);
        ++row;
    }
    grid->addWidget(details, 2, 0, 1, 2);
    grid->setRowStretch(3, 1);

    runButton = new QPushButton;
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(setup);
    layout->addWidget(runButton);
    layout->addWidget(screen, 1);
    layout->addWidget(psgui::hint(tr("Sinal de 2 a 20 Vpp no conector Ext.IN, de 1 Hz a 100 MHz. Use a faixa "
                                     "baixa abaixo de 2 kHz (mais resolução). O gerador mostra a tela de medição "
                                     "enquanto mede; ao parar, volta à tela normal.")));

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &CounterTab::poll);
    connect(runButton, &QPushButton::clicked, this, [this]() { running ? stop() : start(); });
    connect(functionBox, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
        updateRows();
        if (running) start(); // apply the new function
    });
    for (QComboBox *b : {couplingBox, rangeBox})
        connect(b, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
            if (running) start();
        });
    connect(gateBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() {
        if (running) start();
    });
    darkstyle::styleRunButton(runButton, false);
    runButton->setText(QString::fromUtf8("▶  ") + tr("Medir"));
    updateRows();
}

bool CounterTab::isCounter() const { return functionBox->currentIndex() == 1; }

void CounterTab::updateRows() {
    form->setRowVisible(gateBox, !isCounter());
    form->setRowVisible(rangeBox, !isCounter());
    mainCaption->setText(isCounter() ? tr("Pulsos contados") : tr("Frequência"));
    details->setVisible(!isCounter());
}

void CounterTab::start() {
    std::vector<std::string> gate;
    encodeScaled(psgui::spinValue(gateBox), 1000, gate);
    const bool ok = gen->writeRaw(REG_MEASURE_SETUP, {std::to_string(couplingBox->currentIndex()), gate[0],
                                                      std::to_string(rangeBox->currentIndex())}) &&
                    gen->setInteger(REG_MEASURE_MODE, isCounter() ? 0 : 1) &&
                    gen->setInterface(measurementInterface());
    if (!check(ok)) return;
    running = true;
    darkstyle::styleRunButton(runButton, true);
    runButton->setText(QString::fromUtf8("■  ") + tr("Parar"));
    // read about twice per gate time, never faster than 4 per second
    timer->start(isCounter() ? 250 : std::max(250, (int)(gateBox->value() * 500)));
    poll();
}

void CounterTab::stop(bool restoreScreen) {
    timer->stop();
    running = false;
    darkstyle::styleRunButton(runButton, false);
    runButton->setText(QString::fromUtf8("▶  ") + tr("Medir"));
    if (restoreScreen && gen->isOpen()) check(gen->setInterface(waveformInterface(1)));
}

void CounterTab::poll() {
    if (!gen->isOpen()) {
        stop(false);
        return;
    }
    if (isCounter()) {
        long long n = 0;
        if (!check(gen->readInteger(REG_COUNTER, n))) return stop(false);
        mainValue->setText(QString::number(n));
        return;
    }
    double f = 0, pw = 0, nw = 0, per = 0, d = 0;
    const bool low = rangeBox->currentIndex() == 1;
    const bool ok = (low ? gen->readScaled(REG_MEAS_FREQ_LOW, 1000, f) : gen->readScaled(REG_MEAS_FREQ_HIGH, 1, f)) &&
                    gen->readScaled(REG_MEAS_POS_WIDTH, 1e9, pw) && gen->readScaled(REG_MEAS_NEG_WIDTH, 1e9, nw) &&
                    gen->readScaled(REG_MEAS_PERIOD, 1e8, per) && gen->readScaled(REG_MEAS_DUTY, 100, d);
    if (!check(ok)) return stop(false);
    mainValue->setText(f > 0 ? psgui::siText(f, "Hz", low ? 7 : 9) : tr("sem sinal"));
    period->setText(per > 0 ? psgui::siText(per, "s") : QString::fromUtf8("—"));
    positive->setText(pw > 0 ? psgui::siText(pw, "s") : QString::fromUtf8("—"));
    negative->setText(nw > 0 ? psgui::siText(nw, "s") : QString::fromUtf8("—"));
    duty->setText(f > 0 ? QString::number(d, 'f', 2).replace('.', ',') + " %" : QString::fromUtf8("—"));
}

void CounterTab::refresh() {
    if (!gen->isOpen() || running) return;
    std::vector<std::string> f;
    long long mode = 1;
    if (!check(gen->readRaw(REG_MEASURE_SETUP, f) && gen->readInteger(REG_MEASURE_MODE, mode))) return;
    if (f.size() >= 3) {
        long long c = 0, g = 0, r = 0;
        if (decodeInteger({f[0]}, c) && decodeInteger({f[1]}, g) && decodeInteger({f[2]}, r)) {
            QSignalBlocker b1(couplingBox), b2(gateBox), b3(rangeBox), b4(functionBox);
            couplingBox->setCurrentIndex(c ? 1 : 0);
            gateBox->setValue(std::max(0.001, g / 1000.0));
            rangeBox->setCurrentIndex(r ? 1 : 0);
            functionBox->setCurrentIndex(mode ? 0 : 1);
        }
    }
    updateRows();
}

void CounterTab::hideEvent(QHideEvent *event) {
    GeneratorTab::hideEvent(event);
    if (running) stop(); // only measure while the tab is visible
}

void CounterTab::connectionChanged(bool open) {
    if (!open && running) stop(false);
    GeneratorTab::connectionChanged(open);
}

CounterTab::~CounterTab() { delete form; }
