// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "types.h"

#include <QColor>
#include <QFont>
#include <QString>

class QApplication;

namespace lumora::theme {

// Base surfaces: a blue-tinted graphite that stays readable in a dark booth.
inline const QColor bg{0x14, 0x17, 0x1c};
inline const QColor panel{0x1c, 0x20, 0x27};
inline const QColor raised{0x26, 0x2b, 0x34};
inline const QColor line{0x32, 0x38, 0x44};
inline const QColor text{0xdd, 0xe2, 0xea};
inline const QColor muted{0x8b, 0x94, 0xa3};
inline const QColor faint{0x55, 0x5d, 0x6a};

// State colors carry meaning, nothing else uses them.
inline const QColor live{0xf0, 0xb5, 0x4a};        // tungsten: on stage now
inline const QColor programmer{0xef, 0x6b, 0x5b};  // programmer values, not stored yet
inline const QColor learn{0xf0, 0x62, 0xc0};       // MIDI learn is listening

QColor featureColor(Feature f);
QColor presetTypeColor(int type);
QColor mix(const QColor &a, const QColor &b, double t);   // t = 0 → a, 1 → b

QFont uiFont(int pointSize = 10, int weight = QFont::Normal);
void apply(QApplication &app);

} // namespace lumora::theme
