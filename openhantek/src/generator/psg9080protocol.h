// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// \brief Wire protocol of the Joy-IT / JunTek PSG9080 function generator (no Qt, unit tested).
///
/// ASCII over USB serial (CH340, 115200 8-N-1): ":wNN=a,b.\r\n" writes register NN and the device answers
/// ":ok\r\n" (older firmware "OK"/"OK."); ":rNN=0.\r\n" reads it and the answer is ":rNN=a,b.\r\n".
/// Scaling below was verified on hardware with the pypsgctrl driver (2026-10-09), including the mHz/uHz
/// display units.
namespace psg9080 {

/// Built-in waveforms 0..21; arbitrary waveform n (1..99) is code 100 + n.
constexpr int kBuiltinWaveforms = 22;
constexpr int kArbitraryFirst = 101;
constexpr int kArbitraryLast = 199;

/// Frequency display units (second field of registers 13/14).
enum class FreqUnit : int { Hz = 0, kHz = 1, MHz = 2, mHz = 3, uHz = 4 };

/// Register codes; channel registers are for CH1, CH2 is code + 1.
enum Register : int {
    REG_OUTPUTS = 10,   ///< "ch1,ch2" 0/1
    REG_WAVEFORM = 11,  ///< + channel - 1
    REG_FREQUENCY = 13, ///< + channel - 1: ticks,unit
    REG_AMPLITUDE = 15, ///< + channel - 1: mV (Vpp)
    REG_OFFSET = 17,    ///< + channel - 1: 10 mV steps, 1000 = 0 V
    REG_DUTY = 19,      ///< + channel - 1: 0.01 %
    REG_PHASE = 21,     ///< + channel - 1: 0.01 degree
};

/// Command line for writing a register: ":w<code>=<f1>,<f2>.\r\n"
std::string writeCommand(int code, const std::vector<std::string> &fields);
/// Command line for reading a register: ":r<code>=0.\r\n"
std::string readCommand(int code);

/// True for a write acknowledgement (":ok", "OK" or "OK.", without the CRLF).
bool isWriteAck(const std::string &line);
/// Parse a read answer ":r<code>=a,b." (line without CRLF). False if malformed or for another register.
bool parseReadAnswer(const std::string &line, int expectedCode, std::vector<std::string> &fields);

/// Register of a channel parameter (channel 1 or 2).
inline int channelRegister(Register base, int channel) { return (int)base + channel - 1; }

/// Display unit that shows `hz` with the most useful digits (MHz, kHz, Hz, mHz or uHz).
FreqUnit bestUnit(double hz);
/// Ticks per Hz for a display unit (Hz/kHz/MHz count mHz; mHz counts uHz; uHz counts nHz).
double ticksPerHz(FreqUnit unit);
/// Hz per unit shown on the display (MHz -> 1e6 ...).
double hzPerDisplayUnit(FreqUnit unit);
/// Fields for registers 13/14. Returns false (fields untouched) if hz is negative or not finite.
bool encodeFrequency(double hz, FreqUnit unit, std::vector<std::string> &fields);
/// Decode the fields of registers 13/14 to Hz. False if malformed.
bool decodeFrequency(const std::vector<std::string> &fields, double &hz, FreqUnit &unit);

/// Single-field encoders/decoders. Encoders round to the device resolution; decoders return false if malformed.
std::string encodeAmplitude(double vpp);
bool decodeAmplitude(const std::vector<std::string> &fields, double &vpp);
std::string encodeOffset(double volts);
bool decodeOffset(const std::vector<std::string> &fields, double &volts);
std::string encodeHundredths(double value); ///< duty (%) and phase (degrees)
bool decodeHundredths(const std::vector<std::string> &fields, double &value);
bool decodeInteger(const std::vector<std::string> &fields, long long &value);
/// Outputs register: "a,b"
std::string encodeOutputs(bool ch1, bool ch2);
bool decodeOutputs(const std::vector<std::string> &fields, bool &ch1, bool &ch2);

/// Device limits used for input validation.
constexpr double kMaxFrequency = 80e6;     ///< sine; other waveforms are lower (device dependent)
constexpr double kMaxAmplitude = 20.0;     ///< Vpp at low frequency; the device limits it further above ~10 MHz
constexpr double kMaxOffset = 10.0;        ///< +-V
const char *waveformName(int code);        ///< Portuguese name of a built-in waveform, "" otherwise

} // namespace psg9080
