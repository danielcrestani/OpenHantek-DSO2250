// SPDX-License-Identifier: GPL-2.0+
// Medições automáticas em tabela, escolhidas pelo menu "Medições".

#include "MeasurementsDock.h"

#include <QAction>
#include <QHeaderView>
#include <QMenu>
#include <QSettings>
#include <QSignalBlocker>
#include <QTableWidget>

#include <algorithm>
#include <cmath>

#include "utils/printutils.h"

MeasurementsDock::MeasurementsDock(const DsoSettingsScope *scope, const Dso::ControlSpecification *spec,
                                   const std::vector<QColor> &channelColors, QWidget *parent)
    : QDockWidget(tr("Medições"), parent), scope(scope), spec(spec), colors(channelColors) {
    setObjectName("MeasurementsDock");
    results.resize(spec->channels);

    table = new QTableWidget(0, (int)spec->channels);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->verticalHeader()->setDefaultSectionSize(22);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setStyleSheet("QTableWidget { background: #11141a; color: #e6e9ee; gridline-color: #3a404a;"
                         "  font-family: monospace; font-size: 10pt; }"
                         "QHeaderView::section { background: #23272e; color: #c8ccd4; border: 1px solid #3a404a;"
                         "  padding: 3px; font-weight: bold; }");
    for (ChannelID ch = 0; ch < spec->channels; ++ch) {
        QTableWidgetItem *h = new QTableWidgetItem(tr("CH%1").arg(ch + 1));
        if (ch < colors.size()) {
            QColor c = colors[ch];
            c.setAlpha(255);
            h->setForeground(c);
        }
        table->setHorizontalHeaderItem((int)ch, h);
    }
    setWidget(table);

    // menu
    measMenu = new QMenu(tr("&Medições"), parent);
    QSettings settings;
    settings.beginGroup("FrontPanelMeasurements");
    const bool defaults[COUNT] = {true, false, false, true, true, false, true, true, false, false, false, false, false};
    for (int m = 0; m < COUNT; ++m) {
        QAction *a = measMenu->addAction(name(m));
        a->setCheckable(true);
        a->setChecked(settings.value(QString("m%1").arg(m), defaults[m]).toBool());
        connect(a, &QAction::toggled, [this]() {
            saveSelection();
            rebuildRows();
        });
        actions.push_back(a);
    }
    settings.endGroup();
    measMenu->addSeparator();
    QAction *all = measMenu->addAction(tr("Mostrar todas"));
    connect(all, &QAction::triggered, [this]() {
        for (QAction *a : actions) {
            QSignalBlocker b(a);
            a->setChecked(true);
        }
        saveSelection();
        rebuildRows();
    });
    QAction *none = measMenu->addAction(tr("Limpar seleção"));
    connect(none, &QAction::triggered, [this]() {
        for (QAction *a : actions) {
            QSignalBlocker b(a);
            a->setChecked(false);
        }
        saveSelection();
        rebuildRows();
    });
    measMenu->addSeparator();
    QAction *show = toggleViewAction();
    show->setText(tr("Mostrar tabela de medições"));
    measMenu->addAction(show);

    rebuildRows();
}

QString MeasurementsDock::name(int m) {
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

QString MeasurementsDock::format(int m, double value) {
    if (!std::isfinite(value)) return QString("---");
    switch (m) {
    case FREQUENCY: return valueToString(value, UNIT_HERTZ, 4);
    case PERIOD:
    case WIDTH_POS:
    case WIDTH_NEG:
    case RISE:
    case FALL: return valueToString(value, UNIT_SECONDS, 4);
    case DUTY: return QString("%1 %").arg(value * 100.0, 0, 'f', 1);
    default: return valueToString(value, UNIT_VOLTS, 4);
    }
}

void MeasurementsDock::saveSelection() {
    QSettings settings;
    settings.beginGroup("FrontPanelMeasurements");
    for (int m = 0; m < COUNT; ++m) settings.setValue(QString("m%1").arg(m), actions[(size_t)m]->isChecked());
    settings.endGroup();
}

void MeasurementsDock::rebuildRows() {
    rows.clear();
    for (int m = 0; m < COUNT; ++m)
        if (actions[(size_t)m]->isChecked()) rows.push_back(m);
    table->setRowCount((int)rows.size());
    for (size_t r = 0; r < rows.size(); ++r) {
        table->setVerticalHeaderItem((int)r, new QTableWidgetItem(name(rows[r])));
        for (int c = 0; c < table->columnCount(); ++c) {
            QTableWidgetItem *it = new QTableWidgetItem("---");
            it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem((int)r, c, it);
        }
    }
    updateTable();
}

void MeasurementsDock::updateTable() {
    for (size_t r = 0; r < rows.size(); ++r) {
        for (ChannelID ch = 0; ch < spec->channels; ++ch) {
            QTableWidgetItem *it = table->item((int)r, (int)ch);
            if (!it) continue;
            const Result &res = results[ch];
            const bool used = ch < scope->voltage.size() && scope->voltage[ch].used;
            it->setText(used && res.valid ? format(rows[r], res.v[rows[r]]) : QString("---"));
        }
    }
}

void MeasurementsDock::showData(std::shared_ptr<PPresult> data) {
    if (!data) return;
    for (ChannelID ch = 0; ch < spec->channels && ch < data->channelCount(); ++ch) {
        const DataChannel *dc = data->data(ch);
        if (!dc || dc->voltage.sample.empty() || !scope->voltage[ch].used) {
            results[ch].valid = false;
            continue;
        }
        results[ch] = analyze(dc->voltage.sample, dc->voltage.interval, dc->frequency);
    }
    if (isVisible()) updateTable();
}

MeasurementsDock::Result MeasurementsDock::analyze(const std::vector<double> &v, double dt, double fallbackFreq) {
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
