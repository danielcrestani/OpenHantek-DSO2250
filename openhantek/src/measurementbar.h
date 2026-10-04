// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QColor>
#include <QWidget>
#include <memory>
#include <vector>

#include "hantekdso/controlspecification.h"
#include "post/ppresult.h"
#include "scopesettings.h"

class QAction;
class QLabel;
class QMenu;

/// \brief Footer below the scope screen showing the measurements selected per channel,
/// in the channel color (like a bench oscilloscope). The selection is made in the "Medições" menu.
class MeasurementBar : public QWidget {
    Q_OBJECT

  public:
    enum Measurement {
        VPP,
        VMAX,
        VMIN,
        MEAN,
        RMS,
        ACRMS,
        FREQUENCY,
        PERIOD,
        DUTY,
        WIDTH_POS,
        WIDTH_NEG,
        RISE,
        FALL,
        COUNT
    };

    MeasurementBar(const DsoSettingsScope *scope, const Dso::ControlSpecification *spec,
                   const std::vector<QColor> &channelColors, QWidget *parent);

    /// "Medições" menu with one submenu per channel
    QMenu *menu() const { return measMenu; }

    void showData(std::shared_ptr<PPresult> data);

  private:
    struct Result {
        bool valid = false;
        double v[COUNT];
    };
    static QString name(int m);
    static QString shortName(int m);
    static QString format(int m, double value);
    static Result analyze(const std::vector<double> &samples, double interval, double fallbackFreq);
    void updateLabels();
    void saveSelection();
    bool selected(ChannelID ch, int m) const;

    const DsoSettingsScope *scope;
    const Dso::ControlSpecification *spec;
    std::vector<QColor> colors;
    QMenu *measMenu;
    std::vector<std::vector<QAction *>> actions; ///< [channel][measurement]
    std::vector<QLabel *> labels;                ///< one line per channel
    std::vector<Result> results;
};
