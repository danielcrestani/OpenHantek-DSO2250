// SPDX-License-Identifier: GPL-2.0+

#include "scopepreview.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {
const QColor kChannelColor[2] = {QColor(0xe6, 0xb8, 0x00), QColor(0x2f, 0xa8, 0xd8)};

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
    p.fillRect(rect(), QColor(0x10, 0x14, 0x18));
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

    // Traces: min/max per pixel column (no peak disappears when the record is compressed)
    p.setRenderHint(QPainter::Antialiasing, true);
    const int columns = std::max(1, (int)r.width());
    for (size_t ch = 0; ch < 2 && ch < frame.data.size(); ++ch) {
        const std::vector<double> &x = frame.data[ch];
        if (x.empty()) continue;
        const double perDiv = vdiv[ch] > 0 ? vdiv[ch] : 1;
        auto y = [&](double v) {
            const double clamped = std::max(-4.2, std::min(4.2, v / perDiv));
            return r.center().y() - clamped * r.height() / 8.0;
        };
        QPainterPath path;
        for (int c = 0; c < columns; ++c) {
            const size_t a = x.size() * (size_t)c / (size_t)columns;
            const size_t b = std::max(a + 1, x.size() * (size_t)(c + 1) / (size_t)columns);
            double mn = x[a], mx = x[a];
            for (size_t k = a; k < b && k < x.size(); ++k) {
                mn = std::min(mn, x[k]);
                mx = std::max(mx, x[k]);
            }
            const double px = r.left() + c + 0.5;
            if (c == 0)
                path.moveTo(px, y(mx));
            else
                path.lineTo(px, y(mx));
            path.lineTo(px, y(mn));
        }
        p.setPen(QPen(kChannelColor[ch], 1.2));
        p.drawPath(path);
    }

    // Labels under the screen
    p.setRenderHint(QPainter::Antialiasing, false);
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
