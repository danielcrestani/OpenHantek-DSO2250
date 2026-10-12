// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <string>
#include <vector>

/// \brief Electrical power from one voltage and one current record (no Qt, unit tested).
///
/// Everything is computed over a whole number of cycles of the fundamental (found from the voltage zero
/// crossings), so a record that ends mid-cycle does not bias RMS, power or the harmonics.
///
/// Definitions (IEEE 1459):
///   P  = mean(v·i)                       active power (W)
///   S  = Vrms·Irms                       apparent power (VA)
///   N  = √(S² − P²)                      non-active power (var): reactive + harmonics
///   PF = P / S                           power factor (true / total)
///   φ1 = angle(V1) − angle(I1)           > 0: current lags (inductive), < 0: leads (capacitive)
///   P1 = V1·I1·cos φ1, Q1 = V1·I1·sin φ1 fundamental active / reactive power
///   DPF = cos φ1                         displacement power factor
///   THD = √(Σ H_n², n ≥ 2) / H1          total harmonic distortion (% of the fundamental)
namespace power {

struct Options {
    /// Delay of the current probe (s): a current clamp is a little late; the current samples are taken this
    /// much later to line them up with the voltage.
    double currentDelay = 0.0;
    /// Current probe arrow reversed: multiply the current by -1.
    bool invertCurrent = false;
    /// Highest harmonic computed (limited further by the sampling rate).
    int maxHarmonic = 40;
};

struct Harmonic {
    int n = 1;
    double v = 0, i = 0;           ///< RMS of the harmonic
    double phaseV = 0, phaseI = 0; ///< degrees
};

struct Result {
    bool valid = false;
    std::string error; ///< why not valid

    double frequency = 0; ///< Hz
    int cycles = 0;       ///< whole cycles analyzed
    double vrms = 0, irms = 0, vdc = 0, idc = 0, vpeak = 0, ipeak = 0;
    double vcrest = 0, icrest = 0; ///< crest factors (peak / RMS)

    double p = 0;  ///< W
    double s = 0;  ///< VA
    double n = 0;  ///< var: non-active power N (always >= 0; includes harmonics)
    double pf = 0; ///< P / S (negative when power flows from the load to the source)
    bool phaseValid = false; ///< both fundamentals present: phi1, DPF, P1, Q1 are meaningful

    double v1 = 0, i1 = 0; ///< fundamental RMS
    double phi1 = 0;       ///< degrees, > 0 inductive
    double dpf = 0;        ///< cos φ1
    double p1 = 0, q1 = 0, s1 = 0; ///< q1 > 0 inductive: what a capacitor bank compensates
    double sn = 0; ///< non-fundamental apparent power √(S² − S1²) (VA): harmonics and DC; 0 below 2 % of S

    double thdV = 0, thdI = 0; ///< % of the fundamental
    std::vector<Harmonic> harmonics; ///< n = 1..maxHarmonic (those below Nyquist)
};

/// Analyze one record. `v` and `i` are sampled together every `dt` seconds (volts and amperes).
Result analyze(const std::vector<double> &v, const std::vector<double> &i, double dt, const Options &options = {});

/// Fundamental frequency from the rising zero crossings of the AC part, over whole cycles.
/// `first` and `last` get the (fractional) sample positions of the first and last crossing used.
bool fundamental(const std::vector<double> &x, double dt, double &frequency, double &first, double &last, int &cycles);

} // namespace power
