// SPDX-License-Identifier: GPL-2.0+

/*#if defined(OS_UNIX)
#define CONFIG_PATH QDir::homePath() + "/.config/paranoiacs.net/openhantek"
#define CONFIG_FILE CONFIG_PATH "/openhantek.conf"
#elif defined(OS_DARWIN)
#define CONFIG_PATH QDir::homePath() + "/Library/Application Support/OpenHantek"
#define CONFIG_FILE CONFIG_PATH "/openhantek.plist"
#elif defined(OS_WINDOWS)
//#define CONFIG_PATH QDir::homePath() + "" // Too hard to get and this OS sucks
anyway, ignore it
#define CONFIG_FILE "HKEY_CURRENT_USER\\Software\\paranoiacs.net\\OpenHantek"
#endif*/

#define CONFIG_LIST_WIDTH 128     ///< The width of the page selection widget
#define CONFIG_LIST_ITEMHEIGHT 80 ///< The height of one item in the page selection widget
#define CONFIG_LIST_ICONSIZE 48   ///< The icon size in the page selection widget

#include <QDialog>
#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "configdialog.h"

#include "DsoConfigAnalysisPage.h"
#include "DsoConfigColorsPage.h"
#include "DsoConfigFilesPage.h"
#include "DsoConfigScopePage.h"

#include "settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>

/// \brief "Tela": grid contrast, trace interpolation, digital phosphor, cursor table side
class DsoConfigScreenPage : public QWidget {
  public:
    explicit DsoConfigScreenPage(DsoSettings *settings, QWidget *parent = nullptr) : QWidget(parent), settings(settings) {
        QVBoxLayout *main = new QVBoxLayout(this);

        QGroupBox *gridBox = new QGroupBox(QStringLiteral("Grade"));
        QFormLayout *gf = new QFormLayout(gridBox);
        gridCombo = new QComboBox();
        gridCombo->addItems({QStringLiteral("Normal"), QString::fromUtf8("Média"), QStringLiteral("Alta")});
        const int a = settings->view.screen.grid.alpha();
        initialGrid = a <= 0x40 ? 0 : (a <= 0xa0 ? 1 : 2);
        gridCombo->setCurrentIndex(initialGrid);
        gf->addRow(QStringLiteral("Contraste da grade"), gridCombo);
        main->addWidget(gridBox);

        QGroupBox *traceBox = new QGroupBox(QString::fromUtf8("Traço"));
        QFormLayout *tf = new QFormLayout(traceBox);
        interpCombo = new QComboBox();
        interpCombo->addItem(QString::fromUtf8("sen(x)/x (suave, como osciloscópio de bancada)"), Dso::INTERPOLATION_SINC);
        interpCombo->addItem(QStringLiteral("Linear"), Dso::INTERPOLATION_LINEAR);
        interpCombo->addItem(QStringLiteral("Somente pontos"), Dso::INTERPOLATION_OFF);
        interpCombo->setCurrentIndex(interpCombo->findData(settings->view.interpolation));
        tf->addRow(QString::fromUtf8("Interpolação"), interpCombo);
        phosphorDepth = new QSpinBox();
        phosphorDepth->setRange(2, 99);
        phosphorDepth->setValue((int)settings->view.digitalPhosphorDepth);
        phosphorDepth->setSuffix(QString::fromUtf8(" aquisições"));
        phosphorDepth->setToolTip(QString::fromUtf8("Usada quando o fósforo digital está ligado (botão na barra)"));
        tf->addRow(QString::fromUtf8("Persistência do fósforo"), phosphorDepth);
        main->addWidget(traceBox);

        QGroupBox *curBox = new QGroupBox(QStringLiteral("Cursores"));
        QFormLayout *cf = new QFormLayout(curBox);
        cursorSide = new QComboBox();
        cursorSide->addItem(QStringLiteral("Esquerda"), Qt::LeftToolBarArea);
        cursorSide->addItem(QStringLiteral("Direita"), Qt::RightToolBarArea);
        cursorSide->setCurrentIndex(settings->view.cursorGridPosition == Qt::LeftToolBarArea ? 0 : 1);
        cf->addRow(QStringLiteral("Lado da tabela lateral"), cursorSide);
        main->addWidget(curBox);
        main->addStretch(1);
    }

    void saveSettings() {
        static const int gridAlpha[] = {0x3f, 0xa0, 0xff};
        static const int axesAlpha[] = {0x7f, 0xc0, 0xff};
        const int level = gridCombo->currentIndex();
        if (level != initialGrid && level >= 0 && level < 3) {
            settings->view.screen.grid.setAlpha(gridAlpha[level]);
            settings->view.screen.axes.setAlpha(axesAlpha[level]);
            initialGrid = level;
        }
        settings->view.interpolation = (Dso::InterpolationMode)interpCombo->currentData().toInt();
        settings->view.digitalPhosphorDepth = (unsigned)phosphorDepth->value();
        settings->view.cursorGridPosition = (Qt::ToolBarArea)cursorSide->currentData().toInt();
    }

  private:
    DsoSettings *settings;
    QComboBox *gridCombo;
    QComboBox *interpCombo;
    QSpinBox *phosphorDepth;
    QComboBox *cursorSide;
    int initialGrid = 0;
};

////////////////////////////////////////////////////////////////////////////////
// class DsoConfigDialog
/// \brief Creates the configuration dialog and sets initial values.
/// \param settings The target settings object.
/// \param parent The parent widget.
/// \param flags Flags for the window manager.
DsoConfigDialog::DsoConfigDialog(DsoSettings *settings, QWidget *parent, Qt::WindowFlags flags)
    : QDialog(parent, flags), settings(settings) {

    this->setWindowTitle(tr("Configurações"));

    this->contentsWidget = new QListWidget;
    this->contentsWidget->setViewMode(QListView::IconMode);
    this->contentsWidget->setIconSize(QSize(CONFIG_LIST_ICONSIZE, CONFIG_LIST_ICONSIZE));
    this->contentsWidget->setMovement(QListView::Static);
    this->contentsWidget->setGridSize(
        QSize(CONFIG_LIST_WIDTH - 2 * this->contentsWidget->frameWidth(), CONFIG_LIST_ITEMHEIGHT));
    this->contentsWidget->setMaximumWidth(CONFIG_LIST_WIDTH);
    this->contentsWidget->setMinimumWidth(CONFIG_LIST_WIDTH);
    this->contentsWidget->setMinimumHeight(CONFIG_LIST_ITEMHEIGHT * 3 + 2 * (this->contentsWidget->frameWidth()));

    // "Analysis" e "Scope" saíram: janela FFT, referência, interpolação e cursores ficam no painel e nos menus
    this->analysisPage = nullptr;
    this->colorsPage = new DsoConfigColorsPage(settings);
    this->filesPage = new DsoConfigFilesPage(settings);
    this->scopePage = nullptr;
    this->screenPage = new DsoConfigScreenPage(settings);
    this->pagesWidget = new QStackedWidget;
    this->pagesWidget->addWidget(this->screenPage);
    this->pagesWidget->addWidget(this->colorsPage);
    this->pagesWidget->addWidget(this->filesPage);

    this->acceptButton = new QPushButton(tr("&OK"));
    this->acceptButton->setDefault(true);
    this->applyButton = new QPushButton(tr("A&plicar"));
    this->rejectButton = new QPushButton(tr("&Cancelar"));

    this->createIcons();
    this->contentsWidget->setCurrentRow(0);

    this->horizontalLayout = new QHBoxLayout;
    this->horizontalLayout->addWidget(this->contentsWidget);
    this->horizontalLayout->addWidget(this->pagesWidget, 1);

    this->buttonsLayout = new QHBoxLayout;
    this->buttonsLayout->setSpacing(8);
    this->buttonsLayout->addStretch(1);
    this->buttonsLayout->addWidget(this->acceptButton);
    this->buttonsLayout->addWidget(this->applyButton);
    this->buttonsLayout->addWidget(this->rejectButton);

    this->mainLayout = new QVBoxLayout;
    this->mainLayout->addLayout(this->horizontalLayout);
    this->mainLayout->addStretch(1);
    this->mainLayout->addSpacing(8);
    this->mainLayout->addLayout(this->buttonsLayout);
    this->setLayout(this->mainLayout);

    connect(this->acceptButton, &QAbstractButton::clicked, this, &DsoConfigDialog::accept);
    connect(this->applyButton, &QAbstractButton::clicked, this, &DsoConfigDialog::apply);
    connect(this->rejectButton, &QAbstractButton::clicked, this, &QDialog::reject);
}

/// \brief Cleans up the dialog.
DsoConfigDialog::~DsoConfigDialog() {}

/// \brief Create the icons for the pages.
void DsoConfigDialog::createIcons() {
    QListWidgetItem *screenButton = new QListWidgetItem(contentsWidget);
    screenButton->setIcon(QIcon(":config/scope.png"));
    screenButton->setText(tr("Tela"));

    QListWidgetItem *colorsButton = new QListWidgetItem(contentsWidget);
    colorsButton->setIcon(QIcon(":config/colors.png"));
    colorsButton->setText(tr("Cores"));

    QListWidgetItem *filesButton = new QListWidgetItem(contentsWidget);
    filesButton->setIcon(QIcon(":config/files.png"));
    filesButton->setText(tr("Arquivos"));

    connect(contentsWidget, &QListWidget::currentItemChanged, this,
            &DsoConfigDialog::changePage);
}

/// \brief Saves the settings and closes the dialog.
void DsoConfigDialog::accept() {
    this->apply();

    QDialog::accept();
}

/// \brief Saves the settings.
void DsoConfigDialog::apply() {
    if (this->analysisPage) this->analysisPage->saveSettings();
    this->colorsPage->saveSettings();
    this->filesPage->saveSettings();
    if (this->scopePage) this->scopePage->saveSettings();
    this->screenPage->saveSettings();
    emit applied();
}

/// \brief Change the config page.
/// \param current The page that has been selected.
/// \param previous The page that was selected before.
void DsoConfigDialog::changePage(QListWidgetItem *current, QListWidgetItem *previous) {
    if (!current) current = previous;

    pagesWidget->setCurrentIndex(contentsWidget->row(current));
}
