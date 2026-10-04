// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <memory>
#include <vector>

#include "hantekdso/controlspecification.h"
#include "post/ppresult.h"

class QDialog;
class QFile;
class QLabel;
class QPushButton;
class QTextStream;
class QWidget;
struct DsoSettingsScope;

/// \brief Per channel data logger: writes one CSV file per channel, started by hand or by the trigger.
/// Content: one line of measurements per acquisition, or the waveform on the screen.
class DataLogger : public QObject {
    Q_OBJECT

  public:
    enum class State { Idle, Armed, Recording };
    enum Content { MEASUREMENTS = 0, WAVEFORM = 1 };
    enum StartMode { MANUAL = 0, TRIGGER = 1 };

    struct Config {
        std::vector<bool> channels;   ///< which channels are logged
        int content = MEASUREMENTS;
        double intervalSec = 0.0;     ///< minimum time between records (0 = every acquisition)
        int startMode = MANUAL;
        int maxRecords = 0;           ///< stop after N records (0 = no limit)
        double maxMinutes = 0.0;      ///< stop after this time (0 = no limit)
        QString folder;
        QString prefix = "registro";
    };

    DataLogger(const DsoSettingsScope *scope, const Dso::ControlSpecification *spec, QObject *parent = nullptr);
    ~DataLogger();

    State state() const { return m_state; }
    /// Manual: starts recording. Trigger: arms (recording starts at the first trigger crossing)
    void start();
    void stop();
    /// New acquisition (GUI thread)
    void process(std::shared_ptr<PPresult> data);
    /// Configuration window (non modal)
    void showDialog(QWidget *parent);
    QString statusText() const;

  signals:
    void stateChanged();

  private:
    void loadConfig();
    void saveConfig();
    bool openFiles();
    void closeFiles();
    bool triggerCrossed(const PPresult *data) const;
    void writeRecord(const PPresult *data, const QDateTime &now);
    QString num(double v, int prec = 7) const;

    const DsoSettingsScope *scope;
    const Dso::ControlSpecification *spec;
    Config cfg;
    State m_state = State::Idle;

    std::vector<QFile *> files;           ///< per channel (nullptr = not logged)
    std::vector<QTextStream *> streams;
    std::vector<QString> fileNames;
    QDateTime startTime;
    QDateTime lastRecord;
    long records = 0;
    QString lastError;

    QDialog *dialog = nullptr;
    QLabel *statusLabel = nullptr;
    QPushButton *startButton = nullptr;
    QPushButton *stopButton = nullptr;
};
