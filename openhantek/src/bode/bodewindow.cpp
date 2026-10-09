// SPDX-License-Identifier: GPL-2.0+

#include "bodewindow.h"

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
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>
#include <cmath>

#include "bode/scopepreview.h"
#include "generator/GeneratorPanel.h"
#include "generator/psg9080.h"
#include "hantekdso/hantekdsocontrol.h"
#include "style/darkstyle.h"

namespace {

const double kPi = 3.14159265358979323846;
const double kPeriodsOnScreen = 10.0; ///< target number of periods in one acquisition
const double kMinPeriods = 3.0;       ///< below this the frequency cannot be measured
const int kIgnoreAfterFrequency = 2;  ///< acquisitions ignored after changing the generator
const int kIgnoreAfterScope = 3;      ///< acquisitions ignored after changing record time or gain
const int kMaxRangeSteps = 8;         ///< gain corrections per point
const int kMaxAttempts = 3;           ///< measurements tried before giving up a point

const char *kUnitNames[] = {"Hz", "kHz", "MHz"};
const double kUnitScale[] = {1.0, 1e3, 1e6};
const double kProbes[] = {1, 10, 20, 50, 100, 200, 500, 1000};

QString hzText(double hz) {
    if (hz >= 1e6) return GeneratorPanel::formatNumber(hz / 1e6, 6) + " MHz";
    if (hz >= 1e3) return GeneratorPanel::formatNumber(hz / 1e3, 6) + " kHz";
    if (hz >= 1.0) return GeneratorPanel::formatNumber(hz, 4) + " Hz";
    return GeneratorPanel::formatNumber(hz * 1e3, 3) + " mHz";
}

QString num(double v, int decimals) { return QString::number(v, 'f', decimals).replace('.', ','); }

void showFrequency(QLineEdit *edit, QComboBox *unit, double hz) {
    const int u = hz >= 1e6 ? 2 : (hz >= 1e3 ? 1 : 0);
    unit->setCurrentIndex(u);
    edit->setText(GeneratorPanel::formatNumber(hz / kUnitScale[u], 6));
}

} // namespace

double BodeWindow::Result::gainDb() const { return 20.0 * std::log10(std::max(std::abs(h), 1e-20)); }
double BodeWindow::Result::phaseDeg() const { return bode::wrapDegrees(std::arg(h) * 180.0 / kPi); }

BodeWindow::BodeWindow(HantekDsoControl *dsoControl, const Dso::ControlSpecification *spec, SampleTap *tap,
                       QWidget *parent)
    : QMainWindow(parent), dso(dsoControl), spec(spec), tap(tap), gen(new Psg9080(this)) {
    setWindowTitle(tr("OpenHantek Bode — resposta em frequência (PSG9080 + DSO-2250)"));

    plot = new BodePlot;
    preview = new ScopePreview;
    settingsPanel = makeSettings();
    progress = new QProgressBar;
    progress->setTextVisible(true);
    progress->setFormat("%v / %m");
    progress->setValue(0);
    statusLabel = new QLabel;
    statusLabel->setWordWrap(true);
    statusLabel->setMinimumHeight(20);

    QWidget *right = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(plot, 3);
    rightLayout->addWidget(preview, 1);
    rightLayout->addWidget(progress);
    rightLayout->addWidget(statusLabel);

    QScrollArea *scroll = new QScrollArea;
    scroll->setWidget(settingsPanel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // the panel fits; only vertical scrolling
    scroll->setMinimumWidth(std::max(390, settingsPanel->minimumSizeHint().width() + 24));

    QSplitter *split = new QSplitter(Qt::Horizontal);
    split->addWidget(scroll);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    split->setStyleSheet(darkstyle::panelSheet());
    rightLayout->setContentsMargins(4, 6, 6, 6);
    setCentralWidget(split);

    watchdog = new QTimer(this);
    watchdog->setSingleShot(true);
    connect(watchdog, &QTimer::timeout, this, [this]() {
        finish(false, tr("O osciloscópio parou de enviar aquisições. Verifique a conexão USB."));
    });
    connect(tap, &SampleTap::frameReady, this, &BodeWindow::frameReady);
    connect(gen, &Psg9080::connectionChanged, this, [this](bool) { updateGeneratorUi(); });

    loadSettings();
    loadCalibration();
    updateCalibrationUi();
    updateRoles();
    refreshPorts();
    updateGeneratorUi();
    setUiRunning(false);

    QSettings s;
    resize(1180, 760);
    restoreGeometry(s.value("window/geometry").toByteArray());

    configureScope();
    updateLimits();
    status(tr("Conecte o gerador e confira as ligações na tela pequena; depois clique em Iniciar."));
}

// ------------------------------------------------------------------------------------------------ interface
QWidget *BodeWindow::makeSettings() {
    QWidget *panel = new QWidget;
    panel->setObjectName("bodeSettings");
    QVBoxLayout *v = new QVBoxLayout(panel);
    v->setContentsMargins(6, 4, 8, 6);
    v->setSpacing(6);

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
    auto channelCombo = [](const QString &prefix) {
        QComboBox *c = new QComboBox;
        c->addItem(prefix + " CH1", 1);
        c->addItem(prefix + " CH2", 2);
        return c;
    };

    // Generator
    QGroupBox *genBox = new QGroupBox(tr("Gerador PSG9080"));
    QFormLayout *gf = new QFormLayout(genBox);
    portBox = new QComboBox;
    portBox->setEditable(true);
    portRefresh = new QPushButton(QString::fromUtf8("↻"));
    portRefresh->setFixedWidth(32);
    portRefresh->setToolTip(tr("Procurar portas seriais"));
    connectButton = new QPushButton(tr("Conectar"));
    QHBoxLayout *portRow = new QHBoxLayout;
    portRow->addWidget(portBox, 1);
    portRow->addWidget(portRefresh);
    portRow->addWidget(connectButton);
    generatorState = new QLabel;
    generatorState->setWordWrap(true);
    genChannelBox = channelCombo(tr("Saída"));
    amplitudeBox = new QDoubleSpinBox;
    amplitudeBox->setDecimals(3);
    amplitudeBox->setRange(0.001, psg9080::kMaxAmplitude);
    amplitudeBox->setSingleStep(0.1);
    amplitudeBox->setSuffix(" Vpp");
    offsetBox = new QDoubleSpinBox;
    offsetBox->setDecimals(2);
    offsetBox->setRange(-psg9080::kMaxOffset, psg9080::kMaxOffset);
    offsetBox->setSingleStep(0.1);
    offsetBox->setSuffix(" V");
    gf->addRow(tr("Porta"), portRow);
    gf->addRow(generatorState);
    gf->addRow(tr("Sinal de teste"), genChannelBox);
    gf->addRow(tr("Amplitude"), amplitudeBox);
    gf->addRow(tr("Offset"), offsetBox);
    darkstyle::colorSection(genBox, darkstyle::orange());
    darkstyle::plainSpin(amplitudeBox);
    darkstyle::plainSpin(offsetBox);
    connect(portRefresh, &QPushButton::clicked, this, &BodeWindow::refreshPorts);
    connect(connectButton, &QPushButton::clicked, this, &BodeWindow::toggleGenerator);
    v->addWidget(genBox);

    // Oscilloscope
    QGroupBox *scopeBox = new QGroupBox(tr("Osciloscópio DSO-2250"));
    QFormLayout *sf = new QFormLayout(scopeBox);
    refChannelBox = channelCombo(tr("Osciloscópio"));
    outChannelLabel = new QLabel;
    for (int ch = 0; ch < 2; ++ch) {
        probeBox[ch] = new QComboBox;
        for (double p : kProbes) probeBox[ch]->addItem(QString("x%1").arg(p), p);
    }
    couplingBox = new QComboBox;
    couplingBox->addItem("DC", (int)Dso::Coupling::DC);
    couplingBox->addItem("AC", (int)Dso::Coupling::AC);
    couplingBox->setToolTip(tr("DC mede desde 0 Hz; AC remove o nível contínuo (atenua abaixo de ~10 Hz)"));
    memoryBox = new QComboBox;
    const std::vector<unsigned> &lengths = dso->getAvailableRecordLengths();
    for (size_t i = 0; i < lengths.size(); ++i)
        if (lengths[i] != UINT_MAX) memoryBox->addItem(tr("%1 amostras").arg(lengths[i]), (int)i);
    memoryBox->setToolTip(tr("Mais memória mede frequências mais baixas, mas cada ponto demora mais"));
    sf->addRow(tr("Entrada do circuito"), refChannelBox);
    sf->addRow(tr("Saída do circuito"), outChannelLabel);
    sf->addRow(tr("Ponteira CH1"), probeBox[0]);
    sf->addRow(tr("Ponteira CH2"), probeBox[1]);
    sf->addRow(tr("Acoplamento"), couplingBox);
    sf->addRow(tr("Memória"), memoryBox);
    darkstyle::colorSection(scopeBox, darkstyle::blue());
    auto scopeChanged = [this]() {
        if (sweeping) return;
        updateRoles();
        configureScope();
        updateLimits();
    };
    connect(refChannelBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, scopeChanged);
    connect(couplingBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, scopeChanged);
    connect(memoryBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, scopeChanged);
    for (QComboBox *b : probeBox) connect(b, QOverload<int>::of(&QComboBox::currentIndexChanged), this, scopeChanged);
    v->addWidget(scopeBox);

    // Sweep
    QGroupBox *sweep = new QGroupBox(tr("Varredura"));
    QFormLayout *wf = new QFormLayout(sweep);
    wf->addRow(tr("Frequência inicial"), freqRow(startEdit, startUnit));
    wf->addRow(tr("Frequência final"), freqRow(stopEdit, stopUnit));
    perDecadeBox = new QSpinBox;
    perDecadeBox->setRange(1, 100);
    perDecadeBox->setSuffix(tr(" pontos/década"));
    averagesBox = new QSpinBox;
    averagesBox->setRange(1, 32);
    averagesBox->setSuffix(tr(" aquisições"));
    wf->addRow(tr("Resolução"), perDecadeBox);
    wf->addRow(tr("Média por ponto"), averagesBox);
    limitsLabel = new QLabel;
    limitsLabel->setWordWrap(true);
    limitsLabel->setProperty("role", "hint");
    wf->addRow(limitsLabel);
    darkstyle::colorSection(sweep, darkstyle::violet());
    darkstyle::plainSpin(perDecadeBox);
    darkstyle::plainSpin(averagesBox);
    v->addWidget(sweep);

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
    calibrationLabel->setProperty("role", "hint");
    darkstyle::colorSection(cal, darkstyle::gray());
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
    darkstyle::styleRunButton(startButton, false);
    connect(startButton, &QPushButton::clicked, this, [this]() {
        if (sweeping)
            finish(false, tr("Varredura interrompida."));
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
    for (int ch = 0; ch < 2; ++ch) {
        const int i = probeBox[ch]->findData(s.value(QString("probe%1").arg(ch + 1), 1.0).toDouble());
        probeBox[ch]->setCurrentIndex(std::max(0, i));
    }
    couplingBox->setCurrentIndex(std::max(0, couplingBox->findData(s.value("coupling", (int)Dso::Coupling::DC))));
    const int mem = memoryBox->findData(s.value("memoryIndex", 1).toInt());
    memoryBox->setCurrentIndex(std::max(0, mem));
    showFrequency(startEdit, startUnit, s.value("start", 10.0).toDouble());
    showFrequency(stopEdit, stopUnit, s.value("stop", 1e6).toDouble());
    perDecadeBox->setValue(s.value("perDecade", 10).toInt());
    amplitudeBox->setValue(s.value("amplitude", 1.0).toDouble());
    offsetBox->setValue(s.value("offset", 0.0).toDouble());
    averagesBox->setValue(s.value("averages", 2).toInt());
    useCalibrationBox->setChecked(s.value("useCalibration", false).toBool());
    portBox->setEditText(s.value("port", "/dev/ttyUSB0").toString());
    s.endGroup();
}

void BodeWindow::saveSettings() {
    QSettings s;
    s.beginGroup("Bode");
    s.setValue("generatorChannel", genChannel());
    s.setValue("inputChannel", refChannel() + 1);
    for (int ch = 0; ch < 2; ++ch) s.setValue(QString("probe%1").arg(ch + 1), probe[ch]);
    s.setValue("coupling", couplingBox->currentData());
    s.setValue("memoryIndex", memoryBox->currentData());
    double hz = 0;
    if (readFrequency(startEdit, startUnit, hz)) s.setValue("start", hz);
    if (readFrequency(stopEdit, stopUnit, hz)) s.setValue("stop", hz);
    s.setValue("perDecade", perDecadeBox->value());
    s.setValue("amplitude", amplitudeBox->value());
    s.setValue("offset", offsetBox->value());
    s.setValue("averages", averagesBox->value());
    s.setValue("useCalibration", useCalibrationBox->isChecked());
    s.setValue("port", portBox->currentText());
    s.endGroup();
}

int BodeWindow::genChannel() const { return genChannelBox->currentData().toInt(); }
int BodeWindow::refChannel() const { return refChannelBox->currentData().toInt() - 1; }
int BodeWindow::outChannel() const { return refChannel() == 0 ? 1 : 0; }

void BodeWindow::updateRoles() {
    outChannelLabel->setText(tr("Osciloscópio CH%1 (o outro canal)").arg(outChannel() + 1));
    preview->setRoles(refChannel() == 0 ? tr("entrada") : tr("saída"), refChannel() == 1 ? tr("entrada") : tr("saída"));
}

bool BodeWindow::readFrequency(QLineEdit *edit, QComboBox *unit, double &hz) const {
    double v = 0;
    if (!GeneratorPanel::parseNumber(edit->text(), v) || !(v > 0)) return false;
    hz = v * kUnitScale[std::max(0, unit->currentIndex())];
    return true;
}

void BodeWindow::status(const QString &text, bool error) {
    statusLabel->setStyleSheet(error ? "color: #ff6b5b;" : "color: #c8ccd4;");
    statusLabel->setText(text);
}

void BodeWindow::setUiRunning(bool run) {
    for (QGroupBox *w : settingsPanel->findChildren<QGroupBox *>()) w->setEnabled(!run);
    startButton->setText(run ? tr("■  Parar") : tr("▶  Iniciar"));
    darkstyle::styleRunButton(startButton, run);
    csvButton->setEnabled(!run && !results.empty());
    imageButton->setEnabled(!run && !results.empty());
    copyButton->setEnabled(!results.empty());
    if (!run) updateCalibrationUi();
}

void BodeWindow::closeEvent(QCloseEvent *event) {
    if (sweeping) finish(false, tr("Varredura interrompida."));
    saveSettings();
    QSettings().setValue("window/geometry", saveGeometry());
    gen->close();
    QMainWindow::closeEvent(event);
}

// ------------------------------------------------------------------------------------------------ generator
void BodeWindow::refreshPorts() {
    const QString current = portBox->currentText();
    portBox->clear();
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        if (info.portName().startsWith("ttyS") && info.description().isEmpty()) continue;
        portBox->addItem(info.systemLocation());
        portBox->setItemData(portBox->count() - 1, info.description(), Qt::ToolTipRole);
    }
    const int i = portBox->findText(current);
    if (i >= 0)
        portBox->setCurrentIndex(i);
    else
        portBox->setEditText(current.isEmpty() ? QString("/dev/ttyUSB0") : current);
}

void BodeWindow::toggleGenerator() {
    if (gen->isOpen()) {
        gen->close();
        return;
    }
    if (!gen->open(portBox->currentText().trimmed())) {
        generatorState->setStyleSheet("color: #ff6b5b;");
        generatorState->setText(gen->lastError());
        return;
    }
    saveSettings();
}

void BodeWindow::updateGeneratorUi() {
    const bool open = gen->isOpen();
    connectButton->setText(open ? tr("Desconectar") : tr("Conectar"));
    portBox->setEnabled(!open);
    portRefresh->setEnabled(!open);
    generatorState->setStyleSheet(open ? "color: #4fbf62;" : "color: #9aa1ab;");
    generatorState->setText(open ? tr("Conectado em %1.").arg(gen->portName())
                                 : tr("Não conectado. Feche o programa PSG9080 se ele estiver usando a porta."));
}

// ------------------------------------------------------------------------------------------------ oscilloscope
unsigned BodeWindow::recordLength() const {
    const std::vector<unsigned> &lengths = dso->getAvailableRecordLengths();
    const int i = memoryBox->currentData().toInt();
    return (i >= 0 && (size_t)i < lengths.size()) ? lengths[(size_t)i] : 10240;
}

double BodeWindow::minRecordTime() const { return recordLength() / dso->getMaxSamplerate(); }
double BodeWindow::maxRecordTime() const { return recordLength() / dso->getMinSamplerate(); }
double BodeWindow::minimumFrequency() const { return kMinPeriods / maxRecordTime(); }

double BodeWindow::voltsPerDiv(int ch) const { return dso->gainFullScale(gainIndex[ch]) * probe[ch] / 8.0; }
double BodeWindow::fullScale(int ch) const { return dso->gainFullScale(gainIndex[ch]) * probe[ch] / 2.0; }

void BodeWindow::setGain(int ch, unsigned i) {
    gainIndex[ch] = std::min(i, dso->gainCount() - 1);
    dso->setGain((ChannelID)ch, dso->gainFullScale(gainIndex[ch]) * probe[ch]);
}

void BodeWindow::setRecordTime(double seconds) {
    recordTime = std::max(minRecordTime(), std::min(maxRecordTime(), seconds));
    dso->setRecordTime(recordTime);
}

void BodeWindow::configureScope() {
    const Dso::Coupling coupling = (Dso::Coupling)couplingBox->currentData().toInt();
    for (int ch = 0; ch < 2; ++ch) {
        probe[ch] = probeBox[ch]->currentData().toDouble();
        dso->setChannelUsed((ChannelID)ch, true);
        dso->setCoupling((ChannelID)ch, coupling);
        dso->setProbe((ChannelID)ch, probe[ch]);
        dso->setOffset((ChannelID)ch, 0.5); // trace centered: the full scale is symmetric around 0 V
        setGain(ch, gainIndex[ch] ? gainIndex[ch] : dso->gainCount() / 2);
    }
    if (memoryBox->count() > 0) dso->setRecordLength((unsigned)memoryBox->currentData().toInt()); // never Roll
    setRecordTime(recordTime);
    dso->setTriggerMode(Dso::TriggerMode::WAIT_FORCE); // AUTO: acquisitions without a trigger event
    dso->setTriggerSource(false, (unsigned)refChannel());
    dso->setTriggerSlope(Dso::Slope::Positive);
    dso->setTriggerLevel((ChannelID)refChannel(), 0.0);
    dso->setPretriggerPosition(recordTime / 2);
    if (!dso->isSampling()) dso->enableSampling(true);
    idleRangeSteps = 0;
    ignoreFrames(kIgnoreAfterScope);
}

void BodeWindow::ignoreFrames(int frames) { ignoreUntil = std::max(ignoreUntil, lastSequence + (quint64)frames); }

void BodeWindow::updateLimits() {
    limitsLabel->setText(tr("Faixa possível com esta memória: %1 a 80 MHz. Acima de %2 o osciloscópio amostra "
                            "abaixo de Nyquist; a medida continua válida (frequência conhecida).")
                             .arg(hzText(minimumFrequency()), hzText(dso->getMaxSamplerate() / 2.0)));
}

bool BodeWindow::autoRange(const ScopeFrame &frame) {
    bool changedAny = false;
    const unsigned last = dso->gainCount() - 1;
    for (int ch = 0; ch < 2 && (size_t)ch < frame.data.size(); ++ch) {
        const std::vector<double> &x = frame.data[(size_t)ch];
        if (x.empty()) continue;
        double peakAbs = 0;
        for (double v : x) peakAbs = std::max(peakAbs, std::fabs(v));
        const double dev = bode::peakDeviation(x);
        const unsigned idx = gainIndex[ch];
        unsigned want = idx;
        if (peakAbs >= 0.97 * fullScale(ch)) {
            if (idx < last) want = idx + 1; // clipped: one step up and look again
        } else if (dev < 0.25 * fullScale(ch)) {
            // too small: the smallest range where the signal (25 % margin) still fits in 90 % of the screen
            for (unsigned k = 0; k <= last; ++k)
                if (dev * 1.25 <= dso->gainFullScale(k) * probe[ch] / 2.0 * 0.9) {
                    want = std::min(k, idx);
                    break;
                }
        }
        if (want != idx) {
            setGain(ch, want);
            changedAny = true;
        }
    }
    if (changedAny) ignoreFrames(kIgnoreAfterScope);
    return changedAny;
}

void BodeWindow::frameReady(quint64 sequence) {
    ScopeFrame frame;
    if (!tap->latest(frame) || frame.sequence < sequence) return;
    if (frame.sequence <= lastSequence) return; // already handled (notifications can pile up)
    lastSequence = frame.sequence;
    const double vdiv[2] = {voltsPerDiv(0), voltsPerDiv(1)};
    preview->setFrame(frame, vdiv);

    if (frame.sequence <= ignoreUntil) return;
    if (!sweeping) {
        // idle: keep the live view readable, without chasing noise forever
        if (idleRangeSteps < kMaxRangeSteps && autoRange(frame)) ++idleRangeSteps;
        return;
    }
    armWatchdog();
    if (rangeSteps < kMaxRangeSteps && autoRange(frame)) {
        ++rangeSteps;
        return;
    }
    measureFrame(frame);
}

// ------------------------------------------------------------------------------------------------ sweep
void BodeWindow::start(bool calibrationRun) {
    if (sweeping) return;
    if (!gen->isOpen()) {
        status(tr("Conecte o gerador (porta serial do PSG9080) antes de começar."), true);
        return;
    }
    double f1 = 0, f2 = 0;
    if (!readFrequency(startEdit, startUnit, f1) || !readFrequency(stopEdit, stopUnit, f2)) {
        status(tr("Frequência inicial ou final inválida."), true);
        return;
    }
    if (f1 > f2) std::swap(f1, f2);
    if (f1 < minimumFrequency() * 0.999 || f2 > psg9080::kMaxFrequency) {
        status(tr("Escolha frequências entre %1 e 80 MHz (ou mais memória para ir mais baixo).")
                   .arg(hzText(minimumFrequency())),
               true);
        return;
    }

    const int g = genChannel();
    bool a = false, b = false;
    if (!gen->readOutputs(a, b) || !gen->setWaveform(g, 0) || !gen->setAmplitude(g, amplitudeBox->value()) ||
        !gen->setOffset(g, offsetBox->value()) || !gen->setFrequency(g, f1) || !gen->setOutput(g, true)) {
        status(gen->lastError(), true);
        return;
    }
    generatorWasOn = g == 1 ? a : b;

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
    sweeping = true;
    setUiRunning(true);
    configureScope();
    startPoint();
}

void BodeWindow::finish(bool completed, const QString &message) {
    if (!sweeping) return;
    watchdog->stop();
    sweeping = false;
    if (gen->isOpen() && !generatorWasOn) gen->setOutput(genChannel(), false);

    if (calibrating) {
        const auto valid = std::count_if(results.begin(), results.end(), [](const Result &r) { return r.valid; });
        if (completed && valid >= 2) {
            saveCalibration();
            status(tr("Calibração guardada (%1 pontos). Agora ligue o circuito e clique em Iniciar.").arg(valid));
        } else {
            status(message.isEmpty() ? tr("Calibração não concluída.") : message, true);
        }
    } else if (completed) {
        const auto bad = std::count_if(results.begin(), results.end(), [](const Result &r) { return !r.valid; });
        status(bad ? tr("Varredura concluída: %1 pontos, %2 sem medida válida (veja o CSV).")
                         .arg(results.size())
                         .arg(bad)
                   : tr("Varredura concluída: %1 pontos.").arg(results.size()));
    } else {
        status(message, message != tr("Varredura interrompida."));
    }
    calibrating = false;
    setUiRunning(false);
    idleRangeSteps = 0;
}

void BodeWindow::startPoint() {
    const double f = freqs[index];
    if (!gen->setFrequency(genChannel(), f)) {
        finish(false, gen->lastError());
        return;
    }
    setRecordTime(kPeriodsOnScreen / f);
    dso->setPretriggerPosition(recordTime / 2);
    rangeSteps = 0;
    attempts = 0;
    slowerTried = false;
    count = 0;
    sumH = bode::Phasor(0, 0);
    sumIn = sumOut = 0;
    minSnrIn = minSnrOut = 1e9;
    anyAliased = false;
    ignoreFrames(std::max(kIgnoreAfterFrequency, kIgnoreAfterScope));
    status(tr("Ponto %1 de %2: %3…").arg(index + 1).arg(freqs.size()).arg(hzText(f)));
    armWatchdog();
}

void BodeWindow::armWatchdog() {
    // one acquisition lasts about one record time, plus USB and processing
    watchdog->start((int)(std::max(5.0, 4.0 * recordTime + 3.0) * 1000));
}

void BodeWindow::measureFrame(const ScopeFrame &frame) {
    const size_t ref = (size_t)refChannel(), out = (size_t)outChannel();
    if (frame.data.size() <= std::max(ref, out) || frame.samplerate <= 0) return;
    const double f = freqs[index];
    const bode::Measurement m = bode::measure(frame.data[ref], frame.data[out], 1.0 / frame.samplerate, f);
    if (!m.valid) {
        ++attempts;
        if (m.aliased && !slowerTried) {
            slowerTried = true; // the alias fell on DC or fs/2: a slower sample rate moves it
            setRecordTime(recordTime * 2.0);
            ignoreFrames(kIgnoreAfterScope);
            return;
        }
        if (attempts < kMaxAttempts) {
            ignoreFrames(1);
            return;
        }
        Result r;
        r.freq = f;
        r.aliased = m.aliased;
        r.problem = QString::fromStdString(m.problem);
        results.push_back(r);
        plot->addPoint({f, 0, 0, false});
        status(tr("%1: sem medida (%2).").arg(hzText(f), r.problem), true);
        nextPoint();
        return;
    }
    sumH += m.response;
    sumIn += 2 * m.amplitudeIn;
    sumOut += 2 * m.amplitudeOut;
    minSnrIn = std::min(minSnrIn, m.snrInDb);
    minSnrOut = std::min(minSnrOut, m.snrOutDb);
    anyAliased = anyAliased || m.aliased;
    if (++count >= averagesBox->value()) finishPoint();
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
    nextPoint();
}

void BodeWindow::nextPoint() {
    ++index;
    progress->setValue((int)index);
    if (index >= freqs.size())
        finish(true);
    else
        startPoint();
}

// ------------------------------------------------------------------------------------------------ calibration
void BodeWindow::loadCalibration() {
    calibration.clear();
    QSettings s;
    s.beginGroup("Bode/calibration");
    const QStringList pts = s.value("points").toStringList();
    calibrationInfo = s.value("info").toString();
    s.endGroup();
    for (const QString &p : pts) {
        const QStringList f = p.split(';');
        if (f.size() == 3) calibration.push_back({f[0].toDouble(), bode::Phasor(f[1].toDouble(), f[2].toDouble())});
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
    calibrationInfo = tr("%1, %2 a %3, entrada CH%4, ponteiras x%5/x%6")
                          .arg(QDateTime::currentDateTime().toString("dd/MM/yyyy HH:mm"),
                               hzText(calibration.front().freq), hzText(calibration.back().freq))
                          .arg(refChannel() + 1)
                          .arg(probe[0])
                          .arg(probe[1]);
    QSettings s;
    s.beginGroup("Bode/calibration");
    s.setValue("points", pts);
    s.setValue("info", calibrationInfo);
    s.endGroup();
    useCalibrationBox->setChecked(true);
}

void BodeWindow::updateCalibrationUi() {
    useCalibrationBox->setEnabled(!calibration.empty());
    if (calibration.empty()) {
        useCalibrationBox->setChecked(false);
        calibrationLabel->setText(tr("Sem calibração."));
    } else {
        calibrationLabel->setText(tr("Calibração: %1.").arg(calibrationInfo));
    }
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
    t << tr("# Resposta em frequência - PSG9080 CH%1, entrada CH%2, saída CH%3, %4 Vpp%5\n")
             .arg(genChannel())
             .arg(refChannel() + 1)
             .arg(outChannel() + 1)
             .arg(num(amplitudeBox->value(), 3))
             .arg(useCalibrationBox->isChecked() && !calibration.empty() ? tr(", calibração descontada") : QString());
    t << tr("Frequência (Hz);Ganho (dB);Ganho (V/V);Fase (°);Entrada (Vpp);Saída (Vpp);SNR entrada (dB);"
            "SNR saída (dB);Subamostrado;Válido;Observação\n");
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
