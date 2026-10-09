// SPDX-License-Identifier: GPL-2.0+
// Painel frontal estilo osciloscópio de bancada.

#include "FrontPanelDock.h"

#include <QAction>
#include <QButtonGroup>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "HorizontalDock.h"
#include "SpectrumDock.h"
#include "post/postprocessingsettings.h"
#include "TriggerDock.h"
#include "VoltageDock.h"
#include "dsowidget.h"
#include "hantekdsocontrol.h"
#include "utils/printutils.h"
#include "viewconstants.h"
#include "viewsettings.h"


static QString fmtValue(double v, Unit unit) { return valueToString(v, unit, 3); }

FrontPanelDock::FrontPanelDock(DsoSettingsScope *scope, const Dso::ControlSpecification *spec,
                               HantekDsoControl *dsoControl, VoltageDock *voltageDock, HorizontalDock *horizontalDock,
                               TriggerDock *triggerDock, DsoWidget *dsoWidget, QAction *samplingAction,
                               const std::vector<QColor> &channelColors, QWidget *parent)
    : QDockWidget(tr("Painel"), parent), scope(scope), spec(spec), dsoControl(dsoControl), voltageDock(voltageDock),
      horizontalDock(horizontalDock), triggerDock(triggerDock), dsoWidget(dsoWidget), samplingAction(samplingAction),
      colors(channelColors) {
    setObjectName("FrontPanelDock");

    channelUi.resize(spec->channels);
    stats.resize(spec->channels);

    QWidget *content = new QWidget();
    content->setObjectName("panelContent");
    QVBoxLayout *layout = new QVBoxLayout(content);
    mainLayout = layout;
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(5);

    acqBar = makeAcquisitionBar(); // vai para a barra de ferramentas do topo
    for (ChannelID ch = 0; ch < spec->channels; ++ch)
        layout->addWidget(makeCollapsible(makeChannelBox(ch), QString("ch%1").arg(ch)));
    layout->addWidget(makeCollapsible(makeHorizontalBox(), "horizontal"));
    layout->addWidget(makeCollapsible(makeTriggerBox(), "trigger"));
    // "Tela" (grade e interpolação) foi para Osciloscópio > Configurações > Tela
    layout->addStretch(1);

    content->setStyleSheet(
        "QWidget#panelContent { background: #23272e; }"
        "QWidget#panelContent QWidget { font-size: 9pt; }"
        "QGroupBox { color: #c8ccd4; font-weight: bold; border: 1px solid #3a404a; border-radius: 5px;"
        "  margin-top: 8px; padding: 4px 2px 2px 2px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 6px; padding: 0 3px; }"
        "QGroupBox::indicator { width: 0px; height: 0px; }"
        "QLabel { color: #e6e9ee; }"
        "QLabel[role=\"value\"] { background: #11141a; border: 1px solid #3a404a; border-radius: 3px;"
        "  padding: 2px 4px; font-family: monospace; font-size: 10pt; }"
        "QLabel[role=\"readout\"] { font-family: monospace; font-size: 8pt; color: #b8bec8; }"
        "QPushButton { background: #3a404a; color: #f0f2f5; border: 1px solid #555d6a; border-radius: 4px;"
        "  padding: 3px 4px; min-height: 18px; }"
        "QPushButton:hover { background: #475061; }"
        "QPushButton:pressed { background: #2c3139; }"
        "QPushButton:checked { background: #2f6fbf; border-color: #5a95e0; }"
        "QPushButton[role=\"run\"] { font-size: 11pt; font-weight: bold; min-height: 28px; }"
        "QComboBox { background: #3a404a; color: #f0f2f5; border: 1px solid #555d6a; border-radius: 4px;"
        "  padding: 2px 4px; }"
        "QComboBox QAbstractItemView { background: #2b3038; color: #f0f2f5; selection-background-color: #2f6fbf; }");

    QScrollArea *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    // Largura fixa, rolagem só na vertical
    const int panelWidth = 262;
    content->setFixedWidth(panelWidth);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFixedWidth(panelWidth + scroll->verticalScrollBar()->sizeHint().width() + 2);
    setWidget(scroll);

    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &FrontPanelDock::refresh);
    refreshTimer->start(250);
    refresh();
}

/// Click on the title folds / unfolds the section; the state is remembered
QGroupBox *FrontPanelDock::makeCollapsible(QGroupBox *box, const QString &key) {
    const QString title = box->title();
    box->setCheckable(true);
    QSettings st;
    const bool open = st.value("FrontPanel/open_" + key, true).toBool();
    auto apply = [box, title, key](bool on) {
        box->setTitle(QString::fromUtf8(on ? "▾ " : "▸ ") + title);
        for (QWidget *w : box->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) w->setVisible(on);
        QSettings s;
        s.setValue("FrontPanel/open_" + key, on);
    };
    box->setChecked(open);
    apply(open);
    connect(box, &QGroupBox::toggled, box, apply);
    return box;
}

QPushButton *FrontPanelDock::makeButton(const QString &text, const QString &tip, bool checkable) {
    QPushButton *b = new QPushButton(text);
    b->setToolTip(tip);
    b->setCheckable(checkable);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

QString FrontPanelDock::channelColorCss(ChannelID ch) const {
    QColor c = ch < colors.size() ? colors[ch] : QColor(Qt::white);
    c.setAlpha(255);
    return c.name();
}

// ---------------------------------------------------------------- RUN / STOP
QWidget *FrontPanelDock::makeAcquisitionBar() {
    QWidget *bar = new QWidget();
    bar->setObjectName("acqBar");
    QHBoxLayout *h = new QHBoxLayout(bar);
    h->setContentsMargins(6, 1, 6, 1);
    h->setSpacing(4);

    runButton = makeButton(tr("RUN"), tr("Iniciar / parar a aquisição"));
    runButton->setMinimumWidth(84);
    singleButton = makeButton(tr("SINGLE"), tr("Captura única: espera um disparo e para"));
    QPushButton *autoButton = makeButton(tr("AUTOSET"), tr("Ajusta V/div, tempo/div e trigger ao sinal"));
    QPushButton *forceButton = makeButton(tr("FORCE"), tr("Força um disparo agora"));
    for (QPushButton *b : {runButton, singleButton, autoButton, forceButton}) h->addWidget(b);
    bar->setStyleSheet("QWidget#acqBar QPushButton { background: #3a404a; color: #f0f2f5; border: 1px solid #555d6a;"
                       "  border-radius: 4px; padding: 3px 10px; font-weight: bold; min-height: 20px; }"
                       "QWidget#acqBar QPushButton:hover { background: #475061; }"
                       "QWidget#acqBar QPushButton:pressed { background: #2c3139; }");

    connect(runButton, &QPushButton::clicked, this, &FrontPanelDock::runStop);
    connect(singleButton, &QPushButton::clicked, this, &FrontPanelDock::single);
    connect(autoButton, &QPushButton::clicked, this, &FrontPanelDock::autoset);
    connect(forceButton, &QPushButton::clicked, [this]() { dsoControl->forceTrigger(); });
    return bar;
}

void FrontPanelDock::runStop() {
    if (!dsoControl->isSampling() && scope->trigger.mode == Dso::TriggerMode::SINGLE)
        triggerDock->selectMode(Dso::TriggerMode::WAIT_FORCE);
    samplingAction->trigger();
    refresh();
}

void FrontPanelDock::single() {
    triggerDock->selectMode(Dso::TriggerMode::SINGLE);
    if (!dsoControl->isSampling()) samplingAction->trigger();
    refresh();
}

// ---------------------------------------------------------------- VERTICAL
QGroupBox *FrontPanelDock::makeChannelBox(ChannelID ch) {
    QGroupBox *box = new QGroupBox(tr("CH%1").arg(ch + 1));
    box->setStyleSheet(QString("QGroupBox { color: %1; border-color: %1; }").arg(channelColorCss(ch)));
    QGridLayout *g = new QGridLayout(box);
    ChannelUi &u = channelUi[ch];
    u.box = box;

    u.onButton = makeButton(tr("LIGADO"), tr("Liga/desliga o canal"), true);
    u.onButton->setStyleSheet(QString("QPushButton:checked { background: %1; color: #000; font-weight: bold; }")
                                  .arg(channelColorCss(ch)));
    u.couplingButton = makeButton("DC", tr("Acoplamento DC / AC"));
    u.invertButton = makeButton(tr("INV"), tr("Inverter o sinal"), true);
    u.probeGroup = new QButtonGroup(this);
    u.probeGroup->setExclusive(true);
    // Ponteiras de tensão (x1..x100) e garras de corrente Hantek (CC-65 / CC-650): mesmo grupo exclusivo
    QGridLayout *probeRow = new QGridLayout(); // ponteiras de tensão, 4 por linha, em ordem crescente
    probeRow->setSpacing(3);
    QGridLayout *clampGrid = new QGridLayout();
    clampGrid->setSpacing(3);
    const std::vector<ProbeSensor> &sensors = probeSensors();
    std::vector<unsigned> order(sensors.size());
    for (unsigned i = 0; i < sensors.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&sensors](unsigned a, unsigned b) {
        if (sensors[a].current != sensors[b].current) return !sensors[a].current;
        return sensors[a].current ? false : sensors[a].factor < sensors[b].factor;
    });
    int clampPos = 0, probePos = 0;
    for (unsigned i : order) {
        QString tip = QString::fromUtf8(sensors[i].tip);
        tip += sensors[i].current ? tr("\nA tela passa a mostrar corrente (A/div, Ipp, A RMS...).\n"
                                       "Ajuste o zero da garra antes de medir DC.")
                                  : tr("\nUse a mesma posição da chave da ponteira/sonda.");
        QPushButton *b = makeButton(QString::fromUtf8(sensors[i].label), tip, true);
        b->setStyleSheet(QString("QPushButton { padding: 4px 2px; } QPushButton:checked { background: %1; color: #000;"
                                 " font-weight: bold; }")
                             .arg(channelColorCss(ch)));
        u.probeGroup->addButton(b, (int)i);
        if (sensors[i].current) {
            clampGrid->addWidget(b, clampPos / 2, clampPos % 2);
            ++clampPos;
        } else {
            probeRow->addWidget(b, probePos / 4, probePos % 4);
            ++probePos;
        }
    }

    QPushButton *vUp = makeButton(QString::fromUtf8("▲"), tr("Aumentar V/div (sinal menor na tela)"));
    QPushButton *vDown = makeButton(QString::fromUtf8("▼"), tr("Diminuir V/div (sinal maior na tela)"));
    u.vdivLabel = new QLabel("-");
    u.vdivLabel->setProperty("role", "value");
    u.vdivLabel->setAlignment(Qt::AlignCenter);

    QPushButton *pUp = makeButton(QString::fromUtf8("▲"), tr("Subir o traço"));
    QPushButton *pDown = makeButton(QString::fromUtf8("▼"), tr("Descer o traço"));
    QPushButton *pZero = makeButton(tr("0"), tr("Centralizar o traço"));
    u.posLabel = new QLabel("-");
    u.posLabel->setProperty("role", "value");
    u.posLabel->setAlignment(Qt::AlignCenter);

    g->addWidget(u.onButton, 0, 0, 1, 2);
    g->addWidget(u.couplingButton, 0, 2);
    g->addWidget(u.invertButton, 0, 3);
    u.vdivTitle = new QLabel(tr("V/div"));
    g->addWidget(u.vdivTitle, 1, 0);
    g->addWidget(vDown, 1, 1);
    g->addWidget(u.vdivLabel, 1, 2);
    g->addWidget(vUp, 1, 3);
    g->addWidget(new QLabel(tr("Posição")), 2, 0);
    QHBoxLayout *posRow = new QHBoxLayout();
    posRow->setSpacing(3);
    posRow->addWidget(pDown);
    posRow->addWidget(u.posLabel, 1);
    posRow->addWidget(pUp);
    posRow->addWidget(pZero);
    g->addLayout(posRow, 2, 1, 1, 3);
    g->addWidget(new QLabel(tr("Ponteira")), 3, 0);
    g->addLayout(probeRow, 3, 1, 1, 3);
    g->addWidget(new QLabel(tr("Garra")), 4, 0);
    g->addLayout(clampGrid, 4, 1, 1, 3);
    g->setColumnStretch(2, 1);

    connect(u.onButton, &QPushButton::clicked, [this, ch](bool checked) { voltageDock->selectUsed(ch, checked); });
    connect(u.couplingButton, &QPushButton::clicked, [this, ch]() {
        unsigned next = (scope->voltage[ch].couplingOrMathIndex + 1) % (unsigned)spec->couplings.size();
        voltageDock->selectCoupling(ch, next);
        refresh();
    });
    connect(u.invertButton, &QPushButton::clicked,
            [this, ch](bool checked) { voltageDock->selectInverted(ch, checked); });
    connect(vUp, &QPushButton::clicked, [this, ch]() { stepGain(ch, +1); });
    connect(vDown, &QPushButton::clicked, [this, ch]() { stepGain(ch, -1); });
    connect(pUp, &QPushButton::clicked, [this, ch]() { stepOffset(ch, +0.25); });
    connect(pDown, &QPushButton::clicked, [this, ch]() { stepOffset(ch, -0.25); });
    connect(pZero, &QPushButton::clicked, [this, ch]() { stepOffset(ch, -scope->voltage[ch].offset); });
    connect(u.probeGroup, &QButtonGroup::idClicked, [this, ch](int index) {
        if (index >= 0 && (size_t)index < probeSensors().size()) changeProbe(ch, (unsigned)index);
    });
    return box;
}

void FrontPanelDock::stepGain(ChannelID ch, int dir) {
    int idx = (int)scope->voltage[ch].gainStepIndex + dir;
    idx = std::max(0, std::min(idx, (int)scope->gainSteps.size() - 1));
    voltageDock->selectGain(ch, (unsigned)idx);
    refresh();
}

void FrontPanelDock::stepOffset(ChannelID ch, double delta) {
    double v = scope->voltage[ch].offset + delta;
    v = std::max(-(double)DIVS_VOLTAGE / 2, std::min(v, (double)DIVS_VOLTAGE / 2));
    dsoWidget->setOffsetValue(ch, v);
    refresh();
}

void FrontPanelDock::changeProbe(ChannelID ch, unsigned sensor) {
    const ProbeSensor &sn = probeSensors()[sensor];
    const double old = scope->voltage[ch].probe;
    const double probe = sn.factor;
    scope->voltage[ch].sensor = sensor;
    scope->voltage[ch].current = sn.current;
    if (probe == old) {
        // same factor, maybe other unit (x10 -> CC-65 20A): only the labels change
        voltageDock->updateGainLabels(ch);
        dsoWidget->updateVoltageGain(ch);
        dsoWidget->refreshScopes();
        refresh();
        return;
    }
    scope->voltage[ch].probe = probe;
    dsoControl->setProbe(ch, probe);
    dsoControl->setGain(ch, scope->gain(ch) * DIVS_VOLTAGE);
    voltageDock->updateGainLabels(ch);
    dsoWidget->updateVoltageGain(ch);
    // keep the trigger at the same place on screen
    dsoWidget->setTriggerLevelValue(ch, scope->voltage[ch].trigger * probe / old);
    stats[ch].valid = false;
    refresh();
}

// ---------------------------------------------------------------- HORIZONTAL
QGroupBox *FrontPanelDock::makeHorizontalBox() {
    QGroupBox *box = new QGroupBox(tr("Horizontal"));
    box->setStyleSheet("QGroupBox { color: #3fb8e0; border-color: #3fb8e0; }"); // azul
    QGridLayout *g = new QGridLayout(box);

    QPushButton *tFaster = makeButton(QString::fromUtf8("◀"), tr("Menos tempo por divisão (ampliar)"));
    QPushButton *tSlower = makeButton(QString::fromUtf8("▶"), tr("Mais tempo por divisão"));
    timebaseLabel = new QLabel("-");
    timebaseLabel->setProperty("role", "value");
    timebaseLabel->setAlignment(Qt::AlignCenter);

    QPushButton *pLeft = makeButton(QString::fromUtf8("◀"), tr("Mover o ponto de disparo para a esquerda"));
    QPushButton *pRight = makeButton(QString::fromUtf8("▶"), tr("Mover o ponto de disparo para a direita"));
    QPushButton *pCenter = makeButton(tr("50%"), tr("Disparo no centro da tela"));
    pretriggerLabel = new QLabel("-");
    pretriggerLabel->setProperty("role", "value");
    pretriggerLabel->setAlignment(Qt::AlignCenter);

    g->addWidget(new QLabel(tr("Tempo/div")), 0, 0);
    g->addWidget(tFaster, 0, 1);
    g->addWidget(timebaseLabel, 0, 2);
    g->addWidget(tSlower, 0, 3);
    g->addWidget(new QLabel(tr("Pré-disparo")), 1, 0);
    g->addWidget(pLeft, 1, 1);
    g->addWidget(pretriggerLabel, 1, 2);
    g->addWidget(pRight, 1, 3);
    g->addWidget(pCenter, 2, 2);
    // Memória (tamanho do registro): antes só na janela Horizontal antiga
    g->addWidget(new QLabel(tr("Memória")), 3, 0);
    recLenCombo = new QComboBox();
    recLenCombo->setToolTip(tr("Amostras por aquisição. Maior = mais resolução na FFT e na lupa, porém mais lento.\n"
                               "Roll = rolagem contínua (tempos/div longos)."));
    g->addWidget(recLenCombo, 3, 1, 1, 3);
    connect(recLenCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), [this](int index) {
        horizontalDock->selectRecordLength(index);
        refresh();
    });
    g->setColumnStretch(2, 1);

    connect(tFaster, &QPushButton::clicked, [this]() {
        horizontalDock->stepTimebase(-1);
        refresh();
    });
    connect(tSlower, &QPushButton::clicked, [this]() {
        horizontalDock->stepTimebase(+1);
        refresh();
    });
    connect(pLeft, &QPushButton::clicked, [this]() {
        dsoWidget->setPretriggerValue(scope->trigger.position - 0.1);
        refresh();
    });
    connect(pRight, &QPushButton::clicked, [this]() {
        dsoWidget->setPretriggerValue(scope->trigger.position + 0.1);
        refresh();
    });
    connect(pCenter, &QPushButton::clicked, [this]() {
        dsoWidget->setPretriggerValue(0.5);
        refresh();
    });
    return box;
}

// ---------------------------------------------------------------- TRIGGER
QGroupBox *FrontPanelDock::makeTriggerBox() {
    QGroupBox *box = new QGroupBox(tr("Trigger"));
    box->setStyleSheet("QGroupBox { color: #f08c2e; border-color: #f08c2e; }"); // laranja
    QGridLayout *g = new QGridLayout(box);

    // modo
    modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    QPushButton *mAuto = makeButton(tr("AUTO"), tr("Atualiza sempre, mesmo sem disparo"), true);
    QPushButton *mNormal = makeButton(tr("NORMAL"), tr("Só atualiza quando há disparo"), true);
    QPushButton *mSingle = makeButton(tr("ÚNICO"), tr("Para após o primeiro disparo"), true);
    modeGroup->addButton(mAuto, (int)Dso::TriggerMode::WAIT_FORCE);
    modeGroup->addButton(mNormal, (int)Dso::TriggerMode::HARDWARE_SOFTWARE);
    modeGroup->addButton(mSingle, (int)Dso::TriggerMode::SINGLE);

    // fonte
    sourceGroup = new QButtonGroup(this);
    sourceGroup->setExclusive(true);
    QHBoxLayout *srcRow = new QHBoxLayout();
    for (ChannelID ch = 0; ch < spec->channels; ++ch) {
        QPushButton *b = makeButton(tr("CH%1").arg(ch + 1), tr("Disparar pelo CH%1").arg(ch + 1), true);
        sourceGroup->addButton(b, (int)ch);
        srcRow->addWidget(b);
    }
    for (size_t i = 0; i < spec->specialTriggerChannels.size(); ++i) {
        QPushButton *b = makeButton(QString::fromStdString(spec->specialTriggerChannels[i].name),
                                    tr("Disparar pela entrada externa"), true);
        sourceGroup->addButton(b, 100 + (int)i);
        srcRow->addWidget(b);
    }

    // borda
    slopeGroup = new QButtonGroup(this);
    slopeGroup->setExclusive(true);
    QPushButton *sUp = makeButton(QString::fromUtf8("↗ Subida"), tr("Disparo na borda de subida"), true);
    QPushButton *sDown = makeButton(QString::fromUtf8("↘ Descida"), tr("Disparo na borda de descida"), true);
    slopeGroup->addButton(sUp, (int)Dso::Slope::Positive);
    slopeGroup->addButton(sDown, (int)Dso::Slope::Negative);

    // nível
    QPushButton *lUp = makeButton(QString::fromUtf8("▲"), tr("Subir o nível de disparo"));
    QPushButton *lDown = makeButton(QString::fromUtf8("▼"), tr("Descer o nível de disparo"));
    QPushButton *l50 = makeButton(tr("50%"), tr("Nível no meio do sinal"));
    triggerLevelLabel = new QLabel("-");
    triggerLevelLabel->setProperty("role", "value");
    triggerLevelLabel->setAlignment(Qt::AlignCenter);

    g->addWidget(new QLabel(tr("Modo")), 0, 0, 1, 3);
    g->addWidget(mAuto, 1, 0);
    g->addWidget(mNormal, 1, 1);
    g->addWidget(mSingle, 1, 2);
    g->addWidget(new QLabel(tr("Fonte")), 2, 0, 1, 3);
    g->addLayout(srcRow, 3, 0, 1, 3);
    g->addWidget(new QLabel(tr("Borda")), 4, 0, 1, 3);
    g->addWidget(sUp, 5, 0, 1, 1);
    g->addWidget(sDown, 5, 1, 1, 1);
    hfRejectButton = makeButton(tr("REJ. AF"),
                                tr("Rejeição de altas frequências no disparo: liga o filtro de disparo do\n"
                                   "DSO-2250 e ignora ruído/componentes acima de ~50 kHz ao alinhar o traço"),
                                true);
    hfRejectButton->setChecked(scope->trigger.hfReject);
    g->addWidget(hfRejectButton, 5, 2, 1, 1);
    g->addWidget(new QLabel(tr("Nível")), 6, 0, 1, 3);
    QHBoxLayout *lvlRow = new QHBoxLayout();
    lvlRow->addWidget(lDown);
    lvlRow->addWidget(triggerLevelLabel, 1);
    lvlRow->addWidget(lUp);
    lvlRow->addWidget(l50);
    g->addLayout(lvlRow, 7, 0, 1, 3);

    connect(modeGroup, &QButtonGroup::idClicked, [this](int id) {
        triggerDock->selectMode((Dso::TriggerMode)id);
        refresh();
    });
    connect(sourceGroup, &QButtonGroup::idClicked, [this](int id) {
        if (id >= 100)
            triggerDock->selectSource(true, (unsigned)(id - 100));
        else
            triggerDock->selectSource(false, (unsigned)id);
        refresh();
    });
    connect(slopeGroup, &QButtonGroup::idClicked, [this](int id) {
        triggerDock->selectSlope((Dso::Slope)id);
        refresh();
    });
    connect(hfRejectButton, &QPushButton::toggled, [this](bool on) {
        scope->trigger.hfReject = on;
        dsoControl->setTriggerHFReject(on);
        refresh();
    });
    connect(lUp, &QPushButton::clicked, [this]() { stepTriggerLevel(+1); });
    connect(lDown, &QPushButton::clicked, [this]() { stepTriggerLevel(-1); });
    connect(l50, &QPushButton::clicked, this, &FrontPanelDock::triggerLevelTo50);
    return box;
}

void FrontPanelDock::stepTriggerLevel(int dir) {
    if (scope->trigger.special) return;
    const ChannelID ch = scope->trigger.source;
    if (ch >= spec->channels) return;
    const double step = scope->gain(ch) * 0.1; // 0,1 divisão
    dsoWidget->setTriggerLevelValue(ch, scope->voltage[ch].trigger + dir * step);
    refresh();
}

void FrontPanelDock::triggerLevelTo50() {
    if (scope->trigger.special) return;
    const ChannelID ch = scope->trigger.source;
    if (ch >= spec->channels || !stats[ch].valid) return;
    dsoWidget->setTriggerLevelValue(ch, (stats[ch].vmin + stats[ch].vmax) / 2);
    refresh();
}

// ---------------------------------------------------------------- AUTOSET
void FrontPanelDock::autoset() {
    // FFT ligada: também ajusta a escala do espectro com os próximos dados
    for (ChannelID ch = 0; ch < fftAutoPending.size(); ++ch)
        if (scope->spectrum[ch].used) fftAutoPending[ch] = true;
    // 1) escala vertical: sinal ocupando ~6 divisões
    for (ChannelID ch = 0; ch < spec->channels; ++ch) {
        if (!scope->voltage[ch].used || !stats[ch].valid) continue;
        const double extent = std::max(std::fabs(stats[ch].vmax), std::fabs(stats[ch].vmin)) * 2.0;
        const double vpp = std::max(stats[ch].vmax - stats[ch].vmin, extent * 0.5);
        unsigned best = (unsigned)scope->gainSteps.size() - 1;
        for (unsigned i = 0; i < scope->gainSteps.size(); ++i) {
            if (vpp <= scope->gainSteps[i] * scope->voltage[ch].probe * 6.0 && extent <= scope->gainSteps[i] * scope->voltage[ch].probe * 7.5) {
                best = i;
                break;
            }
        }
        voltageDock->selectGain(ch, best);
        dsoWidget->setOffsetValue(ch, 0.0);
    }

    // 2) trigger: canal ligado com sinal, modo AUTO, nível no meio
    ChannelID src = scope->trigger.special ? 0 : scope->trigger.source;
    if (src >= spec->channels || !scope->voltage[src].used || !stats[src].valid) {
        for (ChannelID ch = 0; ch < spec->channels; ++ch)
            if (scope->voltage[ch].used && stats[ch].valid) {
                src = ch;
                break;
            }
    }
    if (src < spec->channels) {
        triggerDock->selectSource(false, src);
        triggerDock->selectMode(Dso::TriggerMode::WAIT_FORCE);
        triggerDock->selectSlope(Dso::Slope::Positive);
        if (stats[src].valid) dsoWidget->setTriggerLevelValue(src, (stats[src].vmin + stats[src].vmax) / 2);

        // 3) tempo/div: cerca de 3 períodos na tela
        const double f = stats[src].freq;
        if (f > 0.0 && std::isfinite(f)) horizontalDock->selectTimebase(3.0 / (f * DIVS_TIME));
    }
    dsoWidget->setPretriggerValue(0.5);
    if (!dsoControl->isSampling()) samplingAction->trigger();
    refresh();
}

// ---------------------------------------------------------------- dados / leituras
void FrontPanelDock::showData(std::shared_ptr<PPresult> data) {
    if (!data) return;
    for (ChannelID ch = 0; ch < spec->channels && ch < data->channelCount(); ++ch) {
        const DataChannel *dc = data->data(ch);
        ChannelStats &s = stats[ch];
        if (!dc || dc->voltage.sample.empty() || !scope->voltage[ch].used) {
            s.valid = false;
            continue;
        }
        const std::vector<double> &v = dc->voltage.sample;
        double mn = v[0], mx = v[0], sum = 0, sum2 = 0;
        for (double x : v) {
            if (x < mn) mn = x;
            if (x > mx) mx = x;
            sum += x;
            sum2 += x * x;
        }
        const double n = (double)v.size();
        s.vmin = mn;
        s.vmax = mx;
        s.mean = sum / n;
        s.rms = std::sqrt(sum2 / n);
        s.freq = dc->frequency;
        s.valid = true;
    }

    // FFT readouts: strongest component in the span, its level and THD
    for (ChannelID ch = 0; ch < data->channelCount(); ++ch) {
        const DataChannel *dc = data->data(ch);
        if (dc && dc->voltage.interval > 0 && ch < spec->channels) lastSampleInterval = dc->voltage.interval;
        if (dc && ch < fftAutoPending.size() && fftAutoPending[ch] && scope->spectrum[ch].used &&
            !dc->spectrum.sample.empty()) {
            fftAutoPending[ch] = false;
            autoSpectrum(ch, dc);
        }
        if (ch >= fftReadouts.size()) continue;
        QLabel *l = fftReadouts[ch];
        if (!dc || !scope->spectrum[ch].used || !dc->specPeakValid) {
            l->setText(scope->spectrum[ch].used ? tr("%1: ---").arg(scope->spectrum[ch].name) : QString());
            l->setVisible(scope->spectrum[ch].used && (!spectrumBox || spectrumBox->isChecked()));
            continue;
        }
        QString t = tr("%1  F %2  %3 dB%4")
                        .arg(scope->spectrum[ch].name)
                        .arg(valueToString(dc->specPeakFreq, UNIT_HERTZ, 5))
                        .arg(QString::number(dc->specPeakDbV, 'f', 2))
                        .arg(scope->unitSymbol(ch));
        if (dc->specThd >= 0) t += tr("  THD %1%").arg(QString::number(dc->specThd, 'f', dc->specThd < 10 ? 2 : 1));
        // harmonics that stand out of the noise, strongest first (max. 6)
        std::vector<SpectrumHarmonic> hs;
        for (size_t i = 1; i < dc->specHarmonics.size(); ++i)
            if (dc->specHarmonics[i].salient) hs.push_back(dc->specHarmonics[i]);
        std::sort(hs.begin(), hs.end(),
                  [](const SpectrumHarmonic &a, const SpectrumHarmonic &b) { return a.dbc > b.dbc; });
        if (hs.size() > 6) hs.resize(6);
        for (size_t i = 0; i < hs.size(); ++i) {
            t += (i % 2 == 0) ? QStringLiteral("\n  ") : QStringLiteral("   ");
            t += tr("H%1 %2 dBc").arg(hs[i].n).arg(QString::number(hs[i].dbc, 'f', 1));
        }
        if (hs.empty() && !dc->specHarmonics.empty()) t += tr("\n  sem harmônicos acima do ruído");
        l->setText(t);
        l->setVisible(!spectrumBox || spectrumBox->isChecked());
    }
}

void FrontPanelDock::refresh() {
    refreshSpectrum();
    const bool sampling = dsoControl->isSampling();
    runButton->setText(sampling ? QString::fromUtf8("■ STOP") : QString::fromUtf8("▶ RUN"));
    runButton->setStyleSheet(sampling ? "QPushButton { background: #b03030; border-color: #e05050; }"
                                      : "QPushButton { background: #2e8b3e; border-color: #4fbf62; }");

    for (ChannelID ch = 0; ch < spec->channels; ++ch) {
        ChannelUi &u = channelUi[ch];
        const DsoSettingsScopeVoltage &v = scope->voltage[ch];
        u.onButton->setChecked(v.used);
        u.onButton->setText(v.used ? tr("LIGADO") : tr("DESLIGADO"));
        if (v.couplingOrMathIndex < spec->couplings.size())
            u.couplingButton->setText(Dso::couplingString(spec->couplings[v.couplingOrMathIndex]));
        u.invertButton->setChecked(v.inverted);
        u.vdivLabel->setText(valueToString(scope->gain(ch), scope->unit(ch), 3) + "/div");
        u.vdivTitle->setText(scope->unitSymbol(ch) + tr("/div"));
        u.posLabel->setText(QString("%1 div").arg(v.offset, 0, 'f', 2));
        if (QAbstractButton *b = u.probeGroup->button((int)v.sensor))
            if (!b->isChecked()) b->setChecked(true);

    }

    timebaseLabel->setText(valueToString(scope->horizontal.timebase, UNIT_SECONDS, 3) + "/div");
    if (recLenCombo) {
        const QStringList names = horizontalDock->recordLengthNames();
        QStringList mine;
        for (int i = 0; i < recLenCombo->count(); ++i) mine << recLenCombo->itemText(i);
        QSignalBlocker blk(recLenCombo);
        if (names != mine) {
            recLenCombo->clear();
            recLenCombo->addItems(names);
        }
        if (recLenCombo->currentIndex() != horizontalDock->recordLengthIndex())
            recLenCombo->setCurrentIndex(horizontalDock->recordLengthIndex());
    }
    pretriggerLabel->setText(QString("%1 %").arg((int)std::round(scope->trigger.position * 100)));

    if (QAbstractButton *b = modeGroup->button((int)scope->trigger.mode)) b->setChecked(true);
    if (QAbstractButton *b = sourceGroup->button(scope->trigger.special ? 100 + (int)scope->trigger.source
                                                                        : (int)scope->trigger.source))
        b->setChecked(true);
    if (QAbstractButton *b = slopeGroup->button((int)scope->trigger.slope)) b->setChecked(true);
    if (hfRejectButton && hfRejectButton->isChecked() != scope->trigger.hfReject) {
        QSignalBlocker blk(hfRejectButton);
        hfRejectButton->setChecked(scope->trigger.hfReject);
    }

    if (scope->trigger.special)
        triggerLevelLabel->setText(tr("externo"));
    else if (scope->trigger.source < spec->channels)
        triggerLevelLabel->setText(
            fmtValue(scope->voltage[scope->trigger.source].trigger, scope->unit(scope->trigger.source)));
}

// ---------------------------------------------------------------- TELA
QGroupBox *FrontPanelDock::makeDisplayBox() {
    QGroupBox *box = new QGroupBox(tr("Tela"));
    QGridLayout *g = new QGridLayout(box);
    gridButton = makeButton(tr("Grade: Normal"), tr("Alterna o contraste da grade (Normal / Média / Alta)"));
    g->addWidget(gridButton, 0, 0);
    interpButton = makeButton(tr("Interpolação"), tr("Como ligar as amostras: sen(x)/x (suave, como osciloscópios de bancada), "
                                                     "linear ou só pontos"));
    g->addWidget(interpButton, 1, 0);
    connect(interpButton, &QPushButton::clicked, [this]() {
        if (!view) return;
        // ciclo: sen(x)/x -> linear -> pontos -> sen(x)/x
        switch (view->interpolation) {
        case Dso::INTERPOLATION_SINC: view->interpolation = Dso::INTERPOLATION_LINEAR; break;
        case Dso::INTERPOLATION_LINEAR: view->interpolation = Dso::INTERPOLATION_OFF; break;
        default: view->interpolation = Dso::INTERPOLATION_SINC; break;
        }
        updateInterpButton();
    });
    connect(gridButton, &QPushButton::clicked, [this]() { emit gridContrastRequested((gridLevel + 1) % 3); });
    return box;
}

void FrontPanelDock::setGridContrastLevel(int level) {
    gridLevel = level;
    static const char *names[] = {"Normal", "Média", "Alta"};
    if (gridButton && level >= 0 && level < 3) gridButton->setText(tr("Grade: %1").arg(QString::fromUtf8(names[level])));
}

void FrontPanelDock::setViewSettings(DsoSettingsView *v) {
    view = v;
    updateInterpButton();
}

void FrontPanelDock::updateInterpButton() {
    if (!interpButton || !view) return;
    switch (view->interpolation) {
    case Dso::INTERPOLATION_SINC: interpButton->setText(tr("Interpolação: sen(x)/x")); break;
    case Dso::INTERPOLATION_LINEAR: interpButton->setText(tr("Interpolação: linear")); break;
    default: interpButton->setText(tr("Interpolação: pontos")); break;
    }
}

// ---------------------------------------------------------------- FFT / analisador de espectro
static double nextOneTwoFive(double value, int dir) {
    // 1-2-5 sequence: next value above (dir>0) or below (dir<0) the current one
    if (value <= 0) value = 1;
    const double decade = std::pow(10.0, std::floor(std::log10(value) + 1e-9));
    static const double m[] = {1, 2, 5};
    std::vector<double> seq;
    for (int d = -1; d <= 1; ++d)
        for (double x : m) seq.push_back(x * decade * std::pow(10.0, d));
    if (dir > 0) {
        for (double v : seq)
            if (v > value * 1.0001) return v;
    } else {
        for (auto it = seq.rbegin(); it != seq.rend(); ++it)
            if (*it < value * 0.9999) return *it;
    }
    return value;
}

QColor FrontPanelDock::spectrumColor(ChannelID ch) const {
    QColor c;
    if (view && ch < view->screen.spectrum.size()) c = view->screen.spectrum[ch];
    else c = ch < colors.size() ? colors[ch] : QColor(Qt::white);
    c.setAlpha(255);
    return c;
}

void FrontPanelDock::setSpectrumControls(SpectrumDock *dock, DsoSettingsPostProcessing *postSettings) {
    spectrumDock = dock;
    post = postSettings;
    if (!spectrumDock || !post || spectrumBox || !mainLayout) return;
    spectrumBox = makeCollapsible(makeSpectrumBox(), "fft");
    mainLayout->insertWidget(std::max(0, mainLayout->count() - 1), spectrumBox); // before the stretch
    refreshSpectrum();
}

QGroupBox *FrontPanelDock::makeSpectrumBox() {
    QGroupBox *box = new QGroupBox(tr("FFT (analisador de espectro)"));
    box->setStyleSheet("QGroupBox { color: #b07cff; border-color: #b07cff; }"); // violeta
    QGridLayout *g = new QGridLayout(box);
    int row = 0;

    // Liga/desliga por canal
    QHBoxLayout *onRow = new QHBoxLayout();
    for (ChannelID ch = 0; ch < scope->spectrum.size(); ++ch) {
        QPushButton *b = makeButton(scope->spectrum[ch].name, tr("Mostra a FFT deste canal"), true);
        const QString col = spectrumColor(ch).name();
        b->setStyleSheet(QString("QPushButton { color: %1; } QPushButton:checked { background: %1; color: #000000; "
                                 "border-color: %1; }")
                             .arg(col));
        connect(b, &QPushButton::toggled, [this, ch](bool on) {
            spectrumDock->selectUsed(ch, on);
            if (on && ch < fftAutoPending.size()) fftAutoPending[ch] = true; // ajusta a escala com o próximo quadro
            refresh();
        });
        fftOnButtons.push_back(b);
        onRow->addWidget(b);
    }
    g->addLayout(onRow, row++, 0, 1, 4);
    fftAutoPending.assign(scope->spectrum.size(), false);

    // Janela
    g->addWidget(new QLabel(tr("Janela")), row++, 0, 1, 4);
    windowGroup = new QButtonGroup(this);
    windowGroup->setExclusive(true);
    struct W {
        const char *name;
        const char *tip;
        Dso::WindowFunction w;
    };
    static const W wins[] = {
        {"Ret.", "Retangular: melhor resolução, só para sinais periódicos exatos ou transientes", Dso::WindowFunction::RECTANGULAR},
        {"Hann", "Hann: uso geral", Dso::WindowFunction::HANN},
        {"Flat-top", "Flat-top: amplitude exata (±0,02 dB), picos largos", Dso::WindowFunction::FLATTOP},
        {"B-Harris", "Blackman-Harris: faixa dinâmica alta (ver harmônicos pequenos)", Dso::WindowFunction::BLACKMANHARRIS}};
    QHBoxLayout *winRow = new QHBoxLayout();
    for (const W &w : wins) {
        QPushButton *b = makeButton(QString::fromUtf8(w.name), QString::fromUtf8(w.tip), true);
        windowGroup->addButton(b, (int)w.w);
        winRow->addWidget(b);
    }
    g->addLayout(winRow, row++, 0, 1, 4);
    connect(windowGroup, &QButtonGroup::idClicked, [this](int id) {
        post->spectrumWindow = (Dso::WindowFunction)id;
        refresh();
    });

    // dB/div
    QPushButton *dbDown = makeButton(QString::fromUtf8("▼"), tr("Menos dB por divisão (mais detalhe)"));
    QPushButton *dbUp = makeButton(QString::fromUtf8("▲"), tr("Mais dB por divisão (mais faixa dinâmica)"));
    dbDivLabel = new QLabel();
    dbDivLabel->setProperty("role", "value");
    dbDivLabel->setAlignment(Qt::AlignCenter);
    g->addWidget(new QLabel(tr("Escala")), row, 0);
    g->addWidget(dbDown, row, 1);
    g->addWidget(dbDivLabel, row, 2);
    g->addWidget(dbUp, row++, 3);
    connect(dbDown, &QPushButton::clicked, [this]() { stepSpectrumMagnitude(-1); });
    connect(dbUp, &QPushButton::clicked, [this]() { stepSpectrumMagnitude(+1); });

    // Nível de referência (topo da tela)
    QPushButton *refDown = makeButton(QString::fromUtf8("▼"), tr("Referência (topo da tela) -10 dB"));
    QPushButton *refUp = makeButton(QString::fromUtf8("▲"), tr("Referência (topo da tela) +10 dB"));
    refLabel = new QLabel();
    refLabel->setProperty("role", "value");
    refLabel->setAlignment(Qt::AlignCenter);
    g->addWidget(new QLabel(tr("Ref. topo")), row, 0);
    g->addWidget(refDown, row, 1);
    g->addWidget(refLabel, row, 2);
    g->addWidget(refUp, row++, 3);
    connect(refDown, &QPushButton::clicked, [this]() {
        post->spectrumReference = std::max(-200.0, post->spectrumReference - 10.0);
        refresh();
    });
    connect(refUp, &QPushButton::clicked, [this]() {
        post->spectrumReference = std::min(60.0, post->spectrumReference + 10.0);
        refresh();
    });

    // Hz/div
    QPushButton *fDown = makeButton(QString::fromUtf8("◀"), tr("Menos Hz por divisão (zoom)"));
    QPushButton *fUp = makeButton(QString::fromUtf8("▶"), tr("Mais Hz por divisão"));
    fbaseLabel = new QLabel();
    fbaseLabel->setProperty("role", "value");
    fbaseLabel->setAlignment(Qt::AlignCenter);
    g->addWidget(new QLabel(tr("Hz/div")), row, 0);
    g->addWidget(fDown, row, 1);
    g->addWidget(fbaseLabel, row, 2);
    g->addWidget(fUp, row++, 3);
    connect(fDown, &QPushButton::clicked, [this]() { stepFrequencybase(-1); });
    connect(fUp, &QPushButton::clicked, [this]() { stepFrequencybase(+1); });

    // Faixa, média, retenção de pico
    QPushButton *nyq = makeButton(tr("Faixa total"), tr("Mostra de 0 Hz até a metade da taxa de amostragem (Nyquist)"));
    avgButton = makeButton(tr("Média"), tr("Média de potência de várias aquisições: reduz o ruído (desl. / 4 / 16 / 64)"));
    holdButton = makeButton(tr("Ret. pico"), tr("Mantém o máximo de cada frequência (max hold)"), true);
    QPushButton *clr = makeButton(tr("Limpar"), tr("Reinicia a média e a retenção de pico"));
    QPushButton *autoFft = makeButton(tr("AUTO FFT"), tr("Ajusta Hz/div na fundamental (harmônicos nas divisões), "
                                                         "10 dB/div e a referência logo acima do pico"));
    autoFft->setStyleSheet("QPushButton { font-weight: bold; }");
    harmButton = makeButton(tr("Marcar harm."), tr("Marca na tela a fundamental (F) e os harmônicos que se destacam "
                                                   "do ruído (2..10)"),
                            true);
    g->addWidget(autoFft, row, 0, 1, 2);
    g->addWidget(harmButton, row++, 2, 1, 2);
    g->addWidget(nyq, row, 0, 1, 2);
    g->addWidget(avgButton, row++, 2, 1, 2);
    g->addWidget(holdButton, row, 0, 1, 2);
    g->addWidget(clr, row++, 2, 1, 2);
    onlyButton = makeButton(tr("Só FFT"), tr("Esconde os sinais no tempo enquanto a FFT está ligada "
                                             "(a aquisição e o disparo continuam)"),
                            true);
    g->addWidget(onlyButton, row++, 0, 1, 4);
    connect(onlyButton, &QPushButton::toggled, [this](bool on) {
        post->spectrumOnly = on;
        refresh();
    });
    connect(autoFft, &QPushButton::clicked, [this]() {
        for (ChannelID ch = 0; ch < fftAutoPending.size(); ++ch)
            if (scope->spectrum[ch].used) fftAutoPending[ch] = true;
    });
    connect(harmButton, &QPushButton::toggled, [this](bool on) {
        post->spectrumShowHarmonics = on;
        refresh();
    });
    connect(nyq, &QPushButton::clicked, this, &FrontPanelDock::spanToNyquist);
    connect(avgButton, &QPushButton::clicked, [this]() {
        static const unsigned seq[] = {1, 4, 16, 64};
        unsigned next = 1;
        for (size_t i = 0; i < 4; ++i)
            if (post->spectrumAverage == seq[i]) next = seq[(i + 1) % 4];
        post->spectrumAverage = next;
        refresh();
    });
    connect(holdButton, &QPushButton::toggled, [this](bool on) {
        post->spectrumPeakHold = on;
        refresh();
    });
    connect(clr, &QPushButton::clicked, [this]() { ++post->spectrumReset; });

    // Leituras: componente mais forte da faixa visível
    QLabel *hdr = new QLabel(tr("Fundamental (F) e harmônicos que se destacam:"));
    hdr->setProperty("role", "readout");
    hdr->setWordWrap(true);
    g->addWidget(hdr, row++, 0, 1, 4);
    for (ChannelID ch = 0; ch < scope->spectrum.size(); ++ch) {
        QLabel *l = new QLabel();
        l->setProperty("role", "readout");
        l->setWordWrap(true);
        l->setStyleSheet(QString("QLabel { color: %1; font-family: monospace; font-size: 9pt; }")
                             .arg(spectrumColor(ch).name()));
        l->setVisible(false);
        fftReadouts.push_back(l);
        g->addWidget(l, row++, 0, 1, 4);
    }
    return box;
}

void FrontPanelDock::refreshSpectrum() {
    if (!spectrumBox || !post) return;
    for (ChannelID ch = 0; ch < fftOnButtons.size() && ch < scope->spectrum.size(); ++ch) {
        QPushButton *b = fftOnButtons[ch];
        if (b->isChecked() != scope->spectrum[ch].used) {
            QSignalBlocker blk(b);
            b->setChecked(scope->spectrum[ch].used);
        }
    }
    if (QAbstractButton *b = windowGroup->button((int)post->spectrumWindow)) {
        if (!b->isChecked()) b->setChecked(true);
    } else if (QAbstractButton *c = windowGroup->checkedButton()) {
        // window chosen in the configuration dialog that has no button here
        windowGroup->setExclusive(false);
        c->setChecked(false);
        windowGroup->setExclusive(true);
    }
    if (!scope->spectrum.empty())
        dbDivLabel->setText(QString("%1 dB/div").arg(scope->spectrum[0].magnitude, 0, 'g', 3));
    bool allCurrent = false;
    for (ChannelID c = 0; c < scope->spectrum.size(); ++c)
        if (scope->spectrum[c].used) {
            allCurrent = scope->unit(c) == UNIT_AMPERE;
            if (!allCurrent) break;
        }
    refLabel->setText(QString("%1 dB%2").arg(post->spectrumReference, 0, 'f', 0).arg(allCurrent ? "A" : "V"));
    fbaseLabel->setText(valueToString(scope->horizontal.frequencybase, UNIT_HERTZ, 3));
    avgButton->setText(post->spectrumAverage > 1 ? tr("Média: %1x").arg(post->spectrumAverage) : tr("Média: desl."));
    avgButton->setStyleSheet(post->spectrumAverage > 1 ? "QPushButton { background: #2f6fbf; border-color: #5a95e0; }"
                                                       : QString());
    if (onlyButton->isChecked() != post->spectrumOnly) {
        QSignalBlocker blk(onlyButton);
        onlyButton->setChecked(post->spectrumOnly);
    }
    if (harmButton->isChecked() != post->spectrumShowHarmonics) {
        QSignalBlocker blk(harmButton);
        harmButton->setChecked(post->spectrumShowHarmonics);
    }
    if (holdButton->isChecked() != post->spectrumPeakHold) {
        QSignalBlocker blk(holdButton);
        holdButton->setChecked(post->spectrumPeakHold);
    }
}

void FrontPanelDock::stepSpectrumMagnitude(int dir) {
    if (!spectrumDock || scope->spectrum.empty()) return;
    const std::vector<double> &steps = spectrumDock->magnitudes();
    if (steps.empty()) return;
    const double cur = scope->spectrum[0].magnitude;
    size_t idx = 0;
    for (size_t i = 0; i < steps.size(); ++i)
        if (std::fabs(steps[i] - cur) < std::fabs(steps[idx] - cur)) idx = i;
    if (dir > 0 && idx + 1 < steps.size()) ++idx;
    if (dir < 0 && idx > 0) --idx;
    for (ChannelID ch = 0; ch < scope->spectrum.size(); ++ch) spectrumDock->selectMagnitude(ch, steps[idx]);
    refresh();
}

void FrontPanelDock::stepFrequencybase(int dir) {
    double f = nextOneTwoFive(scope->horizontal.frequencybase, dir);
    f = std::max(1.0, std::min(100e6, f));
    horizontalDock->selectFrequencybase(f);
    refresh();
}

void FrontPanelDock::spanToNyquist() {
    if (lastSampleInterval <= 0) return;
    // 0 Hz .. Nyquist exactly over the 10 divisions (not rounded to 1-2-5, so the band fills the screen)
    const double nyquist = 0.5 / lastSampleInterval;
    horizontalDock->selectFrequencybase(std::max(1.0, std::min(100e6, nyquist / DIVS_TIME)));
    refresh();
}

void FrontPanelDock::autoSpectrum(ChannelID ch, const DataChannel *dc) {
    if (!dc || dc->voltage.interval <= 0 || dc->spectrum.sample.size() < 16) return;
    const double nyquist = 0.5 / dc->voltage.interval;
    double f0 = dc->frequency > 0 ? dc->frequency : (dc->specPeakValid ? dc->specPeakFreq : 0.0);

    // Hz/div: the 1-2-5 value closest above the fundamental -> F at ~1 div, harmonics on the next divisions
    double fbase;
    if (f0 > 0 && f0 * 10.0 < nyquist) {
        fbase = nextOneTwoFive(f0 * 0.95, +1);
    } else {
        fbase = nyquist / DIVS_TIME; // whole band on the screen
    }
    horizontalDock->selectFrequencybase(std::max(1.0, std::min(100e6, fbase)));

    // 10 dB/div, reference just above the highest bin (ignoring DC)
    double maxDb = -1e9;
    const std::vector<double> &s = dc->spectrum.sample;
    const size_t skip = std::min<size_t>(s.size() / 2, 6);
    for (size_t k = skip; k < s.size(); ++k) maxDb = std::max(maxDb, s[k]);
    if (maxDb > -1e8) post->spectrumReference = std::min(60.0, std::ceil((maxDb + 5.0) / 10.0) * 10.0);
    for (ChannelID c = 0; c < scope->spectrum.size(); ++c) spectrumDock->selectMagnitude(c, 10.0);
    ++post->spectrumReset;
    Q_UNUSED(ch);
    refresh();
}

// ---------------------------------------------------------------- colors
void FrontPanelDock::styleChannel(ChannelID ch) {
    if (ch >= channelUi.size()) return;
    ChannelUi &u = channelUi[ch];
    const QString col = channelColorCss(ch);
    if (u.box) u.box->setStyleSheet(QString("QGroupBox { color: %1; border-color: %1; }").arg(col));
    if (u.onButton)
        u.onButton->setStyleSheet(
            QString("QPushButton:checked { background: %1; color: #000; font-weight: bold; }").arg(col));
    if (u.probeGroup)
        for (QAbstractButton *b : u.probeGroup->buttons())
            b->setStyleSheet(QString("QPushButton { padding: 4px 2px; } QPushButton:checked { background: %1; color: #000;"
                                     " font-weight: bold; }")
                                 .arg(col));
}

void FrontPanelDock::setChannelColors(const std::vector<QColor> &channelColors) {
    colors = channelColors;
    for (ChannelID ch = 0; ch < channelUi.size(); ++ch) styleChannel(ch);
    for (ChannelID ch = 0; ch < fftOnButtons.size(); ++ch) {
        const QString col = spectrumColor(ch).name();
        fftOnButtons[ch]->setStyleSheet(
            QString("QPushButton { color: %1; } QPushButton:checked { background: %1; color: #000000; border-color: %1; }")
                .arg(col));
    }
    for (ChannelID ch = 0; ch < fftReadouts.size(); ++ch)
        fftReadouts[ch]->setStyleSheet(
            QString("QLabel { color: %1; font-family: monospace; font-size: 9pt; }").arg(spectrumColor(ch).name()));
}
