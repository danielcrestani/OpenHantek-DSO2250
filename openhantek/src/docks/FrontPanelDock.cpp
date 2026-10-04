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
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "HorizontalDock.h"
#include "TriggerDock.h"
#include "VoltageDock.h"
#include "dsowidget.h"
#include "hantekdsocontrol.h"
#include "utils/printutils.h"
#include "viewconstants.h"

static const double probeValues[] = {1.0, 10.0, 50.0, 100.0};

static QString fmtVolts(double v) { return valueToString(v, UNIT_VOLTS, 3); }

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
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);

    layout->addWidget(makeRunBox());
    for (ChannelID ch = 0; ch < spec->channels; ++ch) layout->addWidget(makeChannelBox(ch));
    layout->addWidget(makeHorizontalBox());
    layout->addWidget(makeTriggerBox());
    layout->addWidget(makeDisplayBox());
    layout->addStretch(1);

    content->setStyleSheet(
        "QWidget#panelContent { background: #23272e; }"
        "QGroupBox { color: #c8ccd4; font-weight: bold; border: 1px solid #3a404a; border-radius: 6px;"
        "  margin-top: 10px; padding: 6px 4px 4px 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
        "QLabel { color: #e6e9ee; }"
        "QLabel[role=\"value\"] { background: #11141a; border: 1px solid #3a404a; border-radius: 4px;"
        "  padding: 3px 6px; font-family: monospace; font-size: 11pt; }"
        "QLabel[role=\"readout\"] { font-family: monospace; font-size: 9pt; color: #b8bec8; }"
        "QPushButton { background: #3a404a; color: #f0f2f5; border: 1px solid #555d6a; border-radius: 5px;"
        "  padding: 5px 8px; min-height: 22px; }"
        "QPushButton:hover { background: #475061; }"
        "QPushButton:pressed { background: #2c3139; }"
        "QPushButton:checked { background: #2f6fbf; border-color: #5a95e0; }"
        "QPushButton[role=\"run\"] { font-size: 12pt; font-weight: bold; min-height: 34px; }"
        "QComboBox { background: #3a404a; color: #f0f2f5; border: 1px solid #555d6a; border-radius: 5px;"
        "  padding: 3px 6px; }"
        "QComboBox QAbstractItemView { background: #2b3038; color: #f0f2f5; selection-background-color: #2f6fbf; }");

    QScrollArea *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    scroll->setMinimumWidth(290);
    setWidget(scroll);

    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &FrontPanelDock::refresh);
    refreshTimer->start(250);
    refresh();
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
QGroupBox *FrontPanelDock::makeRunBox() {
    QGroupBox *box = new QGroupBox(tr("Aquisição"));
    QGridLayout *g = new QGridLayout(box);

    runButton = makeButton(tr("RUN"), tr("Iniciar / parar a aquisição"));
    runButton->setProperty("role", "run");
    singleButton = makeButton(tr("SINGLE"), tr("Captura única: espera um disparo e para"));
    singleButton->setProperty("role", "run");
    QPushButton *autoButton = makeButton(tr("AUTOSET"), tr("Ajusta V/div, tempo/div e trigger ao sinal"));
    autoButton->setProperty("role", "run");
    QPushButton *forceButton = makeButton(tr("FORCE"), tr("Força um disparo agora"));

    g->addWidget(runButton, 0, 0);
    g->addWidget(singleButton, 0, 1);
    g->addWidget(autoButton, 1, 0);
    g->addWidget(forceButton, 1, 1);

    connect(runButton, &QPushButton::clicked, this, &FrontPanelDock::runStop);
    connect(singleButton, &QPushButton::clicked, this, &FrontPanelDock::single);
    connect(autoButton, &QPushButton::clicked, this, &FrontPanelDock::autoset);
    connect(forceButton, &QPushButton::clicked, [this]() { dsoControl->forceTrigger(); });
    return box;
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

    u.onButton = makeButton(tr("LIGADO"), tr("Liga/desliga o canal"), true);
    u.onButton->setStyleSheet(QString("QPushButton:checked { background: %1; color: #000; font-weight: bold; }")
                                  .arg(channelColorCss(ch)));
    u.couplingButton = makeButton("DC", tr("Acoplamento DC / AC"));
    u.invertButton = makeButton(tr("INV"), tr("Inverter o sinal"), true);
    u.probeGroup = new QButtonGroup(this);
    u.probeGroup->setExclusive(true);
    QHBoxLayout *probeRow = new QHBoxLayout();
    probeRow->setSpacing(3);
    for (int i = 0; i < 4; ++i) {
        QPushButton *b = makeButton(QString("x%1").arg(probeValues[i]),
                                    tr("Atenuação da ponteira (igual à chave da ponteira)"), true);
        b->setStyleSheet(QString("QPushButton { padding: 4px 2px; } QPushButton:checked { background: %1; color: #000;"
                                 " font-weight: bold; }")
                             .arg(channelColorCss(ch)));
        u.probeGroup->addButton(b, i);
        probeRow->addWidget(b);
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
    g->addWidget(new QLabel(tr("V/div")), 1, 0);
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
    connect(u.probeGroup, static_cast<void (QButtonGroup::*)(int)>(&QButtonGroup::buttonClicked), [this, ch](int index) {
        if (index >= 0 && index < 4) changeProbe(ch, probeValues[index]);
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

void FrontPanelDock::changeProbe(ChannelID ch, double probe) {
    const double old = scope->voltage[ch].probe;
    if (probe == old) return;
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
    g->addWidget(new QLabel(tr("Nível")), 6, 0, 1, 3);
    QHBoxLayout *lvlRow = new QHBoxLayout();
    lvlRow->addWidget(lDown);
    lvlRow->addWidget(triggerLevelLabel, 1);
    lvlRow->addWidget(lUp);
    lvlRow->addWidget(l50);
    g->addLayout(lvlRow, 7, 0, 1, 3);

    connect(modeGroup, static_cast<void (QButtonGroup::*)(int)>(&QButtonGroup::buttonClicked), [this](int id) {
        triggerDock->selectMode((Dso::TriggerMode)id);
        refresh();
    });
    connect(sourceGroup, static_cast<void (QButtonGroup::*)(int)>(&QButtonGroup::buttonClicked), [this](int id) {
        if (id >= 100)
            triggerDock->selectSource(true, (unsigned)(id - 100));
        else
            triggerDock->selectSource(false, (unsigned)id);
        refresh();
    });
    connect(slopeGroup, static_cast<void (QButtonGroup::*)(int)>(&QButtonGroup::buttonClicked), [this](int id) {
        triggerDock->selectSlope((Dso::Slope)id);
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
}

void FrontPanelDock::refresh() {
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
        u.vdivLabel->setText(valueToString(scope->gain(ch), UNIT_VOLTS, 3) + "/div");
        u.posLabel->setText(QString("%1 div").arg(v.offset, 0, 'f', 2));
        for (int i = 0; i < 4; ++i)
            if (probeValues[i] == v.probe) {
                QAbstractButton *b = u.probeGroup->button(i);
                if (b && !b->isChecked()) b->setChecked(true);
            }

    }

    timebaseLabel->setText(valueToString(scope->horizontal.timebase, UNIT_SECONDS, 3) + "/div");
    pretriggerLabel->setText(QString("%1 %").arg((int)std::round(scope->trigger.position * 100)));

    if (QAbstractButton *b = modeGroup->button((int)scope->trigger.mode)) b->setChecked(true);
    if (QAbstractButton *b = sourceGroup->button(scope->trigger.special ? 100 + (int)scope->trigger.source
                                                                        : (int)scope->trigger.source))
        b->setChecked(true);
    if (QAbstractButton *b = slopeGroup->button((int)scope->trigger.slope)) b->setChecked(true);

    if (scope->trigger.special)
        triggerLevelLabel->setText(tr("externo"));
    else if (scope->trigger.source < spec->channels)
        triggerLevelLabel->setText(fmtVolts(scope->voltage[scope->trigger.source].trigger));
}

// ---------------------------------------------------------------- TELA
QGroupBox *FrontPanelDock::makeDisplayBox() {
    QGroupBox *box = new QGroupBox(tr("Tela"));
    QGridLayout *g = new QGridLayout(box);
    gridButton = makeButton(tr("Grade: Normal"), tr("Alterna o contraste da grade (Normal / Média / Alta)"));
    g->addWidget(gridButton, 0, 0);
    connect(gridButton, &QPushButton::clicked, [this]() { emit gridContrastRequested((gridLevel + 1) % 3); });
    return box;
}

void FrontPanelDock::setGridContrastLevel(int level) {
    gridLevel = level;
    static const char *names[] = {"Normal", "Média", "Alta"};
    if (gridButton && level >= 0 && level < 3) gridButton->setText(tr("Grade: %1").arg(QString::fromUtf8(names[level])));
}
