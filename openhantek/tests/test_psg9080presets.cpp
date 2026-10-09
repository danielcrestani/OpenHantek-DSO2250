// SPDX-License-Identifier: GPL-2.0+
// The C++ presets must read the file written by the Python psg-gui (and the other way around).

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "generator/psg9080presets.h"
#include "testing.h"

// Written by psg_gui (Python): first format (no "kind"), a "both" preset and a single-channel preset
static const char *kPythonFile = R"({
  "testequadrada": {
    "ch1": {"waveform": 1, "frequency_hz": "1000", "freq_unit": 1, "amplitude": "5", "offset": "0",
            "duty": "50", "phase": "0", "enabled": true},
    "ch2": {"waveform": 0, "frequency_hz": "10000.000", "freq_unit": 0, "amplitude": "1.234",
            "offset": "-1.5", "duty": "33.33", "phase": "90", "enabled": false}
  },
  "ambos": {
    "kind": "both",
    "ch1": {"waveform": 3, "frequency_hz": "0.025786", "freq_unit": 3, "amplitude": "2", "offset": "0",
            "duty": "50", "phase": "0", "enabled": false},
    "ch2": {"waveform": 105, "frequency_hz": "2578600", "freq_unit": 2, "amplitude": "0.5", "offset": "0.25",
            "duty": "50", "phase": "180", "enabled": true}
  },
  "rampa 50 Hz": {
    "kind": "channel", "source": 2,
    "channel": {"waveform": 4, "frequency_hz": "50", "freq_unit": 0, "amplitude": "3.3", "offset": "1",
                "duty": "50", "phase": "0", "enabled": true}
  }
})";

int main() {
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString path = dir.filePath("psg9080-gui/presets.json");
    QDir().mkpath(dir.filePath("psg9080-gui"));
    {
        QFile f(path);
        CHECK(f.open(QIODevice::WriteOnly));
        f.write(kPythonFile);
    }

    Psg9080PresetStore store(path);
    QString error;
    QMap<QString, Psg9080Preset> all = store.load(&error);
    CHECK(error.isEmpty());
    CHECK_EQ(all.size(), 3);

    const Psg9080Preset old = all.value("testequadrada");
    CHECK(!old.isSingle());
    CHECK_EQ(old.states.size(), (size_t)2);
    CHECK_EQ(old.states[0].waveform, 1);
    CHECK_NEAR(old.states[0].amplitude, 5.0, 1e-12);
    CHECK(old.states[0].enabled);
    CHECK_NEAR(old.states[1].offset, -1.5, 1e-12);
    CHECK_NEAR(old.states[1].duty, 33.33, 1e-12);
    CHECK_EQ(old.label().toStdString(), std::string("CH1 + CH2"));

    const Psg9080Preset both = all.value("ambos");
    CHECK(both.states[0].unit == psg9080::FreqUnit::mHz);
    CHECK_NEAR(both.states[0].frequency, 0.025786, 1e-15);
    CHECK_EQ(both.states[1].waveform, 105);
    CHECK_NEAR(both.states[1].frequency, 2578600.0, 1e-9);

    const Psg9080Preset single = all.value("rampa 50 Hz");
    CHECK(single.isSingle());
    CHECK_EQ(single.source, 2);
    CHECK_NEAR(single.states[0].amplitude, 3.3, 1e-12);
    CHECK_EQ(single.label().toStdString(), std::string("CH2"));

    // Round trip through the C++ writer, and the Python reader's expectations (strings, "kind")
    CHECK(store.put("novo", single, &error));
    all = store.load(&error);
    CHECK_EQ(all.size(), 4);
    CHECK_NEAR(all.value("novo").states[0].amplitude, 3.3, 1e-12);
    QFile f(path);
    CHECK(f.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject novo = root.value("novo").toObject();
    CHECK_EQ(novo.value("kind").toString().toStdString(), std::string("channel"));
    CHECK(novo.value("channel").toObject().value("amplitude").isString()); // Python reads Decimal(str)
    CHECK_EQ(novo.value("channel").toObject().value("amplitude").toString().toStdString(), std::string("3.3"));
    CHECK_EQ(root.value("testequadrada").toObject().value("kind").toString().toStdString(), std::string("both"));

    CHECK(store.remove("novo", &error));
    CHECK_EQ(store.load().size(), 3);
    return testing::report("psg9080presets");
}
