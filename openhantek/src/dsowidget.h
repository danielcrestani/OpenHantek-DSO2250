// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QGridLayout>
#include <memory>

#include "glscope.h"
#include "levelslider.h"
#include "hantekdso/controlspecification.h"
#include "scopesettings.h"

class SpectrumGenerator;
struct DsoSettingsScope;
struct DsoSettingsView;
class DataGrid;

/// \brief The widget for the oszilloscope-screen
/// This widget contains the scopes and all level sliders.
class DsoWidget : public QWidget {
    Q_OBJECT

  public:

    struct Sliders {
        LevelSlider *offsetSlider;          ///< The sliders for the graph offsets
        LevelSlider *triggerPositionSlider; ///< The slider for the pretrigger
        LevelSlider *triggerLevelSlider;    ///< The sliders for the trigger level
        LevelSlider *markerSlider;          ///< The sliders for the markers
    };

    /// \brief Initializes the components of the oszilloscope-screen.
    /// \param settings The settings object containing the oscilloscope settings.
    /// \param dataAnalyzer The data analyzer that should be used as data source.
    /// \param parent The parent widget.
    /// \param flags Flags for the window manager.
    DsoWidget(DsoSettingsScope* scope, DsoSettingsView* view, const Dso::ControlSpecification* spec, QWidget *parent = 0, Qt::WindowFlags flags = Qt::WindowFlags());

    // Data arrived
    void showNew(std::shared_ptr<PPresult> data);

  protected:
    virtual void showEvent(QShowEvent *event);
    void setupSliders(Sliders &sliders);
    void adaptTriggerLevelSlider(DsoWidget::Sliders &sliders, ChannelID channel);
    void adaptTriggerPositionSlider();
    void setMeasurementVisible(ChannelID channel);
    void updateMarkerDetails();
    void updateSpectrumDetails(ChannelID channel);
    void updateTriggerDetails();
    void updateVoltageDetails(ChannelID channel);

    double mainToZoom(double position) const;
    double zoomToMain(double position) const;

    Sliders mainSliders;
    Sliders zoomSliders;

    QGridLayout *mainLayout;            ///< The main layout for this widget

    QHBoxLayout *settingsLayout;        ///< The table for the settings info
    QLabel *settingsTriggerLabel;       ///< The trigger details
    QLabel *settingsRecordLengthLabel;  ///< The record length
    QLabel *settingsSamplerateLabel;    ///< The samplerate
    QLabel *settingsTimebaseLabel;      ///< The timebase of the main scope
    QLabel *settingsFrequencybaseLabel; ///< The frequencybase of the main scope

    QLabel *swTriggerStatus;    ///< The status of SW trigger

    QHBoxLayout *markerLayout;        ///< The table for the marker details
    QLabel *markerInfoLabel;          ///< The info about the zoom factor
    QLabel *markerTimeLabel;          ///< The time period between the markers
    QLabel *markerFrequencyLabel;     ///< The frequency for the time period
    QLabel *markerTimebaseLabel;      ///< The timebase for the zoomed scope
    QLabel *markerFrequencybaseLabel; ///< The frequencybase for the zoomed scope

    QGridLayout *measurementLayout;            ///< The table for the signal details
    std::vector<QLabel *> measurementNameLabel;      ///< The name of the channel
    std::vector<QLabel *> measurementGainLabel;      ///< The gain for the voltage (V/div)
    std::vector<QLabel *> measurementMagnitudeLabel; ///< The magnitude for the spectrum (dB/div)
    std::vector<QLabel *> measurementMiscLabel;      ///< Coupling or math mode
    std::vector<QLabel *> measurementAmplitudeLabel; ///< Amplitude of the signal (V)
    std::vector<QLabel *> measurementFrequencyLabel; ///< Frequency of the signal (Hz)

    DataGrid *cursorDataGrid;

    DsoSettingsScope* scope;
    DsoSettingsView* view;
    const Dso::ControlSpecification* spec;

    GlScope *mainScope;     ///< The main scope screen
    unsigned currentCursor = 0; ///< cursor edited with the mouse
    GlScope *zoomScope;     ///< The optional magnified scope screen

  public:
    /// Programmatic access used by the front panel (same effect as moving the sliders)
    void setTriggerLevelValue(ChannelID channel, double value) { updateTriggerLevel(channel, value); }
    void setOffsetValue(ChannelID channel, double value) { updateOffset(channel, value); }
    void setPretriggerValue(double value) {
        if (value < 0.0) value = 0.0;
        if (value > 1.0) value = 1.0;
        mainSliders.triggerPositionSlider->setValue(0, value);
        updateTriggerPosition(0, value, true);
    }

    /// Repaint the scope screens (e.g. after a grid color change)
    void refreshScopes();

    // ---- Cursors (menu "Cursores"). Index 0 = zoom markers, then voltage channels, then spectra
    unsigned cursorCount() const { return 1 + (unsigned)scope->voltage.size() + (unsigned)scope->spectrum.size(); }
    unsigned selectedCursor() const { return currentCursor; }
    DsoSettingsScopeCursor *cursorAt(unsigned index);
    void selectCursor(unsigned index);
    void setCursorShape(unsigned index, DsoSettingsScopeCursor::CursorShape shape);
    void setCursorPositions(unsigned index, const QPointF &p0, const QPointF &p1);
    void setCursorTableVisible(bool visible);
    /// Re-apply the screen colors after the configuration dialog
    void applyColors();

  public slots:
    // Horizontal axis
    // void horizontalFormatChanged(HorizontalFormat format);
    void updateFrequencybase(double frequencybase);
    void updateSamplerate(double samplerate);
    void updateTimebase(double timebase);

    // Trigger
    void updateTriggerMode();
    void updateTriggerSlope();
    void updateTriggerSource();

    // Spectrum
    void updateSpectrumMagnitude(ChannelID channel);
    void updateSpectrumUsed(ChannelID channel, bool used);

    // Vertical axis
    void updateVoltageCoupling(ChannelID channel);
    void updateMathMode();
    void updateVoltageGain(ChannelID channel);
    void updateVoltageUsed(ChannelID channel, bool used);

    // Menus
    void updateRecordLength(unsigned long size);

    // Scope control
    void updateZoom(bool enabled);
    void updateCursorGrid(bool enabled);

  private slots:
    // Sliders
    void updateOffset(ChannelID channel, double value);
    void updateTriggerPosition(int index, double value, bool mainView = true);
    void updateTriggerLevel(ChannelID channel, double value);
    void updateMarker(int marker, double value);

  signals:
    // Sliders
    void offsetChanged(ChannelID channel, double value);       ///< A graph offset has been changed
    void triggerPositionChanged(double value);                    ///< The pretrigger has been changed
    void triggerLevelChanged(ChannelID channel, double value); ///< A trigger level has been changed
};
