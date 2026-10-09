// SPDX-License-Identifier: GPL-2.0+

#include "GeneratorDock.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>

using namespace psg9080;

namespace {

QDoubleSpinBox *makeSpin(int decimals, double min, double max, double step, const QString &suffix) {
    QDoubleSpinBox *box = new QDoubleSpinBox;
    box->setDecimals(decimals);
    box->setRange(min, max);
    box->setSingleStep(step);
    box->setSuffix(suffix);
    box->setKeyboardTracking(false); // send on Enter / arrows / focus out, not on every key
    box->setAccelerated(true);
    return box;
}

double spinValue(const QDoubleSpinBox *box) {
    // the spin box holds a double; round to its decimals so 0.1 does not become 0.10000000000000001
    const double scale = std::pow(10.0, box->decimals());
    return std::round(box->value() * scale) / scale;
}

const FreqUnit kUnits[] = {FreqUnit::Hz, FreqUnit::kHz, FreqUnit::MHz, FreqUnit::mHz, FreqUnit::uHz};
const char *kUnitNames[] = {"Hz", "kHz", "MHz", "mHz", "µHz"};

} // namespace

bool GeneratorDock::parseNumber(const QString &text, double &value) {
    QString t = text.trimmed();
    t.remove(' ');
    t.replace(',', '.');
    bool ok = false;
    value = t.toDouble(&ok);
    return ok && std::isfinite(value);
}

QString GeneratorDock::formatNumber(double value, int maxDecimals) {
    QString s = QString::number(value, 'f', maxDecimals);
    if (s.contains('.')) {
        while (s.endsWith('0')) s.chop(1);
        if (s.endsWith('.')) s.chop(1);
    }
    if (s == "-0") s = "0";
    return s.replace('.', ',');
}

GeneratorDock::GeneratorDock(Psg9080 *generator, QWidget *parent)
    : QDockWidget(tr("Gerador PSG9080"), parent), gen(generator) {
    setObjectName("GeneratorDock");
    QWidget *root = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(root);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);

    // Connection
    portBox = new QComboBox;
    portBox->setEditable(true);
    portBox->setMinimumContentsLength(12);
    portBox->setToolTip(tr("Porta serial do gerador (normalmente /dev/ttyUSB0)"));
    refreshButton = new QPushButton(QString::fromUtf8("↻"));
    refreshButton->setToolTip(tr("Procurar portas seriais"));
    refreshButton->setFixedWidth(32);
    connectButton = new QPushButton(tr("Conectar"));
    QHBoxLayout *portRow = new QHBoxLayout;
    portRow->addWidget(portBox, 1);
    portRow->addWidget(refreshButton);
    portRow->addWidget(connectButton);
    layout->addLayout(portRow);
    connect(refreshButton, &QPushButton::clicked, this, &GeneratorDock::refreshPorts);
    connect(connectButton, &QPushButton::clicked, this, &GeneratorDock::toggleConnection);

    // Channels
    ui.resize(2);
    layout->addWidget(makeChannel(1, "#e6b800"));
    layout->addWidget(makeChannel(2, "#2fa8d8"));

    readButton = new QPushButton(tr("Ler do gerador"));
    readButton->setToolTip(tr("Atualiza o painel com os valores atuais do gerador"));
    allOffButton = new QPushButton(tr("Desligar saídas"));
    allOffButton->setStyleSheet("QPushButton { color: #c0392b; font-weight: bold; }");
    QHBoxLayout *actions = new QHBoxLayout;
    actions->addWidget(readButton);
    actions->addWidget(allOffButton);
    layout->addLayout(actions);
    connect(readButton, &QPushButton::clicked, this, &GeneratorDock::refresh);
    connect(allOffButton, &QPushButton::clicked, this, [this]() {
        if (gen->setOutputs(false, false)) {
            showOutputs(false, false);
            message(tr("Saídas desligadas."));
        } else {
            message(gen->lastError(), true);
        }
    });

    status = new QLabel;
    status->setWordWrap(true);
    layout->addWidget(status);
    layout->addStretch(1);
    setWidget(root);

    connect(gen, &Psg9080::connectionChanged, this, [this](bool) { updateConnectionUi(); });
    connect(gen, &Psg9080::channelWritten, this, [this](int channel) {
        if (!locked) readChannel(channel); // writes from elsewhere (Bode) refresh after unlock
    });
    connect(gen, &Psg9080::outputsWritten, this, [this](bool a, bool b) { showOutputs(a, b); });

    refreshPorts();
    updateConnectionUi();
}

QGroupBox *GeneratorDock::makeChannel(int channel, const QString &color) {
    ChannelUi &c = ui[channel - 1];
    c.box = new QGroupBox(tr("CH%1").arg(channel));
    c.box->setStyleSheet(QString("QGroupBox { font-weight: bold; border: 2px solid %1; border-radius: 5px; "
                                 "margin-top: 9px; padding-top: 6px; }"
                                 "QGroupBox::title { color: %1; subcontrol-origin: margin; left: 8px; }")
                             .arg(color));

    c.output = new QPushButton(tr("SAÍDA DESLIGADA"));
    c.output->setCheckable(true);
    c.output->setMinimumHeight(30);
    c.output->setStyleSheet("QPushButton { font-weight: bold; border-radius: 4px; background: #555; color: #ddd; }"
                            "QPushButton:checked { background: #2e9d4a; color: white; }"
                            "QPushButton:disabled { background: #333; color: #777; }");
    connect(c.output, &QPushButton::clicked, this, [this, channel](bool on) {
        bool a = false, b = false;
        if (gen->setOutput(channel, on) && gen->readOutputs(a, b)) {
            showOutputs(a, b);
        } else {
            message(gen->lastError(), true);
            readChannel(channel);
        }
    });

    c.waveform = new QComboBox;
    for (int code = 0; code < kBuiltinWaveforms; ++code) c.waveform->addItem(QString::fromUtf8(waveformName(code)), code);
    for (int slot = 1; slot <= 99; ++slot)
        c.waveform->addItem(tr("Arbitrária %1").arg(slot, 2, 10, QChar('0')), 100 + slot);
    c.waveform->setMaxVisibleItems(25);
    connect(c.waveform, QOverload<int>::of(&QComboBox::activated), this, [this, channel](int) {
        ChannelUi &u = ui[channel - 1];
        apply(channel, gen->setWaveform(channel, u.waveform->currentData().toInt()));
    });

    c.frequency = new QLineEdit;
    c.frequency->setAlignment(Qt::AlignRight);
    c.unit = new QComboBox;
    for (int i = 0; i < 5; ++i) c.unit->addItem(QString::fromUtf8(kUnitNames[i]), (int)kUnits[i]);
    connect(c.frequency, &QLineEdit::editingFinished, this, [this, channel]() {
        ChannelUi &u = ui[channel - 1];
        if (!u.frequency->isModified()) return;
        u.frequency->setModified(false);
        double v = 0;
        if (!parseNumber(u.frequency->text(), v) || v < 0) {
            message(tr("Frequência inválida: %1").arg(u.frequency->text()), true);
            readChannel(channel);
            return;
        }
        const FreqUnit unit = (FreqUnit)u.unit->currentData().toInt();
        apply(channel, gen->setFrequency(channel, v * hzPerDisplayUnit(unit), unit));
    });
    connect(c.unit, QOverload<int>::of(&QComboBox::activated), this, [this, channel](int) {
        // same frequency, shown in the newly selected unit
        ChannelUi &u = ui[channel - 1];
        apply(channel, gen->setFrequency(channel, u.frequencyHz, (FreqUnit)u.unit->currentData().toInt()));
    });
    QHBoxLayout *freqRow = new QHBoxLayout;
    freqRow->addWidget(c.frequency, 1);
    freqRow->addWidget(c.unit);

    c.amplitude = makeSpin(3, 0, kMaxAmplitude, 0.1, " Vpp");
    c.offset = makeSpin(2, -kMaxOffset, kMaxOffset, 0.1, " V");
    c.duty = makeSpin(2, 0, 100, 1, " %");
    c.phase = makeSpin(2, 0, 359.99, 1, QString::fromUtf8(" °"));
    connect(c.amplitude, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, channel]() { apply(channel, gen->setAmplitude(channel, spinValue(ui[channel - 1].amplitude))); });
    connect(c.offset, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, channel]() { apply(channel, gen->setOffset(channel, spinValue(ui[channel - 1].offset))); });
    connect(c.duty, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, channel]() { apply(channel, gen->setDuty(channel, spinValue(ui[channel - 1].duty))); });
    connect(c.phase, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, channel]() { apply(channel, gen->setPhase(channel, spinValue(ui[channel - 1].phase))); });

    QFormLayout *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->addRow(tr("Forma de onda"), c.waveform);
    form->addRow(tr("Frequência"), freqRow);
    form->addRow(tr("Amplitude"), c.amplitude);
    form->addRow(tr("Offset"), c.offset);
    form->addRow(tr("Duty cycle"), c.duty);
    form->addRow(tr("Fase"), c.phase);

    QVBoxLayout *box = new QVBoxLayout(c.box);
    box->addWidget(c.output);
    box->addLayout(form);
    return c.box;
}

void GeneratorDock::refreshPorts() {
    const QString current =
        portBox->currentText().isEmpty() ? QSettings().value("Generator/port", "/dev/ttyUSB0").toString()
                                         : portBox->currentText();
    QSignalBlocker block(portBox);
    portBox->clear();
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        if (info.portName().startsWith("ttyS") && info.description().isEmpty()) continue; // legacy UARTs
        portBox->addItem(info.systemLocation());
        portBox->setItemData(portBox->count() - 1,
                             info.description().isEmpty() ? info.manufacturer() : info.description(),
                             Qt::ToolTipRole);
    }
    const int index = portBox->findText(current);
    if (index >= 0)
        portBox->setCurrentIndex(index);
    else
        portBox->setEditText(current);
}

void GeneratorDock::toggleConnection() {
    if (gen->isOpen()) {
        gen->close();
        message(tr("Desconectado."));
        return;
    }
    const QString port = portBox->currentText().trimmed();
    if (port.isEmpty()) return;
    if (!gen->open(port)) {
        message(gen->lastError(), true);
        return;
    }
    QSettings().setValue("Generator/port", port);
    message(tr("Conectado em %1.").arg(port));
    refresh();
}

void GeneratorDock::updateConnectionUi() {
    const bool open = gen->isOpen();
    connectButton->setText(open ? tr("Desconectar") : tr("Conectar"));
    connectButton->setEnabled(!locked);
    portBox->setEnabled(!open && !locked);
    refreshButton->setEnabled(!open && !locked);
    for (ChannelUi &c : ui) c.box->setEnabled(open && !locked);
    readButton->setEnabled(open && !locked);
    allOffButton->setEnabled(open); // always available while connected, also during a sweep
}

void GeneratorDock::setLocked(bool lock, const QString &reason) {
    locked = lock;
    updateConnectionUi();
    if (lock) {
        message(reason.isEmpty() ? tr("Painel bloqueado.") : reason);
    } else {
        message(QString());
        if (gen->isOpen()) refresh();
    }
}

void GeneratorDock::refresh() {
    readChannel(1);
    readChannel(2);
}

void GeneratorDock::readChannel(int channel) {
    if (!gen->isOpen()) return;
    Psg9080::ChannelState s;
    if (gen->readChannel(channel, s))
        showChannel(channel, s);
    else
        message(gen->lastError(), true);
}

void GeneratorDock::showChannel(int channel, const Psg9080::ChannelState &s) {
    ChannelUi &c = ui[channel - 1];
    c.frequencyHz = s.frequency;
    {
        QSignalBlocker b1(c.waveform), b2(c.unit), b3(c.amplitude), b4(c.offset), b5(c.duty), b6(c.phase),
            b7(c.output);
        const int w = c.waveform->findData(s.waveform);
        if (w >= 0) c.waveform->setCurrentIndex(w);
        c.unit->setCurrentIndex(std::max(0, c.unit->findData((int)s.unit)));
        c.amplitude->setValue(s.amplitude);
        c.offset->setValue(s.offset);
        c.duty->setValue(s.duty);
        c.phase->setValue(s.phase);
        c.output->setChecked(s.enabled);
    }
    c.output->setText(s.enabled ? tr("SAÍDA LIGADA") : tr("SAÍDA DESLIGADA"));
    c.frequency->setText(formatNumber(s.frequency / hzPerDisplayUnit(s.unit)));
    c.frequency->setModified(false);
}

void GeneratorDock::showOutputs(bool ch1, bool ch2) {
    const bool states[2] = {ch1, ch2};
    for (int i = 0; i < 2; ++i) {
        QSignalBlocker block(ui[i].output);
        ui[i].output->setChecked(states[i]);
        ui[i].output->setText(states[i] ? tr("SAÍDA LIGADA") : tr("SAÍDA DESLIGADA"));
    }
}

void GeneratorDock::apply(int channel, bool ok) {
    if (!ok) message(gen->lastError(), true);
    // channelWritten already refreshed the channel on success; on failure put back the device values
    if (!ok) readChannel(channel);
}

void GeneratorDock::message(const QString &text, bool error) {
    status->setStyleSheet(error ? "color: #c0392b;" : "");
    status->setText(text);
}
