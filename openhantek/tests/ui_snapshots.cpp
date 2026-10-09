// SPDX-License-Identifier: GPL-2.0+
// Renders the PSG9080 window and the Bode plot/preview with synthetic data (no hardware) to JPEG files,
// so the look can be checked in CI (QT_QPA_PLATFORM=offscreen).

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>

#include <algorithm>
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

static void logMessage(QtMsgType, const QMessageLogContext &, const QString &msg) {
    fprintf(stderr, "qt: %s\n", qPrintable(msg)); // stylesheet parse errors end up here
}

int main(int argc, char *argv[]) {
    qInstallMessageHandler(logMessage);
    QApplication app(argc, argv);
    for (const char *f : {":/style/arrow-down@2x.png", ":/style/arrow-up@2x.png"})
        fprintf(stderr, "resource %s: %s\n", f, QFile::exists(f) ? "ok" : "MISSING");
    darkstyle::applyApplicationLook(app);
    const QString dir = argc > 1 ? argv[1] : QString(".");
    QDir().mkpath(dir);

    Psg9080Window gen;
    save(&gen, dir + "/psg9080.jpg", QSize(900, 470));

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
    save(&gen, dir + "/psg9080-conectado.jpg", QSize(900, 470));

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
