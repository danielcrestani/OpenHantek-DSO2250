// SPDX-License-Identifier: GPL-2.0+
// Unit tests of the power analysis with synthetic mains signals of known values.

#include "power/poweranalysis.h"
#include "testing.h"

#include <chrono>
#include <cmath>
#include <cstdio>

using namespace power;

namespace {
const double kPi = 3.14159265358979323846;

struct Signal {
    double f = 60, vrms = 127, irms = 5, lagDeg = 0; ///< current lags the voltage by lagDeg
    double third = 0;                                 ///< 3rd harmonic of the current, fraction of I1
    double vdc = 0, delay = 0;                        ///< current probe delay (s)
    double fs = 50e3;
    size_t n = 10240; // 12,3 cycles at 60 Hz: the record does not end on a whole cycle
    double noise = 0;
};

void make(const Signal &s, std::vector<double> &v, std::vector<double> &i) {
    v.resize(s.n);
    i.resize(s.n);
    unsigned seed = 12345;
    auto rnd = [&]() {
        seed = seed * 1103515245u + 12345u;
        return ((seed >> 8) & 0xffff) / 32768.0 - 1.0;
    };
    for (size_t k = 0; k < s.n; ++k) {
        const double t = k / s.fs, w = 2 * kPi * s.f;
        v[k] = s.vdc + s.vrms * std::sqrt(2.0) * std::cos(w * t) + s.noise * rnd();
        const double ti = t - s.delay; // the probe shows the current late
        i[k] = s.irms * std::sqrt(2.0) *
               (std::cos(w * ti - s.lagDeg * kPi / 180) + s.third * std::cos(3 * (w * ti - s.lagDeg * kPi / 180)));
    }
}
} // namespace

static void resistive() {
    std::vector<double> v, i;
    make(Signal{}, v, i);
    const Result r = analyze(v, i, 1 / 50e3);
    CHECK(r.valid);
    CHECK_NEAR(r.frequency, 60, 0.01);
    CHECK_EQ(r.cycles, 11); // 12,3 cycles on screen: 11 whole cycles between the first and last rising crossing
    CHECK_NEAR(r.vrms, 127, 0.1);
    CHECK_NEAR(r.irms, 5, 0.005);
    CHECK_NEAR(r.p, 635, 0.6);
    CHECK_NEAR(r.pf, 1, 0.001);
    CHECK_NEAR(r.phi1, 0, 0.1);
    CHECK_NEAR(r.thdV, 0, 0.1);
    CHECK_NEAR(r.vcrest, std::sqrt(2.0), 0.01);
}

static void inductiveAndCapacitive() {
    std::vector<double> v, i;
    Signal s;
    s.lagDeg = 30;
    make(s, v, i);
    Result r = analyze(v, i, 1 / 50e3);
    CHECK(r.valid);
    CHECK_NEAR(r.p, 127 * 5 * std::cos(kPi / 6), 0.6);
    CHECK_NEAR(r.q, 127 * 5 * 0.5, 0.6); // positive: inductive
    CHECK_NEAR(r.pf, std::cos(kPi / 6), 0.001);
    CHECK_NEAR(r.phi1, 30, 0.1);
    CHECK_NEAR(r.q1, r.q, 0.6);

    s.lagDeg = -45; // current leads: capacitive
    make(s, v, i);
    r = analyze(v, i, 1 / 50e3);
    CHECK_NEAR(r.phi1, -45, 0.1);
    CHECK(r.q < 0);
    CHECK_NEAR(r.dpf, std::cos(kPi / 4), 0.002);
}

static void distortedCurrent() {
    // rectifier-like current: 3rd harmonic of 50 %, fundamental in phase
    std::vector<double> v, i;
    Signal s;
    s.third = 0.5;
    make(s, v, i);
    const Result r = analyze(v, i, 1 / 50e3);
    CHECK(r.valid);
    CHECK_NEAR(r.thdI, 50, 0.3);
    CHECK_NEAR(r.dpf, 1, 0.001);
    CHECK_NEAR(r.i1, 5, 0.01);
    CHECK_NEAR(r.irms, 5 * std::sqrt(1.25), 0.01);
    CHECK_NEAR(r.pf, 1 / std::sqrt(1.25), 0.002); // PF = DPF x distortion factor
    CHECK_NEAR(r.p, 635, 0.7);                    // only the fundamental carries power (pure voltage)
    CHECK_NEAR(r.d, 127 * 5 * 0.5, 1.0);          // distortion power V·I3
    CHECK(r.harmonics.size() >= 3);
    CHECK_NEAR(r.harmonics[2].i, 2.5, 0.01);
}

static void probeDelayAndDirection() {
    std::vector<double> v, i;
    Signal s;
    s.delay = 100e-6; // clamp 100 µs late: 2,16° at 60 Hz
    make(s, v, i);
    Result r = analyze(v, i, 1 / 50e3);
    CHECK_NEAR(r.phi1, 360 * 60 * 100e-6, 0.05); // without correction it looks inductive
    Options o;
    o.currentDelay = 100e-6;
    r = analyze(v, i, 1 / 50e3, o);
    CHECK_NEAR(r.phi1, 0, 0.05);
    CHECK_NEAR(r.pf, 1, 0.001);

    make(Signal{}, v, i);
    for (double &x : i) x = -x; // clamp arrow reversed
    r = analyze(v, i, 1 / 50e3);
    CHECK(r.p < 0);
    o = Options();
    o.invertCurrent = true;
    r = analyze(v, i, 1 / 50e3, o);
    CHECK_NEAR(r.p, 635, 0.6);
}

static void edgeCases() {
    std::vector<double> v, i;
    Signal s;
    s.n = 400; // half a cycle at 60 Hz, 50 kS/s
    make(s, v, i);
    Result r = analyze(v, i, 1 / 50e3);
    CHECK(!r.valid);
    CHECK(!r.error.empty());
    CHECK(!analyze({}, {}, 1e-3).valid);

    s = Signal();
    s.vdc = 10;
    s.noise = 3; // ~1,7 % noise
    make(s, v, i);
    r = analyze(v, i, 1 / 50e3);
    CHECK(r.valid);
    CHECK_NEAR(r.frequency, 60, 0.05);
    CHECK_NEAR(r.vdc, 10, 0.2);
    CHECK_NEAR(r.vrms, std::sqrt(127.0 * 127 + 100 + 3.0 * 3 / 3), 0.3);

    // 50 Hz grid, slow timebase: 1 M samples at 1 MS/s, 50 cycles
    s = Signal();
    s.f = 50;
    s.fs = 1e6;
    s.n = 1000000;
    s.lagDeg = 10;
    make(s, v, i);
    const auto t0 = std::chrono::steady_clock::now();
    r = analyze(v, i, 1e-6);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  1 M amostras: %.0f ms\n", ms);
    CHECK(r.valid);
    CHECK_NEAR(r.frequency, 50, 0.001);
    CHECK_NEAR(r.phi1, 10, 0.05);
    CHECK_EQ((int)r.harmonics.size(), 40);
}

int main() {
    resistive();
    inductiveAndCapacitive();
    distortedCurrent();
    probeDelayAndDirection();
    edgeCases();
    return testing::report("poweranalysis");
}
