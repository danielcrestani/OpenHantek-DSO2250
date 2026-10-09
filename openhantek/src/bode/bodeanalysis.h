// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <complex>
#include <string>
#include <vector>

/// \brief Frequency response (Bode) measurement from two simultaneously sampled channels (no Qt, unit tested).
///
/// The generator frequency is known, so each channel is reduced to one complex value (a single-frequency DFT
/// with a Hann window): noise, DC and harmonics do not enter the result. Gain and phase come from the ratio
/// output/input measured on the same acquisition, so window scalloping and small frequency errors cancel.
/// Frequencies above fs/2 are measured on their alias (coherent undersampling); when the alias falls in the
/// mirrored half of the band the phase sign is inverted.
namespace bode {

using Phasor = std::complex<double>;

/// Where a sampled sine of frequency f appears (0..fs/2); `mirrored` is true when the spectrum is folded.
double aliasFrequency(double f, double fs, bool &mirrored);

/// Wrap an angle in degrees to (-180, 180].
double wrapDegrees(double deg);

/// Log-spaced frequencies from f1 to f2 (both included, either order allowed), `perDecade` points per decade.
std::vector<double> logSweep(double f1, double f2, int perDecade);

/// Largest absolute deviation from the mean (for clipping / auto range); 0 for an empty vector.
double peakDeviation(const std::vector<double> &x);

struct Measurement {
    bool valid = false;
    std::string problem;      ///< why it is not valid (Portuguese, for the user)
    double analyzedFreq = 0;  ///< frequency in the sampled signal (alias), refined (Hz)
    bool aliased = false;     ///< generator frequency above fs/2
    bool mirrored = false;    ///< phase sign inverted because of the alias
    double amplitudeIn = 0;   ///< peak amplitude of the input (reference) channel
    double amplitudeOut = 0;  ///< peak amplitude of the output channel
    double snrInDb = 0;       ///< sine power / everything else, input channel
    double snrOutDb = 0;      ///< same for the output channel
    Phasor response;          ///< H = out / in (already corrected for mirroring)
    double gainDb() const;
    double phaseDeg() const;
};

/// Measure H = out/in at the generator frequency `genFreq`.
/// `interval` is the time between samples (s). Needs at least 2 periods of the (aliased) frequency in the
/// record and the alias not too close to DC or fs/2.
Measurement measure(const std::vector<double> &in, const std::vector<double> &out, double interval,
                    double genFreq);

/// Calibration curve (both probes on the same point): the measured response of the setup itself.
struct ResponsePoint {
    double freq;
    Phasor h;
};
/// Response of the setup at f, interpolated in log(f) (magnitude in dB and unwrapped phase, linearly).
/// Outside the curve the nearest end is used. Returns 1 for an empty curve.
Phasor interpolateResponse(const std::vector<ResponsePoint> &curve, double f);

} // namespace bode
