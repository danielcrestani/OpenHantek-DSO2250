// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "psg9080protocol.h"

class QSerialPort;

/// \brief Connection to a Joy-IT / JunTek PSG9080 function generator over its USB serial port.
///
/// Synchronous: every call sends one command and waits for the answer (about 5 ms per command on the real
/// device), so it is meant to be used from the GUI thread. Every write is confirmed by the device; reads
/// return the values the device actually holds. Methods return false on failure and lastError() says why.
class Psg9080 : public QObject {
    Q_OBJECT

  public:
    struct ChannelState {
        int waveform = 0;
        double frequency = 1000.0; ///< Hz
        psg9080::FreqUnit unit = psg9080::FreqUnit::kHz;
        double amplitude = 1.0; ///< Vpp
        double offset = 0.0;    ///< V
        double duty = 50.0;     ///< %
        double phase = 0.0;     ///< degrees
        bool enabled = false;
    };

    explicit Psg9080(QObject *parent = nullptr);
    ~Psg9080() override;

    /// Open the port (115200 8-N-1) and check that a PSG9080 answers.
    bool open(const QString &portName);
    void close();
    bool isOpen() const;
    QString portName() const;
    QString lastError() const { return error; }

    bool setOutputs(bool ch1, bool ch2);
    /// Switch one output, keeping the other as it is on the device.
    bool setOutput(int channel, bool on);
    bool readOutputs(bool &ch1, bool &ch2);

    /// Waveform code: 0..21 built-in, 101..199 arbitrary 1..99.
    bool setWaveform(int channel, int code);
    /// Frequency in Hz; the display unit is chosen automatically unless given.
    bool setFrequency(int channel, double hz);
    bool setFrequency(int channel, double hz, psg9080::FreqUnit unit);
    bool setAmplitude(int channel, double vpp);
    bool setOffset(int channel, double volts);
    bool setDuty(int channel, double percent);
    bool setPhase(int channel, double degrees);

    /// Read every parameter of a channel and the output state.
    bool readChannel(int channel, ChannelState &state);

    // ---- Any register (modulation, sweep, measurement, system...)
    bool writeRaw(int code, const std::vector<std::string> &fields);
    bool readRaw(int code, std::vector<std::string> &fields);
    /// Integer register with one field.
    bool setInteger(int code, long long value);
    bool readInteger(int code, long long &value);
    /// Register with one field holding round(value * scale).
    bool setScaled(int code, double value, double scale);
    bool readScaled(int code, double scale, double &value);
    /// Channel register (code + channel - 1) holding round(value * scale).
    bool setChannelScaled(int baseCode, int channel, double value, double scale);
    bool readChannelScaled(int baseCode, int channel, double scale, double &value);
    /// "ch1,ch2" register: write one channel and keep the other one as it is on the device.
    bool setPairValue(int code, int channel, long long value);
    bool readPair(int code, long long &ch1, long long &ch2);
    bool readPairValue(int code, int channel, long long &value);
    /// Screen shown by the device (register 24): modulation, sweep, measurement... are active on their screen.
    bool setInterface(const psg9080::Interface &interface);
    bool readInterface(psg9080::Interface &interface);
    /// Memory slot operation (register 26).
    bool memory(int slot, psg9080::MemoryOp op);
    /// Read registers 0..last in one command; the map holds the fields of every register answered.
    bool readAll(std::map<int, std::vector<std::string>> &registers, int last = 90);

    /// Progress of a long transfer: 0..100; return false to cancel.
    using Progress = std::function<bool(int percent)>;
    /// Write an arbitrary waveform (8192 codes 0..16383) to slot 1..99. About 5 s at 115200 baud.
    bool writeArbitrary(int slot, const std::vector<int> &codes, const Progress &progress = Progress());
    /// Read an arbitrary waveform from slot 1..99 (8192 codes 0..16383).
    bool readArbitrary(int slot, std::vector<int> &codes, const Progress &progress = Progress());

  signals:
    void connectionChanged(bool connected);
    /// A parameter of `channel` was written (used to refresh other views)
    void channelWritten(int channel);
    void outputsWritten(bool ch1, bool ch2);
    /// A general register was written (writeRaw and the helpers above)
    void registerWritten(int code);
    /// An arbitrary waveform was stored in the device
    void arbitraryWritten(int slot);

  private:
    bool exchange(const std::string &command, std::string &answer);
    /// Send `command` and collect `lines` answer lines (without CRLF); onBytes gets the bytes received so far.
    bool exchangeLines(const std::string &command, int lines, int timeoutMs, std::vector<std::string> &answer,
                       const std::function<bool(qint64)> &onBytes = {});
    /// Refuse commands from elsewhere (timers, other tabs) while a waveform transfer processes events.
    bool refuseWhileTransferring();
    /// After a cancelled or failed waveform transfer: end the line we were sending and drop what the device still
    /// sends, so the next command starts clean.
    void resync(bool endLine);
    bool writeRegister(int code, const std::vector<std::string> &fields);
    bool readRegister(int code, std::vector<std::string> &fields);
    bool checkChannel(int channel);
    bool fail(const QString &message);

    QSerialPort *port;
    QString error;
    bool transferring = false;
};
