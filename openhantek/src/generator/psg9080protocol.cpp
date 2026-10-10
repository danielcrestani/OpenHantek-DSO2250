// SPDX-License-Identifier: GPL-2.0+

#include "psg9080protocol.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace psg9080 {

namespace {

bool isDigits(const std::string &s, bool allowMinus) {
    if (s.empty()) return false;
    size_t i = 0;
    if (allowMinus && s[0] == '-') {
        if (s.size() == 1) return false;
        i = 1;
    }
    for (; i < s.size(); ++i)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

bool toInteger(const std::string &s, long long &value) {
    if (!isDigits(s, true) || s.size() > 18) return false;
    value = std::strtoll(s.c_str(), nullptr, 10);
    return true;
}

std::string integerString(long long value) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%lld", value);
    return buf;
}

long long roundTicks(double value) { return (long long)std::llround(value); }

} // namespace

std::string writeCommand(int code, const std::vector<std::string> &fields) {
    std::string cmd = ":w" + std::to_string(code) + "=";
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) cmd += ",";
        cmd += fields[i];
    }
    return cmd + ".\r\n";
}

std::string readCommand(int code) { return ":r" + std::to_string(code) + "=0.\r\n"; }

bool isWriteAck(const std::string &line) { return line == ":ok" || line == "OK" || line == "OK."; }

bool parseReadAnswer(const std::string &line, int expectedCode, std::vector<std::string> &fields) {
    // ":r13=000010000000,0."
    if (line.size() < 6 || line[0] != ':' || line[1] != 'r' || line.back() != '.') return false;
    const size_t eq = line.find('=');
    if (eq == std::string::npos || eq < 3) return false;
    const std::string code = line.substr(2, eq - 2);
    long long c = 0;
    if (!isDigits(code, false) || !toInteger(code, c) || c != expectedCode) return false;
    const std::string payload = line.substr(eq + 1, line.size() - eq - 2);
    if (payload.empty()) return false;
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        const size_t comma = payload.find(',', start);
        out.push_back(payload.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    for (const std::string &f : out)
        if (!isDigits(f, true)) return false;
    fields = out;
    return true;
}

FreqUnit bestUnit(double hz) {
    if (hz >= 1e6) return FreqUnit::MHz;
    if (hz >= 1e3) return FreqUnit::kHz;
    if (hz >= 1.0 || hz == 0.0) return FreqUnit::Hz;
    if (hz >= 1e-3) return FreqUnit::mHz;
    return FreqUnit::uHz;
}

double ticksPerHz(FreqUnit unit) {
    switch (unit) {
    case FreqUnit::mHz: return 1e6;
    case FreqUnit::uHz: return 1e9;
    default: return 1e3;
    }
}

double hzPerDisplayUnit(FreqUnit unit) {
    switch (unit) {
    case FreqUnit::kHz: return 1e3;
    case FreqUnit::MHz: return 1e6;
    case FreqUnit::mHz: return 1e-3;
    case FreqUnit::uHz: return 1e-6;
    default: return 1.0;
    }
}

bool encodeFrequency(double hz, FreqUnit unit, std::vector<std::string> &fields) {
    if (!std::isfinite(hz) || hz < 0.0) return false;
    fields = {integerString(roundTicks(hz * ticksPerHz(unit))), integerString((int)unit)};
    return true;
}

bool decodeFrequency(const std::vector<std::string> &fields, double &hz, FreqUnit &unit) {
    long long ticks = 0, u = 0;
    if (fields.size() != 2 || !toInteger(fields[0], ticks) || !toInteger(fields[1], u) || u < 0 || u > 4)
        return false;
    unit = (FreqUnit)u;
    hz = (double)ticks / ticksPerHz(unit);
    return true;
}

std::string encodeAmplitude(double vpp) { return integerString(roundTicks(vpp * 1000.0)); }

bool decodeAmplitude(const std::vector<std::string> &fields, double &vpp) {
    long long v = 0;
    if (!decodeInteger(fields, v)) return false;
    vpp = v / 1000.0;
    return true;
}

std::string encodeOffset(double volts) { return integerString(roundTicks(volts * 100.0) + 1000); }

bool decodeOffset(const std::vector<std::string> &fields, double &volts) {
    long long v = 0;
    if (!decodeInteger(fields, v)) return false;
    volts = (v - 1000) / 100.0;
    return true;
}

std::string encodeHundredths(double value) { return integerString(roundTicks(value * 100.0)); }

bool decodeHundredths(const std::vector<std::string> &fields, double &value) {
    long long v = 0;
    if (!decodeInteger(fields, v)) return false;
    value = v / 100.0;
    return true;
}

bool decodeInteger(const std::vector<std::string> &fields, long long &value) {
    return fields.size() == 1 && toInteger(fields[0], value);
}

std::string encodeOutputs(bool ch1, bool ch2) { return std::string(ch1 ? "1" : "0") + "," + (ch2 ? "1" : "0"); }

bool decodeOutputs(const std::vector<std::string> &fields, bool &ch1, bool &ch2) {
    long long a = 0, b = 0;
    if (fields.size() != 2 || !toInteger(fields[0], a) || !toInteger(fields[1], b)) return false;
    ch1 = a != 0;
    ch2 = b != 0;
    return true;
}

Interface waveformInterface(int channel) { return {0, channel, 0, 0}; }
Interface modulationInterface(int channel) { return {0, channel, 7, 7}; }
Interface measurementInterface() { return {0, 4, 0, 1}; }
Interface sweepInterface() { return {0, 6, 0, 1}; }
Interface vcoInterface() { return {0, 7, 0, 1}; }

std::vector<std::string> encodeInterface(const Interface &i) {
    // the device documents these as hexadecimal selectors; every value used here is below 10
    char buf[4][8];
    const int v[4] = {i.a, i.page, i.sub, i.item};
    std::vector<std::string> out;
    for (int k = 0; k < 4; ++k) {
        std::snprintf(buf[k], sizeof buf[k], "%x", v[k] & 0xff);
        out.push_back(buf[k]);
    }
    return out;
}

bool decodeInterface(const std::vector<std::string> &fields, Interface &i) {
    if (fields.size() != 4) return false;
    int v[4];
    for (int k = 0; k < 4; ++k) {
        long long x = 0;
        if (!toInteger(fields[k], x)) return false;
        v[k] = (int)x;
    }
    i = {v[0], v[1], v[2], v[3]};
    return true;
}

bool encodeScaled(double value, double scale, std::vector<std::string> &fields, double offset) {
    if (!std::isfinite(value)) return false;
    fields = {integerString(roundTicks(value * scale + offset))};
    return true;
}

bool decodeScaled(const std::vector<std::string> &fields, double scale, double &value, double offset) {
    long long v = 0;
    if (!decodeInteger(fields, v) || scale == 0) return false;
    value = (v - offset) / scale;
    return true;
}

std::vector<std::string> encodePair(long long ch1, long long ch2) { return {integerString(ch1), integerString(ch2)}; }

bool decodePair(const std::vector<std::string> &fields, long long &ch1, long long &ch2) {
    return fields.size() == 2 && toInteger(fields[0], ch1) && toInteger(fields[1], ch2);
}

std::string readAllCommand(int last) {
    char buf[32];
    std::snprintf(buf, sizeof buf, ":r00=%d.\r\n", last);
    return buf;
}

bool parseAnyReadAnswer(const std::string &line, int &code, std::vector<std::string> &fields) {
    if (line.size() < 6 || line[0] != ':' || line[1] != 'r') return false;
    const size_t eq = line.find('=');
    if (eq == std::string::npos || eq < 3) return false;
    long long c = 0;
    if (!toInteger(line.substr(2, eq - 2), c) || c < 0) return false;
    code = (int)c;
    return parseReadAnswer(line, code, fields);
}

bool decodeVersions(const std::vector<std::string> &fields, std::vector<std::string> &versions) {
    std::vector<std::string> out;
    for (const std::string &f : fields) {
        long long v = 0;
        if (!toInteger(f, v) || v < 0) return false;
        char buf[32];
        std::snprintf(buf, sizeof buf, "%lld.%02lld", v / 100, v % 100);
        out.push_back(buf);
    }
    if (out.empty()) return false;
    versions = out;
    return true;
}

static std::string slotText(int slot) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d", slot);
    return buf;
}

std::string arbitraryWriteCommand(int slot, const std::vector<int> &codes) {
    std::string cmd = ":A" + slotText(slot) + "=";
    cmd.reserve(cmd.size() + codes.size() * 6 + 2);
    char buf[16];
    for (int c : codes) {
        const int v = c < 0 ? 0 : (c > kArbitraryMaxCode ? kArbitraryMaxCode : c);
        std::snprintf(buf, sizeof buf, "%d,", v);
        cmd += buf;
    }
    return cmd + "\r\n";
}

std::string arbitraryReadCommand(int slot) { return ":B" + slotText(slot) + "=0.\r\n"; }

bool parseArbitraryAnswer(const std::string &line, int slot, std::vector<int> &codes) {
    const std::string prefix = ":B" + slotText(slot) + "=";
    if (line.compare(0, prefix.size(), prefix) != 0) return false;
    std::vector<int> out;
    out.reserve(kArbitraryPoints);
    size_t i = prefix.size();
    while (i < line.size()) {
        size_t end = line.find(',', i);
        if (end == std::string::npos) end = line.size();
        std::string f = line.substr(i, end - i);
        if (!f.empty() && f.back() == '.') f.pop_back();
        if (!f.empty()) {
            long long v = 0;
            if (!toInteger(f, v) || v < 0 || v > 65535) return false;
            out.push_back((int)v);
        }
        i = end + 1;
    }
    if (out.size() != (size_t)kArbitraryPoints) return false;
    // The device answers with 16-bit values (the scale of the original software's files) although it is
    // written with 14-bit ones; PSG9080_ARB (tested on hardware) shifts every value read by 2 bits.
    for (int &v : out) v >>= 2;
    codes = out;
    return true;
}

const char *waveformName(int code) {
    static const char *names[kBuiltinWaveforms] = {
        "Senoidal",          "Quadrada",           "Pulso",           "Triangular",         "Rampa",
        "CMOS",              "DC",                 "Senoide parcial", "Meia onda",          "Onda completa",
        "Escada positiva",   "Escada negativa",    "Trapézio positivo", "Trapézio negativo", "Ruído",
        "Exponencial subida", "Exponencial descida", "Logarítmica subida", "Logarítmica descida",
        "Pulso sinc",        "Multi-áudio",        "Lorenz"};
    return (code >= 0 && code < kBuiltinWaveforms) ? names[code] : "";
}

} // namespace psg9080
