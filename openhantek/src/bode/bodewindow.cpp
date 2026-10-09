// SPDX-License-Identifier: GPL-2.0+

#include "bodewindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>
#include <cmath>

#include "docks/HorizontalDock.h"
#include "docks/TriggerDock.h"
#include "docks/VoltageDock.h"
#include "dsowidget.h"
#include "generator/GeneratorDock.h"
#include "generator/psg9080.h"
#include "hantekdso/hantekdsocontrol.h"
#include "scopesettings.h"
#include "viewconstants.h"

namespace {

const double kPi = 3.14159265358979323846;
const double kPeriodsOnScreen = 10.0;   ///< target number of periods in one acquisition
const double kMinPeriods = 3.0;         ///< below this the frequency cannot be measured
const int kDiscardAfterFrequency = 2;   ///< acquisitions ignored after changing the generator
const int kDiscardAfterScope = 3;       ///< acquisitions ignored after changing timebase or V/div
const int kMaxRangeSteps = 8;           ///< V/div corrections per point
const int kMaxAttempts = 3;             ///< measurements tried before giving up a point

const char *kUnitNames[] = {"Hz", "kHz", "MHz"};
const double kUnitScale[] = {1.0, 1e3, 1e6};

QString hzText(double hz) {
    if (hz >= 1e6) return GeneratorDock::formatNumber(hz / 1e6, 6) + " MHz";
    if (hz >= 1e3) return GeneratorDock::formatNumber(hz / 1e3, 6) + " kHz";
    if (hz >= 1.0) return GeneratorDock::formatNumber(hz, 4) + " Hz";
    return GeneratorDock::formatNumber(hz * 1e3, 3) + " mHz";
}

QString num(double v, int decimals) { return QString::number(v, 'f', decimals).replace('.', ','); }

/// Put `hz` in the line edit and unit combo with the unit that reads best.
void showFrequency(QLineEdit *edit, QComboBox *unit, double hz) {
    int u = hz >= 1e6 ? 2 : (hz >= 1e3 ? 1 : 0);
    unit->setCurrentIndex(u);
    edit->setText(GeneratorDock::formatNumber(hz / kUnitScale[u], 6));
}

} // namespace

double BodeWindow::Result::gainDb() const { return 20.0 * std::log10(std::max(std::abs(h), 1e-20)); }
double BodeWindow::Result::phaseDeg() const { return bode::wrapDegrees(std::arg(h) * 180.0 / kPi); }

BodeWindow::BodeWindow(Psg9080 *generator, GeneratorDock *generatorDock, HantekDsoControl *dsoControl,
                       DsoSettingsScope *scope, const Dso::ControlSpecification *spec, VoltageDock *voltageDock,
                       HorizontalDock *horizontalDock, TriggerDock *triggerDock, DsoWidget *dsoWidget,
                       QAction *samplingAction, QWidget *parent)
    : QWidget(parent, Qt::Window), gen(generator), genDock(generatorDock), dsoControl(dsoControl), scope(scope),
      spec(spec), voltageDock(voltageDock), horizontalDock(horizontalDock), triggerDock(triggerDock),
      dsoWidget(dsoWidget), samplingAction(samplingAction) {
    setWindowTitle(tr("Resposta em frequência (Bode) — PSG9080 + DSO-2250"));
    resize(1100, 640);

    plot = new BodePlot;
    settingsPanel = makeSettings();

    progress = new QProgressBar;
    progress->setTextVisible(true);
    progress->setFormat("%v / %m");
    statusLabel = new QLabel;
    statusLabel->setWordWrap(true);

    QWidget *right = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(plot, 1);
    rightLayout->addWidget(progress);
    rightLayout->addWidget(statusLabel);

    QSplitter *split = new QSplitter(Qt::Horizontal);
    split->addWidget(settingsPanel);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->addWidget(split);

    watchdog = new QTimer(this);
    watchdog->setSingleShot(true);
    connect(watchdog, &QTimer::timeout, this, [this]() {
        finish(false, tr("O osciloscópio parou de enviar aquisições. Verifique a conexão USB e se a aquisição "
                         "está em RUN."));
    });

    loadSettings();
    loadCalibration();
    updateCalibrationUi();
    updateOutputLabel();
    setUiRunning(false);
}

// ------------------------------------------------------------------------------------------------ interface
QWidget *BodeWindow::makeSettings() {
    QWidget *panel = new QWidget;
    panel->setMinimumWidth(320);
    panel->setMaximumWidth(420);
    QVBoxLayout *v = new QVBoxLayout(panel);
    v->setContentsMargins(0, 0, 6, 0);

    auto channelBox = [](const QString &prefix) {
        QComboBox *c = new QComboBox;
        c->addItem(prefix + " CH1", 1);
        c->addItem(prefix + " CH2", 2);
        return c;
    };
    auto freqRow = [](QLineEdit *&edit, QComboBox *&unit) {
        edit = new QLineEdit;
        edit->setAlignment(Qt::AlignRight);
        unit = new QComboBox;
        for (int i = 0; i < 3; ++i) unit->addItem(kUnitNames[i]);
        QHBoxLayout *row = new QHBoxLayout;
        row->addWidget(edit, 1);
        row->addWidget(unit);
        return row;
    };

    // Connections
    QGroupBox *wiring = new QGroupBox(tr("Ligações"));
    QFormLayout *wf = new QFormLayout(wiring);
    genChannelBox = channelBox(tr("Gerador"));
    refChannelBox = channelBox(tr("Osciloscópio"));
    outChannelLabel = new QLabel;
    wf->addRow(tr("Sinal de teste"), genChannelBox);
    wf->addRow(tr("Entrada do circuito"), refChannelBox);
    wf->addRow(tr("Saída do circuito"), outChannelLabel);
    connect(refChannelBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { updateOutputLabel(); });
    v->addWidget(wiring);

    // Sweep
    QGroupBox *sweep = new QGroupBox(tr("Varredura"));
    QFormLayout *sf = new QFormLayout(sweep);
    sf->addRow(tr("Frequência inicial"), freqRow(startEdit, startUnit));
    sf->addRow(tr("Frequência final"), freqRow(stopEdit, stopUnit));
    perDecadeBox = new QSpinBox;
    perDecadeBox->setRange(1, 100);
    perDecadeBox->setSuffix(tr(" pontos/década"));
    sf->addRow(tr("Resolução"), perDecadeBox);
    limitsLabel = new QLabel;
    limitsLabel->setWordWrap(true);
    sf->addRow(limitsLabel);
    v->addWidget(sweep);

    // Signal and measurement
    QGroupBox *meas = new QGroupBox(tr("Sinal e medição"));
    QFormLayout *mf = new QFormLayout(meas);
    amplitudeBox = new QDoubleSpinBox;
    amplitudeBox->setDecimals(3);
    amplitudeBox->setRange(0.001, 20.0);
    amplitudeBox->setSingleStep(0.1);
    amplitudeBox->setSuffix(" Vpp");
    offsetBox = new QDoubleSpinBox;
    offsetBox->setDecimals(2);
    offsetBox->setRange(-10.0, 10.0);
    offsetBox->setSingleStep(0.1);
    offsetBox->setSuffix(" V");
    averagesBox = new QSpinBox;
    averagesBox->setRange(1, 32);
    averagesBox->setSuffix(tr(" aquisições"));
    autoRangeBox = new QCheckBox(tr("Ajustar V/div automaticamente"));
    mf->addRow(tr("Amplitude"), amplitudeBox);
    mf->addRow(tr("Offset"), offsetBox);
    mf->addRow(tr("Média por ponto"), averagesBox);
    mf->addRow(autoRangeBox);
    v->addWidget(meas);

    // Calibration
    QGroupBox *cal = new QGroupBox(tr("Calibração"));
    QVBoxLayout *cl = new QVBoxLayout(cal);
    useCalibrationBox = new QCheckBox(tr("Descontar a calibração"));
    useCalibrationBox->setToolTip(tr("Divide cada medida pela resposta medida com as duas ponteiras no mesmo "
                                     "ponto: remove diferenças entre canais, ponteiras e cabos."));
    calibrationLabel = new QLabel;
    calibrationLabel->setWordWrap(true);
    calibrateButton = new QPushButton(tr("Calibrar…"));
    calibrateButton->setToolTip(tr("Varredura com as duas ponteiras ligadas no mesmo ponto (saída do gerador)"));
    cl->addWidget(useCalibrationBox);
    cl->addWidget(calibrationLabel);
    cl->addWidget(calibrateButton);
    connect(calibrateButton, &QPushButton::clicked, this, [this]() {
        const auto answer = QMessageBox::question(
            this, tr("Calibrar"),
            tr("<p>Ligue as <b>duas ponteiras no mesmo ponto</b>: a saída do gerador (sem o circuito).</p>"
               "<p>Será feita uma varredura com as configurações atuais; o resultado fica guardado e pode ser "
               "descontado das próximas medidas.</p><p>Começar?</p>"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer == QMessageBox::Yes) start(true);
    });
    v->addWidget(cal);

    // Actions
    startButton = new QPushButton(tr("Iniciar"));
    startButton->setMinimumHeight(34);
    QFont bold = startButton->font();
    bold.setBold(true);
    startButton->setFont(bold);
    connect(startButton, &QPushButton::clicked, this, [this]() {
        if (running())
            stop();
        else
            start(false);
    });
    csvButton = new QPushButton(tr("Exportar CSV…"));
    imageButton = new QPushButton(tr("Salvar imagem…"));
    copyButton = new QPushButton(tr("Copiar imagem"));
    connect(csvButton, &QPushButton::clicked, this, &BodeWindow::exportCsv);
    connect(imageButton, &QPushButton::clicked, this, &BodeWindow::saveImage);
    connect(copyButton, &QPushButton::clicked, this, &BodeWindow::copyImage);
    v->addWidget(startButton);
    QHBoxLayout *files = new QHBoxLayout;
    files->addWidget(csvButton);
    files->addWidget(imageButton);
    v->addLayout(files);
    v->addWidget(copyButton);
    v->addStretch(1);
    return panel;
}

void BodeWindow::loadSettings() {
    QSettings s;
    s.beginGroup("Bode");
    genChannelBox->setCurrentIndex(s.value("generatorChannel", 1).toInt() == 2 ? 1 : 0);
    refChannelBox->setCurrentIndex(s.value("inputChannel", 1).toInt() == 2 ? 1 : 0);
    showFrequency(startEdit, startUnit, s.value("start", 10.0).toDouble());
    showFrequency(stopEdit, stopUnit, s.value("stop", 1e6).toDouble());
    perDecadeBox->setValue(s.value("perDecade", 10).toInt());
    amplitudeBox->setValue(s.value("amplitude", 1.0).toDouble());
    offsetBox->setValue(s.value("offset", 0.0).toDouble());
    averagesBox->setValue(s.value("averages", 2).toInt());
    autoRangeBox->setChecked(s.value("autoRange", true).toBool());
    useCalibrationBox->setChecked(s.value("useCalibration", false).toBool());
    s.endGroup();
}

void BodeWindow::saveSettings() {
    QSettings s;
    s.beginGroup("Bode");
    s.setValue("generatorChannel", genChannel());
    s.setValue("inputChannel", refChannel() + 1);
    double hz = 0;
    if (readFrequency(startEdit, startUnit, hz)) s.setValue("start", hz);
    if (readFrequency(stopEdit, stopUnit, hz)) s.setValue("stop", hz);
    s.setValue("perDecade", perDecadeBox->value());
    s.setValue("amplitude", amplitudeBox->value());
    s.setValue("offset", offsetBox->value());
    s.setValue("averages", averagesBox->value());
    s.setValue("autoRange", autoRangeBox->isChecked());
    s.setValue("useCalibration", useCalibrationBox->isChecked());
    s.endGroup();
}

int BodeWindow::genChannel() const { return genChannelBox->currentData().toInt(); }
int BodeWindow::refChannel() const { return refChannelBox->currentData().toInt() - 1; }
int BodeWindow::outChannel() const { return refChannel() == 0 ? 1 : 0; }

void BodeWindow::updateOutputLabel() {
    outChannelLabel->setText(tr("Osciloscópio CH%1 (o outro canal)").arg(outChannel() + 1));
}

bool BodeWindow::readFrequency(QLineEdit *edit, QComboBox *unit, double &hz) {
    double v = 0;
    if (!GeneratorDock::parseNumber(edit->text(), v) || !(v > 0)) return false;
    hz = v * kUnitScale[std::max(0, unit->currentIndex())];
    return true;
}

double BodeWindow::minRecordTime() const {
    return scope->horizontal.recordLength / dsoControl->getMaxSamplerate();
}

double BodeWindow::maxRecordTime() const {
    return scope->horizontal.recordLength / dsoControl->getMinSamplerate();
}

double BodeWindow::minimumFrequency() const { return kMinPeriods / maxRecordTime(); }

void BodeWindow::updateLimits() {
    if (scope->horizontal.recordLength == UINT_MAX) {
        limitsLabel->setText(tr("A memória está em Roll: escolha outro tamanho no painel (Horizontal → "
                                "Memória) para medir."));
        return;
    }
    limitsLabel->setText(tr("Faixa possível com a memória atual: %1 a 80 MHz. Acima de %2 o osciloscópio "
                            "amostra abaixo de Nyquist; a medida continua válida (frequência conhecida).")
                             .arg(hzText(minimumFrequency()), hzText(dsoControl->getMaxSamplerate() / 2.0)));
}

void BodeWindow::loadCalibration() {
    calibration.clear();
    QSettings s;
    s.beginGroup("Bode/calibration");
    const QStringList pts = s.value("points").toStringList();
    calibrationInfo = s.value("info").toString();
    s.endGroup();
    for (const QString &p : pts) {
        const QStringList f = p.split(';');
        if (f.size() != 3) continue;
        calibration.push_back({f[0].toDouble(), bode::Phasor(f[1].toDouble(), f[2].toDouble())});
    }
    std::sort(calibration.begin(), calibration.end(),
              [](const bode::ResponsePoint &a, const bode::ResponsePoint &b) { return a.freq < b.freq; });
}

void BodeWindow::saveCalibration() {
    calibration.clear();
    for (const Result &r : results)
        if (r.valid) calibration.push_back({r.freq, r.h});
    QStringList pts;
    for (const bode::ResponsePoint &p : calibration)
        pts << QString("%1;%2;%3").arg(p.freq, 0, 'g', 12).arg(p.h.real(), 0, 'g', 12).arg(p.h.imag(), 0, 'g', 12);
    calibrationInfo = tr("%1, %2 a %3, entrada CH%4")
                          .arg(QDateTime::currentDateTime().toString("dd/MM/yyyy HH:mm"),
                               hzText(calibration.front().freq), hzText(calibration.back().freq))
                          .arg(refChannel() + 1);
    QSettings s;
    s.beginGroup("Bode/calibration");
    s.setValue("points", pts);
    s.setValue("info", calibrationInfo);
    s.endGroup();
    useCalibrationBox->setChecked(true);
}

void BodeWindow::updateCalibrationUi() {
    useCalibrationBox->setEnabled(!calibration.empty() && !running());
    if (calibration.empty()) {
        useCalibrationBox->setChecked(false);
        calibrationLabel->setText(tr("Sem calibração."));
    } else {
        calibrationLabel->setText(tr("Calibração: %1.").arg(calibrationInfo));
    }
}

void BodeWindow::setUiRunning(bool run) {
    for (QWidget *w : settingsPanel->findChildren<QGroupBox *>()) w->setEnabled(!run);
    startButton->setText(run ? tr("Parar") : tr("Iniciar"));
    csvButton->setEnabled(!run && !results.empty());
    imageButton->setEnabled(!run && !results.empty());
    copyButton->setEnabled(!results.empty());
    if (!run) updateCalibrationUi();
}

void BodeWindow::status(const QString &text, bool error) {
    statusLabel->setStyleSheet(error ? "color: #c0392b;" : "");
    statusLabel->setText(text);
}

void BodeWindow::showEvent(QShowEvent *event) {
    updateLimits();
    QWidget::showEvent(event);
}

void BodeWindow::closeEvent(QCloseEvent *event) {
    if (running()) stop();
    saveSettings();
    QWidget::closeEvent(event);
}

// ------------------------------------------------------------------------------------------------ sweep
void BodeWindow::start(bool calibrationRun) {
    if (running()) return;
    if (!gen->isOpen()) {
        status(tr("Conecte o gerador no painel \"Gerador PSG9080\" (menu Exibir) antes de começar."), true);
        return;
    }
    if (scope->horizontal.recordLength == UINT_MAX) {
        updateLimits();
        status(tr("A memória do osciloscópio está em Roll: escolha outro tamanho no painel (Horizontal → "
                  "Memória)."),
               true);
        return;
    }
    double f1 = 0, f2 = 0;
    if (!readFrequency(startEdit, startUnit, f1) || !readFrequency(stopEdit, stopUnit, f2)) {
        status(tr("Frequência inicial ou final inválida."), true);
        return;
    }

    // Both channels on before computing limits: the sample rate depends on how many channels are used
    saved = Saved();
    for (ChannelID ch = 0; ch < spec->channels; ++ch) {
        saved.gain.push_back(scope->voltage[ch].gainStepIndex);
        saved.offset.push_back(scope->voltage[ch].offset);
        saved.used.push_back(scope->voltage[ch].used);
        voltageDock->selectUsed(ch, true);
    }
    saved.timebase = scope->horizontal.timebase;
    saved.triggerMode = scope->trigger.mode;
    saved.triggerSpecial = scope->trigger.special;
    saved.triggerSource = scope->trigger.source;
    saved.triggerLevel =
        scope->trigger.source < scope->voltage.size() ? scope->voltage[scope->trigger.source].trigger : 0.0;
    saved.wasSampling = dsoControl->isSampling();
    updateLimits();
    auto undo = [this](const QString &why) {
        for (ChannelID ch = 0; ch < spec->channels; ++ch) voltageDock->selectUsed(ch, saved.used[ch]);
        status(why, true);
    };

    const double fLow = minimumFrequency();
    if (std::min(f1, f2) < fLow * 0.999 || std::max(f1, f2) > psg9080::kMaxFrequency) {
        undo(tr("Escolha frequências entre %1 e 80 MHz.").arg(hzText(fLow)));
        return;
    }
    if (f1 > f2) std::swap(f1, f2);

    // Generator: sine with the chosen amplitude and offset, output on
    const int g = genChannel();
    bool a = false, b = false;
    if (!gen->readOutputs(a, b) || !gen->setWaveform(g, 0) || !gen->setAmplitude(g, amplitudeBox->value()) ||
        !gen->setOffset(g, offsetBox->value()) || !gen->setFrequency(g, f1)) {
        undo(gen->lastError());
        return;
    }
    saved.generatorOutput = g == 1 ? a : b;
    if (!gen->setOutput(g, true)) {
        undo(gen->lastError());
        return;
    }

    // Scope: automatic trigger on the input channel, traces centered
    const int ref = refChannel();
    triggerDock->selectSource(false, (unsigned)ref);
    triggerDock->selectMode(Dso::TriggerMode::WAIT_FORCE);
    for (ChannelID ch = 0; ch < spec->channels; ++ch) dsoWidget->setOffsetValue(ch, 0.0);
    dsoWidget->setTriggerLevelValue(ref, 0.0);
    if (!dsoControl->isSampling()) samplingAction->trigger();

    calibrating = calibrationRun;
    freqs = bode::logSweep(f1, f2, perDecadeBox->value());
    results.clear();
    plot->clear();
    plot->setFrequencyRange(f1, f2);
    plot->setNote(calibrating ? tr("calibração") : (useCalibrationBox->isChecked() ? tr("calibrado") : QString()));
    progress->setRange(0, (int)freqs.size());
    progress->setValue(0);
    index = 0;
    saveSettings();

    state = State::Settling;
    setUiRunning(true);
    genDock->setLocked(true, tr("Em uso pela análise de resposta em frequência."));
    emit runningChanged(true);
    startPoint();
}

void BodeWindow::stop() { finish(false, tr("Varredura interrompida.")); }

void BodeWindow::finish(bool completed, const QString &message) {
    if (!running()) return;
    watchdog->stop();
    state = State::Idle;

    // Scope back to how it was
    horizontalDock->selectTimebase(saved.timebase);
    for (ChannelID ch = 0; ch < spec->channels && ch < saved.gain.size(); ++ch) {
        voltageDock->selectGain(ch, saved.gain[ch]);
        dsoWidget->setOffsetValue(ch, saved.offset[ch]);
        voltageDock->selectUsed(ch, saved.used[ch]);
    }
    triggerDock->selectSource(saved.triggerSpecial, saved.triggerSource);
    triggerDock->selectMode(saved.triggerMode);
    if (!saved.triggerSpecial && saved.triggerSource < spec->channels)
        dsoWidget->setTriggerLevelValue(saved.triggerSource, saved.triggerLevel);
    if (!saved.wasSampling && dsoControl->isSampling()) samplingAction->trigger();

    // Generator output as it was (the last frequency stays)
    if (gen->isOpen() && !saved.generatorOutput) gen->setOutput(genChannel(), false);

    if (calibrating) {
        const size_t valid = std::count_if(results.begin(), results.end(), [](const Result &r) { return r.valid; });
        if (completed && valid >= 2) {
            saveCalibration();
            status(tr("Calibração guardada (%1 pontos). Agora ligue o circuito e clique em Iniciar.").arg(valid));
        } else {
            status(message.isEmpty() ? tr("Calibração não concluída.") : message, true);
        }
    } else if (completed) {
        const size_t bad = std::count_if(results.begin(), results.end(), [](const Result &r) { return !r.valid; });
        status(bad ? tr("Varredura concluída: %1 pontos, %2 sem medida válida (veja o CSV).")
                         .arg(results.size())
                         .arg(bad)
                   : tr("Varredura concluída: %1 pontos.").arg(results.size()));
    } else {
        status(message, !message.isEmpty() && message != tr("Varredura interrompida."));
    }
    calibrating = false;
    setUiRunning(false);
    genDock->setLocked(false);
    emit runningChanged(false);
}

void BodeWindow::startPoint() {
    const double f = freqs[index];
    if (!gen->setFrequency(genChannel(), f)) {
        finish(false, gen->lastError());
        return;
    }
    setTimebaseFor(f);
    rangeSteps = 0;
    attempts = 0;
    slowerTried = false;
    count = 0;
    sumH = bode::Phasor(0, 0);
    sumIn = sumOut = 0;
    minSnrIn = minSnrOut = 1e9;
    anyAliased = false;
    state = State::Settling;
    discard = kDiscardAfterScope;
    status(tr("Ponto %1 de %2: %3…").arg(index + 1).arg(freqs.size()).arg(hzText(f)));
    armWatchdog();
}

void BodeWindow::setTimebaseFor(double f) {
    double t = kPeriodsOnScreen / f;
    t = std::max(minRecordTime(), std::min(maxRecordTime(), t));
    // The dock rounds down to 1-2-5 steps: ask a bit more so that the rounding keeps enough periods
    horizontalDock->selectTimebase(t / DIVS_TIME * 1.0001);
    recordTime = scope->horizontal.timebase * DIVS_TIME;
}

void BodeWindow::changed(int frames) {
    discard = std::max(discard, frames);
    state = State::Settling;
    armWatchdog();
}

void BodeWindow::armWatchdog() {
    // a frame lasts about one record time (plus USB and processing)
    const double seconds = std::max(5.0, 4.0 * std::max(recordTime, scope->horizontal.timebase * DIVS_TIME) + 3.0);
    watchdog->start((int)(seconds * 1000));
}

bool BodeWindow::autoRange(const DataChannel *in, const DataChannel *out) {
    if (!autoRangeBox->isChecked() || rangeSteps >= kMaxRangeSteps) return false;
    bool changedAny = false;
    const DataChannel *dcs[2] = {in, out};
    const int chans[2] = {refChannel(), outChannel()};
    for (int i = 0; i < 2; ++i) {
        const ChannelID ch = (ChannelID)chans[i];
        const std::vector<double> &x = dcs[i]->voltage.sample;
        if (x.empty()) continue;
        const unsigned idx = scope->voltage[ch].gainStepIndex;
        const unsigned last = (unsigned)scope->gainSteps.size() - 1;
        const double probe = scope->voltage[ch].probe;
        const double fullScale = scope->gain(ch) * DIVS_VOLTAGE / 2.0; // traces are centered (offset 0)
        double peakAbs = 0;
        for (double v : x) peakAbs = std::max(peakAbs, std::fabs(v));
        const double dev = bode::peakDeviation(x);
        unsigned want = idx;
        if (peakAbs >= 0.97 * fullScale) {
            if (idx < last) want = idx + 1; // clipped: one step up and look again
        } else if (dev < 0.25 * fullScale) {
            // too small: smallest V/div where the signal (with 25 % margin) still fits in 90 % of the screen
            for (unsigned k = 0; k <= last; ++k)
                if (dev * 1.25 <= scope->gainSteps[k] * probe * DIVS_VOLTAGE / 2.0 * 0.9) {
                    want = std::min(k, idx);
                    break;
                }
        }
        if (want != idx) {
            voltageDock->selectGain(ch, want);
            changedAny = true;
        }
    }
    if (changedAny) ++rangeSteps;
    return changedAny;
}

void BodeWindow::process(std::shared_ptr<PPresult> data) {
    if (!running() || !data) return;
    armWatchdog();
    if (discard > 0) {
        --discard;
        return;
    }
    const DataChannel *in = data->data((ChannelID)refChannel());
    const DataChannel *out = data->data((ChannelID)outChannel());
    if (!in || !out || in->voltage.sample.empty() || out->voltage.sample.empty()) return;

    if (autoRange(in, out)) {
        changed(kDiscardAfterScope);
        return;
    }
    state = State::Measuring;

    const double f = freqs[index];
    const bode::Measurement m = bode::measure(in->voltage.sample, out->voltage.sample, in->voltage.interval, f);
    if (!m.valid) {
        ++attempts;
        if (m.aliased && !slowerTried) {
            // the alias fell on DC or fs/2: a slower sample rate moves it
            slowerTried = true;
            horizontalDock->selectTimebase(scope->horizontal.timebase * 2.0 * 1.0001);
            recordTime = scope->horizontal.timebase * DIVS_TIME;
            changed(kDiscardAfterScope);
            return;
        }
        if (attempts < kMaxAttempts) {
            changed(1);
            return;
        }
        Result r;
        r.freq = f;
        r.valid = false;
        r.aliased = m.aliased;
        r.problem = QString::fromStdString(m.problem);
        results.push_back(r);
        plot->addPoint({f, 0, 0, false});
        status(tr("%1: sem medida (%2).").arg(hzText(f), r.problem), true);
        ++index;
        progress->setValue((int)index);
        if (index >= freqs.size())
            finish(true);
        else
            startPoint();
        return;
    }

    sumH += m.response;
    sumIn += 2 * m.amplitudeIn;
    sumOut += 2 * m.amplitudeOut;
    minSnrIn = std::min(minSnrIn, m.snrInDb);
    minSnrOut = std::min(minSnrOut, m.snrOutDb);
    anyAliased = anyAliased || m.aliased;
    if (++count < averagesBox->value()) return;
    finishPoint();
}

void BodeWindow::finishPoint() {
    Result r;
    r.freq = freqs[index];
    r.h = sumH / (double)count;
    if (!calibrating && useCalibrationBox->isChecked() && !calibration.empty())
        r.h /= bode::interpolateResponse(calibration, r.freq);
    r.vppIn = sumIn / count;
    r.vppOut = sumOut / count;
    r.snrIn = minSnrIn;
    r.snrOut = minSnrOut;
    r.aliased = anyAliased;
    r.valid = true;
    if (r.snrOut < 6) r.problem = tr("sinal de saída fraco: medida pouco precisa");
    results.push_back(r);
    plot->addPoint({r.freq, r.gainDb(), r.phaseDeg(), true});
    status(tr("%1: %2 dB, %3°").arg(hzText(r.freq), num(r.gainDb(), 2), num(r.phaseDeg(), 1)));

    ++index;
    progress->setValue((int)index);
    if (index >= freqs.size())
        finish(true);
    else
        startPoint();
}

// ------------------------------------------------------------------------------------------------ results
void BodeWindow::exportCsv() {
    if (results.empty()) return;
    const QString name = QFileDialog::getSaveFileName(
        this, tr("Exportar CSV"), QString("bode_%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmm")),
        tr("CSV (*.csv)"));
    if (name.isEmpty()) return;
    QFile file(name);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        status(tr("Não foi possível criar %1").arg(name), true);
        return;
    }
    QTextStream t(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    t.setEncoding(QStringConverter::Utf8);
#else
    t.setCodec("UTF-8");
#endif
    t.setGenerateByteOrderMark(true); // LibreOffice / Excel open the accents correctly
    t << QString::fromUtf8("# Resposta em frequência - PSG9080 CH%1, entrada CH%2, saída CH%3, %4 Vpp%5\n")
             .arg(genChannel())
             .arg(refChannel() + 1)
             .arg(outChannel() + 1)
             .arg(num(amplitudeBox->value(), 3))
             .arg(useCalibrationBox->isChecked() && !calibration.empty() ? tr(", calibração descontada") : QString());
    t << QString::fromUtf8("Frequência (Hz);Ganho (dB);Ganho (V/V);Fase (°);Entrada (Vpp);Saída (Vpp);"
                           "SNR entrada (dB);SNR saída (dB);Subamostrado;Válido;Observação\n");
    for (const Result &r : results) {
        t << num(r.freq, 6) << ';';
        if (r.valid)
            t << num(r.gainDb(), 3) << ';' << num(std::abs(r.h), 6) << ';' << num(r.phaseDeg(), 2) << ';'
              << num(r.vppIn, 4) << ';' << num(r.vppOut, 5) << ';' << num(r.snrIn, 1) << ';' << num(r.snrOut, 1);
        else
            t << ";;;;;;;";
        t << ';' << (r.aliased ? tr("sim") : tr("não")) << ';' << (r.valid ? tr("sim") : tr("não")) << ';'
          << r.problem << '\n';
    }
    status(tr("CSV salvo em %1").arg(name));
}

void BodeWindow::saveImage() {
    const QString name = QFileDialog::getSaveFileName(
        this, tr("Salvar imagem"), QString("bode_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmm")),
        tr("Imagens (*.png *.jpg *.bmp)"));
    if (name.isEmpty()) return;
    if (plot->grab().save(name))
        status(tr("Imagem salva em %1").arg(name));
    else
        status(tr("Não foi possível salvar %1").arg(name), true);
}

void BodeWindow::copyImage() {
    QApplication::clipboard()->setPixmap(plot->grab());
    status(tr("Gráfico copiado para a área de transferência."));
}

std::vector<BodePlot::Point> BodeWindow::plotPoints() const {
    std::vector<BodePlot::Point> pts;
    for (const Result &r : results) pts.push_back({r.freq, r.gainDb(), r.phaseDeg(), r.valid});
    return pts;
}
