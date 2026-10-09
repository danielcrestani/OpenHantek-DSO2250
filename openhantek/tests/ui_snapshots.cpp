// SPDX-License-Identifier: GPL-2.0+
// Renders the PSG9080 window and the Bode plot/preview with synthetic data (no hardware) to JPEG files,
// so the look can be checked in CI (QT_QPA_PLATFORM=offscreen).

#include <QApplication>
#include <QDir>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>

#include <cmath>

#include "apps/psg9080window.h"
#include "bode/bodeplot.h"
#include "bode/scopepreview.h"
#include "style/darkstyle.h"

static void save(QWidget *w, const QString &name, QSize size) {
    w->resize(size);
    w->show();
    QApplication::processEvents();
    w->grab().scaledToWidth(std::min(size.width(), 900), Qt::SmoothTransformation).save(name, "JPG", 72);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    darkstyle::applyApplicationLook(app);
    const QString dir = argc > 1 ? argv[1] : QString(".");
    QDir().mkpath(dir);

    Psg9080Window gen;
    save(&gen, dir + "/psg9080.jpg", QSize(900, 470));

    // Bode plot: RC low pass with fc = 1 kHz, 10 Hz .. 100 kHz
    QWidget bode;
    bode.setStyleSheet(darkstyle::panelSheet());
    QVBoxLayout *v = new QVBoxLayout(&bode);
    BodePlot *plot = new BodePlot;
    plot->setFrequencyRange(10, 1e5);
    std::vector<BodePlot::Point> pts;
    for (int i = 0; i <= 40; ++i) {
        const double f = std::pow(10.0, 1 + i * 0.1), x = f / 1000.0;
        pts.push_back({f, -10 * std::log10(1 + x * x), -std::atan(x) * 180 / 3.14159265358979, i != 23});
    }
    plot->setPoints(pts);
    plot->setNote("calibrado");
    ScopePreview *preview = new ScopePreview;
    ScopeFrame frame;
    frame.samplerate = 1e6;
    frame.data.resize(2);
    for (int k = 0; k < 10240; ++k) {
        const double t = k / 1e6, w = 2 * 3.14159265358979 * 1000;
        frame.data[0].push_back(0.5 * std::sin(w * t));
        frame.data[1].push_back(0.354 * std::sin(w * t - 3.14159265358979 / 4));
    }
    const double vdiv[2] = {0.2, 0.2};
    preview->setFrame(frame, vdiv);
    preview->setRoles("entrada", "saída");
    v->addWidget(plot, 3);
    v->addWidget(preview, 1);
    save(&bode, dir + "/bode.jpg", QSize(900, 640));
    return 0;
}
