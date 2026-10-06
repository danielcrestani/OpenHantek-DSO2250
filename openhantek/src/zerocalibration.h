// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QObject>
#include <memory>
#include <vector>

#include "hantekdso/controlspecification.h"
#include "post/ppresult.h"

class HantekDsoControl;
class QProgressDialog;
class QTimer;
class QWidget;
struct DsoSettingsScope;

/// \brief Zero calibration of the channels ("auto zero").
/// With the inputs at 0 V, measures the error of the offset DAC for every V/div at three positions
/// (-3, 0, +3 div), fits a line and gives it to HantekDsoControl, which removes it from the samples and
/// the trigger level. The result is stored in QSettings ("ZeroCalibration") and loaded at start.
class ZeroCalibration : public QObject {
    Q_OBJECT

  public:
    ZeroCalibration(HantekDsoControl *dsoControl, DsoSettingsScope *scope, const Dso::ControlSpecification *spec,
                    QWidget *window);

    /// Load the stored calibration and send it to the device control
    void loadAndApply();
    /// Ask the user and run the calibration
    void start();
    /// Forget the calibration
    void clear();
    bool running() const { return m_running; }
    /// New acquisition (GUI thread)
    void process(std::shared_ptr<PPresult> data);

  private:
    struct Step {
        unsigned gain;
        double offsetFraction;
    };
    void applyStep();
    void finish(bool ok, const QString &message = QString());
    void restoreDevice();

    HantekDsoControl *dsoControl;
    DsoSettingsScope *scope;
    const Dso::ControlSpecification *spec;
    QWidget *window;

    bool m_running = false;
    bool wasSampling = false;
    std::vector<Step> steps;
    size_t stepIndex = 0;
    int frames = 0;
    std::vector<double> sum;                          ///< per channel, current step
    std::vector<std::vector<std::vector<double>>> ys; ///< [channel][gain] measured errors at each position
    QProgressDialog *progress = nullptr;
    QTimer *watchdog = nullptr;
};
