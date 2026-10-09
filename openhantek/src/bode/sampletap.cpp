// SPDX-License-Identifier: GPL-2.0+

#include "sampletap.h"

#include <QMutexLocker>

#include "hantekdso/dsosamples.h"

void SampleTap::capture(const DSOsamples *samples) {
    if (!samples) return;
    quint64 seq;
    {
        QReadLocker read(&samples->lock);
        QMutexLocker lock(&mutex);
        frame.data = samples->data;
        frame.samplerate = samples->samplerate;
        seq = ++frame.sequence;
    }
    emit frameReady(seq); // queued to the GUI thread (this object lives there)
}

bool SampleTap::latest(ScopeFrame &out) const {
    QMutexLocker lock(&mutex);
    if (frame.sequence == 0) return false;
    out = frame;
    return true;
}
