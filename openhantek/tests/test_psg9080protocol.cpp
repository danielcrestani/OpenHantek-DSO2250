// SPDX-License-Identifier: GPL-2.0+
// Unit tests for the PSG9080 wire protocol. Expected values were captured from real hardware.

#include "generator/psg9080protocol.h"
#include "testing.h"

using namespace psg9080;

static void commands() {
    CHECK_EQ(writeCommand(13, {"25786", "0"}), std::string(":w13=25786,0.\r\n"));
    CHECK_EQ(readCommand(15), std::string(":r15=0.\r\n"));
    CHECK(isWriteAck(":ok"));
    CHECK(isWriteAck("OK"));
    CHECK(isWriteAck("OK."));
    CHECK(!isWriteAck("ERR"));
}

static void readAnswers() {
    std::vector<std::string> f;
    CHECK(parseReadAnswer(":r13=000010000000,0.", 13, f));
    CHECK_EQ(f.size(), (size_t)2);
    CHECK_EQ(f[0], std::string("000010000000"));
    CHECK(!parseReadAnswer(":r16=05000.", 15, f));  // other register
    CHECK(!parseReadAnswer(":r15=abc.", 15, f));    // not a number
    CHECK(!parseReadAnswer(":r15=1", 15, f));       // no terminator
    CHECK(!parseReadAnswer("ERROR", 15, f));
    CHECK(!parseReadAnswer(":r15=.", 15, f));
    CHECK(parseReadAnswer(":r10=1,1.", 10, f));
    bool a = false, b = false;
    CHECK(decodeOutputs(f, a, b));
    CHECK(a && b);
    CHECK_EQ(encodeOutputs(true, false), std::string("1,0"));
}

static void frequency() {
    // Hardware: register 13 answered "000010000000,0" for 10 kHz
    double hz = 0;
    FreqUnit u = FreqUnit::kHz;
    CHECK(decodeFrequency({"000010000000", "0"}, hz, u));
    CHECK_NEAR(hz, 10000.0, 1e-9);
    CHECK(u == FreqUnit::Hz);

    // Hardware check of all display units (hw_check.py): value, unit -> raw ticks
    struct Case {
        double hz;
        FreqUnit unit;
        const char *ticks;
    } cases[] = {{25.786, FreqUnit::Hz, "25786"},       {25786, FreqUnit::kHz, "25786000"},
                 {2578600, FreqUnit::MHz, "2578600000"}, {0.025786, FreqUnit::mHz, "25786"},
                 {0.000025786, FreqUnit::uHz, "25786"}};
    for (const Case &c : cases) {
        std::vector<std::string> f;
        CHECK(encodeFrequency(c.hz, c.unit, f));
        CHECK_EQ(f[0], std::string(c.ticks));
        CHECK_EQ(f[1], std::to_string((int)c.unit));
        CHECK(decodeFrequency(f, hz, u));
        CHECK_NEAR(hz, c.hz, c.hz * 1e-12);
    }
    // 80 MHz needs more than 32 bits of ticks
    std::vector<std::string> f;
    CHECK(encodeFrequency(80e6, FreqUnit::MHz, f));
    CHECK_EQ(f[0], std::string("80000000000"));
    CHECK(!encodeFrequency(-1.0, FreqUnit::Hz, f));

    CHECK(bestUnit(80e6) == FreqUnit::MHz);
    CHECK(bestUnit(1234.5) == FreqUnit::kHz);
    CHECK(bestUnit(5) == FreqUnit::Hz);
    CHECK(bestUnit(0) == FreqUnit::Hz);
    CHECK(bestUnit(0.02) == FreqUnit::mHz);
    CHECK(bestUnit(2e-5) == FreqUnit::uHz);
}

static void channelValues() {
    double v = 0;
    CHECK(decodeAmplitude({"05000"}, v)); // hardware: 5 Vpp
    CHECK_NEAR(v, 5.0, 1e-12);
    CHECK_EQ(encodeAmplitude(1.234), std::string("1234"));
    CHECK(decodeOffset({"1000"}, v)); // hardware: 0 V
    CHECK_NEAR(v, 0.0, 1e-12);
    CHECK_EQ(encodeOffset(-1.5), std::string("850"));
    CHECK_EQ(encodeOffset(1.5), std::string("1150"));
    CHECK_EQ(encodeOffset(-10), std::string("0"));
    CHECK(decodeHundredths({"5000"}, v)); // hardware: duty 50 %
    CHECK_NEAR(v, 50.0, 1e-12);
    CHECK_EQ(encodeHundredths(33.33), std::string("3333"));
    CHECK_EQ(encodeHundredths(90), std::string("9000"));
    CHECK(!decodeAmplitude({"1", "2"}, v));
    CHECK_EQ(channelRegister(REG_AMPLITUDE, 2), 16);
    CHECK_EQ(std::string(waveformName(0)), std::string("Senoidal"));
    CHECK_EQ(std::string(waveformName(150)), std::string(""));
}

int main() {
    commands();
    readAnswers();
    frequency();
    channelValues();
    return testing::report("psg9080protocol");
}
