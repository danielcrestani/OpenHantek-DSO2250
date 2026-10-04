// SPDX-License-Identifier: GPL-2.0+

#include <QDebug>
#include <QMutex>
#include <exception>
#include <algorithm>
#include <cmath>

#include "post/graphgenerator.h"
#include "post/ppresult.h"
#include "post/postprocessingsettings.h"
#include "post/softwaretrigger.h"
#include "hantekdso/controlspecification.h"
#include "scopesettings.h"
#include "utils/printutils.h"
#include "viewconstants.h"

static const SampleValues &useSpecSamplesOf(ChannelID channel, const PPresult *result,
                                            const DsoSettingsScope *scope) {
    static SampleValues emptyDefault;
    if (!scope->spectrum[channel].used || !result->data(channel)) return emptyDefault;
    return result->data(channel)->spectrum;
}

static const SampleValues &useVoltSamplesOf(ChannelID channel, const PPresult *result,
                                            const DsoSettingsScope *scope) {
    static SampleValues emptyDefault;
    if (!scope->voltage[channel].used || !result->data(channel)) return emptyDefault;
    return result->data(channel)->voltage;
}

GraphGenerator::GraphGenerator(const DsoSettingsScope *scope, bool isSoftwareTriggerDevice)
    : scope(scope), isSoftwareTriggerDevice(isSoftwareTriggerDevice) {}

bool GraphGenerator::isReady() const { return ready; }

void GraphGenerator::generateGraphsTYvoltage(PPresult *result) {
    unsigned preTrigSamples = 0;
    unsigned postTrigSamples = 0;
    unsigned swTriggerStart = 0;

    // check trigger point for software trigger
    if (isSoftwareTriggerDevice && scope->trigger.source < result->channelCount())
        std::tie(preTrigSamples, postTrigSamples, swTriggerStart) = SoftwareTrigger::compute(result, scope);
    result->softwareTriggerTriggered = postTrigSamples > preTrigSamples;

    result->vaChannelVoltage.resize(scope->voltage.size());

    // Fine trigger alignment (sub-sample): the hardware trigger is only accurate to one sample, which makes
    // fast signals jump on screen. Find the trigger-level crossing closest to the trigger position, compute
    // its exact (interpolated) instant and shift all traces so that it lands exactly on the trigger mark.
    double triggerShift = 0.0;
    if (!scope->trigger.special && scope->trigger.source < scope->voltage.size() && !isSoftwareTriggerDevice) {
        const SampleValues &src = useVoltSamplesOf(scope->trigger.source, result, scope);
        if (src.sample.size() > 4 && src.interval > 0) {
            const double hf = src.interval / scope->horizontal.timebase;
            const double *d = src.sample.data();
            const size_t n = src.sample.size();
            // Rejeição de AF: média móvel centrada (corte de -3 dB em ~50 kHz) só para localizar o disparo.
            // Simétrica, então não desloca o instante do cruzamento; ruído e componentes rápidas deixam de
            // gerar cruzamentos falsos que fazem o traço "pular".
            std::vector<double> smooth;
            if (scope->trigger.hfReject) {
                size_t win = (size_t)(0.443 / (50e3 * src.interval));
                win = std::min(win, n / 8);
                if (win >= 3) {
                    win |= 1; // odd -> centered
                    const size_t h = win / 2;
                    smooth.resize(n);
                    std::vector<double> pre(n + 1, 0.0);
                    for (size_t i = 0; i < n; ++i) pre[i + 1] = pre[i] + d[i];
                    for (size_t i = 0; i < n; ++i) {
                        const size_t a = i >= h ? i - h : 0, b = std::min(n, i + h + 1);
                        smooth[i] = (pre[b] - pre[a]) / (double)(b - a);
                    }
                    d = smooth.data();
                }
            }
            size_t visible = (size_t)std::min<double>((double)n - 1, std::ceil(DIVS_TIME / hf) + 2.0);
            const double expected = scope->trigger.position * DIVS_TIME / hf;
            const double level = scope->voltage[scope->trigger.source].trigger;
            const bool rising = scope->trigger.slope == Dso::Slope::Positive;
            double best = -1.0, bestDist = 1e300;
            for (size_t k = 0; k + 1 <= visible && k + 1 < n; ++k) {
                const double a = d[k], b = d[k + 1];
                const bool cross = rising ? (a < level && b >= level) : (a > level && b <= level);
                if (!cross || b == a) continue;
                const double t = (double)k + (level - a) / (b - a);
                const double dist = std::fabs(t - expected);
                if (dist < bestDist) {
                    bestDist = dist;
                    best = t;
                }
            }
            if (best >= 0.0) triggerShift = expected - best;
        }
    }

    for (ChannelID channel = 0; channel < scope->voltage.size(); ++channel) {
        ChannelGraph &target = result->vaChannelVoltage[channel];
        const SampleValues &samples = useVoltSamplesOf(channel, result, scope);

        // Check if this channel is used and available at the data analyzer
        if (samples.sample.empty()) {
            // Delete all vector arrays
            target.clear();
            continue;
        }
        const size_t start = swTriggerStart - preTrigSamples;
        if (start >= samples.sample.size()) {
            target.clear();
            continue;
        }
        const double *data = samples.sample.data() + start;
        const size_t available = samples.sample.size() - start;

        // Horizontal distance between sampling points (in divs)
        const double horizontalFactor = samples.interval / scope->horizontal.timebase;
        const float gain = (float)scope->gain(channel);
        const float offset = (float)scope->voltage[channel].offset;
        const float invert = scope->voltage[channel].inverted ? -1.0f : 1.0f;
        auto toY = [gain, offset, invert](double v) { return (float)v / gain * invert + offset; };
        auto toX = [horizontalFactor, triggerShift](double pos) {
            return (float)((pos + triggerShift) * horizontalFactor - DIVS_TIME / 2);
        };

        // Only the samples that fall on the screen are drawn
        size_t count = available;
        if (horizontalFactor > 0) {
            const double visible = std::ceil(DIVS_TIME / horizontalFactor) + 2.0 + std::fabs(triggerShift);
            if (visible < (double)count) count = (size_t)visible;
        }

        target.clear();
        const size_t maxPoints = 4000;
        const bool sinc = interpolation && *interpolation == Dso::INTERPOLATION_SINC;

        if (count > maxPoints) {
            // Many samples per pixel: keep min and max of each slice (peak detect), nothing is lost
            const size_t buckets = maxPoints / 2;
            target.reserve(buckets * 2);
            for (size_t b = 0; b < buckets; ++b) {
                const size_t i0 = b * count / buckets, i1 = (b + 1) * count / buckets;
                if (i1 <= i0) continue;
                size_t iMin = i0, iMax = i0;
                for (size_t i = i0 + 1; i < i1; ++i) {
                    if (data[i] < data[iMin]) iMin = i;
                    if (data[i] > data[iMax]) iMax = i;
                }
                const size_t first = std::min(iMin, iMax), second = std::max(iMin, iMax);
                target.push_back(QVector3D(toX((double)first), toY(data[first]), 0.0f));
                if (second != first) target.push_back(QVector3D(toX((double)second), toY(data[second]), 0.0f));
            }
        } else if (sinc && count >= 4 && count < maxPoints / 2) {
            // Few samples per screen: sin(x)/x reconstruction (Lanczos window), like bench oscilloscopes
            const int a = 6;
            size_t factor = (maxPoints / 2) / count;
            if (factor < 2) factor = 2;
            if (factor > 64) factor = 64;
            const size_t outCount = (count - 1) * factor + 1;
            target.reserve(outCount);
            auto sincf = [](double x) {
                if (std::fabs(x) < 1e-12) return 1.0;
                const double px = M_PI * x;
                return std::sin(px) / px;
            };
            for (size_t o = 0; o < outCount; ++o) {
                const double t = (double)o / (double)factor;
                const long i = (long)std::floor(t);
                double sum = 0.0, wsum = 0.0;
                for (long k = i - a + 1; k <= i + a; ++k) {
                    if (k < 0 || k >= (long)available) continue;
                    const double x = t - (double)k;
                    if (std::fabs(x) >= a) continue;
                    const double w = sincf(x) * sincf(x / a);
                    sum += data[k] * w;
                    wsum += w;
                }
                const double value = wsum != 0.0 ? sum / wsum : data[std::min((size_t)i, available - 1)];
                target.push_back(QVector3D(toX(t), toY(value), 0.0f));
            }
        } else {
            target.reserve(count);
            for (size_t i = 0; i < count; ++i) target.push_back(QVector3D(toX((double)i), toY(data[i]), 0.0f));
        }
    }
}

void GraphGenerator::generateGraphsTYspectrum(PPresult *result) {
    ready = true;
    result->vaChannelSpectrum.resize(scope->spectrum.size());
    result->spectrumMarkers.assign(scope->spectrum.size(), {});
    for (ChannelID channel = 0; channel < scope->voltage.size(); ++channel) {
        ChannelGraph &target = result->vaChannelSpectrum[channel];
        const SampleValues &samples = useSpecSamplesOf(channel, result, scope);

        // Check if this channel is used and available at the data analyzer
        if (samples.sample.empty()) {
            // Delete all vector arrays
            target.clear();
            continue;
        }
        // Spectrum samples are in dBV, one per bin from 0 Hz to Nyquist.
        // Reference level = top of the screen; the channel offset moves the trace (divs).
        const size_t bins = samples.sample.size();
        const double hf = samples.interval / scope->horizontal.frequencybase; // divs per bin
        const double magnitude = scope->spectrum[channel].magnitude;
        const double offset = scope->spectrum[channel].offset;
        const double ref = postprocessing ? postprocessing->spectrumReference : 0.0;
        auto toY = [=](double db) {
            double y = (db - ref) / magnitude + DIVS_VOLTAGE / 2 + offset;
            return (float)std::max<double>(-DIVS_VOLTAGE / 2, std::min<double>(DIVS_VOLTAGE / 2, y));
        };
        auto toX = [=](double bin) { return (float)(bin * hf - DIVS_TIME / 2); };

        size_t count = bins;
        if (hf > 0) {
            const double visible = std::ceil(DIVS_TIME / hf) + 2.0;
            if (visible < (double)count) count = (size_t)visible;
        }

        target.clear();
        const size_t maxPoints = 2000;
        if (count > maxPoints) {
            // Many bins per pixel: keep the highest bin of each slice (positive peak detector, like an analyzer)
            target.reserve(maxPoints);
            for (size_t b = 0; b < maxPoints; ++b) {
                const size_t a = b * count / maxPoints;
                const size_t e = std::max(a + 1, (b + 1) * count / maxPoints);
                size_t best = a;
                for (size_t k = a; k < e && k < bins; ++k)
                    if (samples.sample[k] > samples.sample[best]) best = k;
                target.push_back(QVector3D(toX((double)best), toY(samples.sample[best]), 0.0f));
            }
        } else {
            target.reserve(count);
            for (size_t k = 0; k < count; ++k) target.push_back(QVector3D(toX((double)k), toY(samples.sample[k]), 0.0f));
        }

        // Markers of the fundamental and of the salient harmonics (only those inside the screen)
        if (postprocessing && postprocessing->spectrumShowHarmonics && result->data(channel) &&
            channel < result->spectrumMarkers.size()) {
            for (const SpectrumHarmonic &h : result->data(channel)->specHarmonics) {
                if (!h.salient) continue;
                PPresult::SpectrumMarker m;
                m.x = toX(h.freq / samples.interval);
                if (m.x < -DIVS_TIME / 2 || m.x > DIVS_TIME / 2) continue;
                m.y = toY(std::max(h.binDb, postprocessing->spectrumLimit));
                m.n = h.n;
                m.freq = h.freq;
                m.dbv = h.dbv;
                m.dbc = h.dbc;
                result->spectrumMarkers[channel].push_back(m);
            }
        }
    }
}

void GraphGenerator::process(PPresult *data) {
    if (scope->horizontal.format == Dso::GraphFormat::TY) {
        ready = true;
        generateGraphsTYspectrum(data);
        generateGraphsTYvoltage(data);
    } else
        generateGraphsXY(data, scope);
}

void GraphGenerator::generateGraphsXY(PPresult *result, const DsoSettingsScope *scope) {
    result->vaChannelVoltage.resize(scope->voltage.size());

    // Delete all spectrum graphs
    for (ChannelGraph &data : result->vaChannelSpectrum) data.clear();

    // Generate voltage graphs for pairs of channels
    for (ChannelID channel = 0; channel < scope->voltage.size(); channel += 2) {
        // We need pairs of channels.
        if (channel + 1 == scope->voltage.size()) {
            result->vaChannelVoltage[channel].clear();
            continue;
        }

        const ChannelID xChannel = channel;
        const ChannelID yChannel = channel + 1;

        const SampleValues &xSamples = useVoltSamplesOf(xChannel, result, scope);
        const SampleValues &ySamples = useVoltSamplesOf(yChannel, result, scope);

        // The channels need to be active
        if (!xSamples.sample.size() || !ySamples.sample.size()) {
            result->vaChannelVoltage[channel].clear();
            result->vaChannelVoltage[channel + 1].clear();
            continue;
        }

        // Check if the sample count has changed
        const size_t sampleCount = std::min(xSamples.sample.size(), ySamples.sample.size());
        ChannelGraph &drawLines = result->vaChannelVoltage[channel];
        drawLines.reserve(sampleCount * 2);

        // Fill vector array
        std::vector<double>::const_iterator xIterator = xSamples.sample.begin();
        std::vector<double>::const_iterator yIterator = ySamples.sample.begin();
        const double xGain = scope->gain(xChannel);
        const double yGain = scope->gain(yChannel);
        const double xOffset = scope->voltage[xChannel].offset;
        const double yOffset = scope->voltage[yChannel].offset;
        const double xInvert = scope->voltage[xChannel].inverted ? -1.0 : 1.0;
        const double yInvert = scope->voltage[yChannel].inverted ? -1.0 : 1.0;

        for (unsigned int position = 0; position < sampleCount; ++position) {
            drawLines.push_back(QVector3D((float)(*(xIterator++) / xGain * xInvert + xOffset),
                                          (float)(*(yIterator++) / yGain * yInvert + yOffset), 0.0));
        }
    }
}
