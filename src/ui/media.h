// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QImage>
#include <QPixmap>
#include <QString>

namespace lumora {

class Show;
struct FixtureType;
struct Preset;

// Images embedded in fixture files (GDTF gobo slots), decoded once and cached.
QImage mediaImage(const FixtureType *type, const QString &key);

// The gobo image a fixture shows for a value of a wheel attribute, if its fixture file has one.
QImage slotImage(Show *show, int fid, const QString &attribute, double value);

// The first gobo image a preset would put on stage, for pool tiles.
QImage presetImage(Show *show, const Preset &preset);

// A round, softly lit rendering of a gobo image, like a projected beam.
QPixmap goboPixmap(const QImage &image, int size);

} // namespace lumora
