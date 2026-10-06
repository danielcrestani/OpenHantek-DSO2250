// SPDX-License-Identifier: GPL-2.0+

#include "zerocalibration.h"

#include <QDateTime>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSettings>
#include <QTimer>
#include <QWidget>

#include <cmath>
#include <climits>
#include <algorithm>

#include "hantekdso/hantekdsocontrol.h"
#include "scopesettings.h"
#include "viewconstants.h"

static const double kPositions[] = {-3.0, 0.0, 3.0}; // divisions
static const int kDiscardFrames = 3;                 // acquisitions ignored after each change
static const int kMeasureFrames = 4;                 // acquisitions averaged

ZeroCalibration::ZeroCalibration(HantekDsoControl *dsoControl, DsoSettingsScope *scope,
                                 const Dso::ControlSpecification *spec, QWidget *window)
    : QObject(window), dsoControl(dsoControl), scope(scope), spec(spec), window(window) {
    watchdog = new QTimer(this);
    watchdog->setSingleShot(true);
    connect(watchdog, &QTimer::timeout, this, [this]() {
        finish(false, tr("O osciloscópio não enviou aquisições. Verifique a conexão USB e tente de novo."));
    });
}

void ZeroCalibration::loadAndApply() {
    QSettings s;
    s.beginGroup("ZeroCalibration");
    for (ChannelID ch = 0; ch < spec->channels; ++ch)
        for (unsigned g = 0; g < dsoControl->gainCount(); ++g) {
            const QString key = QString("ch%1/g%2/").arg(ch).arg(g);
            if (s.contains(key + "a"))
                dsoControl->setZeroCalibration(ch, g, s.value(key + "a").toDouble(), s.value(key + "b").toDouble());
        }
    s.endGroup();
}

void ZeroCalibration::clear() {
    QSettings s;
    s.remove("ZeroCalibration");
    for (ChannelID ch = 0; ch < spec->channels; ++ch)
        for (unsigned g = 0; g < dsoControl->gainCount(); ++g) dsoControl->setZeroCalibration(ch, g, 0.0, 0.0);
}

void ZeroCalibration::start() {
    if (m_running) return;
    if (scope->horizontal.recordLength == UINT_MAX) { // Roll
        QMessageBox::information(window, tr("Calibrar zero"),
                                 tr("Escolha uma memória diferente de Roll (painel → Horizontal → Memória) e tente de "
                                    "novo."));
        return;
    }
    const auto answer = QMessageBox::question(
        window, tr("Calibrar zero dos canais"),
        tr("<p>Para calibrar, as entradas precisam estar em <b>0 V</b>:</p>"
           "<ul><li>desconecte as ponteiras dos dois canais, <b>ou</b></li>"
           "<li>ligue a ponta de cada ponteira ao próprio jacaré (terra).</li></ul>"
           "<p>O programa vai passar por todas as escalas V/div em três posições. Leva cerca de meio minuto; "
           "no fim as suas escalas e posições voltam como estavam.</p>"
           "<p>Começar?</p>"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) return;

    steps.clear();
    for (unsigned g = 0; g < dsoControl->gainCount(); ++g)
        for (double p : kPositions) steps.push_back({g, p / DIVS_VOLTAGE + 0.5});
    ys.assign(spec->channels, std::vector<std::vector<double>>(dsoControl->gainCount()));
    sum.assign(spec->channels, 0.0);
    stepIndex = 0;

    m_running = true;
    wasSampling = dsoControl->isSampling();
    dsoControl->setZeroCorrectionEnabled(false);
    dsoControl->setTriggerMode(Dso::TriggerMode::WAIT_FORCE); // AUTO: acquisitions without a trigger event
    dsoControl->setRecordTime(1e-3 * DIVS_TIME);              // 1 ms/div: fast acquisitions
    for (ChannelID ch = 0; ch < spec->channels; ++ch) {
        dsoControl->setChannelUsed(ch, true);
        dsoControl->setCoupling(ch, Dso::Coupling::DC);
        dsoControl->setProbe(ch, 1.0);
    }
    if (!wasSampling) dsoControl->enableSampling(true);

    progress = new QProgressDialog(tr("Calibrando o zero dos canais..."), tr("Cancelar"), 0, (int)steps.size(), window);
    progress->setWindowTitle(tr("Calibrar zero"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(0);
    connect(progress, &QProgressDialog::canceled, this, [this]() { finish(false); });

    applyStep();
}

void ZeroCalibration::applyStep() {
    const Step &st = steps[stepIndex];
    for (ChannelID ch = 0; ch < spec->channels; ++ch) {
        dsoControl->setGain(ch, dsoControl->gainFullScale(st.gain));
        dsoControl->setOffset(ch, st.offsetFraction);
        sum[ch] = 0.0;
    }
    frames = 0;
    watchdog->start(6000);
}

void ZeroCalibration::process(std::shared_ptr<PPresult> data) {
    if (!m_running || !data) return;
    ++frames;
    if (frames <= kDiscardFrames) return;

    const Step &st = steps[stepIndex];
    const double fullScale = dsoControl->gainFullScale(st.gain);
    for (ChannelID ch = 0; ch < spec->channels && ch < data->channelCount(); ++ch) {
        const DataChannel *d = data->data(ch);
        if (!d || d->voltage.sample.empty()) continue;
        double m = 0.0;
        for (double v : d->voltage.sample) m += v;
        m /= (double)d->voltage.sample.size();
        sum[ch] += m / fullScale; // error as a fraction of full scale (probe = 1, correction off)
    }
    if (frames < kDiscardFrames + kMeasureFrames) return;

    for (ChannelID ch = 0; ch < spec->channels; ++ch) ys[ch][st.gain].push_back(sum[ch] / kMeasureFrames);
    ++stepIndex;
    if (progress) progress->setValue((int)stepIndex);
    if (stepIndex >= steps.size()) {
        finish(true);
        return;
    }
    applyStep();
}

void ZeroCalibration::finish(bool ok, const QString &message) {
    if (!m_running) return;
    m_running = false;
    watchdog->stop();
    if (progress) {
        progress->blockSignals(true);
        progress->close();
        progress->deleteLater();
        progress = nullptr;
    }

    QString report;
    if (ok) {
        // straight line through the three positions (least squares), x = offset fraction - 0.5
        QSettings s;
        s.beginGroup("ZeroCalibration");
        s.setValue("date", QDateTime::currentDateTime().toString(Qt::ISODate));
        double worst = 0.0;
        for (ChannelID ch = 0; ch < spec->channels; ++ch) {
            for (unsigned g = 0; g < dsoControl->gainCount(); ++g) {
                const std::vector<double> &y = ys[ch][g];
                if (y.size() != 3) continue;
                double xs[3], xm = 0, ym = 0;
                for (int i = 0; i < 3; ++i) {
                    xs[i] = kPositions[i] / DIVS_VOLTAGE;
                    xm += xs[i] / 3;
                    ym += y[i] / 3;
                }
                double sxy = 0, sxx = 0;
                for (int i = 0; i < 3; ++i) {
                    sxy += (xs[i] - xm) * (y[i] - ym);
                    sxx += (xs[i] - xm) * (xs[i] - xm);
                }
                const double b = sxx > 0 ? sxy / sxx : 0.0;
                const double a = ym - b * xm;
                dsoControl->setZeroCalibration(ch, g, a, b);
                const QString key = QString("ch%1/g%2/").arg(ch).arg(g);
                s.setValue(key + "a", a);
                s.setValue(key + "b", b);
                for (double v : y) worst = std::max(worst, std::fabs(v) * DIVS_VOLTAGE);
            }
        }
        s.endGroup();
        report = tr("Calibração concluída. O maior erro corrigido foi de %1 divisão.")
                     .arg(QString::number(worst, 'f', 2));
    }

    dsoControl->setZeroCorrectionEnabled(true);
    restoreDevice();

    if (ok)
        QMessageBox::information(window, tr("Calibrar zero"), report);
    else if (!message.isEmpty())
        QMessageBox::warning(window, tr("Calibrar zero"), message);
}

void ZeroCalibration::restoreDevice() {
    for (ChannelID ch = 0; ch < spec->channels; ++ch) {
        dsoControl->setProbe(ch, scope->voltage[ch].probe);
        dsoControl->setGain(ch, scope->gain(ch) * DIVS_VOLTAGE);
        dsoControl->setOffset(ch, scope->voltage[ch].offset / DIVS_VOLTAGE + 0.5);
        if (scope->voltage[ch].couplingOrMathIndex < spec->couplings.size())
            dsoControl->setCoupling(ch, spec->couplings[scope->voltage[ch].couplingOrMathIndex]);
        dsoControl->setChannelUsed(ch, scope->voltage[ch].used || scope->spectrum[ch].used);
    }
    dsoControl->setRecordTime(scope->horizontal.timebase * DIVS_TIME);
    dsoControl->setTriggerMode(scope->trigger.mode);
    if (!wasSampling) dsoControl->enableSampling(false);
}
