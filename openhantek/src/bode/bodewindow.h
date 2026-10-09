// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QWidget>

#include <memory>
#include <vector>

#include "bode/bodeanalysis.h"
#include "bode/bodeplot.h"
#include "hantekdso/controlspecification.h"
#include "post/ppresult.h"

class DsoWidget;
class GeneratorDock;
class HantekDsoControl;
class HorizontalDock;
class Psg9080;
class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTimer;
class TriggerDock;
class VoltageDock;
struct DsoSettingsScope;

/// \brief Frequency response analyzer: PSG9080 sweeps the frequency, the DSO measures input and output.
///
/// For every frequency the window sets the generator, picks a timebase with about 10 periods on screen,
/// discards the acquisitions that may still hold the previous setting, adjusts V/div of both channels and
/// averages a few measurements (bode::measure). The scope is driven through the docks, so the screen and
/// the front panel follow the sweep; the previous scope settings are restored at the end.
class BodeWindow : public QWidget {
    Q_OBJECT

  public:
    BodeWindow(Psg9080 *generator, GeneratorDock *generatorDock, HantekDsoControl *dsoControl,
               DsoSettingsScope *scope, const Dso::ControlSpecification *spec, VoltageDock *voltageDock,
               HorizontalDock *horizontalDock, TriggerDock *triggerDock, DsoWidget *dsoWidget,
               QAction *samplingAction, QWidget *parent);

    bool running() const { return state != State::Idle; }
    /// New acquisition (GUI thread)
    void process(std::shared_ptr<PPresult> data);

  signals:
    void runningChanged(bool running);

  protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

  private:
    enum class State { Idle, Settling, Measuring };
    struct Result {
        double freq = 0;
        bode::Phasor h{1, 0};
        double vppIn = 0, vppOut = 0, snrIn = 0, snrOut = 0;
        bool valid = false, aliased = false;
        QString problem;
        double gainDb() const;
        double phaseDeg() const;
    };
    struct Saved {
        double timebase = 0;
        std::vector<unsigned> gain;
        std::vector<double> offset;
        std::vector<bool> used;
        Dso::TriggerMode triggerMode;
        bool triggerSpecial = false;
        unsigned triggerSource = 0;
        double triggerLevel = 0;
        bool wasSampling = false;
        bool generatorOutput = false;
    };

    // ui
    QWidget *makeSettings();
    void loadSettings();
    void saveSettings();
    void updateLimits();
    void updateOutputLabel();
    void updateCalibrationUi();
    void setUiRunning(bool running);
    void status(const QString &text, bool error = false);
    bool readFrequency(QLineEdit *edit, QComboBox *unit, double &hz);

    // sweep
    void start(bool calibrationRun);
    void stop();
    void finish(bool completed, const QString &message = QString());
    void startPoint();
    void setTimebaseFor(double freq);
    void changed(int discardFrames);
    bool autoRange(const DataChannel *in, const DataChannel *out);
    void finishPoint();
    void armWatchdog();
    double minRecordTime() const;
    double maxRecordTime() const;
    double minimumFrequency() const;
    int refChannel() const;
    int outChannel() const;
    int genChannel() const;

    // results
    void exportCsv();
    void saveImage();
    void copyImage();
    void loadCalibration();
    void saveCalibration();
    std::vector<BodePlot::Point> plotPoints() const;

    Psg9080 *gen;
    GeneratorDock *genDock;
    HantekDsoControl *dsoControl;
    DsoSettingsScope *scope;
    const Dso::ControlSpecification *spec;
    VoltageDock *voltageDock;
    HorizontalDock *horizontalDock;
    TriggerDock *triggerDock;
    DsoWidget *dsoWidget;
    QAction *samplingAction;

    // settings widgets
    QComboBox *genChannelBox, *refChannelBox;
    QLabel *outChannelLabel, *limitsLabel;
    QLineEdit *startEdit, *stopEdit;
    QComboBox *startUnit, *stopUnit;
    QSpinBox *perDecadeBox, *averagesBox;
    QDoubleSpinBox *amplitudeBox, *offsetBox;
    QCheckBox *autoRangeBox, *useCalibrationBox;
    QLabel *calibrationLabel;
    QPushButton *startButton, *calibrateButton, *csvButton, *imageButton, *copyButton;
    QWidget *settingsPanel;
    QProgressBar *progress;
    QLabel *statusLabel;
    BodePlot *plot;
    QTimer *watchdog;

    // sweep state
    State state = State::Idle;
    bool calibrating = false;
    Saved saved;
    std::vector<double> freqs;
    size_t index = 0;
    int discard = 0;
    int rangeSteps = 0;
    int attempts = 0;
    bool slowerTried = false;
    double recordTime = 0;
    // averaging of the current point
    int count = 0;
    bode::Phasor sumH{0, 0};
    double sumIn = 0, sumOut = 0, minSnrIn = 1e9, minSnrOut = 1e9;
    bool anyAliased = false;

    std::vector<Result> results;
    std::vector<bode::ResponsePoint> calibration;
    QString calibrationInfo;
};
