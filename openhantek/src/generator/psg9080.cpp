// SPDX-License-Identifier: GPL-2.0+

#include "psg9080.h"

#include <QElapsedTimer>
#include <QSerialPort>

#include <cmath>

using namespace psg9080;

static const int kTimeoutMs = 1000; ///< the device answers in about 5 ms

Psg9080::Psg9080(QObject *parent) : QObject(parent), port(new QSerialPort(this)) {}

Psg9080::~Psg9080() { close(); }

bool Psg9080::isOpen() const { return port->isOpen(); }

QString Psg9080::portName() const { return port->portName(); }

bool Psg9080::fail(const QString &message) {
    error = message;
    return false;
}

bool Psg9080::open(const QString &portName) {
    close();
    port->setPortName(portName);
    port->setBaudRate(QSerialPort::Baud115200);
    port->setDataBits(QSerialPort::Data8);
    port->setParity(QSerialPort::NoParity);
    port->setStopBits(QSerialPort::OneStop);
    port->setFlowControl(QSerialPort::NoFlowControl);
    if (!port->open(QIODevice::ReadWrite)) {
        const QString reason = port->errorString();
        if (port->error() == QSerialPort::PermissionError)
            return fail(tr("Sem permissão para abrir %1. Adicione o usuário ao grupo dialout "
                           "(sudo usermod -aG dialout $USER) e entre de novo na sessão.")
                            .arg(portName));
        return fail(tr("Não foi possível abrir %1: %2").arg(portName, reason));
    }
    bool a = false, b = false;
    if (!readOutputs(a, b)) {
        const QString reason = error;
        port->close();
        return fail(tr("%1 não respondeu como um PSG9080 (%2).").arg(portName, reason));
    }
    error.clear();
    emit connectionChanged(true);
    return true;
}

void Psg9080::close() {
    if (!port->isOpen()) return;
    port->close();
    emit connectionChanged(false);
}

bool Psg9080::exchange(const std::string &command, std::string &answer) {
    if (!port->isOpen()) return fail(tr("Gerador não conectado."));
    // Drop anything left from an earlier command (a late answer after a timeout, power-up noise)
    port->clear(QSerialPort::Input);
    port->readAll();
    const QByteArray bytes(command.data(), (int)command.size());
    if (port->write(bytes) != bytes.size() || !port->waitForBytesWritten(kTimeoutMs))
        return fail(tr("Falha ao enviar o comando ao gerador: %1").arg(port->errorString()));

    QByteArray buffer;
    QElapsedTimer timer;
    timer.start();
    while (!buffer.contains("\r\n")) {
        const qint64 left = kTimeoutMs - timer.elapsed();
        if (left <= 0 || !port->waitForReadyRead((int)left)) {
            if (port->error() == QSerialPort::ResourceError) {
                close();
                return fail(tr("O gerador foi desconectado."));
            }
            return fail(tr("O gerador não respondeu."));
        }
        buffer += port->readAll();
        if (buffer.size() > 4096) return fail(tr("Resposta do gerador muito longa."));
    }
    answer = buffer.left(buffer.indexOf("\r\n")).toStdString();
    return true;
}

bool Psg9080::writeRegister(int code, const std::vector<std::string> &fields) {
    std::string answer;
    if (!exchange(writeCommand(code, fields), answer)) return false;
    if (!isWriteAck(answer))
        return fail(tr("O gerador recusou o comando %1 (resposta: %2).")
                        .arg(QString::fromStdString(writeCommand(code, fields)).trimmed(),
                             QString::fromStdString(answer)));
    return true;
}

bool Psg9080::readRegister(int code, std::vector<std::string> &fields) {
    std::string answer;
    if (!exchange(readCommand(code), answer)) return false;
    if (!parseReadAnswer(answer, code, fields))
        return fail(tr("Resposta inesperada do gerador: %1").arg(QString::fromStdString(answer)));
    return true;
}

bool Psg9080::checkChannel(int channel) {
    if (channel == 1 || channel == 2) return true;
    return fail(tr("Canal do gerador inválido: %1").arg(channel));
}

bool Psg9080::setOutputs(bool ch1, bool ch2) {
    if (!writeRegister(REG_OUTPUTS, {encodeOutputs(ch1, ch2)})) return false;
    emit outputsWritten(ch1, ch2);
    return true;
}

bool Psg9080::setOutput(int channel, bool on) {
    bool a = false, b = false;
    if (!checkChannel(channel) || !readOutputs(a, b)) return false;
    (channel == 1 ? a : b) = on;
    return setOutputs(a, b);
}

bool Psg9080::readOutputs(bool &ch1, bool &ch2) {
    std::vector<std::string> f;
    if (!readRegister(REG_OUTPUTS, f)) return false;
    if (!decodeOutputs(f, ch1, ch2)) return fail(tr("Resposta inválida do estado das saídas."));
    return true;
}

bool Psg9080::setWaveform(int channel, int code) {
    if (!checkChannel(channel)) return false;
    if (!((code >= 0 && code < kBuiltinWaveforms) || (code >= kArbitraryFirst && code <= kArbitraryLast)))
        return fail(tr("Forma de onda inválida: %1").arg(code));
    if (!writeRegister(channelRegister(REG_WAVEFORM, channel), {std::to_string(code)})) return false;
    emit channelWritten(channel);
    return true;
}

bool Psg9080::setFrequency(int channel, double hz) { return setFrequency(channel, hz, bestUnit(hz)); }

bool Psg9080::setFrequency(int channel, double hz, FreqUnit unit) {
    if (!checkChannel(channel)) return false;
    if (!std::isfinite(hz) || hz < 0 || hz > kMaxFrequency)
        return fail(tr("Frequência fora da faixa do gerador (0 a 80 MHz)."));
    std::vector<std::string> f;
    encodeFrequency(hz, unit, f);
    if (!writeRegister(channelRegister(REG_FREQUENCY, channel), f)) return false;
    emit channelWritten(channel);
    return true;
}

bool Psg9080::setAmplitude(int channel, double vpp) {
    if (!checkChannel(channel)) return false;
    if (!std::isfinite(vpp) || vpp < 0 || vpp > kMaxAmplitude)
        return fail(tr("Amplitude fora da faixa (0 a %1 Vpp).").arg(kMaxAmplitude));
    if (!writeRegister(channelRegister(REG_AMPLITUDE, channel), {encodeAmplitude(vpp)})) return false;
    emit channelWritten(channel);
    return true;
}

bool Psg9080::setOffset(int channel, double volts) {
    if (!checkChannel(channel)) return false;
    if (!std::isfinite(volts) || volts < kMinOffset - 1e-9 || volts > kMaxOffset + 1e-9)
        return fail(tr("Offset fora da faixa (-9,99 a +12 V)."));
    if (!writeRegister(channelRegister(REG_OFFSET, channel), {encodeOffset(volts)})) return false;
    emit channelWritten(channel);
    return true;
}

bool Psg9080::setDuty(int channel, double percent) {
    if (!checkChannel(channel)) return false;
    if (!std::isfinite(percent) || percent < 0 || percent > 100) return fail(tr("Duty fora da faixa (0 a 100 %)."));
    if (!writeRegister(channelRegister(REG_DUTY, channel), {encodeHundredths(percent)})) return false;
    emit channelWritten(channel);
    return true;
}

bool Psg9080::setPhase(int channel, double degrees) {
    if (!checkChannel(channel)) return false;
    if (!std::isfinite(degrees) || degrees < 0 || degrees >= 360) return fail(tr("Fase fora da faixa (0 a 359,99°)."));
    if (!writeRegister(channelRegister(REG_PHASE, channel), {encodeHundredths(degrees)})) return false;
    emit channelWritten(channel);
    return true;
}

bool Psg9080::readChannel(int channel, ChannelState &s) {
    if (!checkChannel(channel)) return false;
    std::vector<std::string> f;
    // A communication failure keeps its own message; a malformed answer gets one naming the parameter.
    auto get = [&](Register reg) { return readRegister(channelRegister(reg, channel), f); };
    long long code = 0;
    if (!get(REG_WAVEFORM)) return false;
    if (!decodeInteger(f, code)) return fail(tr("Resposta inválida da forma de onda."));
    s.waveform = (int)code;
    if (!get(REG_FREQUENCY)) return false;
    if (!decodeFrequency(f, s.frequency, s.unit)) return fail(tr("Resposta inválida da frequência."));
    if (!get(REG_AMPLITUDE)) return false;
    if (!decodeAmplitude(f, s.amplitude)) return fail(tr("Resposta inválida da amplitude."));
    if (!get(REG_OFFSET)) return false;
    if (!decodeOffset(f, s.offset)) return fail(tr("Resposta inválida do offset."));
    if (!get(REG_DUTY)) return false;
    if (!decodeHundredths(f, s.duty)) return fail(tr("Resposta inválida do duty."));
    if (!get(REG_PHASE)) return false;
    if (!decodeHundredths(f, s.phase)) return fail(tr("Resposta inválida da fase."));
    bool a = false, b = false;
    if (!readOutputs(a, b)) return false;
    s.enabled = channel == 1 ? a : b;
    return true;
}

// ------------------------------------------------------------------------------------------------ any register
bool Psg9080::writeRaw(int code, const std::vector<std::string> &fields) {
    if (!writeRegister(code, fields)) return false;
    emit registerWritten(code);
    return true;
}

bool Psg9080::readRaw(int code, std::vector<std::string> &fields) { return readRegister(code, fields); }

bool Psg9080::setInteger(int code, long long value) { return writeRaw(code, {std::to_string(value)}); }

bool Psg9080::readInteger(int code, long long &value) {
    std::vector<std::string> f;
    if (!readRegister(code, f)) return false;
    if (!decodeInteger(f, value)) return fail(tr("Resposta inválida do registrador %1.").arg(code));
    return true;
}

bool Psg9080::setScaled(int code, double value, double scale) {
    std::vector<std::string> f;
    if (!encodeScaled(value, scale, f) || value < 0) return fail(tr("Valor inválido."));
    return writeRaw(code, f);
}

bool Psg9080::readScaled(int code, double scale, double &value) {
    std::vector<std::string> f;
    if (!readRegister(code, f)) return false;
    if (!decodeScaled(f, scale, value)) return fail(tr("Resposta inválida do registrador %1.").arg(code));
    return true;
}

bool Psg9080::setChannelScaled(int baseCode, int channel, double value, double scale) {
    return checkChannel(channel) && setScaled(baseCode + channel - 1, value, scale);
}

bool Psg9080::readChannelScaled(int baseCode, int channel, double scale, double &value) {
    return checkChannel(channel) && readScaled(baseCode + channel - 1, scale, value);
}

bool Psg9080::readPair(int code, long long &ch1, long long &ch2) {
    std::vector<std::string> f;
    if (!readRegister(code, f)) return false;
    if (!decodePair(f, ch1, ch2)) return fail(tr("Resposta inválida do registrador %1.").arg(code));
    return true;
}

bool Psg9080::readPairValue(int code, int channel, long long &value) {
    long long a = 0, b = 0;
    if (!checkChannel(channel) || !readPair(code, a, b)) return false;
    value = channel == 1 ? a : b;
    return true;
}

bool Psg9080::setPairValue(int code, int channel, long long value) {
    long long a = 0, b = 0;
    if (!checkChannel(channel) || !readPair(code, a, b)) return false;
    (channel == 1 ? a : b) = value;
    return writeRaw(code, encodePair(a, b));
}

bool Psg9080::setInterface(const Interface &i) { return writeRaw(REG_INTERFACE, encodeInterface(i)); }

bool Psg9080::readInterface(Interface &i) {
    std::vector<std::string> f;
    if (!readRegister(REG_INTERFACE, f)) return false;
    if (!decodeInterface(f, i)) return fail(tr("Resposta inválida da tela do gerador."));
    return true;
}

bool Psg9080::memory(int slot, MemoryOp op) {
    if (slot < 0 || slot > 99) return fail(tr("Posição de memória inválida: %1").arg(slot));
    return writeRaw(REG_MEMORY, {std::to_string(slot), std::to_string((int)op)});
}

bool Psg9080::exchangeLines(const std::string &command, int lines, int timeoutMs, std::vector<std::string> &answer,
                            const std::function<bool(qint64)> &onBytes) {
    if (!port->isOpen()) return fail(tr("Gerador não conectado."));
    port->clear(QSerialPort::Input);
    port->readAll();
    const QByteArray bytes(command.data(), (int)command.size());
    if (port->write(bytes) != bytes.size() || !port->waitForBytesWritten(kTimeoutMs))
        return fail(tr("Falha ao enviar o comando ao gerador: %1").arg(port->errorString()));
    QByteArray buffer;
    QElapsedTimer timer;
    timer.start();
    while (buffer.count("\r\n") < lines) {
        const qint64 left = timeoutMs - timer.elapsed();
        if (left <= 0 || !port->waitForReadyRead((int)std::min<qint64>(left, 500))) {
            if (port->error() == QSerialPort::ResourceError) {
                close();
                return fail(tr("O gerador foi desconectado."));
            }
            if (timer.elapsed() >= timeoutMs) return fail(tr("O gerador não respondeu a tempo."));
            continue;
        }
        buffer += port->readAll();
        if (onBytes && !onBytes(buffer.size())) return fail(tr("Cancelado."));
        if (buffer.size() > 512 * 1024) return fail(tr("Resposta do gerador muito longa."));
    }
    answer.clear();
    for (const QByteArray &line : buffer.split('\n')) {
        QByteArray l = line;
        if (l.endsWith('\r')) l.chop(1);
        if (!l.isEmpty()) answer.push_back(l.toStdString());
    }
    return true;
}

bool Psg9080::readAll(std::map<int, std::vector<std::string>> &registers, int last) {
    std::vector<std::string> lines;
    if (!exchangeLines(readAllCommand(last), last + 1, 4000, lines)) return false;
    registers.clear();
    for (const std::string &l : lines) {
        int code = -1;
        std::vector<std::string> f;
        if (parseAnyReadAnswer(l, code, f)) registers[code] = f;
    }
    if (registers.empty()) return fail(tr("Resposta inesperada do gerador."));
    return true;
}

bool Psg9080::writeArbitrary(int slot, const std::vector<int> &codes, const Progress &progress) {
    if (slot < 1 || slot > 99) return fail(tr("Posição de onda arbitrária inválida: %1 (1 a 99).").arg(slot));
    if (codes.size() != (size_t)kArbitraryPoints)
        return fail(tr("A onda precisa de %1 pontos (tem %2).").arg(kArbitraryPoints).arg(codes.size()));
    // the device takes the waveform on its normal output screen (as PSG9080_ARB does)
    if (!setInterface({0, 1, 0, 1})) return false;
    port->clear(QSerialPort::Input);
    port->readAll();
    const std::string command = arbitraryWriteCommand(slot, codes);
    const QByteArray bytes(command.data(), (int)command.size());
    const int chunk = 1024; // ~90 ms each at 115200 baud: progress and cancel stay responsive
    for (int sent = 0; sent < bytes.size(); sent += chunk) {
        const QByteArray part = bytes.mid(sent, chunk);
        if (port->write(part) != part.size() || !port->waitForBytesWritten(3000))
            return fail(tr("Falha ao enviar a onda ao gerador: %1").arg(port->errorString()));
        if (progress && !progress((int)(100.0 * (sent + part.size()) / bytes.size())))
            return fail(tr("Envio cancelado; a onda %1 do gerador pode ter ficado incompleta.").arg(slot));
    }
    // answer ":ok" once the device has stored the waveform
    QByteArray buffer;
    QElapsedTimer timer;
    timer.start();
    while (!buffer.contains("\r\n")) {
        if (timer.elapsed() > 8000 || !port->waitForReadyRead(500)) {
            if (port->error() == QSerialPort::ResourceError) {
                close();
                return fail(tr("O gerador foi desconectado."));
            }
            if (timer.elapsed() > 8000) return fail(tr("O gerador não confirmou a onda."));
            continue;
        }
        buffer += port->readAll();
    }
    const std::string answer = buffer.left(buffer.indexOf("\r\n")).toStdString();
    if (!isWriteAck(answer))
        return fail(tr("O gerador recusou a onda (resposta: %1).").arg(QString::fromStdString(answer)));
    emit arbitraryWritten(slot);
    return true;
}

bool Psg9080::readArbitrary(int slot, std::vector<int> &codes, const Progress &progress) {
    if (slot < 1 || slot > 99) return fail(tr("Posição de onda arbitrária inválida: %1 (1 a 99).").arg(slot));
    if (!setInterface({0, 1, 0, 1})) return false;
    std::vector<std::string> lines;
    const qint64 expected = (qint64)kArbitraryPoints * 6; // ~6 characters per point
    auto onBytes = [&](qint64 n) { return !progress || progress((int)std::min<qint64>(99, 100 * n / expected)); };
    if (!exchangeLines(arbitraryReadCommand(slot), 1, 15000, lines, onBytes)) return false;
    if (lines.empty() || !parseArbitraryAnswer(lines.front(), slot, codes))
        return fail(tr("Resposta inesperada ao ler a onda %1.").arg(slot));
    if (progress) progress(100);
    return true;
}
