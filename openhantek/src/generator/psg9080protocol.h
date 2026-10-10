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

    // General registers (one value, or "ch1,ch2" pairs). Sources: manufacturer protocol (via pypsgctrl), the
    // Theremino PSG9080 script and PSG9080_ARB (qrp73).
    REG_MODEL = 0,            ///< "80" for the PSG9080
    REG_SERIAL = 1,           ///< serial number
    REG_VERSIONS = 2,         ///< "hw,fw,fpga", each as 120 = v1.20
    REG_INTERFACE = 24,       ///< four menu selectors, see Interface below
    REG_SYNC = 25,            ///< waveform,frequency,amplitude,offset,duty,external 0/1 (CH1 leads)
    REG_MEMORY = 26,          ///< write slot,operation (111 load, 222 save, 333 clear, 444 clear all)
    REG_SOUND = 27,           ///< key beep 0/1
    REG_BRIGHTNESS = 28,      ///< %
    REG_LANGUAGE = 29,        ///< 0 English, 1 Chinese
    REG_WAVE_LOADING = 32,    ///< 0 automatic, 1 fast
    REG_MODULATION = 40,      ///< pair: Modulation
    REG_MOD_WAVEFORM = 41,    ///< pair: 0 sine, 1 square, 2 triangle, 3 rising saw, 4 falling saw, 5..9 arbitrary 1..5
    REG_MOD_SOURCE = 42,      ///< pair: 0 internal, 1 external
    REG_PULSE_INVERT = 57,    ///< pair: 0 normal, 1 inverted
    REG_BURST_IDLE = 58,      ///< pair: 0 zero, 1 positive maximum, 2 negative maximum
    REG_POLARITY = 59,        ///< pair: 0 positive, 1 negative (ASK/FSK/PSK)
    REG_TRIGGER_SOURCE = 60,  ///< pair: 0 manual, 1 internal (CH2), 2 external AC, 3 external DC
    REG_BURST_COUNT = 61,     ///< pair: cycles 1..1000000000
    REG_MEASURE_SETUP = 62,   ///< coupling (0 AC, 1 DC), gate time (ms), mode (0 high, 1 low frequency)
    REG_MEASURE_MODE = 63,    ///< 0 counter, 1 measurement
    REG_SWEEP_SETUP = 64,     ///< channel (0/1), time (0.01 s), direction (0 up, 1 down, 2 round trip), 0 lin/1 log
    REG_SWEEP_ENABLE = 65,    ///< sweep, voltage control (VCO): 0/1
    REG_SWEEP_FREQ_START = 66, ///< 0.1 Hz
    REG_SWEEP_FREQ_END = 67,
    REG_SWEEP_AMP_START = 68, ///< mVpp
    REG_SWEEP_AMP_END = 69,
    REG_SWEEP_DUTY_START = 70, ///< 0.01 %
    REG_SWEEP_DUTY_END = 71,
    REG_TRIGGER = 74,         ///< pair: write 1 to fire a manual burst
    REG_COUNTER = 80,         ///< read only: counted pulses
    REG_MEAS_FREQ_HIGH = 81,  ///< read only: Hz (high frequency mode)
    REG_MEAS_FREQ_LOW = 82,   ///< read only: mHz (low frequency mode)
    REG_MEAS_POS_WIDTH = 83,  ///< read only: ns
    REG_MEAS_NEG_WIDTH = 84,  ///< read only: ns
    REG_MEAS_PERIOD = 85,     ///< read only: 10 ns
    REG_MEAS_DUTY = 86,       ///< read only: 0.01 %

    // Channel registers (+ channel - 1)
    REG_MOD_FREQUENCY = 43,   ///< modulation frequency / rate, mHz
    REG_AM_DEPTH = 45,        ///< 0.1 % (also the ASK amplitude)
    REG_FM_DEVIATION = 47,    ///< 0.1 Hz
    REG_FSK_FREQUENCY = 49,   ///< hop frequency, 0.1 Hz
    REG_PM_DEVIATION = 51,    ///< 0.1 degree (also the PSK phase)
    REG_PULSE_WIDTH = 53,     ///< ns
    REG_PULSE_PERIOD = 55,    ///< 10 ns
};

/// Modulation types (register 40).
enum class Modulation : int { AM = 0, FM = 1, PM = 2, ASK = 3, FSK = 4, PSK = 5, Pulse = 6, Burst = 7 };

/// Menu selectors of register 24 (the screen the device shows). The second field selects the page: 1/2 = CH1/CH2,
/// 4 measurement, 6 sweep, 7 voltage control; modulation is page CH1/CH2 with sub-page 7,7.
struct Interface {
    int a = 0, page = 1, sub = 0, item = 0;
};
Interface waveformInterface(int channel);   ///< 0,ch,0,0: normal output (modulation off)
Interface modulationInterface(int channel); ///< 0,ch,7,7
Interface measurementInterface();           ///< 0,4,0,1
Interface sweepInterface();                 ///< 0,6,0,1
Interface vcoInterface();                   ///< 0,7,0,1
std::vector<std::string> encodeInterface(const Interface &i);
bool decodeInterface(const std::vector<std::string> &fields, Interface &i);

/// Fields of a value register: round(value * scale + offset). False for a value that is not finite.
bool encodeScaled(double value, double scale, std::vector<std::string> &fields, double offset = 0.0);
/// value = (raw - offset) / scale, from a single-field answer.
bool decodeScaled(const std::vector<std::string> &fields, double scale, double &value, double offset = 0.0);
/// Fields of a "ch1,ch2" register, and back.
std::vector<std::string> encodePair(long long ch1, long long ch2);
bool decodePair(const std::vector<std::string> &fields, long long &ch1, long long &ch2);

/// Memory operations of register 26.
enum class MemoryOp : int { Load = 111, Save = 222, Clear = 333, ClearAll = 444 };

/// Read every register 0..last at once (":r00=<last>.", the device answers one line per register).
std::string readAllCommand(int last = 90);
/// Parse one answer line of any register: code and fields. False if malformed.
bool parseAnyReadAnswer(const std::string &line, int &code, std::vector<std::string> &fields);

/// Firmware versions of register 2 ("120,120,120") as "1.20" strings.
bool decodeVersions(const std::vector<std::string> &fields, std::vector<std::string> &versions);

// ---- Arbitrary waveforms: 8192 points, 14 bits (0..16383, 8192 = 0 V), slots 1..99
constexpr int kArbitraryPoints = 8192;
constexpr int kArbitraryMaxCode = 16383;
/// ":Ann=v1,v2,...,v8192,\r\n" (answered with ":ok"). Codes outside 0..16383 are clamped.
std::string arbitraryWriteCommand(int slot, const std::vector<int> &codes);
/// ":Bnn=0.\r\n"
std::string arbitraryReadCommand(int slot);
/// Parse ":Bnn=v1,...,v8192," (without CRLF). The device sends 16-bit values (the original PC software's scale);
/// they are converted to 14 bits, the scale arbitraryWriteCommand() takes.
bool parseArbitraryAnswer(const std::string &line, int slot, std::vector<int> &codes);

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
constexpr double kMaxAmplitude = 25.0;     ///< Vpp up to 1 MHz; the device limits it further above (manual)
constexpr double kMinOffset = -9.99;       ///< V (manual: -9.99 to 12.00 V)
constexpr double kMaxOffset = 12.0;        ///< V
const char *waveformName(int code);        ///< Portuguese name of a built-in waveform, "" otherwise

} // namespace psg9080
