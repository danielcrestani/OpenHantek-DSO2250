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
class QButtonGroup;
class QComboBox;
class QLabel;
class QPushButton;
class QTimer;
class QGroupBox;
class HantekDsoControl;
class VoltageDock;
class HorizontalDock;
class TriggerDock;
class DsoWidget;
struct DsoSettingsView;

/// \brief Front panel with oscilloscope-like buttons (RUN/STOP, AUTOSET, V/div, s/div, trigger...).
/// It drives the existing docks so that every change keeps the whole program consistent.
class FrontPanelDock : public QDockWidget {
    Q_OBJECT

  public:
    FrontPanelDock(DsoSettingsScope *scope, const Dso::ControlSpecification *spec, HantekDsoControl *dsoControl,
                   VoltageDock *voltageDock, HorizontalDock *horizontalDock, TriggerDock *triggerDock,
                   DsoWidget *dsoWidget, QAction *samplingAction, const std::vector<QColor> &channelColors,
                   QWidget *parent);

    /// New data arrived: update readouts and the values used by AUTOSET / 50%
    void showData(std::shared_ptr<PPresult> data);
    /// Show the current grid contrast level on the button (0 normal, 1 média, 2 alta)
    void setGridContrastLevel(int level);
    /// View settings (used for the interpolation button)
    void setViewSettings(DsoSettingsView *view);

  signals:
    void gridContrastRequested(int level);

  private:
    struct ChannelUi {
        QPushButton *onButton = nullptr;
        QPushButton *couplingButton = nullptr;
        QPushButton *invertButton = nullptr;
        QButtonGroup *probeGroup = nullptr;
        QLabel *vdivLabel = nullptr;
        QLabel *posLabel = nullptr;
    };
    struct ChannelStats {
        bool valid = false;
        double vmin = 0, vmax = 0, mean = 0, rms = 0, freq = 0;
    };

    QGroupBox *makeChannelBox(ChannelID ch);
    QGroupBox *makeRunBox();
    QGroupBox *makeHorizontalBox();
    QGroupBox *makeTriggerBox();
    QGroupBox *makeDisplayBox();
    QPushButton *makeButton(const QString &text, const QString &tip, bool checkable = false);

    void refresh();
    void stepGain(ChannelID ch, int dir);
    void stepOffset(ChannelID ch, double delta);
    void changeProbe(ChannelID ch, double probe);
    void stepTriggerLevel(int dir);
    void triggerLevelTo50();
    void autoset();
    void runStop();
    void single();
    QString channelColorCss(ChannelID ch) const;

    DsoSettingsScope *scope;
    const Dso::ControlSpecification *spec;
    HantekDsoControl *dsoControl;
    VoltageDock *voltageDock;
    HorizontalDock *horizontalDock;
    TriggerDock *triggerDock;
    DsoWidget *dsoWidget;
    QAction *samplingAction;
    std::vector<QColor> colors;

    std::vector<ChannelUi> channelUi;
    std::vector<ChannelStats> stats;

    QPushButton *runButton = nullptr;
    QPushButton *singleButton = nullptr;
    QLabel *timebaseLabel = nullptr;
    QLabel *pretriggerLabel = nullptr;
    QLabel *triggerLevelLabel = nullptr;
    QButtonGroup *modeGroup = nullptr;
    QButtonGroup *sourceGroup = nullptr;
    QButtonGroup *slopeGroup = nullptr;
    QPushButton *hfRejectButton = nullptr;
    QTimer *refreshTimer = nullptr;
    QPushButton *gridButton = nullptr;
    QPushButton *interpButton = nullptr;
    DsoSettingsView *view = nullptr;
    void updateInterpButton();
    int gridLevel = 0;
};
