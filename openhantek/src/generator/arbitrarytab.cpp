// SPDX-License-Identifier: GPL-2.0+

#include "arbitrarytab.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "psg9080.h"
#include "style/darkstyle.h"

// ------------------------------------------------------------------------------------------------ WavePreview
WavePreview::WavePreview(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 200);
}

void WavePreview::setWave(const std::vector<double> &values, const QString &caption) {
    wave = values;
    text = caption;
    update();
}

void WavePreview::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    const QRectF r = QRectF(rect()).adjusted(6, 6, -6, -22);
    p.setPen(QPen(QColor(255, 255, 255, 40), 1));
    for (int i = 0; i <= 10; ++i) p.drawLine(QPointF(r.left() + r.width() * i / 10, r.top()),
                                             QPointF(r.left() + r.width() * i / 10, r.bottom()));
    for (int i = 0; i <= 8; ++i) p.drawLine(QPointF(r.left(), r.top() + r.height() * i / 8),
                                            QPointF(r.right(), r.top() + r.height() * i / 8));
    p.setPen(QPen(QColor(255, 255, 255, 90), 1));
    p.drawLine(QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y()));
    if (!wave.empty()) {
        // min/max per pixel column; -1..+1 fills 8 divisions minus a small margin
        const int columns = std::max(1, (int)r.width());
        const size_t n = wave.size();
        auto y = [&](double v) { return r.center().y() - std::max(-1.0, std::min(1.0, v)) * r.height() * 0.47; };
        QPolygonF line;
        line.reserve(2 * columns);
        for (int c = 0; c < columns; ++c) {
            const size_t a = n * c / columns, b = std::min(n, std::max(a + 1, n * (c + 1) / columns));
            const auto mm = std::minmax_element(wave.begin() + a, wave.begin() + b);
            line << QPointF(r.left() + c, y(*mm.second)) << QPointF(r.left() + c, y(*mm.first));
        }
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(darkstyle::orange()), 1.4));
        p.drawPolyline(line);
        p.setRenderHint(QPainter::Antialiasing, false);
    }
    p.setPen(QColor(0x9a, 0xa1, 0xab));
    p.drawText(QRectF(r.left(), r.bottom() + 3, r.width(), 16), Qt::AlignLeft | Qt::AlignVCenter,
               wave.empty() ? tr("Crie a onda com uma fórmula, um arquivo ou lendo uma posição do gerador.") : text);
    if (!wave.empty())
        p.drawText(QRectF(r.left(), r.bottom() + 3, r.width(), 16), Qt::AlignRight | Qt::AlignVCenter,
                   tr("1 período, %1 pontos").arg(wave.size()));
}

// ------------------------------------------------------------------------------------------------ ArbitraryTab
ArbitraryTab::ArbitraryTab(Psg9080 *generator, QWidget *parent) : GeneratorTab(generator, parent) {
    exampleBox = new QComboBox;
    exampleBox->addItem(tr("Exemplos…"));
    for (const arbwave::Example &e : arbwave::examples()) exampleBox->addItem(QString::fromUtf8(e.name), e.formula);
    formulaEdit = new QLineEdit("sin(x) + 0.3*sin(3*x)");
    formulaEdit->setClearButtonEnabled(true);
    QPushButton *generateButton = new QPushButton(tr("Gerar"));
    QHBoxLayout *formulaRow = new QHBoxLayout;
    formulaRow->addWidget(formulaEdit, 1);
    formulaRow->addWidget(generateButton);
    QPushButton *openButton = new QPushButton(tr("Abrir arquivo…"));
    fileLabel = new QLabel;
    fileLabel->setStyleSheet("color: #9aa1ab;");
    fileLabel->setWordWrap(true);
    columnBox = new QComboBox;
    acquisitionBox = psgui::intSpin(1, 1);

    QGroupBox *create = new QGroupBox(tr("Criar a onda"));
    darkstyle::colorSection(create, darkstyle::orange());
    QFormLayout *cf = new QFormLayout(create);
    cf->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    cf->addRow(tr("Exemplo"), exampleBox);
    cf->addRow(tr("Fórmula"), formulaRow);
    cf->addRow(QString(), psgui::hint(tr("t vai de 0 a 1 no período e x = 2πt. Funções: sin cos exp sqrt abs, "
                                         "square(x) tri(x) saw(x) pulse(x, duty) sinc gauss noise() if(c, a, b)… "
                                         "Use ponto decimal: 0.5")));
    cf->addRow(tr("Arquivo"), openButton);
    cf->addRow(QString(), fileLabel);
    cf->addRow(tr("Coluna"), columnBox);
    cf->addRow(tr("Aquisição"), acquisitionBox);
    fileForm = cf;
    psgui::setRowVisible(cf, columnBox, false);
    psgui::setRowVisible(cf, acquisitionBox, false);
    psgui::setRowVisible(cf, fileLabel, false);

    scalingBox = psgui::combo({tr("Normalizar (mínimo e máximo usam os 14 bits)"), tr("Fixa: −1 a +1")});
    invertBox = new QCheckBox(tr("Inverter"));
    QGroupBox *adjust = new QGroupBox(tr("Ajustes"));
    darkstyle::colorSection(adjust, darkstyle::gray());
    QFormLayout *af = new QFormLayout(adjust);
    af->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    af->addRow(tr("Escala"), scalingBox);
    af->addRow(QString(), invertBox);

    slotBox = psgui::intSpin(1, 99);
    slotBox->setValue(QSettings().value("Arbitrary/slot", 1).toInt());
    readButton = new QPushButton(tr("Ler do gerador"));
    writeButton = new QPushButton(tr("Enviar ao gerador"));
    writeButton->setStyleSheet("QPushButton { font-weight: bold; }");
    useButtons[0] = new QPushButton(tr("Usar no CH1"));
    useButtons[1] = new QPushButton(tr("Usar no CH2"));
    saveButton = new QPushButton(tr("Salvar arquivo…"));
    QGroupBox *device = new QGroupBox(tr("Gerador"));
    darkstyle::colorSection(device, darkstyle::blue());
    QGridLayout *dg = new QGridLayout(device);
    dg->addWidget(new QLabel(tr("Posição")), 0, 0);
    dg->addWidget(slotBox, 0, 1);
    dg->addWidget(readButton, 1, 0);
    dg->addWidget(writeButton, 1, 1);
    dg->addWidget(useButtons[0], 2, 0);
    dg->addWidget(useButtons[1], 2, 1);
    dg->addWidget(saveButton, 3, 0, 1, 2);

    QVBoxLayout *left = new QVBoxLayout;
    left->addWidget(create);
    left->addWidget(adjust);
    left->addWidget(device);
    left->addStretch(1);
    QWidget *leftPanel = new QWidget;
    leftPanel->setLayout(left);
    leftPanel->setFixedWidth(400);
    left->setContentsMargins(0, 0, 0, 0);

    preview = new WavePreview;
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(leftPanel);
    layout->addWidget(preview, 1);

    connect(exampleBox, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        if (i <= 0) return;
        formulaEdit->setText(exampleBox->itemData(i).toString());
        exampleBox->setCurrentIndex(0);
        generate();
    });
    connect(generateButton, &QPushButton::clicked, this, &ArbitraryTab::generate);
    connect(formulaEdit, &QLineEdit::returnPressed, this, &ArbitraryTab::generate);
    connect(openButton, &QPushButton::clicked, this, &ArbitraryTab::openFile);
    connect(columnBox, QOverload<int>::of(&QComboBox::activated), this, [this](int) { pickFromTable(); });
    connect(acquisitionBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { pickFromTable(); });
    connect(scalingBox, QOverload<int>::of(&QComboBox::activated), this, [this](int) { updatePreview(); });
    connect(invertBox, &QCheckBox::toggled, this, [this]() { updatePreview(); });
    connect(saveButton, &QPushButton::clicked, this, &ArbitraryTab::saveFile);
    connect(readButton, &QPushButton::clicked, this, &ArbitraryTab::readSlot);
    connect(writeButton, &QPushButton::clicked, this, &ArbitraryTab::writeSlot);
    connect(useButtons[0], &QPushButton::clicked, this, [this]() { useOn(1); });
    connect(useButtons[1], &QPushButton::clicked, this, [this]() { useOn(2); });
    connect(slotBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int v) { QSettings().setValue("Arbitrary/slot", v); });
    connectionChanged(gen->isOpen());
    generate();
}

void ArbitraryTab::connectionChanged(bool open) {
    // the editor works offline; only the generator buttons need the connection
    setEnabled(true);
    for (QPushButton *b : {readButton, writeButton, useButtons[0], useButtons[1]}) b->setEnabled(open);
}

void ArbitraryTab::setSource(const std::vector<double> &values, const QString &name, bool exactCodes) {
    source = (int)values.size() == arbwave::kPoints ? values : arbwave::resample(values);
    sourceName = name;
    if (exactCodes) { // a waveform read from the device or a device file: keep its codes as they are
        QSignalBlocker b1(scalingBox), b2(invertBox);
        scalingBox->setCurrentIndex(1);
        invertBox->setChecked(false);
    }
    updatePreview();
}

std::vector<int> ArbitraryTab::codes() const {
    std::vector<double> v = source;
    if (invertBox->isChecked())
        for (double &x : v) x = -x;
    return arbwave::toCodes(v, scalingBox->currentIndex() == 0 ? arbwave::Scaling::Normalize
                                                                : arbwave::Scaling::Fixed);
}

void ArbitraryTab::updatePreview() {
    if (source.empty()) {
        preview->setWave({}, QString());
        saveButton->setEnabled(false);
        return;
    }
    saveButton->setEnabled(true);
    preview->setWave(arbwave::fromCodes(codes()), sourceName);
}

void ArbitraryTab::generate() {
    std::vector<double> v;
    std::string error;
    if (!arbwave::sampleFormula(formulaEdit->text().toStdString(), v, error)) {
        say(tr("Fórmula: %1").arg(QString::fromStdString(error)), true);
        return;
    }
    for (QWidget *w : {(QWidget *)fileLabel, (QWidget *)columnBox, (QWidget *)acquisitionBox})
        psgui::setRowVisible(fileForm, w, false);
    setSource(v, formulaEdit->text().trimmed());
    say(tr("Onda gerada pela fórmula."));
}

void ArbitraryTab::openFile() {
    QSettings s;
    const QString name = QFileDialog::getOpenFileName(
        this, tr("Abrir forma de onda"), s.value("Arbitrary/dir").toString(),
        tr("Formas de onda (*.txt *.csv *.dat);;Todos os arquivos (*)"));
    if (name.isEmpty()) return;
    s.setValue("Arbitrary/dir", QFileInfo(name).absolutePath());
    QFile f(name);
    if (!f.open(QIODevice::ReadOnly)) {
        say(tr("Não foi possível abrir %1: %2").arg(name, f.errorString()), true);
        return;
    }
    if (f.size() > 64 * 1024 * 1024) {
        say(tr("Arquivo grande demais (máximo 64 MB)."), true);
        return;
    }
    std::string error;
    arbwave::Table t;
    if (!arbwave::parseTable(f.readAll().toStdString(), t, error)) {
        say(tr("%1: %2").arg(QFileInfo(name).fileName(), QString::fromStdString(error)), true);
        return;
    }
    table = std::move(t);
    fileLabel->setText(QFileInfo(name).fileName());
    psgui::setRowVisible(fileForm, fileLabel, true);

    // columns with numbers in every line
    {
        QSignalBlocker b(columnBox);
        columnBox->clear();
        for (size_t c = 0; c < table.columns.size(); ++c) {
            const auto &col = table.columns[c];
            if (!std::all_of(col.begin(), col.end(), [](double v) { return std::isfinite(v); })) continue;
            const QString title = c < table.header.size() ? QString::fromStdString(table.header[c]) : QString();
            columnBox->addItem(title.isEmpty() ? tr("Coluna %1").arg(c + 1) : tr("%1 (coluna %2)").arg(title).arg(c + 1),
                               (int)c);
        }
        columnBox->setCurrentIndex(columnBox->count() - 1); // the value is usually the last column
    }
    const int acquisitions = arbwave::openHantekAcquisitions(table);
    {
        QSignalBlocker b(acquisitionBox);
        acquisitionBox->setRange(1, std::max(1, acquisitions));
        acquisitionBox->setValue(1);
    }
    psgui::setRowVisible(fileForm, acquisitionBox, table.openHantekLog && acquisitions > 1);
    psgui::setRowVisible(fileForm, columnBox, !table.openHantekLog && columnBox->count() > 1);
    pickFromTable();
}

void ArbitraryTab::pickFromTable() {
    std::vector<double> v;
    QString what;
    if (table.openHantekLog) {
        v = arbwave::openHantekAcquisition(table, acquisitionBox->value());
        what = tr("captura do OpenHantek, aquisição %1").arg(acquisitionBox->value());
    } else if (columnBox->count() > 0) {
        v = table.columns[(size_t)columnBox->currentData().toInt()];
        what = columnBox->currentText();
    }
    if (v.size() < 2) {
        say(tr("O arquivo não tem pontos suficientes."), true);
        return;
    }
    const QString name = fileLabel->text();
    if (arbwave::isDeviceFormat(v)) {
        setSource(arbwave::fromCodes(std::vector<int>(v.begin(), v.end())), name, true);
        say(tr("Arquivo no formato do gerador (8192 pontos de 14 bits)."));
    } else if (arbwave::isSixteenBitFormat(v)) {
        std::vector<int> c;
        for (double x : v) c.push_back((int)x >> 2);
        setSource(arbwave::fromCodes(c), name, true);
        say(tr("Arquivo de 16 bits do software original, convertido para 14 bits."));
    } else {
        setSource(v, name + " — " + what);
        say(tr("%1 pontos lidos, reamostrados para 8192.").arg(v.size()));
    }
}

void ArbitraryTab::saveFile() {
    QSettings s;
    const QString name = QFileDialog::getSaveFileName(
        this, tr("Salvar forma de onda"),
        s.value("Arbitrary/dir").toString() + QString("/onda%1.txt").arg(slotBox->value(), 2, 10, QChar('0')),
        tr("Formato do gerador, 8192 linhas (*.txt)"));
    if (name.isEmpty()) return;
    s.setValue("Arbitrary/dir", QFileInfo(name).absolutePath());
    QSaveFile f(name);
    const std::string text = arbwave::deviceFormatText(codes());
    if (!f.open(QIODevice::WriteOnly) || f.write(text.data(), (qint64)text.size()) != (qint64)text.size() ||
        !f.commit()) {
        say(tr("Não foi possível salvar %1: %2").arg(name, f.errorString()), true);
        return;
    }
    say(tr("Onda salva em %1 (8192 valores de 0 a 16383, o formato do PSG9080_ARB).").arg(name));
}

void ArbitraryTab::readSlot() {
    const int slot = slotBox->value();
    QProgressDialog progress(tr("Lendo a onda %1 do gerador…").arg(slot), tr("Cancelar"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    std::vector<int> c;
    const bool ok = gen->readArbitrary(slot, c, [&](int percent) {
        progress.setValue(percent);
        QCoreApplication::processEvents();
        return !progress.wasCanceled();
    });
    progress.reset();
    if (!check(ok)) return;
    for (QWidget *w : {(QWidget *)fileLabel, (QWidget *)columnBox, (QWidget *)acquisitionBox})
        psgui::setRowVisible(fileForm, w, false);
    setSource(arbwave::fromCodes(c), tr("posição %1 do gerador").arg(slot), true);
    say(tr("Onda %1 lida do gerador.").arg(slot));
}

void ArbitraryTab::writeSlot() {
    if (source.empty()) return;
    const int slot = slotBox->value();
    QProgressDialog progress(tr("Enviando a onda para a posição %1…").arg(slot), tr("Cancelar"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    const bool ok = gen->writeArbitrary(slot, codes(), [&](int percent) {
        progress.setValue(percent);
        QCoreApplication::processEvents();
        return !progress.wasCanceled();
    });
    progress.reset();
    if (check(ok)) say(tr("Onda gravada na posição %1 do gerador (Arbitrária %2).").arg(slot).arg(slot, 2, 10, QChar('0')));
}

void ArbitraryTab::useOn(int channel) {
    const int slot = slotBox->value();
    if (check(gen->setWaveform(channel, psg9080::kArbitraryFirst - 1 + slot)))
        say(tr("CH%1 usando a Arbitrária %2.").arg(channel).arg(slot, 2, 10, QChar('0')));
}
