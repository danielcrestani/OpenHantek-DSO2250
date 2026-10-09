// SPDX-License-Identifier: GPL-2.0+

#include "psg9080presets.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

// The Python version writes decimals as strings ("1.5"); accept numbers too.
double number(const QJsonValue &v, double fallback) {
    if (v.isDouble()) return v.toDouble();
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        if (ok) return d;
    }
    return fallback;
}

QString decimalString(double v) {
    QString s = QString::number(v, 'f', 9);
    if (s.contains('.')) {
        while (s.endsWith('0')) s.chop(1);
        if (s.endsWith('.')) s.chop(1);
    }
    return s == "-0" ? QString("0") : s;
}

QJsonObject stateToJson(const Psg9080::ChannelState &s) {
    QJsonObject o;
    o["waveform"] = s.waveform;
    o["frequency_hz"] = decimalString(s.frequency);
    o["freq_unit"] = (int)s.unit;
    o["amplitude"] = decimalString(s.amplitude);
    o["offset"] = decimalString(s.offset);
    o["duty"] = decimalString(s.duty);
    o["phase"] = decimalString(s.phase);
    o["enabled"] = s.enabled;
    return o;
}

Psg9080::ChannelState stateFromJson(const QJsonObject &o) {
    Psg9080::ChannelState s;
    s.waveform = o.value("waveform").toInt(s.waveform);
    s.frequency = number(o.value("frequency_hz"), s.frequency);
    const int unit = o.value("freq_unit").toInt((int)psg9080::bestUnit(s.frequency));
    s.unit = (unit >= 0 && unit <= 4) ? (psg9080::FreqUnit)unit : psg9080::bestUnit(s.frequency);
    s.amplitude = number(o.value("amplitude"), s.amplitude);
    s.offset = number(o.value("offset"), s.offset);
    s.duty = number(o.value("duty"), s.duty);
    s.phase = number(o.value("phase"), s.phase);
    s.enabled = o.value("enabled").toBool(s.enabled);
    return s;
}

} // namespace

bool writeChannelState(Psg9080 *g, int channel, const Psg9080::ChannelState &s) {
    return g->setWaveform(channel, s.waveform) && g->setFrequency(channel, s.frequency, s.unit) &&
           g->setAmplitude(channel, s.amplitude) && g->setOffset(channel, s.offset) &&
           g->setDuty(channel, s.duty) && g->setPhase(channel, s.phase);
}

bool Psg9080Preset::apply(Psg9080 *g, int target) const {
    if (isSingle()) {
        const int ch = (target == 1 || target == 2) ? target : source;
        return g->setOutput(ch, false) && writeChannelState(g, ch, states[0]) && g->setOutput(ch, states[0].enabled);
    }
    if (states.size() != 2) return false;
    return g->setOutputs(false, false) && writeChannelState(g, 1, states[0]) && writeChannelState(g, 2, states[1]) &&
           g->setOutputs(states[0].enabled, states[1].enabled);
}

Psg9080PresetStore::Psg9080PresetStore(const QString &path) : file(path) {
    if (file.isEmpty())
        file = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/psg9080-gui/presets.json";
}

QMap<QString, Psg9080Preset> Psg9080PresetStore::load(QString *error) const {
    QMap<QString, Psg9080Preset> out;
    QFile f(file);
    if (!f.exists()) return out;
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QObject::tr("Não foi possível ler %1").arg(file);
        return out;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject()) {
        if (error) *error = QObject::tr("Arquivo de presets inválido (%1): %2").arg(file, pe.errorString());
        return out;
    }
    const QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Psg9080Preset p;
        if (o.value("kind").toString() == "channel") {
            p.states = {stateFromJson(o.value("channel").toObject())};
            p.source = o.value("source").toInt(1) == 2 ? 2 : 1;
        } else { // "both", or the first format (no "kind"), which only had both channels
            p.states = {stateFromJson(o.value("ch1").toObject()), stateFromJson(o.value("ch2").toObject())};
        }
        out.insert(it.key(), p);
    }
    return out;
}

bool Psg9080PresetStore::save(const QMap<QString, Psg9080Preset> &presets, QString *error) const {
    QJsonObject root;
    for (auto it = presets.begin(); it != presets.end(); ++it) {
        const Psg9080Preset &p = it.value();
        QJsonObject o;
        if (p.isSingle()) {
            o["kind"] = "channel";
            o["source"] = p.source;
            o["channel"] = stateToJson(p.states[0]);
        } else {
            o["kind"] = "both";
            o["ch1"] = stateToJson(p.states[0]);
            o["ch2"] = stateToJson(p.states[1]);
        }
        root[it.key()] = o;
    }
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file); // atomic: a crash never leaves a half-written file
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 ||
        !f.commit()) {
        if (error) *error = QObject::tr("Não foi possível gravar %1").arg(file);
        return false;
    }
    return true;
}

bool Psg9080PresetStore::put(const QString &name, const Psg9080Preset &preset, QString *error) const {
    QMap<QString, Psg9080Preset> all = load(error);
    all.insert(name, preset);
    return save(all, error);
}

bool Psg9080PresetStore::remove(const QString &name, QString *error) const {
    QMap<QString, Psg9080Preset> all = load(error);
    all.remove(name);
    return save(all, error);
}
