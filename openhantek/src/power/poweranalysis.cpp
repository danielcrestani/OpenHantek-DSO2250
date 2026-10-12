// SPDX-License-Identifier: GPL-2.0+

#include "poweranalysis.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace power {

namespace {
constexpr double kPi = 3.14159265358979323846;

double wrapDegrees(double a) {
    while (a > 180.0) a -= 360.0;
    while (a <= -180.0) a += 360.0;
    return a;
}

/// Rising crossings of y through zero with hysteresis +-h; fractional positions by linear interpolation.
std::vector<double> crossingsOf(const std::vector<double> &y, double h) {
    std::vector<double> out;
    int state = 0; // -1 below -h, +1 above +h
    size_t lastLow = 0;
    for (size_t k = 0; k < y.size(); ++k) {
        if (y[k] < -h) {
            state = -1;
            lastLow = k;
        } else if (y[k] > h) {
            if (state == -1) {
                size_t j = k; // last sign change between the low sample and here
                while (j > lastLow && y[j - 1] >= 0) --j;
                if (j > 0) {
                    const double y0 = y[j - 1], y1 = y[j];
                    out.push_back((double)(j - 1) + (y1 != y0 ? -y0 / (y1 - y0) : 0.0));
                }
            }
            state = 1;
        }
    }
    return out;
}

double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::nth_element(v.begin(), v.begin() + (long)(v.size() / 2), v.end());
    return v[v.size() / 2];
}

/// Rising zero crossings of the AC part of x, robust to noise and spikes:
/// 1. a rough period from crossings with a wide hysteresis (50 % of the sine peak sqrt(2)·RMS, not the
///    maximum, so a spike does not hide the signal);
/// 2. the signal smoothed by a centered moving average of 1/50 of that period (no phase shift);
/// 3. crossings of the smoothed signal, dropping any that comes less than half a period after the previous one.
std::vector<double> risingCrossings(const std::vector<double> &x) {
    const size_t n = x.size();
    if (n < 4) return {};
    double mean = 0;
    for (double s : x) mean += s;
    mean /= (double)n;
    std::vector<double> ac(n);
    double sum2 = 0;
    for (size_t k = 0; k < n; ++k) {
        ac[k] = x[k] - mean;
        sum2 += ac[k] * ac[k];
    }
    const double peak = std::sqrt(2.0 * sum2 / (double)n);
    if (!(peak > 0)) return {};

    const std::vector<double> rough = crossingsOf(ac, 0.5 * peak);
    if (rough.size() < 2) return {};
    std::vector<double> intervals;
    for (size_t k = 1; k < rough.size(); ++k) intervals.push_back(rough[k] - rough[k - 1]);
    const double period = median(intervals);

    const size_t half = (size_t)std::max(0.0, std::floor(period / 100.0)); // window 2·half+1 ~ period/50
    std::vector<double> smooth(n);
    if (half == 0) {
        smooth = ac;
    } else {
        double acc = 0;
        size_t lo = 0, hi = 0; // window [lo, hi)
        for (size_t k = 0; k < n; ++k) {
            const size_t wantLo = k > half ? k - half : 0, wantHi = std::min(n, k + half + 1);
            while (hi < wantHi) acc += ac[hi++];
            while (lo < wantLo) acc -= ac[lo++];
            smooth[k] = acc / (double)(hi - lo);
        }
    }
    double s2 = 0;
    for (double v : smooth) s2 += v * v;
    const std::vector<double> fine = crossingsOf(smooth, 0.3 * std::sqrt(2.0 * s2 / (double)n));
    std::vector<double> out;
    for (double c : fine)
        if (out.empty() || c - out.back() > 0.5 * period) out.push_back(c);
    return out;
}

/// value of x at fractional position p (linear interpolation, p inside [0, n-1])
double at(const std::vector<double> &x, double p) {
    if (p <= 0) return x.front();
    const size_t k = (size_t)p;
    if (k + 1 >= x.size()) return x.back();
    const double f = p - (double)k;
    return x[k] + (x[k + 1] - x[k]) * f;
}
} // namespace

bool fundamental(const std::vector<double> &x, double dt, double &frequency, double &first, double &last, int &cycles) {
    const std::vector<double> c = risingCrossings(x);
    if (c.size() < 2 || !(dt > 0)) return false;
    first = c.front();
    last = c.back();
    cycles = (int)c.size() - 1;
    frequency = cycles / ((last - first) * dt);
    return std::isfinite(frequency) && frequency > 0;
}

Result analyze(const std::vector<double> &vIn, const std::vector<double> &iIn, double dt, const Options &opt) {
    Result r;
    const size_t n = std::min(vIn.size(), iIn.size());
    if (n < 16 || !(dt > 0)) {
        r.error = "sem amostras";
        return r;
    }
    const std::vector<double> v(vIn.begin(), vIn.begin() + (long)n);

    // current lined up with the voltage: sample k of the current is taken at k + delay
    const double shift = opt.currentDelay / dt;
    std::vector<double> cur(n);
    {
        const std::vector<double> raw(iIn.begin(), iIn.begin() + (long)n);
        const double sign = opt.invertCurrent ? -1.0 : 1.0;
        for (size_t k = 0; k < n; ++k) cur[k] = sign * at(raw, (double)k + shift);
    }
    // samples where the shifted current is real data (not the clamped edge)
    const double validLo = std::max(0.0, -shift), validHi = std::min((double)(n - 1), (double)(n - 1) - shift);

    // whole cycles of the fundamental, from the voltage (the current may be very distorted or zero)
    std::vector<double> c = risingCrossings(v);
    if (c.size() < 2) {
        std::vector<double> ci = risingCrossings(cur); // no voltage: use the current for the frequency
        c.swap(ci);
    }
    std::vector<double> inside;
    for (double p : c)
        if (p >= validLo && p + 1 <= validHi) inside.push_back(p);
    if (inside.size() < 2) {
        r.error = "menos de um ciclo completo na tela";
        return r;
    }
    const double first = inside.front(), last = inside.back();
    r.cycles = (int)inside.size() - 1;
    r.frequency = r.cycles / ((last - first) * dt);

    // [a, b): whole cycles; the length is rounded, not the end, so it is never a sample too long
    const size_t a = (size_t)std::ceil(first);
    const size_t b = std::min(n, a + (size_t)std::llround(last - first));
    const size_t count = b - a;
    if (count < 8) {
        r.error = "poucas amostras por ciclo";
        return r;
    }

    // RMS, DC, peaks and active power
    double sv = 0, si = 0, sv2 = 0, si2 = 0, svi = 0;
    for (size_t k = a; k < b; ++k) {
        const double x = v[k], y = cur[k];
        sv += x;
        si += y;
        sv2 += x * x;
        si2 += y * y;
        svi += x * y;
        r.vpeak = std::max(r.vpeak, std::fabs(x));
        r.ipeak = std::max(r.ipeak, std::fabs(y));
    }
    const double N = (double)count;
    r.vdc = sv / N;
    r.idc = si / N;
    r.vrms = std::sqrt(sv2 / N);
    r.irms = std::sqrt(si2 / N);
    r.p = svi / N;
    r.s = r.vrms * r.irms;
    r.vcrest = r.vrms > 0 ? r.vpeak / r.vrms : 0;
    r.icrest = r.irms > 0 ? r.ipeak / r.irms : 0;

    // Harmonics: DFT at n·f over the whole cycles. Long records are decimated for this part only.
    const size_t stride = std::max<size_t>(1, count / 65536);
    const double step = dt * (double)stride;
    const int maxH = std::max(1, std::min(opt.maxHarmonic, (int)std::floor(0.45 / (r.frequency * step))));
    for (int h = 1; h <= maxH; ++h) {
        const double w = 2.0 * kPi * h * r.frequency * step;
        const std::complex<double> rot = std::polar(1.0, -w);
        std::complex<double> ph(1.0, 0.0), xv = 0, xi = 0;
        size_t used = 0;
        for (size_t k = a; k + stride <= b; k += stride) {
            // average of the block (anti-alias for the decimation; plain sample when stride = 1)
            double bv = 0, bi = 0;
            for (size_t j = k; j < k + stride; ++j) {
                bv += v[j];
                bi += cur[j];
            }
            xv += (bv / (double)stride) * ph;
            xi += (bi / (double)stride) * ph;
            ph *= rot;
            if ((++used & 1023) == 0) ph /= std::abs(ph); // keep |ph| = 1 over long sums
        }
        xv *= 2.0 / (double)used;
        xi *= 2.0 / (double)used;
        Harmonic hm;
        hm.n = h;
        hm.v = std::abs(xv) / std::sqrt(2.0);
        hm.i = std::abs(xi) / std::sqrt(2.0);
        hm.phaseV = std::arg(xv) * 180.0 / kPi;
        hm.phaseI = std::arg(xi) * 180.0 / kPi;
        r.harmonics.push_back(hm);
    }
    const Harmonic &f1 = r.harmonics.front();
    r.v1 = f1.v;
    r.i1 = f1.i;
    r.phi1 = (r.v1 > 0 && r.i1 > 0) ? wrapDegrees(f1.phaseV - f1.phaseI) : 0.0;
    const double phi = r.phi1 * kPi / 180.0;
    r.s1 = r.v1 * r.i1;
    r.p1 = r.s1 * std::cos(phi);
    r.q1 = r.s1 * std::sin(phi);
    r.dpf = std::cos(phi);

    // the phase only means something when both fundamentals are clearly there (not noise or a missing probe)
    const double vac = std::sqrt(std::max(0.0, r.vrms * r.vrms - r.vdc * r.vdc));
    const double iac = std::sqrt(std::max(0.0, r.irms * r.irms - r.idc * r.idc));
    r.phaseValid = r.v1 > 0.2 * vac && r.i1 > 0.2 * iac && r.v1 > 0 && r.i1 > 0;
    if (!r.phaseValid) {
        r.phi1 = 0;
        r.dpf = 0;
        r.p1 = r.q1 = 0;
    }
    r.n = std::sqrt(std::max(0.0, r.s * r.s - r.p * r.p));
    r.pf = r.s > 0 ? r.p / r.s : 0.0;
    // S_N: apparent power that is not at the fundamental (harmonics, DC); small differences between the time
    // domain S and the DFT S1 are not reported as distortion
    const double sn = std::sqrt(std::max(0.0, r.s * r.s - r.s1 * r.s1));
    r.sn = sn > 0.02 * r.s ? sn : 0.0;

    double hv = 0, hi = 0;
    for (size_t k = 1; k < r.harmonics.size(); ++k) {
        hv += r.harmonics[k].v * r.harmonics[k].v;
        hi += r.harmonics[k].i * r.harmonics[k].i;
    }
    r.thdV = r.v1 > 0 ? 100.0 * std::sqrt(hv) / r.v1 : 0.0;
    r.thdI = r.i1 > 0 ? 100.0 * std::sqrt(hi) / r.i1 : 0.0;
    r.valid = true;
    return r;
}

} // namespace power
