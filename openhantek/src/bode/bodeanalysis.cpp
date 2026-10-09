// SPDX-License-Identifier: GPL-2.0+

#include "bodeanalysis.h"

#include <algorithm>
#include <cmath>

namespace bode {

namespace {

const double kPi = 3.14159265358979323846;

/// Windowed single-frequency DFT of x (mean removed by the caller) at `f` (Hz), sampled at `fs`.
/// Uses a rotating phasor: one complex multiplication per sample, renormalized to stay on the unit circle.
Phasor tone(const std::vector<double> &x, const std::vector<double> &w, size_t n, double f, double fs) {
    const double step = -2.0 * kPi * f / fs;
    const Phasor rot(std::cos(step), std::sin(step));
    Phasor e(1.0, 0.0), acc(0.0, 0.0);
    for (size_t k = 0; k < n; ++k) {
        acc += (w[k] * x[k]) * e;
        e *= rot;
        if ((k & 1023) == 1023) e /= std::abs(e);
    }
    return acc;
}

struct Prepared {
    std::vector<double> x;
    double mean = 0;
};

Prepared removeMean(const std::vector<double> &src, size_t n) {
    Prepared p;
    p.x.assign(src.begin(), src.begin() + (long)n);
    double s = 0;
    for (double v : p.x) s += v;
    p.mean = s / (double)n;
    for (double &v : p.x) v -= p.mean;
    return p;
}

/// Power of what is not the sine (residual after removing amplitude/phase at f), relative to the sine power.
double snrDb(const std::vector<double> &x, size_t n, double f, double fs, const Phasor &peakPhasor) {
    // x(k) ~ A cos(2 pi f k / fs + phi), with peakPhasor = A e^{j phi}
    const double step = 2.0 * kPi * f / fs;
    const Phasor rot(std::cos(step), std::sin(step));
    Phasor e(1.0, 0.0);
    double total = 0, residual = 0;
    for (size_t k = 0; k < n; ++k) {
        const double fit = std::real(peakPhasor * e);
        total += x[k] * x[k];
        residual += (x[k] - fit) * (x[k] - fit);
        e *= rot;
        if ((k & 1023) == 1023) e /= std::abs(e);
    }
    const double sine = std::norm(peakPhasor) / 2.0 * (double)n;
    if (residual <= 0 || sine <= 0) return sine > 0 ? 200.0 : -200.0;
    (void)total;
    return 10.0 * std::log10(sine / residual);
}

} // namespace

double aliasFrequency(double f, double fs, bool &mirrored) {
    double a = std::fmod(std::fabs(f), fs);
    mirrored = false;
    if (a > fs / 2.0) {
        a = fs - a;
        mirrored = true;
    }
    return a;
}

double wrapDegrees(double deg) {
    double d = std::fmod(deg, 360.0);
    if (d <= -180.0) d += 360.0;
    if (d > 180.0) d -= 360.0;
    return d;
}

std::vector<double> logSweep(double f1, double f2, int perDecade) {
    std::vector<double> out;
    if (!(f1 > 0) || !(f2 > 0) || perDecade < 1) return out;
    if (f1 > f2) std::swap(f1, f2);
    const double l1 = std::log10(f1), l2 = std::log10(f2);
    const int steps = std::max(1, (int)std::ceil((l2 - l1) * perDecade - 1e-9));
    if (f1 == f2) return {f1};
    for (int i = 0; i <= steps; ++i) out.push_back(std::pow(10.0, l1 + (l2 - l1) * i / steps));
    out.front() = f1;
    out.back() = f2;
    return out;
}

double peakDeviation(const std::vector<double> &x) {
    if (x.empty()) return 0.0;
    double s = 0;
    for (double v : x) s += v;
    const double mean = s / (double)x.size();
    double peak = 0;
    for (double v : x) peak = std::max(peak, std::fabs(v - mean));
    return peak;
}

double Measurement::gainDb() const {
    const double m = std::abs(response);
    return m > 0 ? 20.0 * std::log10(m) : -400.0;
}

double Measurement::phaseDeg() const { return wrapDegrees(std::arg(response) * 180.0 / kPi); }

Measurement measure(const std::vector<double> &in, const std::vector<double> &out, double interval,
                    double genFreq) {
    Measurement m;
    const size_t n = std::min(in.size(), out.size());
    if (n < 64 || !(interval > 0) || !(genFreq > 0)) {
        m.problem = "aquisição sem amostras suficientes";
        return m;
    }
    const double fs = 1.0 / interval;
    const double fa = aliasFrequency(genFreq, fs, m.mirrored);
    m.aliased = genFreq > fs / 2.0;
    const double bin = fs / (double)n;
    if (fa < 2.0 * bin) {
        m.problem = m.aliased ? "frequência coincide com um múltiplo da amostragem" : "menos de 2 períodos na tela";
        return m;
    }
    if (fs / 2.0 - fa < 2.0 * bin) {
        m.problem = "frequência muito perto da metade da amostragem";
        return m;
    }

    const Prepared a = removeMean(in, n), b = removeMean(out, n);
    std::vector<double> w(n);
    double sumW = 0;
    for (size_t k = 0; k < n; ++k) {
        w[k] = 0.5 - 0.5 * std::cos(2.0 * kPi * (double)k / (double)(n - 1));
        sumW += w[k];
    }

    // Refine the frequency on the input channel: coarse search +-1.5 bins, then a parabola on the peak.
    double best = fa, bestMag = -1;
    for (int i = -6; i <= 6; ++i) {
        const double f = fa + i * 0.25 * bin;
        if (f <= bin || f >= fs / 2.0 - bin) continue;
        const double mag = std::abs(tone(a.x, w, n, f, fs));
        if (mag > bestMag) {
            bestMag = mag;
            best = f;
        }
    }
    const double d = 0.25 * bin;
    if (best - d > bin && best + d < fs / 2.0 - bin) {
        const double y0 = std::abs(tone(a.x, w, n, best - d, fs)), y1 = bestMag,
                     y2 = std::abs(tone(a.x, w, n, best + d, fs));
        const double den = y0 - 2.0 * y1 + y2;
        if (den < 0) best += d * 0.5 * (y0 - y2) / den;
    }
    m.analyzedFreq = best;

    const Phasor A = tone(a.x, w, n, best, fs), B = tone(b.x, w, n, best, fs);
    m.amplitudeIn = 2.0 * std::abs(A) / sumW;
    m.amplitudeOut = 2.0 * std::abs(B) / sumW;
    if (!(m.amplitudeIn > 0)) {
        m.problem = "sem sinal no canal de entrada";
        return m;
    }
    m.snrInDb = snrDb(a.x, n, best, fs, 2.0 * A / sumW);
    m.snrOutDb = snrDb(b.x, n, best, fs, 2.0 * B / sumW);
    if (m.snrInDb < 6.0) {
        m.problem = "sinal de entrada fraco ou distorcido (verifique a ligação e o V/div)";
        return m;
    }
    Phasor h = B / A;
    if (m.mirrored) h = std::conj(h);
    m.response = h;
    m.valid = true;
    return m;
}

Phasor interpolateResponse(const std::vector<ResponsePoint> &curve, double f) {
    if (curve.empty()) return Phasor(1.0, 0.0);
    if (f <= curve.front().freq) return curve.front().h;
    if (f >= curve.back().freq) return curve.back().h;
    size_t i = 1;
    while (i < curve.size() && curve[i].freq < f) ++i;
    const ResponsePoint &p0 = curve[i - 1], &p1 = curve[i];
    const double t = (std::log(f) - std::log(p0.freq)) / (std::log(p1.freq) - std::log(p0.freq));
    const double db0 = 20 * std::log10(std::abs(p0.h)), db1 = 20 * std::log10(std::abs(p1.h));
    const double ph0 = std::arg(p0.h);
    double ph1 = std::arg(p1.h);
    while (ph1 - ph0 > kPi) ph1 -= 2 * kPi; // shortest way between the two phases
    while (ph1 - ph0 < -kPi) ph1 += 2 * kPi;
    const double db = db0 + (db1 - db0) * t, ph = ph0 + (ph1 - ph0) * t;
    return std::polar(std::pow(10.0, db / 20.0), ph);
}

} // namespace bode
