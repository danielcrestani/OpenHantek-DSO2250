// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QMutex>
#include <QObject>

#include <vector>

struct DSOsamples;

/// \brief One acquisition copied out of the DSO driver.
struct ScopeFrame {
    std::vector<std::vector<double>> data; ///< volts at the probe tip, per channel
    double samplerate = 0;                 ///< S/s
    quint64 sequence = 0;                  ///< increases by one for every acquisition
};

/// \brief Copies every acquisition in the DSO thread and tells the GUI thread that a new one is ready.
///
/// HantekDsoControl reuses its sample buffer, so the samples are copied right away (DirectConnection, under the
/// buffer's read lock); the GUI receives frameReady() through the event loop and takes the newest frame. The
/// sequence number lets the user of the frames count acquisitions exactly, even if some are skipped.
class SampleTap : public QObject {
    Q_OBJECT

  public:
    explicit SampleTap(QObject *parent = nullptr) : QObject(parent) {}

    /// Connect this to HantekDsoControl::samplesAvailable with Qt::DirectConnection.
    void capture(const DSOsamples *samples);
    /// Newest frame (GUI thread). Returns false if nothing arrived yet.
    bool latest(ScopeFrame &frame) const;

  signals:
    void frameReady(quint64 sequence);

  private:
    mutable QMutex mutex;
    ScopeFrame frame;
};
