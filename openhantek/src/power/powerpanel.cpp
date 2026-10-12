// SPDX-License-Identifier: GPL-2.0+

#include "powerpanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "style/darkstyle.h"

namespace {
enum CellIndex { VRMS, IRMS, FREQ, P, S, Q, PF, DPF, PHI, THDV, THDI, ENERGY };

/// "127,3 V", "4,52 kW", "12,0 mA" with a decimal comma
QString si(double value, const QString &unit, int digits = 4) {
    if (!std::isfinite(value)) return QString::fromUtf8("—");
    if (value == 0) return QString("0 ") + unit;
    const struct {
        double scale;
        const char *prefix;
    } p[] = {{1e6, "M"}, {1e3, "k"}, {1, ""}, {1e-3, "m"}, {1e-6, "µ"}};
    const double a = std::fabs(value);
    for (const auto &x : p)
        if (a >= x.scale * 0.99995 || x.scale == 1e-6) {
            const double v = value / x.scale;
            const int whole = std::fabs(v) < 1 ? 1 : (int)std::floor(std::log10(std::fabs(v))) + 1;
            return QString::number(v, 'f', std::max(0, digits - whole)).replace('.', ',') + " " +
                   QString::fromUtf8(x.prefix) + unit;
        }
    return QString::number(value);
}

QString fixed(double v, int decimals) { return QString::number(v, 'f', decimals).replace('.', ','); }

QString duration(double seconds) {
    const int s = (int)seconds;
    return QString("%1:%2:%3").arg(s / 3600, 2, 10, QChar('0')).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10,
                                                                                                         QChar('0'));
}
} // namespace

// ------------------------------------------------------------------------------------------------ HarmonicBars
HarmonicBars::HarmonicBars(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(260, 120);
}

void HarmonicBars::setData(const std::vector<double> &rms, double t, const QColor &c, const QString &text) {
    values = rms;
    thd = t;
    color = c;
    title = text;
    update();
}

void HarmonicBars::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    p.setFont(small);
    const QRectF r = QRectF(rect()).adjusted(30, 20, -6, -18);
    // grid: 0, 25, 50, 75, 100 % of the fundamental
    for (int k = 0; k <= 4; ++k) {
        const double y = r.bottom() - r.height() * k / 4.0;
        p.setPen(QPen(QColor(255, 255, 255, k == 0 ? 90 : 35), 1));
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        p.setPen(QColor(0x9a, 0xa1, 0xab));
        p.drawText(QRectF(0, y - 7, r.left() - 4, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(k * 25));
    }
    p.setPen(QColor(0xc8, 0xcc, 0xd4));
    p.drawText(QRectF(r.left(), 2, r.width(), 16), Qt::AlignLeft | Qt::AlignVCenter, title);
    if (values.empty() || !(values[0] > 0)) {
        p.setPen(QColor(0x9a, 0xa1, 0xab));
        p.drawText(r, Qt::AlignCenter, tr("sem dados"));
        return;
    }
    p.drawText(QRectF(r.left(), 2, r.width(), 16), Qt::AlignRight | Qt::AlignVCenter,
               tr("THD %1 %").arg(fixed(thd, 1)));
    const int count = std::min<int>(25, (int)values.size());
    const double slot = r.width() / count;
    for (int k = 0; k < count; ++k) {
        const double pct = std::min(100.0, 100.0 * values[k] / values[0]);
        const double h = r.height() * pct / 100.0;
        const QRectF bar(r.left() + k * slot + slot * 0.15, r.bottom() - h, slot * 0.7, h);
        QColor c = color;
        if ((k + 1) % 2 == 0) c.setAlpha(140); // even harmonics dimmer (usually small)
        p.fillRect(bar, c);
        if (k == 0 || (k + 1) % 2 == 1) {
            p.setPen(QColor(0x9a, 0xa1, 0xab));
            p.drawText(QRectF(r.left() + k * slot - 6, r.bottom() + 2, slot + 12, 14), Qt::AlignHCenter | Qt::AlignTop,
                       QString::number(k + 1));
        }
    }
}

// ------------------------------------------------------------------------------------------------ PowerPanel
PowerPanel::PowerPanel(QWidget *parent) : QWidget(parent) {
    setStyleSheet(darkstyle::panelSheet());
    QSettings s;
    s.beginGroup("Power");

    // Settings
    voltageBox = new QComboBox;
    voltageBox->addItem("CH1", 0);
    voltageBox->addItem("CH2", 1);
    voltageBox->setCurrentIndex(s.value("voltageChannel", 0).toInt() == 1 ? 1 : 0);
    currentLabel = new QLabel;
    delayBox = new QDoubleSpinBox;
    delayBox->setRange(-1000, 1000);
    delayBox->setDecimals(1);
    delayBox->setSingleStep(1);
    delayBox->setSuffix(QString::fromUtf8(" µs"));
    delayBox->setValue(s.value("currentDelayUs", 0.0).toDouble());
    darkstyle::plainSpin(delayBox);
    delayBox->setToolTip(tr("Atraso da garra em relação à ponteira de tensão. Ajuste com uma carga resistiva "
                            "(lâmpada incandescente, resistor) até o ângulo φ1 ficar em 0°."));
    invertBox = new QCheckBox(tr("Inverter corrente"));
    invertBox->setChecked(s.value("invertCurrent", false).toBool());
    invertBox->setToolTip(tr("Use se a potência ativa der negativa: a seta da garra está ao contrário"));
    QPushButton *resetButton = new QPushButton(tr("Zerar energia"));
    harmonicsBox = new QComboBox;
    harmonicsBox->addItem(tr("Harmônicos da corrente"));
    harmonicsBox->addItem(tr("Harmônicos da tensão"));
    harmonicsBox->setCurrentIndex(s.value("harmonicsOfVoltage", false).toBool() ? 1 : 0);
    s.endGroup();

    QGroupBox *setup = new QGroupBox(tr("Ligação"));
    darkstyle::colorSection(setup, darkstyle::orange());
    QGridLayout *sg = new QGridLayout(setup);
    sg->addWidget(new QLabel(tr("Tensão")), 0, 0);
    sg->addWidget(voltageBox, 0, 1);
    sg->addWidget(new QLabel(tr("Corrente")), 1, 0);
    sg->addWidget(currentLabel, 1, 1);
    sg->addWidget(new QLabel(tr("Atraso da garra")), 2, 0);
    sg->addWidget(delayBox, 2, 1);
    sg->addWidget(invertBox, 3, 0, 1, 2);
    sg->addWidget(resetButton, 4, 0, 1, 2);
    sg->addWidget(harmonicsBox, 5, 0, 1, 2);
    sg->setRowStretch(6, 1);
    setup->setFixedWidth(230);

    // Readout: black screen like the oscilloscope
    QWidget *screen = new QWidget;
    screen->setObjectName("powerScreen");
    screen->setStyleSheet("#powerScreen { background: #000; border: 1px solid #3a404a; border-radius: 6px; }"
                          "QLabel { background: transparent; }");
    QGridLayout *grid = new QGridLayout(screen);
    grid->setContentsMargins(12, 8, 12, 8);
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(2);
    QFont big("monospace");
    big.setStyleHint(QFont::Monospace);
    big.setPointSizeF(15);
    big.setBold(true);
    const char *captions[12] = {"Tensão (RMS)",     "Corrente (RMS)",   "Frequência",
                                "Potência ativa P", "Aparente S",       "Reativa Q1",
                                "Fator de potência", "cos φ1 (deslocamento)", "Ângulo φ1",
                                "THD tensão",       "THD corrente",     "Energia"};
    for (int k = 0; k < 12; ++k) {
        Cell &c = cells[k];
        c.caption = new QLabel(tr(captions[k]));
        c.caption->setStyleSheet("color: #9aa1ab; font-size: 8.5pt;");
        c.value = new QLabel(QString::fromUtf8("—"));
        c.value->setFont(big);
        c.value->setStyleSheet("color: #e6e9ee;");
        c.note = new QLabel;
        c.note->setStyleSheet("color: #9aa1ab; font-size: 8.5pt;");
        QVBoxLayout *box = new QVBoxLayout;
        box->setSpacing(0);
        box->addWidget(c.caption);
        QHBoxLayout *line = new QHBoxLayout;
        line->addWidget(c.value);
        line->addWidget(c.note, 1, Qt::AlignLeft | Qt::AlignBottom);
        box->addLayout(line);
        grid->addLayout(box, k / 3, k % 3);
    }
    cells[P].value->setStyleSheet(QString("color: %1;").arg(darkstyle::orange()));
    cells[PF].value->setStyleSheet(QString("color: %1;").arg(darkstyle::blue()));
    message = new QLabel;
    message->setWordWrap(true);
    message->setStyleSheet("color: #f2a03d;");
    grid->addWidget(message, 4, 0, 1, 3);
    grid->setRowStretch(5, 1);

    bars = new HarmonicBars;

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->addWidget(setup);
    layout->addWidget(screen, 3);
    layout->addWidget(bars, 2);

    timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this]() { analyzePending(true); });
    // a new connection setting changes what the energy means: start it again, and show the new result now
    // (also with the acquisition stopped, e.g. to adjust the clamp delay on a frozen screen)
    auto settingChanged = [this]() {
        saveSettings();
        resetEnergy();
        if (hasPending || source) analyzePending(false);
    };
    connect(voltageBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, settingChanged);
    connect(delayBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, settingChanged);
    connect(invertBox, &QCheckBox::toggled, this, settingChanged);
    connect(harmonicsBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        saveSettings();
        showHarmonics();
    });
    connect(resetButton, &QPushButton::clicked, this, &PowerPanel::resetEnergy);
    currentLabel->setText(voltageChannel() == 0 ? "CH2" : "CH1");
    resetEnergy();
}

int PowerPanel::voltageChannel() const { return voltageBox->currentData().toInt(); }

void PowerPanel::setChannelColors(const QColor &ch1, const QColor &ch2) {
    colors[0] = ch1;
    colors[1] = ch2;
    colors[0].setAlpha(255);
    colors[1].setAlpha(255);
    showHarmonics();
}

void PowerPanel::saveSettings() {
    QSettings s;
    s.beginGroup("Power");
    s.setValue("voltageChannel", voltageChannel());
    s.setValue("currentDelayUs", delayBox->value());
    s.setValue("invertCurrent", invertBox->isChecked());
    s.setValue("harmonicsOfVoltage", harmonicsBox->currentIndex() == 1);
    s.endGroup();
    currentLabel->setText(voltageChannel() == 0 ? "CH2" : "CH1");
}

void PowerPanel::resetEnergy() {
    energy = 0;
    measured = 0;
    energyClock.invalidate();
    cells[ENERGY].value->setText(si(0, "Wh"));
    cells[ENERGY].note->setText(duration(0));
}

void PowerPanel::setSource(std::function<bool(Input &)> fn) { source = std::move(fn); }

void PowerPanel::setData(const Input &in) {
    pending = in;
    hasPending = true;
    notifyData();
}

void PowerPanel::notifyData() {
    if (timer->isActive()) return; // the newest data is analyzed when the timer fires
    const qint64 since = throttle.isValid() ? throttle.elapsed() : intervalMs;
    timer->start((int)std::max<qint64>(0, intervalMs - since));
}

void PowerPanel::analyzePending(bool accumulate) {
    if (source) { // the newest acquisition, copied once per analysis
        Input in;
        if (source(in)) {
            pending = std::move(in);
            hasPending = true;
        }
    }
    if (!hasPending) return;
    throttle.restart();
    power::Options o;
    o.currentDelay = delayBox->value() * 1e-6;
    o.invertCurrent = invertBox->isChecked();
    QElapsedTimer cost;
    cost.start();
    const power::Result r = pending.bothOn ? power::analyze(pending.v, pending.i, pending.dt, o) : power::Result();
    // long records take longer: keep the GUI responsive by analyzing less often
    intervalMs = std::max(200, (int)(3 * cost.elapsed()));
    // energy: average power times the wall-clock time since the last valid analysis (gaps over 5 s, such as a
    // stopped acquisition, are not counted)
    if (accumulate) {
        if (r.valid) {
            if (energyClock.isValid()) {
                const double seconds = energyClock.elapsed() / 1000.0;
                if (seconds < 5) {
                    energy += r.p * seconds / 3600.0;
                    measured += seconds;
                }
            }
            energyClock.restart();
        } else {
            energyClock.invalidate();
        }
    }
    last = r;
    display(r, pending);
}

void PowerPanel::display(const power::Result &r, const Input &in) {
    const int vch = voltageChannel(), ich = 1 - vch;
    cells[VRMS].value->setStyleSheet(QString("color: %1;").arg(colors[vch].name()));
    cells[IRMS].value->setStyleSheet(QString("color: %1;").arg(colors[ich].name()));
    QStringList warnings;
    if (!in.bothOn) warnings << tr("Ligue o CH1 e o CH2: um mede a tensão, o outro a corrente.");
    if (!in.voltageIsVolts) warnings << tr("%1 está com garra de corrente: escolha a ponteira ou sonda de tensão.").arg(in.voltageName);
    if (!in.currentIsAmperes)
        warnings << tr("%1 não está com garra de corrente: escolha CC65/CC650 no painel (sem ela a \"corrente\" "
                       "sai em volts).")
                        .arg(in.currentName);
    if (!r.valid) {
        for (Cell &c : cells)
            if (&c != &cells[ENERGY]) {
                c.value->setText(QString::fromUtf8("—"));
                c.note->clear();
            }
        if (in.bothOn)
            warnings << tr("Sem medição: %1. Ajuste a base de tempo para ver de 2 a 10 ciclos da rede.")
                            .arg(QString::fromStdString(r.error.empty() ? "sem sinal" : r.error));
        message->setText(warnings.join("\n"));
        bars->setData({}, 0, colors[ich], harmonicsBox->currentText());
        return;
    }
    cells[VRMS].value->setText(si(r.vrms, "V"));
    cells[VRMS].note->setText(tr("pico %1").arg(si(r.vpeak, "V", 3)));
    cells[IRMS].value->setText(si(r.irms, "A"));
    cells[IRMS].note->setText(tr("crista %1").arg(fixed(r.icrest, 2)));
    cells[FREQ].value->setText(si(r.frequency, "Hz", 5));
    cells[FREQ].note->setText(tr("%1 ciclos").arg(r.cycles));
    cells[P].value->setText(si(r.p, "W"));
    cells[S].value->setText(si(r.s, "VA"));
    cells[S].note->setText(r.sn > 0 ? tr("harmônicos %1").arg(si(r.sn, "VA", 3)) : QString());
    cells[PF].value->setText(fixed(r.pf, 3));
    if (r.phaseValid) {
        // Q1: the reactive power of the fundamental, what a capacitor bank compensates (signed);
        // N also counts the harmonics and is shown when it is clearly larger
        cells[Q].value->setText(si(r.q1, "var"));
        cells[Q].note->setText(r.n > 1.05 * std::fabs(r.q1) + 0.01 * r.s ? tr("N total %1").arg(si(r.n, "var", 3))
                                                                          : QString());
        const QString kind =
            std::fabs(r.phi1) < 0.5 ? tr("resistivo") : (r.phi1 > 0 ? tr("indutivo") : tr("capacitivo"));
        cells[PF].note->setText(kind);
        cells[DPF].value->setText(fixed(r.dpf, 3));
        cells[PHI].value->setText(fixed(r.phi1, 1) + QString::fromUtf8("°"));
        cells[PHI].note->setText(r.phi1 > 0.5 ? tr("corrente atrasada")
                                              : (r.phi1 < -0.5 ? tr("corrente adiantada") : QString()));
    } else {
        cells[Q].value->setText(si(r.n, "var"));
        cells[Q].note->setText(tr("N (sem fase)"));
        cells[PF].note->clear();
        for (int c : {DPF, PHI}) {
            cells[c].value->setText(QString::fromUtf8("—"));
            cells[c].note->clear();
        }
        warnings << tr("Sem a fundamental da tensão ou da corrente: confira a ponteira e a garra (fase não calculada).");
    }
    cells[THDV].value->setText(fixed(r.thdV, 1) + " %");
    cells[THDI].value->setText(fixed(r.thdI, 1) + " %");
    cells[ENERGY].value->setText(si(energy, "Wh"));
    cells[ENERGY].note->setText(duration(measured));

    if (r.p < 0 && r.pf < -0.2)
        warnings << tr("Potência ativa negativa: a garra está invertida? Marque \"Inverter corrente\" ou vire a garra.");
    const double perCycle = 1.0 / (r.frequency * in.dt);
    if (perCycle < 40) warnings << tr("Poucos pontos por ciclo (%1): diminua a base de tempo para os harmônicos.").arg((int)perCycle);
    if (r.cycles > 40) warnings << tr("Muitos ciclos na tela (%1): de 2 a 10 ciclos dá mais resolução.").arg(r.cycles);
    message->setText(warnings.join("\n"));
    showHarmonics();
}

void PowerPanel::showHarmonics() {
    std::vector<double> h;
    const bool voltage = harmonicsBox->currentIndex() == 1;
    for (const power::Harmonic &x : last.harmonics) h.push_back(voltage ? x.v : x.i);
    const int ch = voltage ? voltageChannel() : 1 - voltageChannel();
    bars->setData(last.valid ? h : std::vector<double>(), voltage ? last.thdV : last.thdI, colors[ch],
                  harmonicsBox->currentText() + tr(" (% da fundamental)"));
}
