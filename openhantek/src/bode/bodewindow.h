// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QElapsedTimer>
#include <QMainWindow>

#include <vector>

#include "bode/bodeanalysis.h"
#include "bode/bodeplot.h"
#include "bode/sampletap.h"
#include "hantekdso/controlspecification.h"

class HantekDsoControl;
class Psg9080;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTimer;
class ScopePreview;

/// \brief OpenHantek Bode: frequency response with the PSG9080 (signal) and the DSO-2250 (measurement).
///
/// The window owns the generator connection and drives the oscilloscope directly. For every frequency it sets
/// the generator, picks a record time with about 10 periods, ignores the acquisitions that may still hold the
/// previous setting (counted by sequence number), adjusts the gain of both channels and averages a few
/// measurements (bode::measure). A small live view shows both channels all the time.
class BodeWindow : public QMainWindow {
    Q_OBJECT

  public:
    BodeWindow(HantekDsoControl *dsoControl, const Dso::ControlSpecification *spec, SampleTap *tap,
               QWidget *parent = nullptr);

    bool running() const { return sweeping; }

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    struct Result {
        double freq = 0;
        bode::Phasor h{1, 0};
        double vppIn = 0, vppOut = 0, snrIn = 0, snrOut = 0;
        bool valid = false, aliased = false;
        QString problem;
        double gainDb() const;
        double phaseDeg() const;
    };

    // interface
    QWidget *makeSettings();
    void loadSettings();
    void saveSettings();
    void updateLimits();
    void updateRoles();
    void updateCalibrationUi();
    void setUiRunning(bool running);
    void status(const QString &text, bool error = false);
    bool readFrequency(QLineEdit *edit, QComboBox *unit, double &hz) const;

    // generator connection
    void refreshPorts();
    void toggleGenerator();
    void updateGeneratorUi();
    void applyLevels(); ///< sends amplitude and offset to the generator channel now

    // oscilloscope
    void configureScope();
    void setGain(int channel, unsigned index);
    double voltsPerDiv(int channel) const;
    double fullScale(int channel) const; ///< peak volts at the probe tip that still fit the screen
    unsigned recordLength() const;
    double minRecordTime() const;
    double maxRecordTime() const;
    double minimumFrequency() const;
    void setRecordTime(double seconds);
    bool autoRange(const ScopeFrame &frame);
    void ignoreFrames(int count);

    // sweep
    void frameReady(quint64 sequence);
    void start(bool calibrationRun);
    void finish(bool completed, const QString &message = QString());
    void startPoint();
    void measureFrame(const ScopeFrame &frame);
    void finishPoint();
    void nextPoint();
    void armWatchdog();
    int refChannel() const;
    int outChannel() const;
    int genChannel() const;

    // results
    void exportCsv();
    void saveImage();
    void copyImage();
    void loadCalibration();
    void saveCalibration();

    HantekDsoControl *dso;
    const Dso::ControlSpecification *spec;
    SampleTap *tap;
    Psg9080 *gen;

    // widgets
    QComboBox *portBox;
    QPushButton *portRefresh, *connectButton;
    QLabel *generatorState;
    QComboBox *genChannelBox, *refChannelBox, *couplingBox, *memoryBox;
    QComboBox *probeBox[2];
    QLabel *outChannelLabel, *limitsLabel;
    QLineEdit *startEdit, *stopEdit;
    QComboBox *startUnit, *stopUnit;
    QSpinBox *perDecadeBox, *averagesBox;
    QDoubleSpinBox *amplitudeBox, *offsetBox;
    QCheckBox *useCalibrationBox;
    QLabel *calibrationLabel;
    QPushButton *startButton, *calibrateButton, *csvButton, *imageButton, *copyButton;
    QWidget *settingsPanel;
    QProgressBar *progress;
    QLabel *statusLabel;
    BodePlot *plot;
    ScopePreview *preview;
    QTimer *watchdog;
    QElapsedTimer previewClock; ///< limits the small screen to ~25 frames/s

    // oscilloscope state
    unsigned gainIndex[2] = {0, 0};
    double probe[2] = {1, 1};
    double recordTime = 1e-2;
    quint64 lastSequence = 0;
    quint64 ignoreUntil = 0; ///< frames with a sequence up to this may hold an old setting
    int idleRangeSteps = 0;

    // sweep state
    bool sweeping = false;
    bool calibrating = false;
    bool generatorWasOn = false;
    std::vector<double> freqs;
    size_t index = 0;
    int rangeSteps = 0;
    int attempts = 0;
    bool slowerTried = false;
    int count = 0;
    bode::Phasor sumH{0, 0};
    double sumIn = 0, sumOut = 0, minSnrIn = 1e9, minSnrOut = 1e9;
    bool anyAliased = false;

    std::vector<Result> results;
    std::vector<bode::ResponsePoint> calibration;
    QString calibrationInfo;
};
