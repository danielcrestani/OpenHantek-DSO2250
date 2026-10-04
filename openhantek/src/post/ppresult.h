// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QVector3D>
#include <QReadWriteLock>

#include <vector>
#include "hantekprotocol/types.h"

/// \brief Struct for a array of sample values.
struct SampleValues {
    std::vector<double> sample; ///< Vector holding the sampling data
    double interval = 0.0;      ///< The interval between two sample values
};

/// \brief A spectral line: fundamental (n = 1) or harmonic
struct SpectrumHarmonic {
    int n = 1;
    double freq = 0.0;  ///< Hz
    double dbv = 0.0;   ///< RMS level in dBV
    double dbc = 0.0;   ///< relative to the fundamental
    double binDb = 0.0; ///< level of the displayed bin (position of the marker)
    bool salient = false;
};

/// \brief Struct for the analyzed data.
struct DataChannel {
    SampleValues voltage;   ///< The time-domain voltage levels (V)
    SampleValues spectrum;  ///< The frequency-domain power levels (dB)

    double frequency = 0.0; ///< The frequency of the signal
    // Spectrum marker (strongest component inside the displayed span)
    bool specPeakValid = false;
    double specPeakFreq = 0.0; ///< Hz (interpolated)
    double specPeakDbV = 0.0;  ///< RMS level of that component in dBV (window independent)
    double specThd = -1.0;     ///< THD in % (harmonics 2..10), <0 if not available
    std::vector<SpectrumHarmonic> specHarmonics; ///< [0] fundamental, then harmonics 2..10
    // Calculate peak-to-peak voltage
    double computeAmplitude() const;
};

typedef std::vector<QVector3D> ChannelGraph;
typedef std::vector<ChannelGraph> ChannelsGraphs;

/// Post processing results
class PPresult {
  public:
    PPresult(unsigned int channelCount);

    /// \brief Returns the analyzed data.
    /// \param channel Channel, whose data should be returned.
    const DataChannel *data(ChannelID channel) const;
    /// \brief Returns the analyzed data. The data structure can be modifed.
    /// \param channel Channel, whose data should be returned.
    DataChannel *modifyData(ChannelID channel);
    /// \return The maximum sample count of the last analyzed data. This assumes there is at least one channel.
    unsigned int sampleCount() const;
    unsigned int channelCount() const;

    bool softwareTriggerTriggered = false;

    ChannelsGraphs vaChannelSpectrum;
    /// Spectrum markers already in screen coordinates (divs), per channel
    struct SpectrumMarker {
        float x = 0, y = 0;
        int n = 1;
        double freq = 0, dbv = 0, dbc = 0;
    };
    std::vector<std::vector<SpectrumMarker>> spectrumMarkers;
    /// Scale of the spectrum screen, for the axis labels
    struct SpectrumAxis {
        bool valid = false;
        double fbase = 0;     ///< Hz/div
        double ref = 0;       ///< dBV at the top line (without offset)
        double dbPerDiv = 10; ///< dB/div
        double offset = 0;    ///< trace offset in divs
        ChannelID channel = 0;
        bool current = false; ///< dBA instead of dBV
    } spectrumAxis;
    ChannelsGraphs vaChannelVoltage;
  private:
    std::vector<DataChannel> analyzedData; ///< The analyzed data for each channel
};
