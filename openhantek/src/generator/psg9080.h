// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QObject>
#include <QString>

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

  signals:
    void connectionChanged(bool connected);
    /// A parameter of `channel` was written (used to refresh other views)
    void channelWritten(int channel);
    void outputsWritten(bool ch1, bool ch2);

  private:
    bool exchange(const std::string &command, std::string &answer);
    bool writeRegister(int code, const std::vector<std::string> &fields);
    bool readRegister(int code, std::vector<std::string> &fields);
    bool checkChannel(int channel);
    bool fail(const QString &message);

    QSerialPort *port;
    QString error;
};
