// SPDX-License-Identifier: GPL-2.0+

#include "bodeplot.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {

const QColor kGainColor(0xf0, 0x8c, 0x2e); // orange
const QColor kPhaseColor(0x3f, 0xb8, 0xe0); // blue

QString freqLabel(double f) {
    struct {
        double scale;
        const char *suffix;
    } units[] = {{1e6, "M"}, {1e3, "k"}, {1, ""}, {1e-3, "m"}};
    for (auto &u : units)
        if (f >= u.scale * 0.999) {
            QString s = QString::number(f / u.scale, 'g', 4);
            return s.replace('.', ',') + u.suffix;
        }
    return QString::number(f, 'g', 3).replace('.', ',');
}

QString freqText(double f) { return freqLabel(f) + "Hz"; }

QString number(double v, int decimals) { return QString::number(v, 'f', decimals).replace('.', ','); }

} // namespace

BodePlot::BodePlot(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void BodePlot::setFrequencyRange(double f1, double f2) {
    if (!(f1 > 0) || !(f2 > 0)) return;
    fMin = std::min(f1, f2);
    fMax = std::max(f1, f2);
    if (fMax / fMin < 10) { // at least one decade on screen
        const double c = std::sqrt(fMin * fMax);
        fMin = c / std::sqrt(10.0);
        fMax = c * std::sqrt(10.0);
    }
    update();
}

void BodePlot::setPoints(const std::vector<Point> &p) {
    points = p;
    hoverIndex = -1;
    update();
}

void BodePlot::addPoint(const Point &p) {
    points.push_back(p);
    update();
}

void BodePlot::clear() {
    points.clear();
    hoverIndex = -1;
    update();
}

void BodePlot::setNote(const QString &n) {
    note = n;
    update();
}

double BodePlot::xOf(double f, const QRectF &r) const {
    return r.left() + r.width() * (std::log10(f) - std::log10(fMin)) / (std::log10(fMax) - std::log10(fMin));
}

double BodePlot::yOf(double v, const Pane &p) const {
    return p.rect.bottom() - p.rect.height() * (v - p.min) / (p.max - p.min);
}

void BodePlot::gainRange(double &lo, double &hi) const {
    lo = 1e9;
    hi = -1e9;
    for (const Point &p : points)
        if (p.valid) {
            lo = std::min(lo, p.gainDb);
            hi = std::max(hi, p.gainDb);
        }
    if (lo > hi) { // no data yet
        lo = -40;
        hi = 10;
        return;
    }
    lo = std::floor((lo - 1) / 10.0) * 10.0;
    hi = std::ceil((hi + 1) / 10.0) * 10.0;
    if (hi - lo < 20) lo = hi - 20;
}

void BodePlot::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), Qt::black); // like the OpenHantek screen

    const double left = 58, right = 22, top = 10, bottom = 46, gap = 26;
    const double h = (height() - top - bottom - gap) / 2.0;
    gainPane.rect = QRectF(left, top, width() - left - right, h);
    phasePane.rect = QRectF(left, top + h + gap, width() - left - right, h);
    gainRange(gainPane.min, gainPane.max);
    const double span = gainPane.max - gainPane.min;
    const double gainStep = span <= 40 ? 5 : (span <= 100 ? 10 : 20);

    drawPane(p, gainPane, true, tr("Ganho"), "dB", gainStep);
    drawPane(p, phasePane, false, tr("Fase"), QString::fromUtf8("°"), 45);

    if (!note.isEmpty()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(gainPane.rect.adjusted(0, 4, -6, 0), Qt::AlignRight | Qt::AlignTop, note);
    }

    // Hover readout
    if (hoverIndex >= 0 && hoverIndex < (int)points.size()) {
        const Point &pt = points[hoverIndex];
        const double x = xOf(pt.freq, gainPane.rect);
        QPen cursor(palette().color(QPalette::WindowText));
        cursor.setStyle(Qt::DashLine);
        cursor.setWidthF(1.0);
        p.setPen(cursor);
        p.drawLine(QPointF(x, gainPane.rect.top()), QPointF(x, phasePane.rect.bottom()));
        const QString text = pt.valid ? QString("%1   %2 dB   %3%4")
                                            .arg(freqText(pt.freq), number(pt.gainDb, 2), number(pt.phaseDeg, 1),
                                                 QString::fromUtf8("°"))
                                      : QString("%1   %2").arg(freqText(pt.freq), tr("medida inválida"));
        QFontMetricsF fm(font());
        QRectF box(0, 0, fm.horizontalAdvance(text) + 12, fm.height() + 6);
        box.moveTop(gainPane.rect.top() + 4);
        box.moveLeft(std::min(x + 8, gainPane.rect.right() - box.width()));
        p.setPen(Qt::NoPen);
        QColor bg = palette().color(QPalette::ToolTipBase);
        bg.setAlpha(230);
        p.setBrush(bg);
        p.drawRoundedRect(box, 4, 4);
        p.setPen(palette().color(QPalette::ToolTipText));
        p.drawText(box, Qt::AlignCenter, text);
    }
}

void BodePlot::drawPane(QPainter &p, const Pane &pane, bool gain, const QString &title, const QString &unit,
                        double tickStep) {
    const QRectF &r = pane.rect;
    const QColor text = palette().color(QPalette::WindowText);
    QColor grid = text;
    grid.setAlpha(45);
    QColor gridMinor = text;
    gridMinor.setAlpha(20);

    // Vertical grid: decades (strong) and 2..9 (light)
    const int d0 = (int)std::floor(std::log10(fMin)), d1 = (int)std::ceil(std::log10(fMax));
    for (int d = d0; d <= d1; ++d)
        for (int m = 1; m <= 9; ++m) {
            const double f = m * std::pow(10.0, d);
            if (f < fMin * 0.999 || f > fMax * 1.001) continue;
            const double x = xOf(f, r);
            p.setPen(QPen(m == 1 ? grid : gridMinor, 1));
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
            if (!gain && (m == 1 || ((d1 - d0) <= 3 && (m == 2 || m == 5)))) {
                p.setPen(text);
                p.drawText(QRectF(x - 30, r.bottom() + 3, 60, 16), Qt::AlignHCenter | Qt::AlignTop, freqLabel(f));
            }
        }

    // Horizontal grid and y labels
    for (double v = std::ceil(pane.min / tickStep) * tickStep; v <= pane.max + 1e-9; v += tickStep) {
        const double y = yOf(v, pane);
        p.setPen(QPen(std::fabs(v) < 1e-9 ? grid.darker(80) : grid, std::fabs(v) < 1e-9 ? 1.5 : 1));
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        p.setPen(text);
        p.drawText(QRectF(0, y - 8, r.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(v));
    }

    // Frame and title
    p.setPen(QPen(text, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
    p.setPen(gain ? kGainColor : kPhaseColor);
    QFont bold = font();
    bold.setBold(true);
    p.setFont(bold);
    p.drawText(r.adjusted(6, 4, 0, 0), Qt::AlignLeft | Qt::AlignTop, title + " (" + unit + ")");
    p.setFont(font());
    if (!gain) { // axis caption centered under the frequency labels
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(QRectF(r.left(), r.bottom() + 22, r.width(), 18), Qt::AlignHCenter | Qt::AlignTop,
                   tr("Frequência (Hz)"));
    }

    // Curve: lines between consecutive valid points, dots on every valid point
    p.save();
    p.setClipRect(r.adjusted(-1, -1, 1, 1));
    const QColor color = gain ? kGainColor : kPhaseColor;
    QPainterPath path;
    bool penDown = false;
    double lastPhase = 0;
    for (const Point &pt : points) {
        if (!pt.valid) {
            penDown = false;
            continue;
        }
        const double v = gain ? pt.gainDb : pt.phaseDeg;
        const QPointF q(xOf(pt.freq, r), yOf(v, pane));
        // do not draw a vertical line across the pane when the phase wraps at +-180
        if (penDown && !gain && std::fabs(v - lastPhase) > 180) penDown = false;
        if (penDown)
            path.lineTo(q);
        else
            path.moveTo(q);
        penDown = true;
        lastPhase = v;
    }
    p.setPen(QPen(color, 2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    for (const Point &pt : points)
        if (pt.valid) p.drawEllipse(QPointF(xOf(pt.freq, r), yOf(gain ? pt.gainDb : pt.phaseDeg, pane)), 2.5, 2.5);
    p.restore();
}

void BodePlot::mouseMoveEvent(QMouseEvent *event) {
    int best = -1;
    double bestDist = 1e9;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const double mx = event->position().x();
#else
    const double mx = event->localPos().x();
#endif
    for (size_t i = 0; i < points.size(); ++i) {
        const double d = std::fabs(xOf(points[i].freq, gainPane.rect) - mx);
        if (d < bestDist) {
            bestDist = d;
            best = (int)i;
        }
    }
    if (bestDist > 30) best = -1;
    if (best != hoverIndex) {
        hoverIndex = best;
        update();
    }
}

void BodePlot::leaveEvent(QEvent *) {
    hoverIndex = -1;
    update();
}
