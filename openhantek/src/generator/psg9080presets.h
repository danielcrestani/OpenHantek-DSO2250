// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QMap>
#include <QString>

#include <vector>

#include "psg9080.h"

/// \brief Saved PSG9080 configurations: one channel (applied to CH1 or CH2) or both.
///
/// Stored in ~/.config/psg9080-gui/presets.json, the same file and format as the Python psg-gui, so presets
/// saved by either program show up in the other.
struct Psg9080Preset {
    std::vector<Psg9080::ChannelState> states; ///< one state (single channel) or two (CH1, CH2)
    int source = 1;                            ///< channel a single-channel preset was saved from

    bool isSingle() const { return states.size() == 1; }
    QString label() const { return isSingle() ? QString("CH%1").arg(source) : QString("CH1 + CH2"); }

    /// Write to the generator. `target` (1 or 2) picks the channel for a single-channel preset.
    /// Outputs are switched off first and set last, so the circuit never sees a half-configured signal;
    /// a single-channel preset touches only the target channel.
    bool apply(Psg9080 *generator, int target = 0) const;
};

class Psg9080PresetStore {
  public:
    /// Default: $XDG_CONFIG_HOME/psg9080-gui/presets.json (~/.config/...)
    explicit Psg9080PresetStore(const QString &path = QString());

    QMap<QString, Psg9080Preset> load(QString *error = nullptr) const;
    bool save(const QMap<QString, Psg9080Preset> &presets, QString *error = nullptr) const;
    bool put(const QString &name, const Psg9080Preset &preset, QString *error = nullptr) const;
    bool remove(const QString &name, QString *error = nullptr) const;
    QString path() const { return file; }

  private:
    QString file;
};

/// Write all parameters of one channel (waveform, frequency, amplitude, offset, duty, phase).
bool writeChannelState(Psg9080 *generator, int channel, const Psg9080::ChannelState &s);
