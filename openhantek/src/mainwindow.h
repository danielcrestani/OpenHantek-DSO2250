#pragma once
#include "post/ppresult.h"
#include <QMainWindow>
#include <memory>

class SpectrumGenerator;
class HantekDsoControl;
class DsoSettings;
class DataLogger;
namespace Dso {
struct ControlSpecification;
}
class QToolButton;
class DsoWidget;
class HorizontalDock;
class TriggerDock;
class SpectrumDock;
class VoltageDock;
class FrontPanelDock;
class MeasurementBar;

namespace Ui {
class MainWindow;
}

/// \brief The main window of the application.
/// The main window contains the classic oszilloscope-screen and the gui
/// elements used to control the oszilloscope.
class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(HantekDsoControl *dsoControl, DsoSettings *mSettings, QWidget *parent = 0);
    ~MainWindow();
  public slots:
    void showNewData(std::shared_ptr<PPresult> data);

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    Ui::MainWindow *ui;

    // Central widgets
    DsoWidget *dsoWidget;
    FrontPanelDock *frontPanel = nullptr;
    MeasurementBar *measurementBar = nullptr;
    void applyGridContrast(int level);
    void setupCursorMenu();
    void positionCursorOnSignal();
    QString cursorReadout(QColor *color) const;
    std::shared_ptr<PPresult> lastData;

    // Settings used for the whole program
    DsoSettings *mSettings;
    DataLogger *logger = nullptr;
    const Dso::ControlSpecification *deviceSpec = nullptr;
    QToolButton *recButton = nullptr;
    void setupExportAndLog();
    void exportScreenImage();
    void updateRecButton();
};
