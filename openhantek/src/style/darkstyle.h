// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <QApplication>
#include <QColor>
#include <QGroupBox>
#include <QPalette>
#include <QPushButton>
#include <QString>
#include <QAbstractSpinBox>
#include <QStyleFactory>

/// \brief Look of the OpenHantek front panel, shared by the PSG9080 and OpenHantekBode programs:
/// dark instrument panel (#23272e), sections with a colored border, rounded dark buttons, green/red RUN.
namespace darkstyle {

// Colors of the OpenHantek front panel
const QColor kPanel(0x23, 0x27, 0x2e);
const QColor kField(0x11, 0x14, 0x1a);
const QColor kButton(0x3a, 0x40, 0x4a);
const QColor kBorder(0x55, 0x5d, 0x6a);
const QColor kText(0xe6, 0xe9, 0xee);
const QColor kDimText(0x9a, 0xa1, 0xab);
const QColor kAccent(0x2f, 0x6f, 0xbf);

// Section colors (same as the OpenHantek panel)
inline const char *blue() { return "#3fb8e0"; }   ///< horizontal / oscilloscope
inline const char *orange() { return "#f08c2e"; } ///< trigger / generator
inline const char *violet() { return "#b07cff"; } ///< FFT / sweep
inline const char *gray() { return "#9aa1ab"; }

// Channel colors: oscilloscope (OpenHantek defaults) and generator (the PSG9080 display)
inline const char *scopeChannel(int ch) { return ch == 0 ? "#ff3b3b" : "#f2e600"; }
inline const char *generatorChannel(int ch) { return ch == 0 ? "#e6b800" : "#2fa8d8"; }

/// Fusion style with a dark palette, so menus, dialogs and tooltips match the panel.
inline void applyApplicationLook(QApplication &app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, kPanel);
    p.setColor(QPalette::WindowText, kText);
    p.setColor(QPalette::Base, kField);
    p.setColor(QPalette::AlternateBase, kPanel.lighter(115));
    p.setColor(QPalette::ToolTipBase, QColor(0x2b, 0x30, 0x38));
    p.setColor(QPalette::ToolTipText, kText);
    p.setColor(QPalette::PlaceholderText, kDimText);
    p.setColor(QPalette::Text, kText);
    p.setColor(QPalette::Button, kButton);
    p.setColor(QPalette::ButtonText, QColor(0xf0, 0xf2, 0xf5));
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, kAccent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(blue()));
    p.setColor(QPalette::Mid, kBorder);
    for (QPalette::ColorRole r : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, r, QColor(0x6c, 0x73, 0x7d));
    p.setColor(QPalette::Disabled, QPalette::Button, kPanel.lighter(108));
    p.setColor(QPalette::Disabled, QPalette::Base, kPanel);
    app.setPalette(p);
    app.setStyleSheet(
        "QToolTip { color: #e6e9ee; background: #2b3038; border: 1px solid #555d6a; padding: 3px; }"
        "QMenu { background: #2b3038; color: #f0f2f5; border: 1px solid #3a404a; }"
        "QMenu::item:selected { background: #2f6fbf; }");
}

/// Stylesheet of the OpenHantek front panel, extended to the input widgets the other programs use.
inline QString panelSheet() {
    return "QGroupBox { color: #c8ccd4; font-weight: bold; border: 1px solid #3a404a; border-radius: 5px;"
           "  margin-top: 10px; padding: 8px 4px 4px 4px; }"
           "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
           "QLabel { color: #e6e9ee; }"
           "QLabel[role=\"hint\"] { color: #9aa1ab; font-size: 8.5pt; }"
           "QPushButton { background: #3a404a; color: #f0f2f5; border: 1px solid #555d6a; border-radius: 4px;"
           "  padding: 4px 10px; min-height: 20px; }"
           "QPushButton:hover { background: #475061; }"
           "QPushButton:pressed { background: #2c3139; }"
           "QPushButton:checked { background: #2f6fbf; border-color: #5a95e0; }"
           "QPushButton:disabled { background: #2a2e35; color: #6c737d; border-color: #3a404a; }"
           "QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox { background: #11141a; color: #f0f2f5;"
           "  border: 1px solid #3a404a; border-radius: 4px; padding: 3px 6px; min-height: 20px;"
           "  selection-background-color: #2f6fbf; }"
           "QComboBox:focus, QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: #5a95e0; }"
           "QComboBox:disabled, QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {"
           "  color: #6c737d; background: #1b1e24; }"
           "QComboBox { background: #3a404a; border-color: #555d6a; }"
           "QComboBox QAbstractItemView { background: #2b3038; color: #f0f2f5; selection-background-color: #2f6fbf;"
           "  border: 1px solid #555d6a; }"
           // light arrows (res/style.qrc): the Fusion arrows vanish on the dark fields
           "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 20px;"
           "  border: none; }"
           "QComboBox::down-arrow { image: url(\":/style/arrow-down.png\"); width: 10px; height: 6px; }"
           "QComboBox::down-arrow:disabled { image: url(\":/style/arrow-down-disabled.png\"); }"
           "QCheckBox, QRadioButton { color: #e6e9ee; spacing: 6px; }"
           "QProgressBar { background: #11141a; color: #e6e9ee; border: 1px solid #3a404a; border-radius: 4px;"
           "  text-align: center; min-height: 18px; }"
           "QProgressBar::chunk { background: #2f6fbf; border-radius: 3px; }"
           "QScrollArea { background: transparent; border: none; }"
           "QSplitter::handle { background: #1b1e24; width: 3px; }";
}

/// Number fields without the up/down buttons (like the OpenHantek panel): wheel and arrow keys still work.
inline void plainSpin(QAbstractSpinBox *box) {
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    box->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    box->setToolTip(QObject::tr("Digite o valor, ou use a roda do mouse / as setas ↑ ↓ do teclado"));
}

/// Colored section like the CH1/CH2/Horizontal/Trigger boxes of the OpenHantek panel.
inline void colorSection(QGroupBox *box, const QString &color) {
    box->setStyleSheet(QString("QGroupBox { color: %1; border-color: %1; }").arg(color));
}

/// Big RUN-like button: green to start, red while running (same colors as RUN/STOP in OpenHantek).
inline void styleRunButton(QPushButton *b, bool running) {
    b->setStyleSheet(running ? "QPushButton { background: #b03030; border-color: #e05050; color: white;"
                               "  font-weight: bold; font-size: 11pt; min-height: 32px; }"
                               "QPushButton:hover { background: #c03838; }"
                             : "QPushButton { background: #2e8b3e; border-color: #4fbf62; color: white;"
                               "  font-weight: bold; font-size: 11pt; min-height: 32px; }"
                               "QPushButton:hover { background: #359a47; }"
                               "QPushButton:disabled { background: #2a2e35; color: #6c737d; border-color: #3a404a; }");
}

/// Output / ON button checked in the channel color with black text, like CH1/CH2 "LIGADO" in OpenHantek.
inline QString channelOnButtonSheet(const QString &color) {
    return QString("QPushButton { font-weight: bold; font-size: 10pt; min-height: 30px; }"
                   "QPushButton:checked { background: %1; border-color: %1; color: #000000; }"
                   "QPushButton:checked:hover { background: %1; }")
        .arg(color);
}

} // namespace darkstyle
