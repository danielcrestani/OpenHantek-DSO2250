// SPDX-License-Identifier: GPL-2.0+
// Renders the PSG9080 window and the Bode plot/preview with synthetic data (no hardware) to JPEG files,
// so the look can be checked in CI (QT_QPA_PLATFORM=offscreen).

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QTabWidget>

#include <algorithm>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>

#include <cmath>

#include "apps/psg9080window.h"
#include "power/powerpanel.h"
#include "bode/bodeplot.h"
#include "bode/scopepreview.h"
#include "style/darkstyle.h"

static void save(QWidget *w, const QString &name, QSize size) {
    w->resize(size);
    w->show();
    QApplication::processEvents();
    w->grab().scaledToWidth(std::min(size.width(), 900), Qt::SmoothTransformation).save(name, "JPG", 72);
}

static void logMessage(QtMsgType, const QMessageLogContext &, const QString &msg) {
    fprintf(stderr, "qt: %s\n", qPrintable(msg)); // stylesheet parse errors end up here
}

int main(int argc, char *argv[]) {
    qInstallMessageHandler(logMessage);
    QApplication app(argc, argv);
    for (const char *f : {":/style/arrow-down.png", ":/style/arrow-up.png", ":/style/check.png"})
        fprintf(stderr, "resource %s: %s\n", f, QFile::exists(f) ? "ok" : "MISSING");
    darkstyle::applyApplicationLook(app);
    const QString dir = argc > 1 ? argv[1] : QString(".");
    QDir().mkpath(dir);

    Psg9080Window gen;
    save(&gen, dir + "/psg9080.jpg", QSize(980, 620));

    // Same window as when connected: controls enabled, example values, CH1 output on
    for (QWidget *w : gen.findChildren<QWidget *>()) w->setEnabled(true);
    const auto spins = gen.findChildren<QDoubleSpinBox *>();
    const double values[] = {5.0, 0.0, 50.0, 0.0, 1.234, -1.5, 33.33, 90.0};
    for (int i = 0; i < spins.size() && i < 8; ++i) {
        QSignalBlocker b(spins[i]);
        spins[i]->setValue(values[i]);
    }
    // the frequency fields: right-aligned line edits that are not inside a spin box or a combo box
    for (QLineEdit *e : gen.findChildren<QLineEdit *>())
        if ((e->alignment() & Qt::AlignRight) && !qobject_cast<QAbstractSpinBox *>(e->parent()) &&
            !qobject_cast<QComboBox *>(e->parent()))
            e->setText("25,786");
    const auto buttons = gen.findChildren<QPushButton *>();
    for (QPushButton *b : buttons)
        if (b->isCheckable()) {
            QSignalBlocker block(b);
            b->setChecked(b == buttons[0] || b->text().contains("SAÍDA") && !b->isChecked() &&
                                                 b == *std::find_if(buttons.begin(), buttons.end(),
                                                                    [](QPushButton *x) { return x->isCheckable(); }));
            if (b->isChecked()) b->setText("SAÍDA LIGADA");
        }
    save(&gen, dir + "/psg9080-conectado.jpg", QSize(980, 620));

    // every other tab, enabled as when connected
    const char *tabNames[] = {"", "modulacao", "varredura", "frequencimetro", "ondas", "sequencias", "sistema"};
    for (int t = 1; t < gen.tabWidget()->count() && t < 7; ++t) {
        gen.tabWidget()->setCurrentIndex(t);
        QApplication::processEvents();
        for (QWidget *w : gen.tabWidget()->widget(t)->findChildren<QWidget *>()) w->setEnabled(true);
        const auto boxes = gen.tabWidget()->widget(t)->findChildren<QCheckBox *>();
        for (int k = 0; k < boxes.size(); k += 2) boxes[k]->setChecked(true); // show both states
        gen.tabWidget()->widget(t)->setEnabled(true);
        save(&gen, dir + "/psg9080-" + tabNames[t] + ".jpg", QSize(980, 620));
    }
    gen.tabWidget()->setCurrentIndex(0);

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

    // Power analysis: 127 V / 60 Hz with 2 % of 5th harmonic, 4 A lagging 25° with 40 % of 3rd and 20 % of 5th
    PowerPanel power;
    PowerPanel::Input in;
    in.dt = 1 / 50e3;
    for (int k = 0; k < 10240; ++k) {
        const double t = k * in.dt, w = 2 * 3.14159265358979 * 60, lag = 25 * 3.14159265358979 / 180;
        in.v.push_back(127 * 1.41421356 * (std::sin(w * t) + 0.02 * std::sin(5 * w * t)));
        in.i.push_back(4 * 1.41421356 * (std::sin(w * t - lag) + 0.4 * std::sin(3 * (w * t - lag)) +
                                         0.2 * std::sin(5 * (w * t - lag))));
    }
    power.setData(in);
    QApplication::processEvents(); // the analysis runs from a timer
    QTimer wait;
    wait.setSingleShot(true);
    wait.start(300);
    while (wait.isActive()) QApplication::processEvents();
    save(&power, dir + "/potencia.jpg", QSize(1150, 250));
    const power::Result &pr = power.result();
    fprintf(stderr, "potencia: valido=%d P=%.1f W S=%.1f VA Q1=%.1f var N=%.1f FP=%.3f cos=%.3f phi=%.1f THDi=%.1f %%\n",
            pr.valid, pr.p, pr.s, pr.q1, pr.n, pr.pf, pr.dpf, pr.phi1, pr.thdI);
    return 0;
}
