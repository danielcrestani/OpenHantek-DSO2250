// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QWidget>

#include "sampletap.h"

/// \brief Small live view of both channels (to check the wiring and see clipping during a sweep).
class ScopePreview : public QWidget {
    Q_OBJECT

  public:
    explicit ScopePreview(QWidget *parent = nullptr);

    /// Show a frame. `voltsPerDiv` per channel; 8 vertical divisions, 10 horizontal.
    void setFrame(const ScopeFrame &frame, const double voltsPerDiv[2]);
    /// Names shown next to each channel (e.g. "entrada", "saída").
    void setRoles(const QString &ch1, const QString &ch2);

    QSize sizeHint() const override { return QSize(420, 170); }
    QSize minimumSizeHint() const override { return QSize(200, 110); }

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    ScopeFrame frame;
    double vdiv[2] = {1, 1};
    QString roles[2];
};
