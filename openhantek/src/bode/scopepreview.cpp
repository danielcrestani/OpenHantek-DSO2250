// SPDX-License-Identifier: GPL-2.0+

#include "scopepreview.h"

#include "style/darkstyle.h"

#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace {
const QColor kChannelColor[2] = {QColor(darkstyle::scopeChannel(0)), QColor(darkstyle::scopeChannel(1))};

QString siText(double v, const QString &unit) {
    const struct {
        double scale;
        const char *prefix;
    } p[] = {{1, ""}, {1e-3, "m"}, {1e-6, "µ"}, {1e-9, "n"}};
    for (const auto &x : p)
        if (std::fabs(v) >= x.scale * 0.999 || x.scale == 1e-9)
            return QString::number(v / x.scale, 'g', 3).replace('.', ',') + " " + QString::fromUtf8(x.prefix) + unit;
    return QString::number(v, 'g', 3) + " " + unit;
}
} // namespace

ScopePreview::ScopePreview(QWidget *parent) : QWidget(parent) { setAttribute(Qt::WA_OpaquePaintEvent); }

void ScopePreview::setFrame(const ScopeFrame &f, const double voltsPerDiv[2]) {
    frame = f;
    vdiv[0] = voltsPerDiv[0];
    vdiv[1] = voltsPerDiv[1];
    update();
}

void ScopePreview::setRoles(const QString &ch1, const QString &ch2) {
    roles[0] = ch1;
    roles[1] = ch2;
    update();
}

void ScopePreview::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black); // like the OpenHantek screen
    const QRectF r = QRectF(rect()).adjusted(4, 4, -4, -20);

    // Grid: 10 x 8 divisions
    QPen grid(QColor(255, 255, 255, 40));
    p.setPen(grid);
    for (int i = 0; i <= 10; ++i) p.drawLine(QPointF(r.left() + r.width() * i / 10, r.top()),
                                             QPointF(r.left() + r.width() * i / 10, r.bottom()));
    for (int i = 0; i <= 8; ++i) p.drawLine(QPointF(r.left(), r.top() + r.height() * i / 8),
                                            QPointF(r.right(), r.top() + r.height() * i / 8));
    p.setPen(QPen(QColor(255, 255, 255, 90)));
    p.drawLine(QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y()));

    // Traces: min/max per pixel column (no peak disappears when the record is compressed). Drawn without
    // antialiasing, as a polyline of whole pixels: a noisy trace is thousands of tall zigzags, and stroking that
    // with antialiasing took most of the GUI time (menus froze).
    const int columns = std::max(1, (int)r.width());
    QPolygonF line;
    line.reserve(2 * columns);
    for (size_t ch = 0; ch < 2 && ch < frame.data.size(); ++ch) {
        const std::vector<double> &x = frame.data[ch];
        if (x.empty()) continue;
        const double perDiv = vdiv[ch] > 0 ? vdiv[ch] : 1;
        auto y = [&](double v) {
            const double clamped = std::max(-4.2, std::min(4.2, v / perDiv));
            return std::round(r.center().y() - clamped * r.height() / 8.0);
        };
        line.clear();
        const size_t n = x.size();
        for (int c = 0; c < columns; ++c) {
            const size_t a = n * (size_t)c / (size_t)columns;
            const size_t b = std::min(n, std::max(a + 1, n * (size_t)(c + 1) / (size_t)columns));
            if (a >= n) break;
            const auto mm = std::minmax_element(x.begin() + a, x.begin() + b);
            const double px = r.left() + c;
            line << QPointF(px, y(*mm.second)) << QPointF(px, y(*mm.first));
        }
        p.setPen(QPen(kChannelColor[ch], 1));
        p.drawPolyline(line);
    }

    // Labels under the screen
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.9);
    p.setFont(f);
    double x0 = r.left();
    for (int ch = 0; ch < 2; ++ch) {
        const QString text = QString("CH%1 %2/div%3")
                                 .arg(ch + 1)
                                 .arg(siText(vdiv[ch], "V"))
                                 .arg(roles[ch].isEmpty() ? QString() : " (" + roles[ch] + ")");
        p.setPen(kChannelColor[ch]);
        p.drawText(QRectF(x0, r.bottom() + 3, r.width() / 2.6, 16), Qt::AlignLeft | Qt::AlignVCenter, text);
        x0 += r.width() / 2.6;
    }
    if (frame.samplerate > 0 && !frame.data.empty() && !frame.data[0].empty()) {
        const double record = frame.data[0].size() / frame.samplerate;
        p.setPen(QColor(220, 220, 220));
        p.drawText(QRectF(r.left(), r.bottom() + 3, r.width(), 16), Qt::AlignRight | Qt::AlignVCenter,
                   siText(record / 10.0, "s") + "/div");
    }
}
