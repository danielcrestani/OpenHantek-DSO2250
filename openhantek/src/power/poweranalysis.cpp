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

/// Fractional positions of the rising zero crossings of x - mean(x), with hysteresis (15 % of the peak) so noise
/// and small harmonics near zero do not count as extra crossings.
std::vector<double> risingCrossings(const std::vector<double> &x) {
    std::vector<double> out;
    const size_t n = x.size();
    if (n < 4) return out;
    double mean = 0;
    for (double s : x) mean += s;
    mean /= (double)n;
    double peak = 0;
    for (double s : x) peak = std::max(peak, std::fabs(s - mean));
    if (!(peak > 0)) return out;
    const double h = 0.15 * peak;
    int state = 0; // -1 below -h, +1 above +h
    size_t lastLow = 0;
    for (size_t k = 0; k < n; ++k) {
        const double a = x[k] - mean;
        if (a < -h) {
            state = -1;
            lastLow = k;
        } else if (a > h) {
            if (state == -1) {
                // the crossing is between the last low sample and here: last sign change before k
                size_t j = k;
                while (j > lastLow && x[j - 1] - mean >= 0) --j;
                if (j > 0) {
                    const double y0 = x[j - 1] - mean, y1 = x[j] - mean;
                    out.push_back((double)(j - 1) + (y1 != y0 ? -y0 / (y1 - y0) : 0.0));
                }
            }
            state = 1;
        }
    }
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

    const size_t a = (size_t)std::ceil(first), b = (size_t)std::ceil(last); // [a, b): whole cycles
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
        for (size_t k = a; k < b; k += stride) {
            xv += v[k] * ph;
            xi += cur[k] * ph;
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

    const double nonActive = std::sqrt(std::max(0.0, r.s * r.s - r.p * r.p));
    r.q = r.q1 < 0 ? -nonActive : nonActive;
    r.pf = r.s > 0 ? r.p / r.s : 0.0;
    r.d = std::sqrt(std::max(0.0, r.s * r.s - r.p1 * r.p1 - r.q1 * r.q1));

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
