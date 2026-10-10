// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class Psg9080;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QSpinBox;

/// \brief Shared pieces of the PSG9080 tabs (Modulação, Varredura, Frequencímetro, Ondas, Sequências, Sistema).
namespace psgui {

/// Spin box like the OpenHantek panel: no arrows, sends on Enter / focus out / wheel, right aligned.
QDoubleSpinBox *spin(int decimals, double min, double max, double step, const QString &suffix);
QSpinBox *intSpin(int min, int max, const QString &suffix = QString());
/// Value rounded to the decimals the box shows (0.1 stays 0.1).
double spinValue(const QDoubleSpinBox *box);
/// Combo box whose item data is the index (or the given codes).
QComboBox *combo(const QStringList &items, const QVector<int> &codes = {});
/// Gray explanation text that wraps.
QLabel *hint(const QString &text);
/// Show or hide a form row (QFormLayout::setRowVisible needs Qt 6.4; Pop!_OS 22.04 has 6.2).
void setRowVisible(QFormLayout *form, QWidget *field, bool visible);
/// "1,5 kHz" style text.
QString siText(double value, const QString &unit, int digits = 6);

/// \brief Number with a unit selector (Hz/kHz/MHz or ns/µs/ms/s); value() is in the base unit.
class UnitField : public QWidget {
    Q_OBJECT

  public:
    struct Unit {
        QString name;
        double factor; ///< base units per unit (kHz -> 1000)
    };
    static QVector<Unit> frequencyUnits(); ///< mHz, Hz, kHz, MHz
    static QVector<Unit> timeUnits();      ///< ns, µs, ms, s

    explicit UnitField(const QVector<Unit> &units, QWidget *parent = nullptr);
    double value() const { return current; }
    /// Show a value, in the unit that reads best.
    void setValue(double base);
    void setRange(double min, double max);

  signals:
    /// The user typed a valid new value (in base units) or changed the unit.
    void edited(double value);
    void invalid(const QString &text);

  private:
    void commit();
    QVector<Unit> units;
    QLineEdit *edit;
    QComboBox *unitBox;
    double current = 0, minimum = 0, maximum = 1e300;
};

/// \brief Base of the tabs: holds the generator, reads it when shown, enables itself while connected.
class GeneratorTab : public QWidget {
    Q_OBJECT

  public:
    GeneratorTab(Psg9080 *generator, QWidget *parent = nullptr);
    /// Read the device and show its values.
    virtual void refresh() = 0;

  signals:
    void statusMessage(const QString &text, bool error);

  protected:
    void showEvent(QShowEvent *event) override;
    /// Show the driver error if `ok` is false; returns ok.
    bool check(bool ok);
    void say(const QString &text) { emit statusMessage(text, false); }
    /// Called when the connection opens or closes (default: enable the tab and read it).
    virtual void connectionChanged(bool open);

    Psg9080 *gen;
    bool needsRefresh = true;
};

} // namespace psgui
