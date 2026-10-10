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

static void generalRegisters() {
    std::vector<std::string> f;
    // interface selectors, as the Theremino script and PSG9080_ARB send them
    CHECK_EQ(writeCommand(REG_INTERFACE, encodeInterface(modulationInterface(2))), std::string(":w24=0,2,7,7.\r\n"));
    CHECK_EQ(writeCommand(REG_INTERFACE, encodeInterface(sweepInterface())), std::string(":w24=0,6,0,1.\r\n"));
    CHECK_EQ(writeCommand(REG_INTERFACE, encodeInterface(waveformInterface(1))), std::string(":w24=0,1,0,0.\r\n"));
    Interface i;
    CHECK(decodeInterface({"0", "4", "0", "1"}, i));
    CHECK_EQ(i.page, 4);
    CHECK(!decodeInterface({"0", "4"}, i));
    CHECK(decodeInterface({"0", "1", "a", "f"}, i)); // hexadecimal
    CHECK_EQ(i.sub, 10);
    CHECK(!decodeInterface({"0", "1", "x", "0"}, i));
    std::vector<std::string> hex;
    CHECK(parseReadAnswer(":r24=0,1,a,0.", 24, hex));
    CHECK(!parseReadAnswer(":r23=0,1,a,0.", 23, hex)); // letters only in register 24

    // scaled values (Theremino script: sweep start Hz x 10, PWM width us x 1000, period us x 100)
    CHECK(encodeScaled(1234.5, 10, f));
    CHECK_EQ(f[0], std::string("12345"));
    CHECK(encodeScaled(1e-6, 1e9, f)); // 1 us pulse width in ns
    CHECK_EQ(f[0], std::string("1000"));
    CHECK(encodeScaled(0.25, 100, f, 1000)); // offset-style
    CHECK_EQ(f[0], std::string("1025"));
    double v = 0;
    CHECK(decodeScaled({"0000012345"}, 10, v));
    CHECK_NEAR(v, 1234.5, 1e-9);
    CHECK(!encodeScaled(std::nan(""), 10, f));
    CHECK_EQ(writeCommand(REG_MEMORY, {"5", std::to_string((int)MemoryOp::Save)}), std::string(":w26=5,222.\r\n"));
    long long a = 0, b = 0;
    CHECK(decodePair({"0000000010", "0000000020"}, a, b));
    CHECK_EQ(a, 10LL);
    CHECK_EQ(b, 20LL);
    CHECK_EQ(writeCommand(REG_BURST_COUNT, encodePair(5, 1)), std::string(":w61=5,1.\r\n"));

    // bulk read: one answer line per register
    CHECK_EQ(readAllCommand(), std::string(":r00=90.\r\n"));
    int code = -1;
    CHECK(parseAnyReadAnswer(":r28=100.", code, f));
    CHECK_EQ(code, 28);
    CHECK(parseAnyReadAnswer(":r00=80.", code, f));
    CHECK_EQ(code, 0);
    CHECK(!parseAnyReadAnswer(":ok", code, f));
    std::vector<std::string> versions;
    CHECK(decodeVersions({"120", "120", "105"}, versions));
    CHECK_EQ(versions[2], std::string("1.05"));
}

static void arbitrary() {
    std::vector<int> codes(kArbitraryPoints, 8192);
    codes[0] = 0;
    codes[1] = 16383;
    codes[2] = 20000; // clamped
    codes[3] = -5;    // clamped
    const std::string cmd = arbitraryWriteCommand(7, codes);
    CHECK_EQ(cmd.substr(0, 20), std::string(":A07=0,16383,16383,0"));
    CHECK_EQ(cmd.substr(cmd.size() - 7), std::string("8192,\r\n"));
    CHECK_EQ(arbitraryReadCommand(12), std::string(":B12=0.\r\n"));

    // the device answers in 16 bits: 65532 -> 16383
    std::string answer = ":B03=";
    for (int k = 0; k < kArbitraryPoints; ++k) answer += k == 0 ? "65532," : "32768,";
    std::vector<int> back;
    CHECK(parseArbitraryAnswer(answer, 3, back));
    CHECK_EQ(back.size(), (size_t)kArbitraryPoints);
    CHECK_EQ(back[0], 16383);
    CHECK_EQ(back[1], 8192);
    CHECK(!parseArbitraryAnswer(answer, 4, back));          // other slot
    CHECK(!parseArbitraryAnswer(":B03=1,2,3,", 3, back));   // too short
    CHECK(!parseArbitraryAnswer(":B03=1,x,3,", 3, back));
}

int main() {
    commands();
    readAnswers();
    frequency();
    channelValues();
    generalRegisters();
    arbitrary();
    return testing::report("psg9080protocol");
}
