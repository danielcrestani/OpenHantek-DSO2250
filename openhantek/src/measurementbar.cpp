// SPDX-License-Identifier: GPL-2.0+
// Rodapé de medições por canal, na cor do canal.

#include "measurementbar.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QSettings>
#include <QSignalBlocker>
#include <QPalette>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "utils/printutils.h"

MeasurementBar::MeasurementBar(const DsoSettingsScope *scope, const Dso::ControlSpecification *spec,
                               const std::vector<QColor> &channelColors, QWidget *parent)
    : QWidget(parent), scope(scope), spec(spec), colors(channelColors) {
    results.resize(spec->channels);
    actions.resize(spec->channels);

    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 2, 8, 4);
    layout->setSpacing(1);
    for (ChannelID ch = 0; ch < spec->channels; ++ch) {
        QLabel *l = new QLabel();
        QColor c = ch < colors.size() ? colors[ch] : QColor(Qt::white);
        c.setAlpha(255);
        l->setStyleSheet(QString("QLabel { color: %1; font-family: monospace; font-size: 10pt; }").arg(c.name()));
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(l);
        labels.push_back(l);
    }

    cursorLabel = new QLabel();
    cursorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cursorLabel->setVisible(false);
    layout->addWidget(cursorLabel);

    // Menu: Medições > CH1 / CH2 > (lista)
    measMenu = new QMenu(tr("&Medições"), parent);
    QSettings settings;
    settings.beginGroup("ChannelMeasurements");
    const bool defaults[COUNT] = {true, false, false, false, true, false, true, false, false, false, false, false, false};
    for (ChannelID ch = 0; ch < spec->channels; ++ch) {
        QMenu *sub = measMenu->addMenu(tr("CH%1").arg(ch + 1));
        for (int m = 0; m < COUNT; ++m) {
            QAction *a = sub->addAction(name(m));
            a->setCheckable(true);
            a->setChecked(settings.value(QString("ch%1_m%2").arg(ch).arg(m), defaults[m]).toBool());
            connect(a, &QAction::toggled, [this]() {
                saveSelection();
                updateLabels();
            });
            actions[ch].push_back(a);
        }
        sub->addSeparator();
        QAction *clear = sub->addAction(tr("Nenhuma"));
        connect(clear, &QAction::triggered, [this, ch]() {
            for (QAction *a : actions[ch]) {
                QSignalBlocker b(a);
                a->setChecked(false);
            }
            saveSelection();
            updateLabels();
        });
    }
    settings.endGroup();
    measMenu->addSeparator();
    QAction *showBar = measMenu->addAction(tr("Mostrar rodapé de medições"));
    showBar->setCheckable(true);
    showBar->setChecked(true);
    connect(showBar, &QAction::toggled, this, &QWidget::setVisible);

    updateLabels();
}

bool MeasurementBar::selected(ChannelID ch, int m) const { return actions[ch][(size_t)m]->isChecked(); }

QString MeasurementBar::name(int m) {
    switch (m) {
    case VPP: return tr("Pico a pico (Vpp)");
    case VMAX: return tr("Máximo");
    case VMIN: return tr("Mínimo");
    case MEAN: return tr("Média (DC)");
    case RMS: return tr("RMS (AC+DC)");
    case ACRMS: return tr("RMS AC");
    case FREQUENCY: return tr("Frequência");
    case PERIOD: return tr("Período");
    case DUTY: return tr("Ciclo de trabalho");
    case WIDTH_POS: return tr("Largura positiva");
    case WIDTH_NEG: return tr("Largura negativa");
    case RISE: return tr("Tempo de subida (10–90%)");
    case FALL: return tr("Tempo de descida (90–10%)");
    }
    return QString();
}

QString MeasurementBar::shortName(int m) {
    switch (m) {
    case VPP: return "Vpp";
    case VMAX: return tr("Máx");
    case VMIN: return tr("Mín");
    case MEAN: return tr("Média");
    case RMS: return "RMS";
    case ACRMS: return "RMS AC";
    case FREQUENCY: return tr("Freq");
    case PERIOD: return tr("Per");
    case DUTY: return tr("Ciclo");
    case WIDTH_POS: return "+Larg";
    case WIDTH_NEG: return "-Larg";
    case RISE: return tr("Subida");
    case FALL: return tr("Descida");
    }
    return QString();
}

QString MeasurementBar::format(int m, double value, Unit unit) {
    if (!std::isfinite(value)) return QString("---");
    switch (m) {
    case FREQUENCY: return valueToString(value, UNIT_HERTZ, 4);
    case PERIOD:
    case WIDTH_POS:
    case WIDTH_NEG:
    case RISE:
    case FALL: return valueToString(value, UNIT_SECONDS, 4);
    case DUTY: return QString("%1 %").arg(value * 100.0, 0, 'f', 1);
    default: return valueToString(value, unit, 4);
    }
}

void MeasurementBar::saveSelection() {
    QSettings settings;
    settings.beginGroup("ChannelMeasurements");
    for (ChannelID ch = 0; ch < spec->channels; ++ch)
        for (int m = 0; m < COUNT; ++m) settings.setValue(QString("ch%1_m%2").arg(ch).arg(m), selected(ch, m));
    settings.endGroup();
}

void MeasurementBar::updateLabels() {
    for (ChannelID ch = 0; ch < spec->channels; ++ch) {
        QStringList parts;
        const bool used = ch < scope->voltage.size() && scope->voltage[ch].used;
        const Result &res = results[ch];
        for (int m = 0; m < COUNT; ++m) {
            if (!selected(ch, m)) continue;
            QString nm = shortName(m);
            if (scope->unit(ch) == UNIT_AMPERE && m == VPP) nm = "Ipp";
            parts << QString("%1 %2").arg(nm, used && res.valid ? format(m, res.v[m], scope->unit(ch)) : QString("---"));
        }
        labels[ch]->setVisible(used && !parts.isEmpty());
        labels[ch]->setText(QString("CH%1   ").arg(ch + 1) + parts.join("    "));
    }
}

void MeasurementBar::showData(std::shared_ptr<PPresult> data) {
    if (!data) return;
    for (ChannelID ch = 0; ch < spec->channels && ch < data->channelCount(); ++ch) {
        const DataChannel *dc = data->data(ch);
        if (!dc || dc->voltage.sample.empty() || !scope->voltage[ch].used) {
            results[ch].valid = false;
            continue;
        }
        results[ch] = analyze(dc->voltage.sample, dc->voltage.interval, dc->frequency);
    }
    if (isVisible()) updateLabels();
}

MeasurementBar::Result MeasurementBar::analyze(const std::vector<double> &v, double dt, double fallbackFreq) {
    Result r;
    for (double &x : r.v) x = NAN;
    const size_t n = v.size();
    if (n < 4) return r;

    double mn = v[0], mx = v[0], sum = 0, sum2 = 0;
    for (double x : v) {
        if (x < mn) mn = x;
        if (x > mx) mx = x;
        sum += x;
        sum2 += x * x;
    }
    const double mean = sum / n;
    const double rms = std::sqrt(sum2 / n);
    r.v[VPP] = mx - mn;
    r.v[VMAX] = mx;
    r.v[VMIN] = mn;
    r.v[MEAN] = mean;
    r.v[RMS] = rms;
    r.v[ACRMS] = std::sqrt(std::max(0.0, rms * rms - mean * mean));
    r.valid = true;

    // Bordas: cruzamentos do nível de 50% com histerese de 10%
    const double vpp = mx - mn;
    if (vpp <= 0 || dt <= 0) {
        if (fallbackFreq > 0) {
            r.v[FREQUENCY] = fallbackFreq;
            r.v[PERIOD] = 1.0 / fallbackFreq;
        }
        return r;
    }
    const double lo = mn + 0.1 * vpp, mid = mn + 0.5 * vpp, hi = mn + 0.9 * vpp;
    const double hyst = 0.1 * vpp;
    std::vector<size_t> rising, falling;
    bool high = v[0] > mid;
    for (size_t i = 1; i < n; ++i) {
        if (!high && v[i] > mid + hyst) {
            size_t j = i;
            while (j > 0 && v[j - 1] > mid) --j;
            rising.push_back(j);
            high = true;
        } else if (high && v[i] < mid - hyst) {
            size_t j = i;
            while (j > 0 && v[j - 1] < mid) --j;
            falling.push_back(j);
            high = false;
        }
    }

    if (rising.size() >= 2) {
        const double period = (double)(rising.back() - rising.front()) * dt / (double)(rising.size() - 1);
        r.v[PERIOD] = period;
        r.v[FREQUENCY] = 1.0 / period;
    } else if (fallbackFreq > 0) {
        r.v[FREQUENCY] = fallbackFreq;
        r.v[PERIOD] = 1.0 / fallbackFreq;
    }

    // Larguras: de cada subida até a descida seguinte (e vice-versa)
    double wpSum = 0, wnSum = 0;
    int wpN = 0, wnN = 0;
    for (size_t a : rising) {
        auto it = std::upper_bound(falling.begin(), falling.end(), a);
        if (it != falling.end()) {
            wpSum += (double)(*it - a) * dt;
            ++wpN;
        }
    }
    for (size_t a : falling) {
        auto it = std::upper_bound(rising.begin(), rising.end(), a);
        if (it != rising.end()) {
            wnSum += (double)(*it - a) * dt;
            ++wnN;
        }
    }
    if (wpN) r.v[WIDTH_POS] = wpSum / wpN;
    if (wnN) r.v[WIDTH_NEG] = wnSum / wnN;
    if (wpN && wnN) r.v[DUTY] = r.v[WIDTH_POS] / (r.v[WIDTH_POS] + r.v[WIDTH_NEG]);

    // Tempos de subida/descida (10% a 90%), média das primeiras bordas
    double rSum = 0, fSum = 0;
    int rN = 0, fN = 0;
    for (size_t k = 0; k < rising.size() && k < 200; ++k) {
        size_t a = rising[k], b = rising[k];
        while (a > 0 && v[a] > lo) --a;
        while (b < n - 1 && v[b] < hi) ++b;
        if (v[a] <= lo && v[b] >= hi) {
            rSum += (double)(b - a) * dt;
            ++rN;
        }
    }
    for (size_t k = 0; k < falling.size() && k < 200; ++k) {
        size_t a = falling[k], b = falling[k];
        while (a > 0 && v[a] < hi) --a;
        while (b < n - 1 && v[b] > lo) ++b;
        if (v[a] >= hi && v[b] <= lo) {
            fSum += (double)(b - a) * dt;
            ++fN;
        }
    }
    if (rN) r.v[RISE] = rSum / rN;
    if (fN) r.v[FALL] = fSum / fN;
    return r;
}

void MeasurementBar::setCursorText(const QString &text, const QColor &color) {
    if (text.isEmpty()) {
        cursorLabel->setVisible(false);
        return;
    }
    QColor c = color;
    c.setAlpha(255);
    cursorLabel->setStyleSheet(
        QString("QLabel { color: %1; font-family: monospace; font-size: 10pt; border-top: 1px solid #333; }")
            .arg(c.name()));
    cursorLabel->setText(text);
    cursorLabel->setVisible(true);
}
