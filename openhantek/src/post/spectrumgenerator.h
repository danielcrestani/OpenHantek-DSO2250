// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <vector>

#include <QMutex>
#include <QThread>
#include <memory>

#include "ppresult.h"
#include "dsosamples.h"
#include "utils/printutils.h"
#include "postprocessingsettings.h"

#include "processor.h"

#include <fftw3.h>

class DsoSettings;
struct DsoSettingsScope;

/// \brief Analyzes the data from the dso.
/// Calculates the spectrum (calibrated in dBV RMS), the signal frequency, the main spectral peak and THD.
class SpectrumGenerator : public Processor {
  public:
    SpectrumGenerator(const DsoSettingsScope* scope, const DsoSettingsPostProcessing* postprocessing);
    virtual ~SpectrumGenerator();
    virtual void process(PPresult *data) override;

  private:
    void updateWindow(size_t n);
    void updatePlans(size_t n);
    void freeBuffers();

    /// Average / peak hold state of one channel
    struct ChannelState {
        std::vector<double> average; ///< averaged power (V^2) per bin
        std::vector<double> hold;    ///< max hold power per bin
        unsigned count = 0;
        double interval = 0.0;
        Dso::WindowFunction window = (Dso::WindowFunction)-1;
        unsigned averageSetting = 0;
        bool peakHold = false;
        unsigned reset = 0;
    };

    const DsoSettingsScope* scope;
    const DsoSettingsPostProcessing* postprocessing;
    unsigned int lastRecordLength = 0;                        ///< The record length of the previously analyzed data
    Dso::WindowFunction lastWindow = (Dso::WindowFunction)-1; ///< The previously used dft window function
    double *lastWindowBuffer = nullptr;
    double windowCoherentGain = 1.0; ///< mean(w): amplitude correction
    double windowEnbw = 1.0;         ///< equivalent noise bandwidth in bins: N*sum(w^2)/sum(w)^2

    size_t planLength = 0;
    double *inBuffer = nullptr;   ///< windowed samples
    double *outBuffer = nullptr;  ///< half-complex FFT result
    double *acBuffer = nullptr;   ///< power spectrum for autocorrelation
    double *corrBuffer = nullptr; ///< autocorrelation result
    fftw_plan forwardPlan = nullptr;
    fftw_plan inversePlan = nullptr;

    std::vector<ChannelState> states;
};
