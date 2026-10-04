// SPDX-License-Identifier: GPL-2.0+

#include "datalogger.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "measurementbar.h"
#include "scopesettings.h"
#include "viewconstants.h"

DataLogger::DataLogger(const DsoSettingsScope *scope, const Dso::ControlSpecification *spec, QObject *parent)
    : QObject(parent), scope(scope), spec(spec) {
    cfg.channels.assign(spec->channels, true);
    cfg.folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/OpenHantek";
    loadConfig();
}

DataLogger::~DataLogger() { closeFiles(); }

void DataLogger::loadConfig() {
    QSettings s;
    s.beginGroup("DataLogger");
    for (ChannelID ch = 0; ch < cfg.channels.size(); ++ch)
        cfg.channels[ch] = s.value(QString("ch%1").arg(ch), (bool)cfg.channels[ch]).toBool();
    cfg.content = s.value("content", cfg.content).toInt();
    cfg.intervalSec = s.value("intervalSec", cfg.intervalSec).toDouble();
    cfg.startMode = s.value("startMode", cfg.startMode).toInt();
    cfg.maxRecords = s.value("maxRecords", cfg.maxRecords).toInt();
    cfg.maxMinutes = s.value("maxMinutes", cfg.maxMinutes).toDouble();
    cfg.folder = s.value("folder", cfg.folder).toString();
    cfg.prefix = s.value("prefix", cfg.prefix).toString();
    s.endGroup();
}

void DataLogger::saveConfig() {
    QSettings s;
    s.beginGroup("DataLogger");
    for (ChannelID ch = 0; ch < cfg.channels.size(); ++ch) s.setValue(QString("ch%1").arg(ch), (bool)cfg.channels[ch]);
    s.setValue("content", cfg.content);
    s.setValue("intervalSec", cfg.intervalSec);
    s.setValue("startMode", cfg.startMode);
    s.setValue("maxRecords", cfg.maxRecords);
    s.setValue("maxMinutes", cfg.maxMinutes);
    s.setValue("folder", cfg.folder);
    s.setValue("prefix", cfg.prefix);
    s.endGroup();
}

// Brazilian spreadsheet friendly: decimal comma, ';' separator, no thousand separators
QString DataLogger::num(double v, int prec) const {
    if (!std::isfinite(v)) return QString();
    static QLocale br = [] {
        QLocale l(QLocale::Portuguese, QLocale::Brazil);
        l.setNumberOptions(QLocale::OmitGroupSeparator);
        return l;
    }();
    // between quotes: the decimal comma never splits the value, even if the spreadsheet import has
    // "comma" ticked as a separator; quoted numbers are still read as numbers
    return QStringLiteral("\"") + br.toString(v, 'g', prec) + QStringLiteral("\"");
}

void DataLogger::start() {
    if (m_state != State::Idle) return;
    lastError.clear();
    bool any = false;
    for (bool b : cfg.channels) any = any || b;
    if (!any) {
        lastError = tr("Nenhum canal selecionado");
        emit stateChanged();
        return;
    }
    records = 0;
    if (cfg.startMode == TRIGGER) {
        m_state = State::Armed;
    } else {
        if (!openFiles()) {
            emit stateChanged();
            return;
        }
        startTime = QDateTime::currentDateTime();
        lastRecord = QDateTime();
        m_state = State::Recording;
    }
    emit stateChanged();
}

void DataLogger::stop() {
    if (m_state == State::Idle) return;
    closeFiles();
    m_state = State::Idle;
    emit stateChanged();
}

bool DataLogger::openFiles() {
    closeFiles();
    QDir dir(cfg.folder);
    if (!dir.exists() && !QDir().mkpath(cfg.folder)) {
        lastError = tr("Não foi possível criar a pasta %1").arg(cfg.folder);
        return false;
    }
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QStringList names = {QStringLiteral("Vpp"), QStringLiteral("Vmax"), QStringLiteral("Vmin"),
                               QString::fromUtf8("Média"), QStringLiteral("RMS"), QStringLiteral("RMS AC")};
    files.assign(spec->channels, nullptr);
    streams.assign(spec->channels, nullptr);
    fileNames.assign(spec->channels, QString());
    for (ChannelID ch = 0; ch < spec->channels; ++ch) {
        if (ch >= cfg.channels.size() || !cfg.channels[ch]) continue;
        const QString name = dir.filePath(QString("%1_CH%2_%3.csv").arg(cfg.prefix.isEmpty() ? "registro" : cfg.prefix).arg(ch + 1).arg(stamp));
        QFile *f = new QFile(name);
        if (!f->open(QIODevice::WriteOnly | QIODevice::Text)) {
            lastError = tr("Não foi possível criar %1").arg(name);
            delete f;
            closeFiles();
            return false;
        }
        QTextStream *t = new QTextStream(f);
        t->setCodec("UTF-8");
        t->setGenerateByteOrderMark(true); // Excel / LibreOffice open the accents correctly
        const DsoSettingsScopeVoltage &v = scope->voltage[ch];
        const QString u = scope->unitSymbol(ch);
        const QString sensor =
            v.sensor < probeSensors().size() ? QString::fromUtf8(probeSensors()[v.sensor].label) : QString("x%1").arg(v.probe);
        *t << "# OpenHantek DSO-2250 - registro do CH" << (ch + 1) << "\n";
        *t << "# inicio:;" << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << ";ponteira/garra:;" << sensor
           << ";unidade:;" << u << ";" << u << "/div:;" << num(scope->gain(ch), 4) << "\n";
        if (cfg.content == MEASUREMENTS) {
            *t << "data_hora";
            for (int i = 0; i < names.size(); ++i) {
                QString n = names[i];
                if (u == "A" && n.startsWith('V')) n[0] = 'I';
                *t << ";" << n << " (" << u << ")";
            }
            *t << QString::fromUtf8(";Frequência (Hz);Período (s);Ciclo ativo (%)\n");
        } else {
            *t << QString::fromUtf8("aquisicao;data_hora;t (s);valor (%1)\n").arg(u);
        }
        t->flush();
        files[ch] = f;
        streams[ch] = t;
        fileNames[ch] = name;
    }
    return true;
}

void DataLogger::closeFiles() {
    for (size_t i = 0; i < files.size(); ++i) {
        if (streams[i]) {
            streams[i]->flush();
            delete streams[i];
        }
        if (files[i]) {
            files[i]->close();
            delete files[i];
        }
    }
    files.clear();
    streams.clear();
}

/// The trigger source crosses the trigger level with the selected slope anywhere in the record
bool DataLogger::triggerCrossed(const PPresult *data) const {
    if (scope->trigger.special) return true; // EXT: every acquisition that arrives was triggered (use NORMAL mode)
    const ChannelID src = scope->trigger.source;
    if (src >= data->channelCount() || !data->data(src)) return false;
    const std::vector<double> &s = data->data(src)->voltage.sample;
    const double level = scope->voltage[src].trigger;
    const bool rising = scope->trigger.slope == Dso::Slope::Positive;
    for (size_t i = 1; i < s.size(); ++i) {
        if (rising ? (s[i - 1] < level && s[i] >= level) : (s[i - 1] > level && s[i] <= level)) return true;
    }
    return false;
}

void DataLogger::process(std::shared_ptr<PPresult> data) {
    if (m_state == State::Idle || !data) return;
    const QDateTime now = QDateTime::currentDateTime();

    if (m_state == State::Armed) {
        if (!triggerCrossed(data.get())) return;
        if (!openFiles()) {
            m_state = State::Idle;
            emit stateChanged();
            return;
        }
        startTime = now;
        lastRecord = QDateTime();
        m_state = State::Recording;
        emit stateChanged();
    }

    if (lastRecord.isValid() && cfg.intervalSec > 0 && lastRecord.msecsTo(now) < (qint64)(cfg.intervalSec * 1000.0))
        return;
    writeRecord(data.get(), now);
    lastRecord = now;
    ++records;

    if ((cfg.maxRecords > 0 && records >= cfg.maxRecords) ||
        (cfg.maxMinutes > 0 && startTime.msecsTo(now) >= (qint64)(cfg.maxMinutes * 60000.0)))
        stop();
}

void DataLogger::writeRecord(const PPresult *data, const QDateTime &now) {
    const QString stamp = now.toString("yyyy-MM-dd HH:mm:ss.zzz");
    for (ChannelID ch = 0; ch < streams.size(); ++ch) {
        QTextStream *t = streams[ch];
        if (!t || ch >= data->channelCount() || !data->data(ch)) continue;
        const DataChannel *d = data->data(ch);
        const std::vector<double> &s = d->voltage.sample;
        if (s.empty()) continue;
        if (cfg.content == MEASUREMENTS) {
            const MeasurementBar::Result r = MeasurementBar::analyze(s, d->voltage.interval, d->frequency);
            *t << stamp;
            const int order[] = {MeasurementBar::VPP,  MeasurementBar::VMAX,      MeasurementBar::VMIN,
                                 MeasurementBar::MEAN, MeasurementBar::RMS,       MeasurementBar::ACRMS,
                                 MeasurementBar::FREQUENCY, MeasurementBar::PERIOD};
            for (int m : order) *t << ";" << (r.valid ? num(r.v[m]) : QString());
            *t << ";" << (r.valid ? num(r.v[MeasurementBar::DUTY] * 100.0, 4) : QString()) << "\n";
        } else {
            // the part of the record that is on the screen (the full record can be 1 M samples)
            const double interval = d->voltage.interval;
            size_t count = s.size();
            if (interval > 0)
                count = std::min(count, (size_t)std::ceil(DIVS_TIME * scope->horizontal.timebase / interval) + 1);
            for (size_t i = 0; i < count; ++i)
                *t << (records + 1) << ";" << stamp << ";" << num(i * interval, 9) << ";" << num(s[i], 6) << "\n";
        }
        t->flush();
    }
}

QString DataLogger::statusText() const {
    if (!lastError.isEmpty() && m_state == State::Idle) return tr("Erro: %1").arg(lastError);
    switch (m_state) {
    case State::Idle: return tr("Parado");
    case State::Armed: return tr("Armado: esperando o sinal da fonte do trigger cruzar o nível...");
    case State::Recording: {
        const qint64 s = startTime.secsTo(QDateTime::currentDateTime());
        QString t = tr("Gravando: %1 registros, %2:%3:%4")
                        .arg(records)
                        .arg(s / 3600, 2, 10, QChar('0'))
                        .arg((s / 60) % 60, 2, 10, QChar('0'))
                        .arg(s % 60, 2, 10, QChar('0'));
        for (const QString &n : fileNames)
            if (!n.isEmpty()) t += "\n" + n;
        return t;
    }
    }
    return QString();
}

// ---------------------------------------------------------------- configuration window
void DataLogger::showDialog(QWidget *parent) {
    if (dialog) {
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    dialog = new QDialog(parent);
    dialog->setWindowTitle(tr("Registro de dados (log)"));
    QVBoxLayout *main = new QVBoxLayout(dialog);

    // Canais e conteúdo
    QGroupBox *what = new QGroupBox(tr("O que gravar"));
    QFormLayout *wf = new QFormLayout(what);
    QHBoxLayout *chRow = new QHBoxLayout();
    std::vector<QCheckBox *> chBoxes;
    for (ChannelID ch = 0; ch < spec->channels; ++ch) {
        QCheckBox *c = new QCheckBox(QString("CH%1").arg(ch + 1));
        c->setChecked(ch < cfg.channels.size() && cfg.channels[ch]);
        chRow->addWidget(c);
        chBoxes.push_back(c);
    }
    chRow->addStretch(1);
    wf->addRow(tr("Canais (um arquivo cada)"), chRow);
    QRadioButton *rMeas = new QRadioButton(tr("Medições: uma linha por aquisição (Vpp, máx, mín, média, RMS, frequência...)"));
    QRadioButton *rWave = new QRadioButton(tr("Forma de onda: as amostras que estão na tela, a cada aquisição"));
    (cfg.content == WAVEFORM ? rWave : rMeas)->setChecked(true);
    QVBoxLayout *cv = new QVBoxLayout();
    cv->addWidget(rMeas);
    cv->addWidget(rWave);
    wf->addRow(tr("Conteúdo"), cv);
    QDoubleSpinBox *interval = new QDoubleSpinBox();
    interval->setRange(0, 86400);
    interval->setDecimals(1);
    interval->setSuffix(" s");
    interval->setSpecialValueText(tr("toda aquisição"));
    interval->setValue(cfg.intervalSec);
    wf->addRow(tr("Intervalo mínimo"), interval);
    main->addWidget(what);

    // Início e fim
    QGroupBox *when = new QGroupBox(tr("Início e fim"));
    QFormLayout *ff = new QFormLayout(when);
    QRadioButton *rManual = new QRadioButton(tr("Manual (botão Iniciar ou REG na barra)"));
    QRadioButton *rTrig = new QRadioButton(tr("Por trigger: arma e começa quando a fonte do trigger cruzar o nível"));
    (cfg.startMode == TRIGGER ? rTrig : rManual)->setChecked(true);
    QVBoxLayout *sv = new QVBoxLayout();
    sv->addWidget(rManual);
    sv->addWidget(rTrig);
    ff->addRow(tr("Início"), sv);
    QSpinBox *maxRec = new QSpinBox();
    maxRec->setRange(0, 100000000);
    maxRec->setSpecialValueText(tr("sem limite"));
    maxRec->setValue(cfg.maxRecords);
    ff->addRow(tr("Parar após (registros)"), maxRec);
    QDoubleSpinBox *maxMin = new QDoubleSpinBox();
    maxMin->setRange(0, 100000);
    maxMin->setDecimals(1);
    maxMin->setSuffix(" min");
    maxMin->setSpecialValueText(tr("sem limite"));
    maxMin->setValue(cfg.maxMinutes);
    ff->addRow(tr("Parar após (tempo)"), maxMin);
    main->addWidget(when);

    // Arquivos
    QGroupBox *where = new QGroupBox(tr("Arquivos (CSV: separador ';' e vírgula decimal)"));
    QFormLayout *pf = new QFormLayout(where);
    QHBoxLayout *folderRow = new QHBoxLayout();
    QLineEdit *folder = new QLineEdit(cfg.folder);
    QPushButton *browse = new QPushButton(tr("..."));
    folderRow->addWidget(folder, 1);
    folderRow->addWidget(browse);
    pf->addRow(tr("Pasta"), folderRow);
    QLineEdit *prefix = new QLineEdit(cfg.prefix);
    pf->addRow(tr("Nome (prefixo)"), prefix);
    QLabel *example = new QLabel();
    example->setStyleSheet("QLabel { color: gray; }");
    pf->addRow(QString(), example);
    main->addWidget(where);

    statusLabel = new QLabel();
    statusLabel->setWordWrap(true);
    statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    main->addWidget(statusLabel);

    QHBoxLayout *buttons = new QHBoxLayout();
    startButton = new QPushButton(tr("Iniciar"));
    stopButton = new QPushButton(tr("Parar"));
    QPushButton *openFolder = new QPushButton(tr("Abrir pasta"));
    QPushButton *close = new QPushButton(tr("Fechar"));
    buttons->addWidget(startButton);
    buttons->addWidget(stopButton);
    buttons->addWidget(openFolder);
    buttons->addStretch(1);
    buttons->addWidget(close);
    main->addLayout(buttons);

    // read the widgets into the configuration
    auto collect = [=]() {
        for (ChannelID ch = 0; ch < chBoxes.size(); ++ch) cfg.channels[ch] = chBoxes[ch]->isChecked();
        cfg.content = rWave->isChecked() ? WAVEFORM : MEASUREMENTS;
        cfg.intervalSec = interval->value();
        cfg.startMode = rTrig->isChecked() ? TRIGGER : MANUAL;
        cfg.maxRecords = maxRec->value();
        cfg.maxMinutes = maxMin->value();
        cfg.folder = folder->text().trimmed();
        cfg.prefix = prefix->text().trimmed();
        saveConfig();
        example->setText(tr("ex.: %1_CH1_%2.csv")
                             .arg(cfg.prefix.isEmpty() ? "registro" : cfg.prefix)
                             .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")));
    };
    auto refreshUi = [=]() {
        const bool idle = m_state == State::Idle;
        startButton->setEnabled(idle);
        stopButton->setEnabled(!idle);
        startButton->setText(rTrig->isChecked() ? tr("Armar") : tr("Iniciar"));
        what->setEnabled(idle);
        when->setEnabled(idle);
        where->setEnabled(idle);
        statusLabel->setText(statusText());
    };
    collect();
    refreshUi();

    for (QCheckBox *c : chBoxes) connect(c, &QCheckBox::toggled, dialog, collect);
    connect(rWave, &QRadioButton::toggled, dialog, collect);
    connect(rTrig, &QRadioButton::toggled, dialog, [=]() {
        collect();
        refreshUi();
    });
    connect(interval, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), dialog, collect);
    connect(maxRec, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), dialog, collect);
    connect(maxMin, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), dialog, collect);
    connect(folder, &QLineEdit::editingFinished, dialog, collect);
    connect(prefix, &QLineEdit::textChanged, dialog, collect);
    connect(browse, &QPushButton::clicked, dialog, [=]() {
        const QString d = QFileDialog::getExistingDirectory(dialog, tr("Pasta dos registros"), folder->text());
        if (!d.isEmpty()) {
            folder->setText(d);
            collect();
        }
    });
    connect(startButton, &QPushButton::clicked, dialog, [=]() {
        collect();
        start();
    });
    connect(stopButton, &QPushButton::clicked, dialog, [=]() { stop(); });
    connect(openFolder, &QPushButton::clicked, dialog, [=]() {
        QDir().mkpath(cfg.folder);
        QDesktopServices::openUrl(QUrl::fromLocalFile(cfg.folder));
    });
    connect(close, &QPushButton::clicked, dialog, &QDialog::hide);
    connect(this, &DataLogger::stateChanged, dialog, refreshUi);
    QTimer *timer = new QTimer(dialog);
    connect(timer, &QTimer::timeout, dialog, [=]() {
        if (dialog->isVisible()) statusLabel->setText(statusText());
    });
    timer->start(500);

    dialog->show();
}
