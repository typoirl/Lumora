// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme.h"

#include <QApplication>
#include <QHash>
#include <QPalette>
#include <QStyleFactory>

namespace lumora::theme {

QColor featureColor(Feature f)
{
    switch (f) {
    case Feature::Dimmer: return QColor(0xe9, 0xc6, 0x4b);
    case Feature::Position: return QColor(0x5e, 0x8f, 0xf0);
    case Feature::Gobo: return QColor(0x57, 0xc0, 0x8b);
    case Feature::Color: return QColor(0xb0, 0x7c, 0xf2);
    case Feature::Beam: return QColor(0xe9, 0x8a, 0x4e);
    case Feature::Focus: return QColor(0x4f, 0xc3, 0xc9);
    case Feature::Control: return QColor(0x8d, 0x96, 0xa6);
    }
    return muted;
}

QColor presetTypeColor(int type)
{
    if (type >= 1 && type <= 7)
        return featureColor(static_cast<Feature>(type));
    return QColor(0xc9, 0xcf, 0xd8);
}

QColor mix(const QColor &a, const QColor &b, double t)
{
    t = qBound(0.0, t, 1.0);
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

QFont uiFont(int pointSize, int weight)
{
    // Painting asks for the same few fonts many times a second: build each once.
    static QHash<int, QFont> cache;
    const int key = pointSize * 1000 + weight;
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();
    QFont f;
    f.setFamilies({"Inter", "IBM Plex Sans", "Segoe UI Variable Text", "Segoe UI", "SF Pro Text",
                   "Helvetica Neue", "Cantarell", "Noto Sans", "DejaVu Sans"});
    f.setPointSize(pointSize);
    f.setWeight(QFont::Weight(weight));
    cache.insert(key, f);
    return f;
}

void apply(QApplication &app)
{
    app.setStyle(QStyleFactory::create("Fusion"));
    app.setFont(uiFont(10));

    QPalette p;
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, panel);
    p.setColor(QPalette::AlternateBase, QColor(0x20, 0x24, 0x2c));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, raised);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Highlight, QColor(0x34, 0x3c, 0x4a));
    p.setColor(QPalette::HighlightedText, text);
    p.setColor(QPalette::Link, QColor(0x8f, 0xb8, 0xf0));
    p.setColor(QPalette::LinkVisited, QColor(0x8f, 0xb8, 0xf0));
    p.setColor(QPalette::ToolTipBase, raised);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, faint);
    p.setColor(QPalette::Disabled, QPalette::Text, faint);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, faint);
    p.setColor(QPalette::Disabled, QPalette::WindowText, faint);
    app.setPalette(p);

    const QString css = QStringLiteral(R"(
QWidget { color: %1; }
QMainWindow, QDialog { background: %2; }
QToolTip { background: %4; color: %1; border: 1px solid %5; padding: 4px 6px; }

/* Buttons: quiet by default, state colors only when they mean something */
QPushButton, QToolButton {
  background: %4; border: 1px solid %5; border-radius: 4px; padding: 5px 12px; min-height: 18px;
}
QPushButton:hover, QToolButton:hover { background: #2d333d; }
QPushButton:pressed, QToolButton:pressed { background: #353c48; }
QPushButton:disabled { color: %6; }
QToolButton::menu-indicator { image: none; }
QPushButton[role="primary"] { background: #3a3220; border-color: %7; color: %7; }
QPushButton[role="primary"]:disabled { background: transparent; border-color: #32384a; color: #555d6a; }
QPushButton:checked, QToolButton:checked { background: #33302a; border-color: %7; color: %7; }
QToolButton#store:checked { background: #3a2422; border-color: %8; color: %8; }
QToolButton#learn:checked { background: #3a2133; border-color: %9; color: %9; }
QToolButton#blackout:checked { background: #3a2422; border-color: %8; color: %8; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
  background: %3; border: 1px solid %5; border-radius: 4px; padding: 4px 6px; selection-background-color: #3d4656;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #5a6578; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView { background: %4; border: 1px solid %5; selection-background-color: #3d4656; }

QTableView, QListWidget, QTreeWidget {
  background: %3; border: 1px solid %5; border-radius: 6px; gridline-color: transparent;
  alternate-background-color: #1f232b;
}
QListWidget::item, QTreeWidget::item { padding: 3px 6px; }
QTableView::item:selected, QListWidget::item:selected, QTreeWidget::item:selected { background: #2f3644; }
QHeaderView::section {
  background: %3; color: %10; border: none; border-bottom: 1px solid %5; padding: 6px 6px; font-weight: 500;
}
QHeaderView::section:horizontal { border-right: 1px solid %5; }   /* where to drag to resize */
QHeaderView::section:horizontal:hover { background: #222731; }
QTableCornerButton::section { background: %3; border: none; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle { background: #3a414d; border-radius: 3px; min-height: 24px; min-width: 24px; }
QScrollBar::handle:hover { background: #4a5260; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; border: none; width: 0; height: 0; }

QSlider::groove:horizontal { height: 4px; background: %5; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #6b7587; border-radius: 2px; }
QSlider::handle:horizontal { background: %1; width: 12px; height: 12px; margin: -5px 0; border-radius: 6px; }

QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #4a5260; border-radius: 3px; background: %3; }
QCheckBox::indicator:checked { background: %7; border-color: %7; }

QMenu { background: %4; border: 1px solid %5; padding: 4px; }
QMenu::item { padding: 5px 22px 5px 14px; border-radius: 3px; }
QMenu::item:selected { background: #353c48; }
QMenu::separator { height: 1px; background: %5; margin: 4px 6px; }

QSplitter::handle { background: %2; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }
QLabel[role="title"] { font-size: 13pt; font-weight: 600; }
QLabel[role="muted"] { color: %10; }
QFrame[role="panel"] { background: %3; border: 1px solid %5; border-radius: 8px; }
)")
                            .arg(text.name(), bg.name(), panel.name(), raised.name(), line.name(), faint.name(),
                                 live.name(), programmer.name(), learn.name(), muted.name());
    app.setStyleSheet(css);
}

} // namespace lumora::theme
