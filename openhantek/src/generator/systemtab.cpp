// SPDX-License-Identifier: GPL-2.0+

#include "systemtab.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "psg9080.h"
#include "style/darkstyle.h"

using namespace psg9080;

namespace {
/// What each register holds, for the diagnosis table.
QString registerName(int code) {
    switch (code) {
    case 0: return QObject::tr("modelo");
    case 1: return QObject::tr("número de série");
    case 2: return QObject::tr("versões hw, fw, FPGA");
    case 10: return QObject::tr("saídas CH1, CH2");
    case 11: case 12: return QObject::tr("forma de onda CH%1").arg(code - 10);
    case 13: case 14: return QObject::tr("frequência CH%1 (passos, unidade)").arg(code - 12);
    case 15: case 16: return QObject::tr("amplitude CH%1 (mVpp)").arg(code - 14);
    case 17: case 18: return QObject::tr("offset CH%1 (10 mV, 1000 = 0 V)").arg(code - 16);
    case 19: case 20: return QObject::tr("duty CH%1 (0,01 %)").arg(code - 18);
    case 21: case 22: return QObject::tr("fase CH%1 (0,01°)").arg(code - 20);
    case 24: return QObject::tr("tela do gerador");
    case 25: return QObject::tr("sincronismo");
    case 26: return QObject::tr("memória");
    case 27: return QObject::tr("som");
    case 28: return QObject::tr("brilho (%)");
    case 29: return QObject::tr("idioma");
    case 30: return QObject::tr("nº de ondas internas");
    case 31: return QObject::tr("nº de ondas arbitrárias");
    case 32: return QObject::tr("carregamento de ondas");
    case 33: return QObject::tr("ajuste fino de frequência");
    case 40: return QObject::tr("tipo de modulação CH1, CH2");
    case 41: return QObject::tr("onda moduladora CH1, CH2");
    case 42: return QObject::tr("fonte da modulação CH1, CH2");
    case 43: case 44: return QObject::tr("frequência moduladora CH%1 (mHz)").arg(code - 42);
    case 45: case 46: return QObject::tr("profundidade AM CH%1 (0,1 %)").arg(code - 44);
    case 47: case 48: return QObject::tr("desvio FM CH%1 (0,1 Hz)").arg(code - 46);
    case 49: case 50: return QObject::tr("salto FSK CH%1 (0,1 Hz)").arg(code - 48);
    case 51: case 52: return QObject::tr("desvio PM CH%1 (0,1°)").arg(code - 50);
    case 53: case 54: return QObject::tr("largura do pulso CH%1 (ns)").arg(code - 52);
    case 55: case 56: return QObject::tr("período do pulso CH%1 (10 ns)").arg(code - 54);
    case 57: return QObject::tr("inversão do pulso CH1, CH2");
    case 58: return QObject::tr("repouso do burst CH1, CH2");
    case 59: return QObject::tr("polaridade CH1, CH2");
    case 60: return QObject::tr("fonte de disparo CH1, CH2");
    case 61: return QObject::tr("ciclos do burst CH1, CH2");
    case 62: return QObject::tr("medição: acoplamento, porta (ms), faixa");
    case 63: return QObject::tr("medição/contador");
    case 64: return QObject::tr("varredura: canal, tempo, sentido, escala");
    case 65: return QObject::tr("varredura, VCO ligados");
    case 66: case 67: return QObject::tr("frequência %1 da varredura (0,1 Hz)").arg(code == 66 ? "inicial" : "final");
    case 68: case 69: return QObject::tr("amplitude %1 da varredura (mVpp)").arg(code == 68 ? "inicial" : "final");
    case 70: case 71: return QObject::tr("duty %1 da varredura (0,01 %)").arg(code == 70 ? "inicial" : "final");
    case 72: return QObject::tr("calibração do VCO, mínimo");
    case 73: return QObject::tr("calibração do VCO, máximo");
    case 74: return QObject::tr("disparo CH1, CH2");
    case 80: return QObject::tr("contador");
    case 81: return QObject::tr("frequência medida (Hz)");
    case 82: return QObject::tr("frequência medida (mHz)");
    case 83: return QObject::tr("largura + medida (ns)");
    case 84: return QObject::tr("largura − medida (ns)");
    case 85: return QObject::tr("período medido (10 ns)");
    case 86: return QObject::tr("duty medido (0,01 %)");
    default: return QString();
    }
}

QString join(const std::vector<std::string> &fields) {
    QStringList l;
    for (const std::string &f : fields) l << QString::fromStdString(f);
    return l.join(", ");
}
} // namespace

SystemTab::SystemTab(Psg9080 *generator, QWidget *parent) : GeneratorTab(generator, parent) {
    // Device
    model = new QLabel(QString::fromUtf8("—"));
    serial = new QLabel(QString::fromUtf8("—"));
    versions = new QLabel(QString::fromUtf8("—"));
    for (QLabel *l : {model, serial, versions}) l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QGroupBox *device = new QGroupBox(tr("Aparelho"));
    darkstyle::colorSection(device, darkstyle::gray());
    QFormLayout *df = new QFormLayout(device);
    df->addRow(tr("Modelo"), model);
    df->addRow(tr("Número de série"), serial);
    df->addRow(tr("Versões"), versions);

    // Synchronization
    QGroupBox *syncBox = new QGroupBox(tr("Sincronismo: o CH2 acompanha o CH1"));
    darkstyle::colorSection(syncBox, darkstyle::blue());
    QGridLayout *sg = new QGridLayout(syncBox);
    const char *names[6] = {"Forma de onda", "Frequência", "Amplitude", "Offset", "Duty cycle", "Externo"};
    for (int i = 0; i < 6; ++i) {
        sync[i] = new QCheckBox(tr(names[i]));
        sg->addWidget(sync[i], i / 3, i % 3);
        connect(sync[i], &QCheckBox::clicked, this, &SystemTab::writeSync);
    }
    trimBox = psgui::intSpin(0, 99);
    trimBox->setToolTip(tr("Ajuste fino para alinhar as formas de onda com a frequência sincronizada (0 a 99)"));
    QHBoxLayout *trimRow = new QHBoxLayout;
    trimRow->addWidget(new QLabel(tr("Ajuste fino de frequência")));
    trimRow->addWidget(trimBox);
    trimRow->addStretch(1);
    sg->addLayout(trimRow, 2, 0, 1, 3);
    connect(trimBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) { check(gen->setInteger(REG_FREQUENCY_TRIM, v)); });

    // Memory
    slotBox = psgui::intSpin(0, 99);
    slotBox->setToolTip(tr("A posição 00 é carregada quando o gerador liga"));
    QPushButton *loadButton = new QPushButton(tr("Carregar"));
    QPushButton *saveButton = new QPushButton(tr("Salvar"));
    QPushButton *clearButton = new QPushButton(tr("Apagar"));
    QPushButton *clearAllButton = new QPushButton(tr("Apagar todas…"));
    clearAllButton->setStyleSheet("QPushButton { color: #ff6b5b; }");
    QGroupBox *memoryBox = new QGroupBox(tr("Memórias do gerador (00 a 99)"));
    darkstyle::colorSection(memoryBox, darkstyle::violet());
    QHBoxLayout *mr = new QHBoxLayout(memoryBox);
    mr->addWidget(new QLabel(tr("Posição")));
    mr->addWidget(slotBox);
    for (QPushButton *b : {loadButton, saveButton, clearButton, clearAllButton}) mr->addWidget(b);
    connect(loadButton, &QPushButton::clicked, this, [this]() { memory((int)MemoryOp::Load); });
    connect(saveButton, &QPushButton::clicked, this, [this]() { memory((int)MemoryOp::Save); });
    connect(clearButton, &QPushButton::clicked, this, [this]() { memory((int)MemoryOp::Clear); });
    connect(clearAllButton, &QPushButton::clicked, this, [this]() { memory((int)MemoryOp::ClearAll); });

    // Display and sound
    brightnessBox = psgui::intSpin(0, 100, " %");
    soundBox = new QCheckBox(tr("Bipe das teclas"));
    languageBox = psgui::combo({tr("Inglês"), tr("Chinês")});
    loadingBox = psgui::combo({tr("Automático"), tr("Rápido")});
    QGroupBox *panelBox = new QGroupBox(tr("Tela e som"));
    darkstyle::colorSection(panelBox, darkstyle::orange());
    QFormLayout *pf = new QFormLayout(panelBox);
    pf->addRow(tr("Brilho"), brightnessBox);
    pf->addRow(QString(), soundBox);
    pf->addRow(tr("Idioma"), languageBox);
    pf->addRow(tr("Carregamento de ondas"), loadingBox);
    connect(brightnessBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) { check(gen->setInteger(REG_BRIGHTNESS, v)); });
    connect(soundBox, &QCheckBox::clicked, this, [this](bool on) { check(gen->setInteger(REG_SOUND, on)); });
    connect(languageBox, QOverload<int>::of(&QComboBox::activated), this,
            [this](int i) { check(gen->setInteger(REG_LANGUAGE, i)); });
    connect(loadingBox, QOverload<int>::of(&QComboBox::activated), this,
            [this](int i) { check(gen->setInteger(REG_WAVE_LOADING, i)); });

    // Registers
    registers = new QTableWidget(0, 3);
    registers->setHorizontalHeaderLabels({tr("Nº"), tr("Valor"), tr("Significado")});
    registers->verticalHeader()->hide();
    registers->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    registers->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    registers->horizontalHeader()->setStretchLastSection(true);
    registers->verticalHeader()->setDefaultSectionSize(22);
    registers->setEditTriggers(QAbstractItemView::NoEditTriggers);
    registers->setStyleSheet("QTableWidget { background: #11141a; color: #f0f2f5; gridline-color: #2c3139;"
                             "  border: 1px solid #3a404a; }"
                             "QHeaderView::section { background: #2b3038; color: #c8ccd4; border: none; padding: 3px; }");
    QPushButton *readAllButton = new QPushButton(tr("Ler todos"));
    QGroupBox *regBox = new QGroupBox(tr("Registradores (diagnóstico)"));
    darkstyle::colorSection(regBox, darkstyle::gray());
    QVBoxLayout *rl = new QVBoxLayout(regBox);
    rl->addWidget(readAllButton);
    rl->addWidget(registers, 1);
    rl->addWidget(psgui::hint(tr("Em amarelo, o que mudou desde a última leitura: mude algo no painel do gerador "
                                 "e leia de novo para ver qual registrador corresponde.")));
    connect(readAllButton, &QPushButton::clicked, this, &SystemTab::readRegisters);

    QVBoxLayout *left = new QVBoxLayout;
    left->addWidget(device);
    left->addWidget(syncBox);
    left->addWidget(memoryBox);
    left->addWidget(panelBox);
    left->addStretch(1);
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(left, 3);
    layout->addWidget(regBox, 2);
}

void SystemTab::writeSync() {
    std::vector<std::string> f;
    for (QCheckBox *c : sync) f.push_back(c->isChecked() ? "1" : "0");
    if (!check(gen->writeRaw(REG_SYNC, f))) refresh();
}

void SystemTab::memory(int op) {
    const int slot = slotBox->value();
    const QString what = op == (int)MemoryOp::Load    ? tr("carregar a posição %1").arg(slot, 2, 10, QChar('0'))
                         : op == (int)MemoryOp::Save  ? tr("salvar a configuração atual na posição %1").arg(slot, 2, 10, QChar('0'))
                         : op == (int)MemoryOp::Clear ? tr("apagar a posição %1").arg(slot, 2, 10, QChar('0'))
                                                      : tr("apagar TODAS as 100 posições de memória");
    if (op != (int)MemoryOp::Load &&
        QMessageBox::question(this, tr("Memórias do gerador"), tr("Deseja %1?").arg(what)) != QMessageBox::Yes)
        return;
    if (!check(gen->memory(op == (int)MemoryOp::ClearAll ? 0 : slot, (MemoryOp)op))) return;
    say(tr("Feito: %1.").arg(what));
    if (op == (int)MemoryOp::Load) {
        // every view reads the device again
        emit gen->channelWritten(1);
        emit gen->channelWritten(2);
    }
}

void SystemTab::refresh() {
    if (!gen->isOpen()) return;
    std::map<int, std::vector<std::string>> regs;
    if (!check(gen->readAll(regs, 33))) return;
    auto field = [&](int code, size_t i) -> QString {
        auto it = regs.find(code);
        return it != regs.end() && it->second.size() > i ? QString::fromStdString(it->second[i]) : QString();
    };
    model->setText(field(0, 0).isEmpty() ? QString::fromUtf8("—") : "PSG90" + field(0, 0));
    serial->setText(field(1, 0).isEmpty() ? QString::fromUtf8("—") : field(1, 0));
    std::vector<std::string> v;
    if (regs.count(2) && decodeVersions(regs[2], v) && v.size() >= 3)
        versions->setText(tr("hardware %1 · firmware %2 · FPGA %3")
                              .arg(QString::fromStdString(v[0]), QString::fromStdString(v[1]), QString::fromStdString(v[2])));
    for (int i = 0; i < 6; ++i) {
        QSignalBlocker b(sync[i]);
        sync[i]->setChecked(field(REG_SYNC, i).toInt() != 0);
    }
    QSignalBlocker b1(trimBox), b2(brightnessBox), b3(soundBox), b4(languageBox), b5(loadingBox);
    trimBox->setValue(field(REG_FREQUENCY_TRIM, 0).toInt());
    // some units answer 101 here; show it limited to the range the device accepts
    brightnessBox->setValue(std::min(100, field(REG_BRIGHTNESS, 0).toInt()));
    soundBox->setChecked(field(REG_SOUND, 0).toInt() != 0);
    languageBox->setCurrentIndex(field(REG_LANGUAGE, 0).toInt() ? 1 : 0);
    loadingBox->setCurrentIndex(field(REG_WAVE_LOADING, 0).toInt() ? 1 : 0);
}

void SystemTab::readRegisters() {
    std::map<int, std::vector<std::string>> regs;
    if (!check(gen->readAll(regs))) return;
    registers->setRowCount(0);
    int changed = 0;
    for (const auto &r : regs) {
        const int row = registers->rowCount();
        registers->insertRow(row);
        QTableWidgetItem *code = new QTableWidgetItem(QString("%1").arg(r.first, 2, 10, QChar('0')));
        QTableWidgetItem *value = new QTableWidgetItem(join(r.second));
        QTableWidgetItem *name = new QTableWidgetItem(registerName(r.first));
        const auto old = lastRegisters.find(r.first);
        if (!lastRegisters.empty() && (old == lastRegisters.end() || old->second != r.second)) {
            ++changed;
            for (QTableWidgetItem *i : {code, value, name}) i->setForeground(QColor("#f2e600"));
        }
        registers->setItem(row, 0, code);
        registers->setItem(row, 1, value);
        registers->setItem(row, 2, name);
    }
    say(lastRegisters.empty() ? tr("%1 registradores lidos.").arg(regs.size())
                              : tr("%1 registradores lidos, %2 mudaram.").arg(regs.size()).arg(changed));
    lastRegisters = regs;
}
