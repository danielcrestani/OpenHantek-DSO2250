// SPDX-License-Identifier: GPL-2.0+

#define _USE_MATH_DEFINES
#include <algorithm>
#include <cmath>

#include <QColor>
#include <QMutex>
#include <QTimer>

#include <fftw3.h>

#include "spectrumgenerator.h"

#include "glscope.h"
#include "settings.h"
#include "utils/printutils.h"
#include "viewconstants.h"

/// \brief Analyzes the data from the dso.
SpectrumGenerator::SpectrumGenerator(const DsoSettingsScope *scope, const DsoSettingsPostProcessing *postprocessing)
    : scope(scope), postprocessing(postprocessing) {}

SpectrumGenerator::~SpectrumGenerator() {
    if (lastWindowBuffer) fftw_free(lastWindowBuffer);
    freeBuffers();
}

void SpectrumGenerator::freeBuffers() {
    if (forwardPlan) fftw_destroy_plan(forwardPlan);
    if (inversePlan) fftw_destroy_plan(inversePlan);
    forwardPlan = inversePlan = nullptr;
    if (inBuffer) fftw_free(inBuffer);
    if (outBuffer) fftw_free(outBuffer);
    if (acBuffer) fftw_free(acBuffer);
    if (corrBuffer) fftw_free(corrBuffer);
    inBuffer = outBuffer = acBuffer = corrBuffer = nullptr;
    planLength = 0;
}

/// Plans are created once per record length and reused (creating a plan for 512k points every frame was slow)
void SpectrumGenerator::updatePlans(size_t n) {
    if (n == planLength && forwardPlan && inversePlan) return;
    freeBuffers();
    inBuffer = fftw_alloc_real(n);
    outBuffer = fftw_alloc_real(n);
    acBuffer = fftw_alloc_real(n);
    corrBuffer = fftw_alloc_real(n);
    forwardPlan = fftw_plan_r2r_1d((int)n, inBuffer, outBuffer, FFTW_R2HC, FFTW_ESTIMATE);
    inversePlan = fftw_plan_r2r_1d((int)n, acBuffer, corrBuffer, FFTW_HC2R, FFTW_ESTIMATE);
    planLength = n;
}

void SpectrumGenerator::updateWindow(size_t sampleCount) {
    if (lastWindowBuffer && lastWindow == postprocessing->spectrumWindow && lastRecordLength == sampleCount) return;

    if (lastWindowBuffer) fftw_free(lastWindowBuffer);
    lastWindowBuffer = fftw_alloc_real(sampleCount);
    lastRecordLength = (unsigned)sampleCount;
    lastWindow = postprocessing->spectrumWindow;

    const double N = (double)sampleCount;
    const double end = N - 1.0;
    double *w = lastWindowBuffer;

    for (size_t i = 0; i < sampleCount; ++i) {
        const double p = (double)i;
        const double x = 2.0 * M_PI * p / end; // 0 .. 2pi
        double v;
        switch (postprocessing->spectrumWindow) {
        case Dso::WindowFunction::HAMMING: v = 0.54 - 0.46 * cos(x); break;
        case Dso::WindowFunction::HANN: v = 0.5 * (1.0 - cos(x)); break;
        case Dso::WindowFunction::COSINE: v = sin(M_PI * p / end); break;
        case Dso::WindowFunction::LANCZOS: {
            const double s = (2.0 * p / end - 1.0) * M_PI;
            v = (s == 0.0) ? 1.0 : sin(s) / s;
        } break;
        case Dso::WindowFunction::BARTLETT: v = 1.0 - std::fabs((p - end / 2.0) / (end / 2.0)); break;
        case Dso::WindowFunction::TRIANGULAR: v = 1.0 - std::fabs((p - end / 2.0) / (N / 2.0)); break;
        case Dso::WindowFunction::GAUSS: {
            const double sigma = 0.4;
            const double t = (p - end / 2.0) / (sigma * end / 2.0);
            v = exp(-0.5 * t * t);
        } break;
        case Dso::WindowFunction::BARTLETTHANN:
            v = 0.62 - 0.48 * std::fabs(p / end - 0.5) - 0.38 * cos(x);
            break;
        case Dso::WindowFunction::BLACKMAN: {
            const double alpha = 0.16;
            v = (1 - alpha) / 2 - 0.5 * cos(x) + alpha / 2 * cos(2 * x);
        } break;
        case Dso::WindowFunction::NUTTALL:
            v = 0.355768 - 0.487396 * cos(x) + 0.144232 * cos(2 * x) - 0.012604 * cos(3 * x);
            break;
        case Dso::WindowFunction::BLACKMANHARRIS:
            v = 0.35875 - 0.48829 * cos(x) + 0.14128 * cos(2 * x) - 0.01168 * cos(3 * x);
            break;
        case Dso::WindowFunction::BLACKMANNUTTALL:
            v = 0.3635819 - 0.4891775 * cos(x) + 0.1365995 * cos(2 * x) - 0.0106411 * cos(3 * x);
            break;
        case Dso::WindowFunction::FLATTOP:
            v = 1.0 - 1.93 * cos(x) + 1.29 * cos(2 * x) - 0.388 * cos(3 * x) + 0.032 * cos(4 * x);
            break;
        default: v = 1.0; // RECTANGULAR
        }
        w[i] = v;
    }

    double sum = 0.0, sum2 = 0.0;
    for (size_t i = 0; i < sampleCount; ++i) {
        sum += w[i];
        sum2 += w[i] * w[i];
    }
    windowCoherentGain = sum / N;
    windowEnbw = (sum != 0.0) ? N * sum2 / (sum * sum) : 1.0;
}

void SpectrumGenerator::process(PPresult *result) {
    if (states.size() < result->channelCount()) states.resize(result->channelCount());

    for (ChannelID channel = 0; channel < result->channelCount(); ++channel) {
        DataChannel *const channelData = result->modifyData(channel);
        channelData->specPeakValid = false;
        channelData->specThd = -1.0;
        channelData->specHarmonics.clear();

        const size_t sampleCount = channelData->voltage.sample.size();
        if (sampleCount < 16 || channelData->voltage.interval <= 0) {
            // Clear unused channels
            channelData->spectrum.interval = 0;
            channelData->spectrum.sample.clear();
            channelData->frequency = 0;
            states[channel] = ChannelState();
            continue;
        }

        updateWindow(sampleCount);

        const size_t n = sampleCount;
        // Zero padding: when only a few bins fall on the screen the trace would be a chain of triangles.
        // Padding interpolates the spectrum (smooth lobes like a bench analyzer); it does not change
        // the resolution nor the levels. Same factor for every channel of a frame (plans are shared).
        size_t zp = 1;
        bool anySpectrum = false;
        for (ChannelID c = 0; c < scope->spectrum.size(); ++c) anySpectrum = anySpectrum || scope->spectrum[c].used;
        if (anySpectrum && scope->horizontal.frequencybase > 0) {
            const double visibleBins =
                DIVS_TIME * scope->horizontal.frequencybase * channelData->voltage.interval * (double)n;
            while (visibleBins * (double)zp < 2000.0 && zp < 16 && n * zp * 2 <= (size_t)4194304) zp *= 2;
        }
        const size_t nfft = n * zp;
        updatePlans(nfft);

        const size_t half = nfft / 2;
        const double binWidth = 1.0 / channelData->voltage.interval / (double)nfft; // Hz per bin

        // Window + FFT
        const double *samples = channelData->voltage.sample.data();
        for (size_t i = 0; i < n; ++i) inBuffer[i] = lastWindowBuffer[i] * samples[i];
        for (size_t i = n; i < nfft; ++i) inBuffer[i] = 0.0;
        fftw_execute(forwardPlan); // outBuffer: half-complex r0 r1 .. r(N/2) i((N+1)/2-1) .. i1

        // ---- Frequency of the signal (autocorrelation, as before) ----
        {
            const double correctionFactor = 1.0 / (double)half / (double)half;
            acBuffer[0] = outBuffer[0] * outBuffer[0] * correctionFactor;
            size_t position;
            for (position = 1; position < half; ++position)
                acBuffer[position] = (outBuffer[position] * outBuffer[position] +
                                      outBuffer[nfft - position] * outBuffer[nfft - position]) *
                                     correctionFactor;
            acBuffer[half] = outBuffer[half] * outBuffer[half] * correctionFactor;
            for (++position; position < nfft; ++position) acBuffer[position] = 0;
            fftw_execute(inversePlan);

            double minimumCorrelation = corrBuffer[0];
            double peakCorrelation = 0;
            size_t peakPosition = 0;
            for (size_t p = 1; p < n / 2; ++p) { // lags in samples (padding does not change them)
                if (corrBuffer[p] > peakCorrelation && corrBuffer[p] > minimumCorrelation * 2) {
                    peakCorrelation = corrBuffer[p];
                    peakPosition = p;
                } else if (corrBuffer[p] < minimumCorrelation)
                    minimumCorrelation = corrBuffer[p];
            }
            channelData->frequency = peakPosition ? 1.0 / (channelData->voltage.interval * (double)peakPosition) : 0;
        }

        if (!scope->spectrum[channel].used) {
            channelData->spectrum.interval = 0;
            channelData->spectrum.sample.clear();
            states[channel] = ChannelState();
            continue;
        }

        // ---- Power spectrum in V^2 (RMS), corrected for the window's coherent gain ----
        // Bin k of a sine with peak amplitude A: |X[k]| = A * N * CG / 2  ->  Vrms^2 = 2 |X|^2 / (N CG)^2
        const size_t bins = half + 1;
        const double norm = 1.0 / ((double)n * windowCoherentGain);
        std::vector<double> power(bins);
        for (size_t k = 0; k < bins; ++k) {
            const double re = outBuffer[k];
            const double im = (k > 0 && k < nfft - k) ? outBuffer[nfft - k] : 0.0;
            const double mag2 = (re * re + im * im) * norm * norm;
            const bool single = (k == 0) || (nfft % 2 == 0 && k == half); // DC and Nyquist are not doubled
            power[k] = single ? mag2 : 2.0 * mag2;
        }

        // ---- Average / peak hold ----
        ChannelState &st = states[channel];
        const unsigned avgSetting = std::max(1u, postprocessing->spectrumAverage);
        if (st.average.size() != bins || st.interval != channelData->voltage.interval ||
            st.window != postprocessing->spectrumWindow || st.averageSetting != avgSetting ||
            st.peakHold != postprocessing->spectrumPeakHold || st.reset != postprocessing->spectrumReset) {
            st.average.assign(bins, 0.0);
            st.hold.assign(bins, 0.0);
            st.count = 0;
            st.interval = channelData->voltage.interval;
            st.window = postprocessing->spectrumWindow;
            st.averageSetting = avgSetting;
            st.peakHold = postprocessing->spectrumPeakHold;
            st.reset = postprocessing->spectrumReset;
        }
        if (avgSetting > 1) {
            ++st.count;
            const double alpha = 1.0 / (double)std::min(st.count, avgSetting);
            for (size_t k = 0; k < bins; ++k) st.average[k] += alpha * (power[k] - st.average[k]);
        } else {
            st.average.swap(power);
        }
        const std::vector<double> *shown = &st.average;
        if (postprocessing->spectrumPeakHold) {
            for (size_t k = 0; k < bins; ++k) st.hold[k] = std::max(st.hold[k], st.average[k]);
            shown = &st.hold;
        }
        const std::vector<double> &P = *shown;

        // ---- dBV for the display ----
        channelData->spectrum.interval = binWidth;
        channelData->spectrum.sample.resize(bins);
        const double floorDb = postprocessing->spectrumLimit;
        for (size_t k = 0; k < bins; ++k) {
            const double db = 10.0 * log10(std::max(P[k], 1e-30));
            channelData->spectrum.sample[k] = std::max(db, floorDb);
        }

        // ---- Marker: strongest component in the displayed span (window independent level) ----
        // bins summed around a peak: enough for the main lobe of the selected window
        // lobeMax: comfortable sum width; lobeMin: half width of the main lobe (cannot go below)
        size_t lobeMax, lobeMin;
        switch (postprocessing->spectrumWindow) {
        case Dso::WindowFunction::RECTANGULAR: lobeMax = 3; lobeMin = 1; break;
        case Dso::WindowFunction::HANN:
        case Dso::WindowFunction::HAMMING:
        case Dso::WindowFunction::COSINE:
        case Dso::WindowFunction::BARTLETT:
        case Dso::WindowFunction::TRIANGULAR: lobeMax = 3; lobeMin = 2; break;
        case Dso::WindowFunction::FLATTOP: lobeMax = 6; lobeMin = 5; break;
        case Dso::WindowFunction::BLACKMAN: lobeMax = 4; lobeMin = 3; break;
        default: lobeMax = 5; lobeMin = 4; break;
        }
        lobeMax *= zp; // padded bins are zp times denser
        lobeMin *= zp;
        size_t lobe = lobeMax;
        const double enbwBins = windowEnbw * (double)zp; // noise bandwidth in (padded) bins
        size_t kEnd = bins - 1;
        if (scope->horizontal.frequencybase > 0) {
            const double spanBins = DIVS_TIME * scope->horizontal.frequencybase / binWidth;
            if (spanBins < (double)kEnd) kEnd = (size_t)spanBins;
        }
        const size_t kStart = lobeMin + 1; // skip DC and its leakage
        channelData->specHarmonics.clear();
        if (kEnd > kStart + 2) {
            size_t kp = kStart;
            for (size_t k = kStart; k <= kEnd; ++k)
                if (P[k] > P[kp]) kp = k;

            auto lobePower = [&](size_t center) {
                const size_t a = center > lobe ? center - lobe : 0;
                const size_t b = std::min(bins - 1, center + lobe);
                double s = 0.0;
                for (size_t k = a; k <= b; ++k) s += P[k];
                return s / enbwBins; // Vrms^2 of the component
            };
            auto toDb = [](double p) { return 10.0 * log10(std::max(p, 1e-30)); };
            auto interpolate = [&](size_t k) {
                if (k == 0 || k + 1 >= bins) return 0.0;
                const double y0 = toDb(P[k - 1]), y1 = toDb(P[k]), y2 = toDb(P[k + 1]);
                const double den = y0 - 2 * y1 + y2;
                return (den != 0.0) ? std::max(-0.5, std::min(0.5, 0.5 * (y0 - y2) / den)) : 0.0;
            };

            if (P[kp] > 0.0 && kp > 0 && kp + 1 < bins) {
                // Harmonics are kp bins apart: shrink the summed width so neighbouring lobes do not overlap
                if (kp >= 2 * lobeMin + 1) lobe = std::min(lobeMax, (kp - 1) / 2);
                const double delta = interpolate(kp);
                const double f0 = ((double)kp + delta) * binWidth;
                const double p1 = lobePower(kp);

                channelData->specPeakValid = true;
                channelData->specPeakFreq = f0;
                channelData->specPeakDbV = toDb(p1);

                // Noise in one lobe: 20th percentile of the bins in the span x lobe width. A low percentile
                // stays in the gaps even when a harmonic comb (square wave) covers half of the bins.
                std::vector<double> tmp(P.begin() + (long)kStart, P.begin() + (long)kEnd + 1);
                const size_t q = tmp.size() / 5;
                std::nth_element(tmp.begin(), tmp.begin() + (long)q, tmp.end());
                const double noiseLobe = tmp[q] * (double)(2 * lobe + 1) / enbwBins;

                SpectrumHarmonic fund;
                fund.n = 1;
                fund.freq = f0;
                fund.dbv = toDb(p1);
                fund.dbc = 0.0;
                fund.binDb = toDb(P[kp]);
                fund.salient = true;
                channelData->specHarmonics.push_back(fund);

                // Harmonics 2..10 below Nyquist (THD) -- each one searched +-2 bins around n*f0
                const bool separated = kp >= 2 * lobeMin + 1;
                double ph = 0.0;
                int harmonics = 0;
                for (int h = 2; h <= 10 && separated; ++h) {
                    const double c = h * ((double)kp + delta);
                    const size_t search = 2 * zp;
                    if (c + lobe + search >= (double)(bins - 1)) break;
                    size_t kh = (size_t)std::lround(c);
                    size_t best = kh;
                    for (size_t k = kh - search; k <= kh + search; ++k)
                        if (P[k] > P[best]) best = k;
                    const double phh = lobePower(best);
                    ph += phh;
                    ++harmonics;

                    SpectrumHarmonic hh;
                    hh.n = h;
                    hh.freq = ((double)best + interpolate(best)) * binWidth;
                    hh.dbv = toDb(phh);
                    hh.dbc = toDb(phh / p1);
                    hh.binDb = toDb(P[best]);
                    hh.salient = phh > 10.0 * noiseLobe; // at least 10 dB above the noise
                    channelData->specHarmonics.push_back(hh);
                }
                if (separated && harmonics > 0 && p1 > 0) channelData->specThd = 100.0 * std::sqrt(ph / p1);
            }
        }
    }
}
