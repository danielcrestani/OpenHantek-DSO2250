// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <deque>

#include <QObject>
#include <QVector3D>

#include "hantekdso/enums.h"
#include "hantekprotocol/types.h"
#include "processor.h"

struct DsoSettingsScope;
class PPresult;
struct DsoSettingsPostProcessing;
namespace Dso {
struct ControlSpecification;
}

/// \brief Generates ready to be used vertex arrays
class GraphGenerator : public QObject, public Processor {
    Q_OBJECT

  public:
    GraphGenerator(const DsoSettingsScope *scope, bool isSoftwareTriggerDevice);
    void generateGraphsXY(PPresult *result, const DsoSettingsScope *scope);

    bool isReady() const;
    /// Interpolation mode used for the voltage graphs (points, linear or sin(x)/x)
    void setInterpolation(const Dso::InterpolationMode *mode) { interpolation = mode; }
    void setPostProcessing(const DsoSettingsPostProcessing *post) { postprocessing = post; }

  private:
    void generateGraphsTYvoltage(PPresult *result);
    void generateGraphsTYspectrum(PPresult *result);

  private:
    bool ready = false;
    const DsoSettingsScope *scope;
    const bool isSoftwareTriggerDevice;
    const Dso::InterpolationMode *interpolation = nullptr;
    const DsoSettingsPostProcessing *postprocessing = nullptr;

    // Processor interface
    private:
    virtual void process(PPresult *) override;
};
