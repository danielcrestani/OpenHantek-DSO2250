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
