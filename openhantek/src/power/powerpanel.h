// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QWidget>

#include <functional>
#include <vector>

#include "poweranalysis.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QTimer;

/// \brief Bars of the harmonics (% of the fundamental) of the voltage or the current.
class HarmonicBars : public QWidget {
  public:
    explicit HarmonicBars(QWidget *parent = nullptr);
    void setData(const std::vector<double> &rms, double thd, const QColor &color, const QString &title);

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    std::vector<double> values; ///< RMS of n = 1, 2, 3...
    double thd = 0;
    QColor color;
    QString title;
};

/// \brief Power analysis readout: V, I, f, P, S, Q, power factor, cos φ1, THD, harmonics and energy.
///
/// The voltage comes from one channel (differential probe) and the current from the other (current clamp).
/// Settings (voltage channel, clamp delay, current direction) are kept in QSettings.
class PowerPanel : public QWidget {
    Q_OBJECT

  public:
    explicit PowerPanel(QWidget *parent = nullptr);

    struct Input {
        std::vector<double> v, i; ///< volts and amperes, same sampling
        double dt = 0;            ///< s
        bool bothOn = true;       ///< both channels are on
        bool voltageIsVolts = true, currentIsAmperes = true;
        QString voltageName = "CH1", currentName = "CH2";
    };
    /// New acquisition. The analysis runs at most ~5 times per second (less for very long records); the last
    /// acquisition is always analyzed, also after STOP or SINGLE.
    void setData(const Input &input);
    /// Where the newest acquisition comes from: called when an analysis runs (false = no data).
    void setSource(std::function<bool(Input &)> source);
    /// A new acquisition arrived (the samples are fetched from the source when the analysis runs).
    void notifyData();
    /// 0 = CH1, 1 = CH2
    int voltageChannel() const;
    void setChannelColors(const QColor &ch1, const QColor &ch2);
    const power::Result &result() const { return last; }

  private:
    /// Analyze `pending`; `accumulate`: add to the energy (not when only a setting changed)
    void analyzePending(bool accumulate);
    void display(const power::Result &r, const Input &in);
    void showHarmonics();
    void resetEnergy();
    void saveSettings();

    QComboBox *voltageBox, *harmonicsBox;
    QLabel *currentLabel;
    QDoubleSpinBox *delayBox;
    QCheckBox *invertBox;
    struct Cell {
        QLabel *caption, *value, *note;
    };
    Cell cells[12];
    QLabel *message;
    HarmonicBars *bars;
    QColor colors[2] = {QColor("#ff3b3b"), QColor("#f2e600")};

    power::Result last;
    Input pending;
    bool hasPending = false;
    std::function<bool(Input &)> source;
    QTimer *timer;
    int intervalMs = 200;
    QElapsedTimer throttle, energyClock;
    double energy = 0;  ///< Wh
    double measured = 0; ///< s of measurement in the energy
};
