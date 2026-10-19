// Lumora — open source lighting console
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace lumora {

// Feature groups, in the order the encoder bar shows them.
enum class Feature : int {
    Dimmer = 1,
    Position = 2,
    Gobo = 3,
    Color = 4,
    Beam = 5,
    Focus = 6,
    Control = 7,
};

// The console renders and outputs up to this many DMX universes.
constexpr int kMaxUniverses = 2048;

// Subfixtures (pixels, heads): the fixture keeps its ID (101) and each subfixture is a fixture
// of its own, written 101.1, 101.2… and one level deeper 101.1.1, 101.1.2. Inside, a
// subfixture's ID is an int above every fixture ID: the fixture's ID and the subfixture's slot
// (its place in the fixture type's list of subfixtures). So anything that takes fixture IDs
// (groups, presets, cues, effects, the programmer) takes subfixtures as they are.
constexpr int kCellBase = 1 << 30;
constexpr int kCellsPerFixture = 1024;    // cells 1–1023
constexpr int kMaxFixtureId = (0x7fffffff - kCellBase) / kCellsPerFixture;
inline bool isCellId(int fid) { return fid >= kCellBase; }
inline int cellId(int master, int cell) { return kCellBase + master * kCellsPerFixture + cell; }
inline int masterOf(int fid) { return isCellId(fid) ? (fid - kCellBase) / kCellsPerFixture : fid; }
inline int cellOf(int fid) { return isCellId(fid) ? (fid - kCellBase) % kCellsPerFixture : 0; }
// (How a cell is written, 101.3 or 101.2.1, depends on the fixture type: see Show::idText.)
// Sorts masters by ID, each followed by its subfixtures in order.
inline qint64 fixtureSortKey(int fid) { return qint64(masterOf(fid)) * kCellsPerFixture + cellOf(fid); }

// Preset pool types. 1-7 mirror the features; 8 is the "All" pool that stores every attribute.
constexpr int kPresetTypeAll = 8;

QString featureName(Feature f);
QString presetTypeName(int type);
// Presets are written by pool name: "Color.1", "Position.12", "All.3".
QString presetRef(int type, int id);
// "color", "Colour", "pos", "4"... → pool type, or 0 when it isn't one.
int presetTypeFromName(const QString &name);
QVector<Feature> allFeatures();

// Maps a standardized attribute name ("Dimmer", "Pan", "ColorAdd_R", "Gobo1", ...) to its feature.
Feature featureOf(const QString &attribute);

// True when the attribute is intensity-like and merges highest-takes-precedence.
inline bool isIntensity(const QString &attribute) { return attribute == QLatin1String("Dimmer"); }

// True when the attribute is an additive color emitter scaled by a virtual dimmer.
inline bool isColorAdd(const QString &attribute) { return attribute.startsWith(QLatin1String("ColorAdd_")); }
// Pan and Tilt (and Pan2, Tilt2… on fixtures with more than one) are programmed in physical
// degrees, 0 being the middle of the fixture's range: -270 to 270 for a 540° pan. Everything
// else is 0..1 of its DMX range.
bool isAngle(const QString &attribute);
double defaultAngleSpan(const QString &attribute);   // when the fixture file doesn't say: 540 pan, 270 tilt

// "1 fixture" / "3 fixtures". English plurals without needing a translation file.
inline QString countOf(int n, const QString &singular, const QString &plural)
{
    return QString::number(n) + QLatin1Char(' ') + (n == 1 ? singular : plural);
}

// sRGB transfer curve: linear light ↔ the values screens and color pickers use (both 0..1).
double linearToSrgb(double linear);
double srgbToLinear(double srgb);

// Human label for an attribute name ("ColorAdd_R" -> "Red").
QString attributeLabel(const QString &attribute);

// Stable display/sort order for attributes inside a feature.
int attributeSortKey(const QString &attribute);

// A programmed value. When presetId > 0 the value is a live reference to a preset,
// so editing the preset updates every cue that uses it. `value` keeps the last
// resolved number as a fallback if the preset is deleted.
struct ValueRef {
    double value = 0.0;
    int presetType = 0;
    int presetId = 0;
    // Individual timing: seconds, or < 0 to use
    // the cue's own fade and delay.
    double fade = -1.0;
    double delay = -1.0;

    bool isPreset() const { return presetId > 0; }
    bool hasTiming() const { return fade >= 0 || delay >= 0; }
    bool operator==(const ValueRef &o) const {
        return qFuzzyCompare(value + 1.0, o.value + 1.0) && presetType == o.presetType && presetId == o.presetId
               && qFuzzyCompare(fade + 2.0, o.fade + 2.0) && qFuzzyCompare(delay + 2.0, o.delay + 2.0);
    }
};

using AttrValues = QMap<QString, ValueRef>;   // attribute -> value
using ValueTable = QMap<int, AttrValues>;      // fixture id -> attributes
using RawAttr = QHash<QString, double>;        // attribute -> 0..1
using RawTable = QMap<int, RawAttr>;           // fixture id -> attributes

} // namespace lumora
