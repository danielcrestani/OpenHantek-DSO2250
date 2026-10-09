// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QWidget>

#include <vector>

/// \brief Bode plot: gain (dB) on top, phase (degrees) below, logarithmic frequency axis. QPainter only.
class BodePlot : public QWidget {
    Q_OBJECT

  public:
    struct Point {
        double freq = 0;
        double gainDb = 0;
        double phaseDeg = 0;
        bool valid = false;
    };

    explicit BodePlot(QWidget *parent = nullptr);

    /// Frequency span shown (the sweep limits), in Hz.
    void setFrequencyRange(double f1, double f2);
    void setPoints(const std::vector<Point> &points);
    void addPoint(const Point &p);
    void clear();
    /// Text in the corner of the gain pane (e.g. "calibrado").
    void setNote(const QString &note);

    QSize sizeHint() const override { return QSize(720, 480); }
    QSize minimumSizeHint() const override { return QSize(360, 260); }

  protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *) override;

  private:
    struct Pane {
        QRectF rect;
        double min, max; ///< value range of the y axis
    };
    double xOf(double f, const QRectF &r) const;
    double yOf(double v, const Pane &p) const;
    void drawPane(QPainter &p, const Pane &pane, bool gain, const QString &title, const QString &unit,
                  double tickStep);
    void gainRange(double &lo, double &hi) const;

    std::vector<Point> points;
    double fMin = 10, fMax = 1e6;
    QString note;
    int hoverIndex = -1;
    Pane gainPane{{}, -40, 10}, phasePane{{}, -180, 180};
};
