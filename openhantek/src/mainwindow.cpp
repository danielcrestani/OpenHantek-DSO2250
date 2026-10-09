#include "mainwindow.h"
#include "iconfont/QtAwesome.h"
#include "ui_mainwindow.h"

#include "FrontPanelDock.h"
#include "measurementbar.h"
#include "HorizontalDock.h"
#include "SpectrumDock.h"
#include "TriggerDock.h"
#include "VoltageDock.h"
#include "dockwindows.h"

#include "configdialog.h"
#include "dockwindows.h"
#include "dsomodel.h"
#include "dsowidget.h"
#include "datalogger.h"
#include "zerocalibration.h"
#include "hantekdsocontrol.h"
#include "usb/usbdevice.h"
#include "viewconstants.h"

#include "settings.h"

#include <QActionGroup>
#include <QFileDialog>
#include <QMenu>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QTimer>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QToolButton>
#include <QClipboard>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QDateTime>

#include <algorithm>
#include <cmath>

MainWindow::MainWindow(HantekDsoControl *dsoControl, DsoSettings *settings, QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), mSettings(settings) {
    ui->setupUi(this);
    ui->actionSave->setIcon(iconFont->icon(fa::save));
    ui->actionAbout->setIcon(iconFont->icon(fa::questioncircle));
    ui->actionOpen->setIcon(iconFont->icon(fa::folderopen));
    ui->actionSampling->setIcon(iconFont->icon(fa::pause,
                                               {std::make_pair("text-selected-off", QChar(fa::play)),
                                                std::make_pair("text-off", QChar(fa::play)),
                                                std::make_pair("text-active-off", QChar(fa::play))}));
    ui->actionSettings->setIcon(iconFont->icon(fa::gear));
    ui->actionManualCommand->setIcon(iconFont->icon(fa::edit));
    ui->actionDigital_phosphor->setIcon(QIcon(":/images/digitalphosphor.svg"));
    ui->actionZoom->setIcon(iconFont->icon(fa::crop));
    ui->actionCursors->setIcon(iconFont->icon(fa::crosshairs));

    // Menus em português; itens que o painel frontal já faz saem do menu
    ui->menuFile->setTitle(tr("&Arquivo"));
    ui->menuExport->setTitle(tr("E&xportar"));
    ui->menuView->setTitle(tr("E&xibir"));
    ui->menuOscilloscope->setTitle(tr("&Osciloscópio"));
    ui->menuHelp->setTitle(tr("A&juda"));
    ui->actionOpen->setText(tr("Abrir configuração..."));
    ui->actionSave->setText(tr("Salvar configuração"));
    ui->actionSave_as->setText(tr("Salvar configuração como..."));
    ui->actionExit->setText(tr("Sair"));
    ui->actionSettings->setText(tr("Configurações..."));
    ui->actionDigital_phosphor->setText(tr("Fósforo digital (persistência)"));
    ui->actionZoom->setText(tr("Lupa (tela ampliada)"));
    ui->actionAbout->setText(tr("Sobre"));
    ui->menuView->removeAction(ui->actionCursors);       // vai para o menu Cursores
    ui->menuView->removeAction(ui->actionManualCommand); // comando manual: só para depuração
    ui->menuOscilloscope->removeAction(ui->actionSampling); // RUN/STOP fica no painel e na barra

    // Window title
    setWindowIcon(QIcon(":openhantek.png"));
    setWindowTitle(
        tr("OpenHantek DSO-2250 (fork) - Dispositivo %1 - %2")
            .arg(QString::fromStdString(dsoControl->getDevice()->getModel()->name))
            .arg(QSurfaceFormat::defaultFormat().renderableType() == QSurfaceFormat::OpenGL ? "OpenGL" : "OpenGL ES"));

#if (QT_VERSION >= QT_VERSION_CHECK(5, 6, 0))
    setDockOptions(dockOptions() | QMainWindow::GroupedDragging);
#endif


    DsoSettingsScope *scope = &(mSettings->scope);
    const Dso::ControlSpecification *spec = dsoControl->getDevice()->getModel()->spec();
    deviceSpec = spec;

    registerDockMetaTypes();

    // Docking windows
    // Create dock windows before the dso widget, they fix messed up settings
    HorizontalDock *horizontalDock;
    TriggerDock *triggerDock;
    SpectrumDock *spectrumDock;
    VoltageDock *voltageDock;
    horizontalDock = new HorizontalDock(scope, this);
    triggerDock = new TriggerDock(scope, spec, this);
    spectrumDock = new SpectrumDock(scope, this);
    voltageDock = new VoltageDock(scope, spec, this);

    addDockWidget(Qt::RightDockWidgetArea, horizontalDock);
    addDockWidget(Qt::RightDockWidgetArea, triggerDock);
    addDockWidget(Qt::RightDockWidgetArea, voltageDock);
    addDockWidget(Qt::RightDockWidgetArea, spectrumDock);

    restoreGeometry(mSettings->mainWindowGeometry);
    restoreState(mSettings->mainWindowState);

    // Central oszilloscope widget
    dsoWidget = new DsoWidget(&mSettings->scope, &mSettings->view, spec);

    // Tela + rodapé de medições por canal
    measurementBar = new MeasurementBar(scope, spec, mSettings->view.screen.voltage, this);
    QWidget *central = new QWidget(this);
    QVBoxLayout *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(dsoWidget, 1);
    centralLayout->addWidget(measurementBar, 0);
    setCentralWidget(central);

    // Painel frontal estilo osciloscópio; as janelas antigas ficam agrupadas em abas
    frontPanel = new FrontPanelDock(scope, spec, dsoControl, voltageDock, horizontalDock, triggerDock, dsoWidget,
                                    ui->actionSampling, mSettings->view.screen.voltage, this);
    addDockWidget(Qt::RightDockWidgetArea, frontPanel);
    // RUN/STOP, SINGLE, AUTOSET, FORCE no cabeçalho (substituem o botão play da barra)
    ui->toolBar->insertWidget(ui->actionSampling, frontPanel->acquisitionBar());
    ui->toolBar->removeAction(ui->actionSampling);
    tabifyDockWidget(horizontalDock, triggerDock);
    tabifyDockWidget(horizontalDock, voltageDock);
    tabifyDockWidget(horizontalDock, spectrumDock);
    horizontalDock->raise();

    // Menu de medições (rodapé por canal)
    menuBar()->insertMenu(ui->menuHelp->menuAction(), measurementBar->menu());

    // Exibir: fósforo, lupa e painel frontal. As janelas antigas (Horizontal, Trigger, Voltage, Spectrum)
    // ficam escondidas: tudo o que elas faziam está no painel frontal.
    frontPanel->toggleViewAction()->setText(tr("Painel frontal"));
    ui->menuView->addAction(frontPanel->toggleViewAction());
    for (QDockWidget *d : std::vector<QDockWidget *>{horizontalDock, triggerDock, voltageDock, spectrumDock}) {
        d->setFloating(false);
        d->hide();
    }
    frontPanel->show();

    // Contraste da grade: só no painel (botão "Grade")
    const int alpha = mSettings->view.screen.grid.alpha();
    const int currentLevel = alpha <= 0x40 ? 0 : (alpha <= 0xa0 ? 1 : 2);
    frontPanel->setGridContrastLevel(currentLevel);
    frontPanel->setViewSettings(&mSettings->view);
    frontPanel->setSpectrumControls(spectrumDock, &mSettings->post);
    connect(frontPanel, &FrontPanelDock::gridContrastRequested, [this](int level) { applyGridContrast(level); });

    // Command field inside the status bar
    QLineEdit *commandEdit = new QLineEdit(this);
    commandEdit->hide();

    statusBar()->addPermanentWidget(commandEdit, 1);

    connect(ui->actionManualCommand, &QAction::toggled, [this, commandEdit](bool checked) {
        commandEdit->setVisible(checked);
        if (checked) commandEdit->setFocus();
    });

    connect(commandEdit, &QLineEdit::returnPressed, [this, commandEdit, dsoControl]() {
        Dso::ErrorCode errorCode = dsoControl->stringCommand(commandEdit->text());
        commandEdit->clear();
        this->ui->actionManualCommand->setChecked(false);
        if (errorCode != Dso::ErrorCode::NONE) statusBar()->showMessage(tr("Invalid command"), 3000);
    });

    // Connect general signals
    connect(dsoControl, &HantekDsoControl::statusMessage, statusBar(), &QStatusBar::showMessage);

    // Connect signals to DSO controller and widget
    connect(horizontalDock, &HorizontalDock::samplerateChanged, [dsoControl, this]() {
        dsoControl->setSamplerate(mSettings->scope.horizontal.samplerate);
        this->dsoWidget->updateSamplerate(mSettings->scope.horizontal.samplerate);
    });
    connect(horizontalDock, &HorizontalDock::timebaseChanged, [dsoControl, this]() {
        dsoControl->setRecordTime(mSettings->scope.horizontal.timebase * DIVS_TIME);
        this->dsoWidget->updateTimebase(mSettings->scope.horizontal.timebase);
    });
    connect(horizontalDock, &HorizontalDock::frequencybaseChanged, dsoWidget, &DsoWidget::updateFrequencybase);
    connect(horizontalDock, &HorizontalDock::recordLengthChanged,
            [dsoControl](unsigned long recordLength) { dsoControl->setRecordLength(recordLength); });

    connect(dsoControl, &HantekDsoControl::recordTimeChanged,
            [this, settings, horizontalDock, dsoControl](double duration) {
                if (settings->scope.horizontal.samplerateSource == DsoSettingsScopeHorizontal::Samplerrate &&
                    settings->scope.horizontal.recordLength != UINT_MAX) {
                    // The samplerate was set, let's adapt the timebase accordingly
                    settings->scope.horizontal.timebase = horizontalDock->setTimebase(duration / DIVS_TIME);
                }

                // The trigger position should be kept at the same place but the timebase has
                // changed
                dsoControl->setPretriggerPosition(settings->scope.trigger.position *
                                                  settings->scope.horizontal.timebase * DIVS_TIME);

                this->dsoWidget->updateTimebase(settings->scope.horizontal.timebase);
            });
    connect(dsoControl, &HantekDsoControl::samplerateChanged, [this, horizontalDock](double samplerate) {
        if (mSettings->scope.horizontal.samplerateSource == DsoSettingsScopeHorizontal::Duration &&
            mSettings->scope.horizontal.recordLength != UINT_MAX) {
            // The timebase was set, let's adapt the samplerate accordingly
            mSettings->scope.horizontal.samplerate = samplerate;
            horizontalDock->setSamplerate(samplerate);
            dsoWidget->updateSamplerate(samplerate);
        }
    });

    connect(triggerDock, &TriggerDock::modeChanged, dsoControl, &HantekDsoControl::setTriggerMode);
    connect(triggerDock, &TriggerDock::modeChanged, dsoWidget, &DsoWidget::updateTriggerMode);
    connect(triggerDock, &TriggerDock::sourceChanged, dsoControl, &HantekDsoControl::setTriggerSource);
    connect(triggerDock, &TriggerDock::sourceChanged, dsoWidget, &DsoWidget::updateTriggerSource);
    connect(triggerDock, &TriggerDock::slopeChanged, dsoControl, &HantekDsoControl::setTriggerSlope);
    connect(triggerDock, &TriggerDock::slopeChanged, dsoWidget, &DsoWidget::updateTriggerSlope);
    connect(dsoWidget, &DsoWidget::triggerPositionChanged, dsoControl, &HantekDsoControl::setPretriggerPosition);
    connect(dsoWidget, &DsoWidget::triggerLevelChanged, dsoControl, &HantekDsoControl::setTriggerLevel);

    auto usedChanged = [this, dsoControl, spec](ChannelID channel, bool used) {
        if (channel >= (unsigned int)mSettings->scope.voltage.size()) return;

//        if (!used) dsoWidget->
        bool mathUsed = mSettings->scope.anyUsed(spec->channels);

        // Normal channel, check if voltage/spectrum or math channel is used
        if (channel < spec->channels) dsoControl->setChannelUsed(channel, mathUsed | mSettings->scope.anyUsed(channel));
        // Math channel, update all channels
        else if (channel == spec->channels) {
            for (ChannelID c = 0; c < spec->channels; ++c)
                dsoControl->setChannelUsed(c, mathUsed | mSettings->scope.anyUsed(c));
        }
    };
    connect(voltageDock, &VoltageDock::usedChanged, usedChanged);
    connect(spectrumDock, &SpectrumDock::usedChanged, usedChanged);

    connect(voltageDock, &VoltageDock::couplingChanged, dsoControl, &HantekDsoControl::setCoupling);
    connect(voltageDock, &VoltageDock::couplingChanged, dsoWidget, &DsoWidget::updateVoltageCoupling);
    connect(voltageDock, &VoltageDock::modeChanged, dsoWidget, &DsoWidget::updateMathMode);
    connect(voltageDock, &VoltageDock::gainChanged, [this, dsoControl, spec](ChannelID channel, double gain) {
        if (channel >= spec->channels) return;

        dsoControl->setGain(channel, mSettings->scope.gain(channel) * DIVS_VOLTAGE);
    });
    connect(voltageDock, &VoltageDock::gainChanged, dsoWidget, &DsoWidget::updateVoltageGain);
    connect(dsoWidget, &DsoWidget::offsetChanged, [this, dsoControl, spec](ChannelID channel) {
        if (channel >= spec->channels) return;
        dsoControl->setOffset(channel, (mSettings->scope.voltage[channel].offset / DIVS_VOLTAGE) + 0.5);
    });

    connect(voltageDock, &VoltageDock::usedChanged, dsoWidget, &DsoWidget::updateVoltageUsed);
    connect(spectrumDock, &SpectrumDock::usedChanged, dsoWidget, &DsoWidget::updateSpectrumUsed);
    connect(spectrumDock, &SpectrumDock::magnitudeChanged, dsoWidget, &DsoWidget::updateSpectrumMagnitude);

    // Started/stopped signals from oscilloscope
    connect(dsoControl, &HantekDsoControl::samplingStatusChanged, [this, dsoControl](bool enabled) {
        QSignalBlocker blocker(this->ui->actionSampling);
        if (enabled) {
            this->ui->actionSampling->setText(tr("Parar"));
            this->ui->actionSampling->setStatusTip(tr("Parar a aquisição"));
        } else {
            this->ui->actionSampling->setText(tr("Iniciar"));
            this->ui->actionSampling->setStatusTip(tr("Iniciar a aquisição"));
        }
        this->ui->actionSampling->setChecked(enabled);
    });
    connect(this->ui->actionSampling, &QAction::triggered, dsoControl, &HantekDsoControl::enableSampling);
    this->ui->actionSampling->setChecked(dsoControl->isSampling());

    connect(dsoControl, &HantekDsoControl::availableRecordLengthsChanged, horizontalDock,
            &HorizontalDock::setAvailableRecordLengths);
    connect(dsoControl, &HantekDsoControl::samplerateLimitsChanged, horizontalDock,
            &HorizontalDock::setSamplerateLimits);
    connect(dsoControl, &HantekDsoControl::samplerateSet, horizontalDock, &HorizontalDock::setSamplerateSteps);

    connect(ui->actionOpen, &QAction::triggered, [this]() {
        QString fileName = QFileDialog::getOpenFileName(this, tr("Open file"), "", tr("Settings (*.ini)"));
        if (!fileName.isEmpty()) {
            if (mSettings->setFilename(fileName)) { mSettings->load(); }
        }
    });

    connect(ui->actionSave, &QAction::triggered, [this]() {
        mSettings->mainWindowGeometry = saveGeometry();
        mSettings->mainWindowState = saveState();
        mSettings->save();
    });

    connect(ui->actionSave_as, &QAction::triggered, [this]() {
        QString fileName = QFileDialog::getSaveFileName(this, tr("Save settings"), "", tr("Settings (*.ini)"));
        if (fileName.isEmpty()) return;
        mSettings->mainWindowGeometry = saveGeometry();
        mSettings->mainWindowState = saveState();
        mSettings->setFilename(fileName);
        mSettings->save();
    });

    connect(ui->actionExit, &QAction::triggered, this, &QWidget::close);

    connect(ui->actionSettings, &QAction::triggered, [this]() {
        mSettings->mainWindowGeometry = saveGeometry();
        mSettings->mainWindowState = saveState();

        DsoConfigDialog *configDialog = new DsoConfigDialog(this->mSettings, this);
        configDialog->setModal(true);
        configDialog->setAttribute(Qt::WA_DeleteOnClose);
        // OK / Aplicar: cores, grade, interpolação e fósforo valem na hora
        connect(configDialog, &DsoConfigDialog::applied, this, [this]() {
            dsoWidget->applyColors();
            if (measurementBar) measurementBar->setChannelColors(mSettings->view.screen.voltage);
            if (frontPanel) frontPanel->setChannelColors(mSettings->view.screen.voltage);
            if (ui->actionDigital_phosphor->isChecked() != mSettings->view.digitalPhosphor)
                ui->actionDigital_phosphor->setChecked(mSettings->view.digitalPhosphor);
            dsoWidget->updateCursorGrid(mSettings->view.cursorsVisible);
            dsoWidget->refreshScopes();
        });
        configDialog->show();
    });

    connect(this->ui->actionDigital_phosphor, &QAction::toggled, [this](bool enabled) {
        mSettings->view.digitalPhosphor = enabled;

        if (mSettings->view.digitalPhosphor)
            this->ui->actionDigital_phosphor->setStatusTip(tr("Disable fading of previous graphs"));
        else
            this->ui->actionDigital_phosphor->setStatusTip(tr("Enable fading of previous graphs"));
    });
    this->ui->actionDigital_phosphor->setChecked(mSettings->view.digitalPhosphor);

    connect(ui->actionZoom, &QAction::toggled, [this](bool enabled) {
        mSettings->view.zoom = enabled;

        if (mSettings->view.zoom)
            this->ui->actionZoom->setStatusTip(tr("Hide magnified scope"));
        else
            this->ui->actionZoom->setStatusTip(tr("Show magnified scope"));

        this->dsoWidget->updateZoom(enabled);
    });
    ui->actionZoom->setChecked(mSettings->view.zoom);

    connect(ui->actionCursors, &QAction::toggled, [this](bool enabled) {
        mSettings->view.cursorsVisible = enabled;

        if (mSettings->view.cursorsVisible)
            this->ui->actionCursors->setStatusTip(tr("Hide measurements"));
        else
            this->ui->actionCursors->setStatusTip(tr("Show measurements"));

        this->dsoWidget->updateCursorGrid(enabled);
    });
    ui->actionCursors->setChecked(mSettings->view.cursorsVisible);
    setupCursorMenu();
    setupExportAndLog();

    // Calibração de zero dos canais (erro do DAC de posição deste aparelho)
    zeroCal = new ZeroCalibration(dsoControl, &mSettings->scope, deviceSpec, this);
    zeroCal->loadAndApply();
    ui->menuOscilloscope->addSeparator();
    QAction *calZero = ui->menuOscilloscope->addAction(tr("Calibrar zero dos canais..."));
    calZero->setStatusTip(tr("Com as entradas em 0 V, mede e corrige o desvio do traço em cada V/div e posição"));
    connect(calZero, &QAction::triggered, [this]() { zeroCal->start(); });
    QAction *calClear = ui->menuOscilloscope->addAction(tr("Apagar calibração de zero"));
    connect(calClear, &QAction::triggered, [this]() {
        if (QMessageBox::question(this, tr("Apagar calibração de zero"),
                                  tr("Voltar a usar só a calibração gravada no aparelho?")) == QMessageBox::Yes)
            zeroCal->clear();
    });

    connect(ui->actionAbout, &QAction::triggered, [this]() {
        QMessageBox::about(
            this, tr("Sobre o OpenHantek DSO-2250"),
            tr("<h3>OpenHantek &ndash; edição DSO-2250</h3>"
               "<p>Versão %1 &mdash; <b>versão modificada</b> do OpenHantek.</p>"
               "<p><b>Projeto original</b><br>"
               "Copyright &copy; 2010, 2011 Oliver Haag<br>"
               "Copyright &copy; 2012&ndash;2017 comunidade OpenHantek<br>"
               "<a href='https://github.com/OpenHantek/openhantek'>github.com/OpenHantek/openhantek</a></p>"
               "<p><b>Modificações</b> (2026): Daniel Crestani, com assistência do Claude (Anthropic).<br>"
               "Suporte completo ao Hantek DSO-2250, painel frontal, FFT calibrada com harmônicos, "
               "ponteiras e garras de corrente CC-65/CC-650, cursores, registro de dados e outras.<br>"
               "Código-fonte: <a href='https://github.com/danielcrestani/OpenHantek-DSO2250'>"
               "github.com/danielcrestani/OpenHantek-DSO2250</a></p>"
               "<p>Este programa é software livre: você pode redistribuí-lo e/ou modificá-lo sob os termos da "
               "<a href='https://www.gnu.org/licenses/gpl-3.0.html'>GNU General Public License</a>, versão 3 ou "
               "(a seu critério) qualquer versão posterior, publicada pela Free Software Foundation.</p>"
               "<p>Este programa é distribuído na esperança de que seja útil, mas <b>SEM NENHUMA GARANTIA</b>; "
               "sem mesmo a garantia implícita de COMERCIALIZAÇÃO ou ADEQUAÇÃO A UM PROPÓSITO ESPECÍFICO. "
               "Veja a licença para mais detalhes.</p>"
               "<p><small>O DSO-2250 não é isolado: o terra das ponteiras é o terra do computador.</small></p>")
                .arg(VERSION));
    });
    ui->actionAbout->setText(tr("Sobre o OpenHantek DSO-2250"));
    QAction *forkPage = new QAction(tr("Página deste fork (código e documentação)"), this);
    connect(forkPage, &QAction::triggered,
            []() { QDesktopServices::openUrl(QUrl("https://github.com/danielcrestani/OpenHantek-DSO2250")); });
    QAction *origPage = new QAction(tr("Projeto original OpenHantek"), this);
    connect(origPage, &QAction::triggered,
            []() { QDesktopServices::openUrl(QUrl("https://github.com/OpenHantek/openhantek")); });
    QAction *aboutQt = new QAction(tr("Sobre o Qt"), this);
    connect(aboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);
    ui->menuHelp->insertAction(ui->actionAbout, forkPage);
    ui->menuHelp->insertAction(ui->actionAbout, origPage);
    ui->menuHelp->insertSeparator(ui->actionAbout);
    ui->menuHelp->addAction(aboutQt);

    if (mSettings->scope.horizontal.samplerateSource == DsoSettingsScopeHorizontal::Samplerrate)
        dsoWidget->updateSamplerate(mSettings->scope.horizontal.samplerate);
    else
        dsoWidget->updateTimebase(mSettings->scope.horizontal.timebase);

    for (ChannelID channel = 0; channel < spec->channels; ++channel) {
        this->dsoWidget->updateVoltageUsed(channel, mSettings->scope.voltage[channel].used);
        this->dsoWidget->updateSpectrumUsed(channel, mSettings->scope.spectrum[channel].used);
    }
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::showNewData(std::shared_ptr<PPresult> data) {
    lastData = data;
    if (zeroCal && zeroCal->running()) zeroCal->process(data);
    else if (logger) logger->process(data);
    dsoWidget->showNew(data);
    if (frontPanel) frontPanel->showData(data);
    if (measurementBar) measurementBar->showData(data);
}

void MainWindow::applyGridContrast(int level) {
    static const int gridAlpha[] = {0x3f, 0xa0, 0xff};
    static const int axesAlpha[] = {0x7f, 0xc0, 0xff};
    if (level < 0 || level > 2) return;
    QColor grid = mSettings->view.screen.grid;
    grid.setAlpha(gridAlpha[level]);
    mSettings->view.screen.grid = grid;
    QColor axes = mSettings->view.screen.axes;
    axes.setAlpha(axesAlpha[level]);
    mSettings->view.screen.axes = axes;
    if (frontPanel) frontPanel->setGridContrastLevel(level);
    dsoWidget->refreshScopes();
}



/// \brief Save the settings before exiting.
/// \param event The close event that should be handled.
void MainWindow::closeEvent(QCloseEvent *event) {
    if (mSettings->alwaysSave) {
        mSettings->mainWindowGeometry = saveGeometry();
        mSettings->mainWindowState = saveState();
        mSettings->save();
    }

    QMainWindow::closeEvent(event);
}

// ---------------------------------------------------------------- menu Cursores
void MainWindow::setupCursorMenu() {
    DsoSettingsScope *scope = &mSettings->scope;
    QMenu *menu = new QMenu(tr("&Cursores"), this);
    menuBar()->insertMenu(ui->menuHelp->menuAction(), menu);

    ui->actionCursors->setText(tr("Mostrar cursores"));
    menu->addAction(ui->actionCursors);
    menu->addSeparator();

    // Fonte: qual cursor o mouse arrasta
    QMenu *srcMenu = menu->addMenu(tr("Canal do cursor"));
    QActionGroup *srcGroup = new QActionGroup(this);
    std::vector<QAction *> srcActions;
    for (unsigned i = 0; i < dsoWidget->cursorCount(); ++i) {
        QString name;
        if (i == 0)
            name = tr("Marcadores da lupa (1 e 2)");
        else if (i - 1 < scope->voltage.size())
            name = tr("%1 (tempo / amplitude)").arg(scope->voltage[i - 1].name);
        else
            name = tr("%1 (frequência / nível)").arg(scope->spectrum[i - 1 - scope->voltage.size()].name);
        QAction *a = srcMenu->addAction(name);
        a->setCheckable(true);
        srcGroup->addAction(a);
        srcActions.push_back(a);
        connect(a, &QAction::triggered, [this, i, scope]() {
            if (!ui->actionCursors->isChecked()) ui->actionCursors->setChecked(true);
            dsoWidget->selectCursor(i);
            DsoSettingsScopeCursor *c = dsoWidget->cursorAt(i);
            if (i > 0 && c && c->shape == DsoSettingsScopeCursor::NONE)
                dsoWidget->setCursorShape(i, DsoSettingsScopeCursor::RECTANGULAR);
            Q_UNUSED(scope);
        });
    }

    // Tipo do cursor selecionado
    QMenu *typeMenu = menu->addMenu(tr("Tipo"));
    QActionGroup *typeGroup = new QActionGroup(this);
    struct T {
        const char *text;
        DsoSettingsScopeCursor::CursorShape shape;
    };
    static const T types[] = {{"Desligado", DsoSettingsScopeCursor::NONE},
                              {"Linhas verticais (tempo / frequência)", DsoSettingsScopeCursor::VERTICAL},
                              {"Linhas horizontais (amplitude / nível)", DsoSettingsScopeCursor::HORIZONTAL},
                              {"Ambos (retângulo)", DsoSettingsScopeCursor::RECTANGULAR}};
    std::vector<QAction *> typeActions;
    for (const T &t : types) {
        QAction *a = typeMenu->addAction(tr(t.text));
        a->setCheckable(true);
        typeGroup->addAction(a);
        typeActions.push_back(a);
        const DsoSettingsScopeCursor::CursorShape shape = t.shape;
        connect(a, &QAction::triggered, [this, shape]() {
            if (!ui->actionCursors->isChecked()) ui->actionCursors->setChecked(true);
            dsoWidget->setCursorShape(dsoWidget->selectedCursor(), shape);
        });
    }

    menu->addSeparator();
    QAction *fit = menu->addAction(tr("Posicionar no sinal"));
    fit->setStatusTip(tr("Canal: 1 período a partir do disparo e máx./mín.  Espectro: fundamental e maior harmônico"));
    connect(fit, &QAction::triggered, this, &MainWindow::positionCursorOnSignal);
    QAction *center = menu->addAction(tr("Centralizar"));
    connect(center, &QAction::triggered, [this]() {
        dsoWidget->setCursorPositions(dsoWidget->selectedCursor(), QPointF(-1.0, -1.0), QPointF(1.0, 1.0));
    });
    // a tabela lateral antiga não é mais usada: a leitura dos cursores fica no rodapé
    if (mSettings->view.cursorTable) dsoWidget->setCursorTableVisible(false);

    // keep the check marks in sync (the table and the mouse can also change things)
    connect(menu, &QMenu::aboutToShow, [this, srcActions, typeActions, typeMenu, fit]() {
        const unsigned sel = dsoWidget->selectedCursor();
        for (unsigned i = 0; i < srcActions.size(); ++i) srcActions[i]->setChecked(i == sel);
        DsoSettingsScopeCursor *c = dsoWidget->cursorAt(sel);
        typeMenu->setEnabled(sel > 0);
        fit->setEnabled(sel > 0);
        static const DsoSettingsScopeCursor::CursorShape order[] = {
            DsoSettingsScopeCursor::NONE, DsoSettingsScopeCursor::VERTICAL, DsoSettingsScopeCursor::HORIZONTAL,
            DsoSettingsScopeCursor::RECTANGULAR};
        for (unsigned i = 0; i < typeActions.size(); ++i) typeActions[i]->setChecked(c && c->shape == order[i]);
    });

    // Leitura dos cursores no rodapé
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, [this]() {
        QColor col;
        const QString text = cursorReadout(&col);
        measurementBar->setCursorText(text, col);
    });
    timer->start(200);
}

QString MainWindow::cursorReadout(QColor *color) const {
    const DsoSettingsScope &scope = mSettings->scope;
    if (!mSettings->view.cursorsVisible) return QString();
    const unsigned sel = dsoWidget->selectedCursor();
    const unsigned nv = (unsigned)scope.voltage.size();
    const double trigX = scope.trigger.position * DIVS_TIME - DIVS_TIME / 2;
    auto timeAt = [&](double x) { return (x - trigX) * scope.horizontal.timebase; };
    const QString sep = QStringLiteral("   │   ");

    if (sel == 0) {
        *color = mSettings->view.screen.text;
        const double t1 = timeAt(scope.horizontal.cursor.pos[0].x()), t2 = timeAt(scope.horizontal.cursor.pos[1].x());
        const double dt = std::fabs(t2 - t1);
        return tr("Cursor marcadores   t1 %1   t2 %2   Δt %3   1/Δt %4")
            .arg(valueToString(t1, UNIT_SECONDS, 4), valueToString(t2, UNIT_SECONDS, 4),
                 valueToString(dt, UNIT_SECONDS, 4), dt > 0 ? valueToString(1.0 / dt, UNIT_HERTZ, 4) : QString("---"));
    }
    if (sel - 1 < nv) {
        const ChannelID ch = sel - 1;
        const DsoSettingsScopeVoltage &v = scope.voltage[ch];
        if (!v.used || v.cursor.shape == DsoSettingsScopeCursor::NONE) return QString();
        *color = mSettings->view.screen.voltage[ch];
        const double inv = v.inverted ? -1.0 : 1.0;
        auto valueAt = [&](double y) { return (y - v.offset) * scope.gain(ch) * inv; };
        QStringList parts;
        parts << tr("Cursor %1").arg(v.name);
        if (v.cursor.shape == DsoSettingsScopeCursor::VERTICAL || v.cursor.shape == DsoSettingsScopeCursor::RECTANGULAR) {
            const double t1 = timeAt(v.cursor.pos[0].x()), t2 = timeAt(v.cursor.pos[1].x());
            const double dt = std::fabs(t2 - t1);
            parts << tr("t1 %1  t2 %2  Δt %3  1/Δt %4")
                         .arg(valueToString(t1, UNIT_SECONDS, 4), valueToString(t2, UNIT_SECONDS, 4),
                              valueToString(dt, UNIT_SECONDS, 4),
                              dt > 0 ? valueToString(1.0 / dt, UNIT_HERTZ, 4) : QString("---"));
        }
        if (v.cursor.shape == DsoSettingsScopeCursor::HORIZONTAL || v.cursor.shape == DsoSettingsScopeCursor::RECTANGULAR) {
            const Unit u = scope.unit(ch);
            const double a = valueAt(v.cursor.pos[0].y()), b = valueAt(v.cursor.pos[1].y());
            const QString sym = scope.unitSymbol(ch);
            parts << tr("%1₁ %2  %1₂ %3  Δ%1 %4")
                         .arg(sym, valueToString(a, u, 4), valueToString(b, u, 4), valueToString(std::fabs(b - a), u, 4));
        }
        return parts.join(sep);
    }
    const ChannelID ch = sel - 1 - nv;
    if (ch >= scope.spectrum.size()) return QString();
    const DsoSettingsScopeSpectrum &s = scope.spectrum[ch];
    if (!s.used || s.cursor.shape == DsoSettingsScopeCursor::NONE) return QString();
    *color = mSettings->view.screen.spectrum[ch];
    const double fbase = scope.horizontal.frequencybase;
    auto freqAt = [&](double x) { return (x + DIVS_TIME / 2) * fbase; };
    auto dbAt = [&](double y) { return mSettings->post.spectrumReference + (y - DIVS_VOLTAGE / 2 - s.offset) * s.magnitude; };
    const QString dbu = scope.unit(ch) == UNIT_AMPERE ? QStringLiteral("dBA") : QStringLiteral("dBV");
    QStringList parts;
    parts << tr("Cursor %1").arg(s.name);
    if (s.cursor.shape == DsoSettingsScopeCursor::VERTICAL || s.cursor.shape == DsoSettingsScopeCursor::RECTANGULAR) {
        const double f1 = freqAt(s.cursor.pos[0].x()), f2 = freqAt(s.cursor.pos[1].x());
        parts << tr("f1 %1  f2 %2  Δf %3")
                     .arg(valueToString(f1, UNIT_HERTZ, 5), valueToString(f2, UNIT_HERTZ, 5),
                          valueToString(std::fabs(f2 - f1), UNIT_HERTZ, 5));
    }
    if (s.cursor.shape == DsoSettingsScopeCursor::HORIZONTAL || s.cursor.shape == DsoSettingsScopeCursor::RECTANGULAR) {
        const double a = dbAt(s.cursor.pos[0].y()), b = dbAt(s.cursor.pos[1].y());
        parts << tr("L1 %1 %4  L2 %2 %4  ΔL %3 dB")
                     .arg(QString::number(a, 'f', 2), QString::number(b, 'f', 2), QString::number(b - a, 'f', 2), dbu);
    }
    return parts.join(sep);
}

void MainWindow::positionCursorOnSignal() {
    const DsoSettingsScope &scope = mSettings->scope;
    const unsigned sel = dsoWidget->selectedCursor();
    const unsigned nv = (unsigned)scope.voltage.size();
    if (sel == 0 || !lastData) return;
    DsoSettingsScopeCursor *c = dsoWidget->cursorAt(sel);
    if (!c) return;

    if (sel - 1 < nv) {
        const ChannelID ch = sel - 1;
        const DataChannel *d = lastData->data(ch);
        if (!d || d->voltage.sample.empty()) return;
        const DsoSettingsScopeVoltage &v = scope.voltage[ch];
        // x: trigger point and one period later
        const double trigX = scope.trigger.position * DIVS_TIME - DIVS_TIME / 2;
        double x1 = trigX + 1.0;
        if (d->frequency > 0) x1 = trigX + (1.0 / d->frequency) / scope.horizontal.timebase;
        // y: max and min of the samples on the screen
        const size_t visible =
            std::min(d->voltage.sample.size(),
                     (size_t)std::ceil(DIVS_TIME * scope.horizontal.timebase / d->voltage.interval) + 1);
        auto mm = std::minmax_element(d->voltage.sample.begin(), d->voltage.sample.begin() + (long)visible);
        const double inv = v.inverted ? -1.0 : 1.0;
        const double g = scope.gain(ch);
        const double yMax = *mm.second / g * inv + v.offset, yMin = *mm.first / g * inv + v.offset;
        if (c->shape == DsoSettingsScopeCursor::NONE) dsoWidget->setCursorShape(sel, DsoSettingsScopeCursor::RECTANGULAR);
        dsoWidget->setCursorPositions(sel, QPointF(trigX, yMin), QPointF(x1, yMax));
        return;
    }
    const ChannelID ch = sel - 1 - nv;
    const DataChannel *d = lastData->data(ch);
    if (!d || d->specHarmonics.empty()) return;
    const DsoSettingsScopeSpectrum &s = scope.spectrum[ch];
    const SpectrumHarmonic &f = d->specHarmonics[0];
    SpectrumHarmonic h = f;
    bool haveH = false;
    for (size_t i = 1; i < d->specHarmonics.size(); ++i)
        if (!haveH || d->specHarmonics[i].dbv > h.dbv) {
            h = d->specHarmonics[i];
            haveH = true;
        }
    if (!haveH) h.freq = 2 * f.freq;
    const double fbase = scope.horizontal.frequencybase;
    auto xOf = [&](double fr) { return fr / fbase - DIVS_TIME / 2; };
    auto yOf = [&](double db) {
        return (db - mSettings->post.spectrumReference) / s.magnitude + DIVS_VOLTAGE / 2 + s.offset;
    };
    if (c->shape == DsoSettingsScopeCursor::NONE) dsoWidget->setCursorShape(sel, DsoSettingsScopeCursor::RECTANGULAR);
    dsoWidget->setCursorPositions(sel, QPointF(xOf(f.freq), yOf(f.binDb)), QPointF(xOf(h.freq), yOf(h.binDb)));
}

// ---------------------------------------------------------------- exportar imagem / registro de dados
void MainWindow::setupExportAndLog() {
    // Exportar: só imagem do que está na tela (tela + rodapé de medições), como um print do osciloscópio
    QAction *img = ui->menuExport->addAction(iconFont->icon(fa::image), tr("Imagem da tela (PNG/JPG)..."));
    img->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(img, &QAction::triggered, this, &MainWindow::exportScreenImage);
    QAction *copy = ui->menuExport->addAction(tr("Copiar imagem da tela"));
    copy->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    connect(copy, &QAction::triggered, [this]() {
        QGuiApplication::clipboard()->setPixmap(centralWidget()->grab());
        statusBar()->showMessage(tr("Imagem da tela copiada"), 3000);
    });

    // Registro de dados por canal
    logger = new DataLogger(&mSettings->scope, deviceSpec, this);
    QAction *logAction = new QAction(tr("Registro de dados (log)..."), this);
    logAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(logAction, &QAction::triggered, [this]() { logger->showDialog(this); });
    ui->menuFile->insertAction(ui->actionExit, logAction);
    ui->menuFile->insertSeparator(ui->actionExit);

    recButton = new QToolButton(this);
    recButton->setPopupMode(QToolButton::MenuButtonPopup);
    QMenu *recMenu = new QMenu(recButton);
    recMenu->addAction(logAction);
    recButton->setMenu(recMenu);
    recButton->setToolTip(tr("Registro de dados: clique para iniciar/parar.\nSeta: configurar (canais, início manual ou por trigger, pasta)."));
    connect(recButton, &QToolButton::clicked, [this]() {
        if (logger->state() == DataLogger::State::Idle)
            logger->start();
        else
            logger->stop();
    });
    ui->toolBar->addWidget(recButton);
    connect(logger, &DataLogger::stateChanged, this, &MainWindow::updateRecButton);
    updateRecButton();
}

void MainWindow::updateRecButton() {
    if (!recButton || !logger) return;
    switch (logger->state()) {
    case DataLogger::State::Idle:
        recButton->setText(QString::fromUtf8("● REG"));
        recButton->setStyleSheet("QToolButton { color: #c03030; font-weight: bold; padding: 3px 8px; }");
        break;
    case DataLogger::State::Armed:
        recButton->setText(QString::fromUtf8("◌ ARMADO"));
        recButton->setStyleSheet("QToolButton { background: #d08a10; color: #000; font-weight: bold; padding: 3px 8px;"
                                 " border-radius: 4px; }");
        break;
    case DataLogger::State::Recording:
        recButton->setText(QString::fromUtf8("● GRAVANDO"));
        recButton->setStyleSheet("QToolButton { background: #c02020; color: #fff; font-weight: bold; padding: 3px 8px;"
                                 " border-radius: 4px; }");
        break;
    }
    const QString st = logger->statusText();
    if (st.startsWith(tr("Erro"))) statusBar()->showMessage(st, 8000);
}

void MainWindow::exportScreenImage() {
    const QPixmap pm = centralWidget()->grab();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString suggestion =
        dir + "/" + QString("osciloscopio_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString selected;
    QString fileName = QFileDialog::getSaveFileName(this, tr("Exportar imagem da tela"), suggestion,
                                                    tr("PNG (*.png);;JPEG (*.jpg *.jpeg);;BMP (*.bmp)"), &selected);
    if (fileName.isEmpty()) return;
    if (QFileInfo(fileName).suffix().isEmpty()) {
        if (selected.startsWith("JPEG"))
            fileName += ".jpg";
        else if (selected.startsWith("BMP"))
            fileName += ".bmp";
        else
            fileName += ".png";
    }
    if (pm.save(fileName, nullptr, 95))
        statusBar()->showMessage(tr("Imagem salva: %1").arg(fileName), 5000);
    else
        statusBar()->showMessage(tr("Não foi possível salvar %1").arg(fileName), 8000);
}
