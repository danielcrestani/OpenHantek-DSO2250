// SPDX-License-Identifier: GPL-2.0+
// Unit tests for the Bode measurement with synthetic, simultaneously sampled signals.

#include "bode/bodeanalysis.h"
#include "testing.h"

#include <chrono>
#include <random>

using namespace bode;

namespace {
const double kPi = 3.14159265358979323846;

struct Signal {
    double amplitude = 1.0, phaseDeg = 0.0, dc = 0.0;
    double harmonic3 = 0.0; ///< relative amplitude of the 3rd harmonic
    double noiseRms = 0.0;
};

/// 8-bit-like capture: samples of a sine at `f` taken at `fs`, optional quantization step.
std::vector<double> sample(const Signal &s, double f, double fs, size_t n, unsigned seed, double lsb = 0.0) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, s.noiseRms > 0 ? s.noiseRms : 1.0);
    std::vector<double> x(n);
    for (size_t k = 0; k < n; ++k) {
        const double t = (double)k / fs;
        double v = s.dc + s.amplitude * std::cos(2 * kPi * f * t + s.phaseDeg * kPi / 180.0);
        v += s.amplitude * s.harmonic3 * std::cos(3 * 2 * kPi * f * t);
        if (s.noiseRms > 0) v += noise(rng);
        if (lsb > 0) v = std::round(v / lsb) * lsb;
        x[k] = v;
    }
    return x;
}
} // namespace

static void helpers() {
    bool mirrored = false;
    CHECK_NEAR(aliasFrequency(1e3, 100e3, mirrored), 1e3, 1e-9);
    CHECK(!mirrored);
    CHECK_NEAR(aliasFrequency(80e6, 100e6, mirrored), 20e6, 1e-3);
    CHECK(mirrored);
    CHECK_NEAR(aliasFrequency(130e6, 100e6, mirrored), 30e6, 1e-3);
    CHECK(!mirrored);

    CHECK_NEAR(wrapDegrees(190), -170, 1e-12);
    CHECK_NEAR(wrapDegrees(-180), 180, 1e-12);
    CHECK_NEAR(wrapDegrees(-725), -5, 1e-12);

    std::vector<double> s = logSweep(10, 1e6, 10);
    CHECK_EQ(s.size(), (size_t)51);
    CHECK_NEAR(s.front(), 10, 1e-12);
    CHECK_NEAR(s.back(), 1e6, 1e-6);
    CHECK_NEAR(s[10], 100, 1e-9);
    CHECK_EQ(logSweep(1e6, 10, 10).size(), (size_t)51); // reversed order is accepted
    CHECK_EQ(logSweep(1e3, 1e3, 10).size(), (size_t)1);
    CHECK(logSweep(0, 10, 10).empty());

    CHECK_NEAR(peakDeviation({1, 3, 2}), 1.0, 1e-12);
}

static void basicResponse() {
    // RC low pass at its corner: -3.01 dB, -45 degrees. 10240 samples like the DSO-2250.
    const double fs = 1e6, f = 12345.6;
    const size_t n = 10240;
    Signal in, out;
    in.amplitude = 1.0;
    in.dc = 0.3;
    out.amplitude = 1.0 / std::sqrt(2.0);
    out.phaseDeg = -45;
    out.dc = -0.1;
    Measurement m = measure(sample(in, f, fs, n, 1), sample(out, f, fs, n, 2), 1 / fs, f);
    CHECK(m.valid);
    CHECK_NEAR(m.gainDb(), -3.0103, 0.01);
    CHECK_NEAR(m.phaseDeg(), -45.0, 0.05);
    CHECK_NEAR(m.amplitudeIn, 1.0, 0.01);
    CHECK(!m.aliased);
}

static void robustness() {
    const double fs = 1e6;
    const size_t n = 10240;
    // Generator 50 ppm off the nominal frequency, harmonics, noise and 8-bit quantization
    const double nominal = 1000.0, actual = nominal * (1 + 50e-6);
    Signal in, out;
    in.amplitude = 2.0;
    in.harmonic3 = 0.1;
    in.noiseRms = 0.02;
    out.amplitude = 0.02; // -40 dB
    out.phaseDeg = 120;
    out.noiseRms = 0.002;
    const double lsbIn = 8 * 0.5 / 256, lsbOut = 8 * 0.005 / 256; // 8 divisions, 0.5 V/div and 5 mV/div
    Measurement m = measure(sample(in, actual, fs, n, 3, lsbIn), sample(out, actual, fs, n, 4, lsbOut), 1 / fs,
                            nominal);
    CHECK(m.valid);
    CHECK_NEAR(m.gainDb(), -40.0, 0.2);
    CHECK_NEAR(m.phaseDeg(), 120.0, 1.0);
    CHECK(m.snrInDb > 15);

    // Few periods (low frequency end): 3 periods in the record
    const double fLow = 3 * fs / n;
    m = measure(sample(in, fLow, fs, n, 5), sample(out, fLow, fs, n, 6), 1 / fs, fLow);
    CHECK(m.valid);
    CHECK_NEAR(m.gainDb(), -40.0, 0.5);

    // Phase near +-180 must not jump: -179 degrees
    out.phaseDeg = -179;
    out.noiseRms = 0;
    m = measure(sample(in, 5000, fs, n, 7), sample(out, 5000, fs, n, 8), 1 / fs, 5000);
    CHECK(m.valid);
    CHECK_NEAR(m.phaseDeg(), -179.0, 1.0);
}

static void undersampling() {
    // DSO-2250 with both channels: 100 MSa/s. Generator up to 80 MHz.
    const double fs = 100e6;
    const size_t n = 10240;
    for (double f : {60e6, 80e6, 130e6}) {
        Signal in, out;
        out.amplitude = 0.5;
        out.phaseDeg = -30;
        Measurement m = measure(sample(in, f, fs, n, 9), sample(out, f, fs, n, 10), 1 / fs, f);
        CHECK(m.valid);
        CHECK(m.aliased);
        CHECK_NEAR(m.gainDb(), -6.0206, 0.02);
        CHECK_NEAR(m.phaseDeg(), -30.0, 0.2); // sign restored for the mirrored alias
    }
    // 50 ppm between the generator and the scope clocks is 4 kHz at 80 MHz: ~0.4 bin of the alias.
    // Gain/phase cancel in the ratio, but the absolute amplitude and the SNR need the refined frequency.
    const double nominal = 80e6, actual = nominal * (1 + 50e-6);
    Signal in, out;
    in.amplitude = 1.0;
    out.amplitude = 0.25;
    out.phaseDeg = 60;
    Measurement m = measure(sample(in, actual, fs, n, 11), sample(out, actual, fs, n, 12), 1 / fs, nominal);
    CHECK(m.valid);
    CHECK_NEAR(m.amplitudeIn, 1.0, 0.005);
    CHECK_NEAR(m.amplitudeOut, 0.25, 0.002);
    CHECK(m.snrInDb > 40);
    CHECK_NEAR(m.gainDb(), -12.04, 0.02);
    CHECK_NEAR(m.phaseDeg(), 60.0, 0.2);
}

static void invalidCases() {
    const double fs = 1e6;
    const size_t n = 10240;
    Signal in, out;
    // Less than 2 periods
    double f = 1.0 * fs / n;
    CHECK(!measure(sample(in, f, fs, n, 1), sample(out, f, fs, n, 2), 1 / fs, f).valid);
    // Alias on DC: f = fs
    CHECK(!measure(sample(in, fs, fs, n, 1), sample(out, fs, fs, n, 2), 1 / fs, fs).valid);
    // Exactly fs/2
    CHECK(!measure(sample(in, fs / 2, fs, n, 1), sample(out, fs / 2, fs, n, 2), 1 / fs, fs / 2).valid);
    // No input signal (noise only)
    Signal quiet;
    quiet.amplitude = 0;
    quiet.noiseRms = 0.01;
    Measurement m = measure(sample(quiet, 1000, fs, n, 3), sample(out, 1000, fs, n, 4), 1 / fs, 1000);
    CHECK(!m.valid);
    CHECK(!m.problem.empty());
    // Too few samples
    CHECK(!measure(std::vector<double>(10, 0.0), std::vector<double>(10, 0.0), 1e-6, 1000).valid);
}

static void calibration() {
    std::vector<ResponsePoint> curve = {{100, std::polar(1.0, 0.0)}, {10000, std::polar(0.1, -kPi / 2)}};
    Phasor h = interpolateResponse(curve, 1000); // halfway in log f
    CHECK_NEAR(20 * std::log10(std::abs(h)), -10.0, 1e-9);
    CHECK_NEAR(std::arg(h) * 180 / kPi, -45.0, 1e-9);
    CHECK_NEAR(std::abs(interpolateResponse(curve, 10)), 1.0, 1e-12);
    CHECK_NEAR(std::abs(interpolateResponse({}, 10)), 1.0, 1e-12);
    // Phase interpolation takes the short way around +-180
    curve = {{100, std::polar(1.0, kPi * 170 / 180)}, {10000, std::polar(1.0, -kPi * 170 / 180)}};
    CHECK_NEAR(std::fabs(std::arg(interpolateResponse(curve, 1000)) * 180 / kPi), 180.0, 1e-6);
}

static void speed() {
    // Largest DSO-2250 record with both channels: 524288 samples
    const double fs = 100e6, f = 1.234e6;
    const size_t n = 524288;
    Signal in, out;
    std::vector<double> a = sample(in, f, fs, n, 1), b = sample(out, f, fs, n, 2);
    const auto t0 = std::chrono::steady_clock::now();
    Measurement m = measure(a, b, 1 / fs, f);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(m.valid);
    std::printf("  medição com %zu amostras: %.0f ms\n", n, ms);
    CHECK(ms < 2000);
}

int main() {
    helpers();
    basicResponse();
    robustness();
    undersampling();
    invalidCases();
    calibration();
    speed();
    return testing::report("bodeanalysis");
}
