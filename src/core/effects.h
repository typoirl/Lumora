// SPDX-License-Identifier: GPL-3.0-or-later
//
// Effects
// =======
// An Effect is a stack of Layers. Each layer drives one part of a fixture:
//
//   Dimmer      intensity
//   Position    pan/tilt
//   Color       every color system a fixture has (RGB/W/A/UV/Lime, CMY, CTO, wheels),
//               with per-family "affects" toggles
//   Attributes  any other attributes you pick (zoom, iris, gobo rotation...)
//
// and moves through Points in one of three modes:
//
//   Wave   between a low and a high point, following a Shape (a curve over one cycle)
//   Steps  through any number of points, crossfading from one to the next
//   Form   (position) a circle, figure 8, line... around where the fixture points
//
// A point is a value, a color, or a reference to a preset, so effects carry no
// fixture-specific data and work on whatever you select. Shapes live in their own
// pool and are referenced by layers: edit a shape and every effect using it follows.
//
// Phase spreads across the fixtures (in selection order) by spread: groups, blocks, wings,
// direction and a phase range. An effect run without spread uses the layout it was made with.

#pragma once

#include "types.h"

#include <QJsonObject>
#include <functional>
#include <QString>
#include <QStringList>
#include <QVector>

namespace lumora {

class Show;

// ---- Shapes -----------------------------------------------------------------

// Built-in shapes have negative ids; custom shapes in the show's pool are positive.
enum BuiltinShape : int {
    ShapeSine = -1,
    ShapeRampUp = -2,
    ShapeRampDown = -3,
    ShapeTriangle = -4,
    ShapeSquare = -5,
    ShapePulse = -6,
    ShapeRandom = -7,
    ShapeSmoothRandom = -8,
    ShapeBreathe = -9,
};

struct ShapePoint {
    double x = 0;   // 0..1 through the cycle
    double y = 0;   // 0..1 output
};

struct Shape {
    int id = 0;
    QString name;
    QVector<ShapePoint> points;   // custom shapes: sorted by x
    bool smooth = true;           // custom shapes: eased (cosine) between points

    // q in [0,1), cycle = how many whole cycles have passed (for random shapes), seed per fixture
    double value(double q, qint64 cycle = 0, int seed = 0) const;

    QJsonObject toJson() const;
    static Shape fromJson(const QJsonObject &o);
};

const QVector<Shape> &builtinShapes();
const Shape *builtinShape(int id);

// ---- Points and layers ------------------------------------------------------

struct EffectPoint {
    enum Kind { Value, Color, Preset };
    Kind kind = Value;
    RawAttr values;             // Value: attribute -> 0..1 ("Level" means "every attribute the layer drives")
    double r = 1, g = 1, b = 1; // Color: linear RGB, 0..1
    int presetType = 0;
    int presetId = 0;

    static EffectPoint level(double v);
    static EffectPoint color(double r, double g, double b);
    static EffectPoint preset(int type, int id);
    static EffectPoint panTilt(double pan, double tilt);

    QJsonObject toJson() const;
    static EffectPoint fromJson(const QJsonObject &o);
};

enum class LayerKind { Dimmer, Position, Color, Attributes };
enum class LayerMode { Wave, Steps, Form };
enum class StepOrder { Loop, Bounce, Random };
enum class FormType { Circle, Figure8, LineHorizontal, LineVertical, Square, Triangle };

QString layerKindName(LayerKind k);
QString layerModeName(LayerMode m);
QString formTypeName(FormType f);

// Families the Color layer can drive; each can be switched off per layer.
struct ColorFamily {
    QString id;      // "red", "white", "cmy", "wheel" ...
    QString label;
    QStringList attributes;
};
const QVector<ColorFamily> &colorFamilies();

struct EffectLayer {
    QString name;
    LayerKind kind = LayerKind::Dimmer;
    LayerMode mode = LayerMode::Wave;
    bool enabled = true;
    int shapeId = ShapeSine;
    QVector<EffectPoint> points;      // Wave: [low, high]; Steps: 2 or more
    double width = 1.0;               // portion of the cycle the wave or steps use; rest holds the first point
    double crossfade = 1.0;           // Steps: portion of each step spent fading in (0 = snap)
    // Relative (Dimmer and Attributes layers): the points are amounts added to whatever value is
    // underneath (a running executor, the programmer), and can be negative: -40% to 0% dips a
    // level that's already up instead of replacing it. Off: the points are the values themselves.
    bool relative = false;
    StepOrder order = StepOrder::Loop;
    double speed = 1.0;               // multiplier on the effect's rate
    double offset = 0.0;              // phase offset in degrees
    // Form
    FormType form = FormType::Circle;
    double sizePan = 0.12;            // fraction of the full pan range
    double sizeTilt = 0.12;
    bool hasCenter = false;           // false = around the fixture's current position
    EffectPoint center;
    // Color: families switched off. Attributes: the attributes this layer drives.
    QStringList excludedFamilies;
    QStringList attributes;

    QStringList drivenAttributes(const QStringList &fixtureAttributes) const;
    QJsonObject toJson() const;
    static EffectLayer fromJson(const QJsonObject &o);
};

enum class SpreadDirection { Forward, Backward, Inward, Outward, Random };
QString spreadDirectionName(SpreadDirection d);

struct Spread;
struct FixtureMode;

struct Effect {
    int id = 0;
    QString name;
    QString description;
    QVector<EffectLayer> layers;
    double cycle = 2.0;          // seconds per cycle at 100% rate
    double spread = 0.0;         // degrees of phase spread across the selection (360 = one full chase)
    int groups = 0;              // 0 = off; N = fixtures repeat their phase every N
    int blocks = 1;              // consecutive fixtures that share a phase
    int wings = 1;               // mirror the spread in this many wings
    SpreadDirection direction = SpreadDirection::Forward;
    int rateMaster = 0;          // 0 = its own speed; 1–10 = runs at that rate master's speed
    // The spread from the pool that lays it out (phase, blocks, groups, wings, direction), so
    // effects share layouts and editing the spread changes them all. 0: its own layout above;
    // -1: not chosen yet (a new or older effect: Show::setEffect gives it the matching spread).
    int spreadId = -1;
    Spread ownLayout() const;    // its own layout as a spread (phase 0 to `spread`)

    QList<Feature> features() const;   // features the layers drive
    QJsonObject toJson() const;
    static Effect fromJson(const QJsonObject &o);
};

// spread: how a selection is thinned and laid out. Interleave (every Nth fixture
// from an offset) picks fixtures; blocks, groups, wings, direction and a phase range lay effects
// out across them. Kept in a pool, recalled onto the programmer's selection, and referenced by
// blueprint lines.
struct Spread {
    int id = 0;
    QString name;
    int every = 1;      // interleave: every Nth fixture…
    int offset = 0;     // …from this one (0-based)
    int blocks = 1;     // consecutive fixtures that share a phase
    int groups = 0;     // 0 = off; N = the phase repeats every N
    int wings = 1;      // mirror the layout in this many wings
    SpreadDirection direction = SpreadDirection::Forward;
    double phaseFrom = 0.0;     // degrees: the first fixture's phase…
    double phaseTo = 360.0;     // …and the spread to the last (360 = one full chase, 0 = together)

    bool isDefault() const;
    QString summary() const;                               // "Every 2 · Wings 2", or "Off"
    QVector<int> apply(const QVector<int> &fids) const;    // the interleave
    QVector<double> phases(int count) const;               // 0..1 of a cycle, per fixture in order
    QVector<double> timeFractions(int count) const;        // 0 (first) .. 1 (last), per fixture in order: for individual times
    QJsonObject toJson() const;
    static Spread fromJson(const QJsonObject &o);
};

// A value along points spread evenly from 0 to 1: {0, 5} at 0.5 is 2.5; {0, 5, 0} at 0.5 is 5
// ("0 Thru 5 Thru 0").
double alongPoints(const QVector<double> &points, double fraction);
// Fractions 0..1 for fixtures in order: the first 0, the last 1.
QVector<double> evenFractions(int count);

// A running effect: which effect, on which fixtures, with live rate/size adjustments.
// In presets the fixture list is empty: the preset's effect runs on whatever it's applied to.
struct EffectInstance {
    int effectId = 0;
    QVector<int> fixtures;       // in selection order
    double rate = 1.0;           // multiplier on speed
    double size = 1.0;           // multiplier on amplitude
    double spread = -1.0;        // (shows from before spread) degrees, or < 0 to use the effect's
    // How the phase spreads across the fixtures. Without spread: the effect's own layout.
    bool hasLayout = false;
    Spread layout;
    // Stored in a preset, which fixtures it plays on, like the preset's values: Universal (any
    // fixture it's applied to), Global (fixtures of these types) or Selective (these fixtures).
    enum Scope { Universal = 0, Global = 1, Selective = 2 };
    int scope = Universal;
    QStringList types;   // Global: the fixture types

    QJsonObject toJson() const;
    static EffectInstance fromJson(const QJsonObject &o);
};

// ---- Rate masters ------------------------------------------------------------
// Ten masters that effects can follow. Each is one of:
//   Rate   a multiplier on the effect's own speed: the fader's middle is 100%, the top 200%
//   BPM    an absolute tempo: one effect cycle per beat, whatever the effect's own speed
//   Ratio  a multiplier in musical steps: ×1/16 … ×1 (the middle) … ×16
// Put one on an executor to control it from a fader; its buttons are Learn (tap tempo), Reset
// and Freeze.
enum class RateMode { Rate, BPM, Ratio };
QString rateModeName(RateMode m);
RateMode rateModeFromName(const QString &n);

struct RateMaster {
    static constexpr int kCount = 10;
    int id = 1;
    QString name;              // empty: "Rate 1"
    RateMode mode = RateMode::Rate;
    double value = 1.0;        // Rate, Ratio: multiplier; BPM: beats per minute
    bool frozen = false;       // Freeze: effects that follow it stand still
    bool button = false;       // in the Rate masters window: a Learn key instead of a fader

    QString displayName() const;
    QString valueText() const;           // "100%", "120 BPM", "×1/2"
    double defaultValue() const;          // 1× or 120 BPM
    double faderPosition() const;         // 0..1 as an executor fader shows it
    void setFromFader(double f);
    // Value as the command line types it: Rate in percent, BPM in BPM, Ratio as the multiplier.
    void setTyped(double v);
    // Cycles per second for an effect following this master (cycleSeconds: the effect's own).
    double cyclesPerSecond(double cycleSeconds, double instanceRate) const;
    // Learn: one tap. Returns true when the tempo changed (after the second tap).
    bool tap(qint64 msecs);

    QJsonObject toJson() const;
    static RateMaster fromJson(const QJsonObject &o);

private:
    QVector<qint64> m_taps;
};

// Phase position of a fixture in the spread, 0..1.
double spreadPosition(const Effect &e, int index, int count);

// Evaluates an effect instance at `phase` (cycles since it started) and writes the results
// into `out`. `base` returns the value an attribute would have without the effect (used by
// Form layers, by color families that are switched off, and for attributes a point lacks).
using BaseValue = std::function<double(int fid, const QString &attribute)>;
void evaluateEffect(const Show &show, const Effect &effect, const EffectInstance &inst, double phase,
                    const BaseValue &base, RawTable &out);

// Attribute values that make this fixture show a color (linear RGB), using whatever it has:
// RGB(W/A/UV/L) mixing, CMY, or the nearest named color-wheel slot.
RawAttr colorAttributes(const Show &show, int fid, double r, double g, double b, const QStringList *allowed = nullptr);
// A linear RGB color on a fixture's emitters: white first, then amber, orange, yellow, lime, mint,
// cyan, indigo, purple and magenta where it has them (each as far as the color allows), red,
// green and blue for the rest; or hue, saturation and brightness on a virtual-color fixture.
// `allowed`: only these attributes may be used (the rest of the color goes to RGB).
RawAttr emitterMix(const FixtureMode *m, double r, double g, double b, const QStringList *allowed = nullptr);

// Ready-made effects to load into the pool.
QVector<Effect> effectTemplates();

} // namespace lumora
