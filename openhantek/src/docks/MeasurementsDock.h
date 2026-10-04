// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QDockWidget>
#include <QColor>
#include <memory>
#include <vector>

#include "hantekdso/controlspecification.h"
#include "post/ppresult.h"
#include "scopesettings.h"

class QAction;
class QMenu;
class QTableWidget;

/// \brief Automatic measurements (Vpp, RMS, frequency, duty cycle, rise time...) shown as a table.
/// The measurements to show are chosen in the "Medições" menu.
class MeasurementsDock : public QDockWidget {
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

    MeasurementsDock(const DsoSettingsScope *scope, const Dso::ControlSpecification *spec,
                     const std::vector<QColor> &channelColors, QWidget *parent);

    /// Menu with one checkable action per measurement (to be inserted in the menu bar)
    QMenu *menu() const { return measMenu; }

    void showData(std::shared_ptr<PPresult> data);

  private:
    struct Result {
        bool valid = false;
        double v[COUNT];
    };
    static QString name(int m);
    static QString format(int m, double value);
    static Result analyze(const std::vector<double> &samples, double interval, double fallbackFreq);
    void rebuildRows();
    void updateTable();
    void saveSelection();

    const DsoSettingsScope *scope;
    const Dso::ControlSpecification *spec;
    std::vector<QColor> colors;
    QTableWidget *table;
    QMenu *measMenu;
    std::vector<QAction *> actions;
    std::vector<int> rows; ///< measurement shown in each row
    std::vector<Result> results;
};
