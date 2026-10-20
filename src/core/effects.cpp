// SPDX-License-Identifier: GPL-3.0-or-later
#include "effects.h"

#include "show.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace lumora {

namespace {
constexpr double kPi = 3.14159265358979323846;

double hash01(quint64 a, quint64 b)
{
    quint64 z = a * 0x9E3779B97F4A7C15ull + b * 0xBF58476D1CE4E5B9ull + 0x94D049BB133111EBull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return double(z >> 11) / double(1ull << 53);
}

double smoothstep(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

double cosineBlend(double a, double b, double t)
{
    const double u = 0.5 - 0.5 * std::cos(kPi * std::clamp(t, 0.0, 1.0));
    return a + (b - a) * u;
}

bool isDiscrete(const QString &a)
{
    return a == QLatin1String("Color1") || a == QLatin1String("Color2") || a == QLatin1String("ColorMacro")
           || (a.startsWith(QLatin1String("Gobo")) && !a.endsWith(QLatin1String("Pos")))
           || (a.startsWith(QLatin1String("Prism")) && !a.endsWith(QLatin1String("Pos")))
           || a.startsWith(QLatin1String("Ctl_")) || a == QLatin1String("AnimationWheel");
}
} // namespace

// ===========================================================================
// Shapes
// ===========================================================================

double Shape::value(double q, qint64 cycle, int seed) const
{
    q = q - std::floor(q);
    switch (id) {
    case ShapeSine: return 0.5 - 0.5 * std::cos(2 * kPi * q);
    case ShapeRampUp: return q;
    case ShapeRampDown: return 1.0 - q;
    case ShapeTriangle: return 1.0 - std::fabs(2 * q - 1);
    case ShapeSquare: return q < 0.5 ? 1.0 : 0.0;
    case ShapePulse: return q < 0.04 ? q / 0.04 : std::exp(-(q - 0.04) * 9.0);
    case ShapeRandom: return hash01(quint64(cycle), quint64(seed) + 17);
    case ShapeSmoothRandom: return cosineBlend(hash01(quint64(cycle), quint64(seed) + 17), hash01(quint64(cycle + 1), quint64(seed) + 17), q);
    case ShapeBreathe: return smoothstep(1.0 - std::fabs(2 * q - 1));
    default: break;
    }
    if (points.isEmpty())
        return 0.0;
    if (points.size() == 1)
        return points.first().y;
    // Custom: piecewise between points, wrapping from the last point back to the first.
    const ShapePoint *a = &points.last();
    const ShapePoint *b = &points.first();
    double ax = a->x - 1.0, bx = b->x;
    for (int i = 0; i < points.size(); ++i) {
        if (q < points[i].x) {
            b = &points[i];
            bx = b->x;
            if (i > 0) {
                a = &points[i - 1];
                ax = a->x;
            } else {
                a = &points.last();
                ax = a->x - 1.0;
            }
            break;
        }
        if (i == points.size() - 1) {
            a = &points[i];
            ax = a->x;
            b = &points.first();
            bx = b->x + 1.0;
        }
    }
    const double t = bx - ax > 1e-9 ? (q - ax) / (bx - ax) : 0.0;
    return std::clamp(smooth ? cosineBlend(a->y, b->y, t) : a->y + (b->y - a->y) * t, 0.0, 1.0);
}

QJsonObject Shape::toJson() const
{
    QJsonArray pts;
    for (const ShapePoint &p : points)
        pts.append(QJsonArray{p.x, p.y});
    return {{"id", id}, {"name", name}, {"points", pts}, {"smooth", smooth}};
}

Shape Shape::fromJson(const QJsonObject &o)
{
    Shape s;
    s.id = o["id"].toInt();
    s.name = o["name"].toString();
    s.smooth = o["smooth"].toBool(true);
    for (const QJsonValue &v : o["points"].toArray()) {
        const QJsonArray a = v.toArray();
        s.points.append({std::clamp(a[0].toDouble(), 0.0, 1.0), std::clamp(a[1].toDouble(), 0.0, 1.0)});
    }
    std::sort(s.points.begin(), s.points.end(), [](const ShapePoint &a, const ShapePoint &b) { return a.x < b.x; });
    return s;
}

const QVector<Shape> &builtinShapes()
{
    static const QVector<Shape> shapes = {
        {ShapeSine, QObject::tr("Sine"), {}, true},
        {ShapeRampUp, QObject::tr("Ramp up"), {}, true},
        {ShapeRampDown, QObject::tr("Ramp down"), {}, true},
        {ShapeTriangle, QObject::tr("Triangle"), {}, true},
        {ShapeSquare, QObject::tr("Square"), {}, true},
        {ShapePulse, QObject::tr("Pulse"), {}, true},
        {ShapeBreathe, QObject::tr("Breathe"), {}, true},
        {ShapeRandom, QObject::tr("Random"), {}, true},
        {ShapeSmoothRandom, QObject::tr("Smooth random"), {}, true},
    };
    return shapes;
}

const Shape *builtinShape(int id)
{
    for (const Shape &s : builtinShapes())
        if (s.id == id)
            return &s;
    return nullptr;
}

// ===========================================================================
// Points, layers, effects: names and JSON
// ===========================================================================

EffectPoint EffectPoint::level(double v)
{
    EffectPoint p;
    p.kind = Value;
    p.values.insert(QStringLiteral("Level"), v);
    return p;
}

EffectPoint EffectPoint::color(double r, double g, double b)
{
    EffectPoint p;
    p.kind = Color;
    p.r = r;
    p.g = g;
    p.b = b;
    return p;
}

EffectPoint EffectPoint::preset(int type, int id)
{
    EffectPoint p;
    p.kind = Preset;
    p.presetType = type;
    p.presetId = id;
    return p;
}

EffectPoint EffectPoint::panTilt(double pan, double tilt)
{
    EffectPoint p;
    p.values.insert(QStringLiteral("Pan"), pan);
    p.values.insert(QStringLiteral("Tilt"), tilt);
    return p;
}

QJsonObject EffectPoint::toJson() const
{
    switch (kind) {
    case Color: return {{"kind", "color"}, {"rgb", QJsonArray{r, g, b}}};
    case Preset: return {{"kind", "preset"}, {"preset", QStringLiteral("%1.%2").arg(presetType).arg(presetId)}};
    case Value: break;
    }
    QJsonObject v;
    for (auto it = values.begin(); it != values.end(); ++it)
        v[it.key()] = it.value();
    return {{"kind", "value"}, {"values", v}};
}

EffectPoint EffectPoint::fromJson(const QJsonObject &o)
{
    EffectPoint p;
    const QString k = o["kind"].toString();
    if (k == "color") {
        p.kind = Color;
        const QJsonArray a = o["rgb"].toArray();
        p.r = a[0].toDouble();
        p.g = a[1].toDouble();
        p.b = a[2].toDouble();
    } else if (k == "preset") {
        p.kind = Preset;
        const QStringList parts = o["preset"].toString().split('.');
        p.presetType = parts.value(0).toInt();
        p.presetId = parts.value(1).toInt();
    } else {
        const QJsonObject v = o["values"].toObject();
        for (auto it = v.begin(); it != v.end(); ++it)
            p.values.insert(it.key(), it.value().toDouble());
    }
    return p;
}

QString layerKindName(LayerKind k)
{
    switch (k) {
    case LayerKind::Dimmer: return QObject::tr("Dimmer");
    case LayerKind::Position: return QObject::tr("Position");
    case LayerKind::Color: return QObject::tr("Color");
    case LayerKind::Attributes: return QObject::tr("Attributes");
    }
    return {};
}

QString layerModeName(LayerMode m)
{
    switch (m) {
    case LayerMode::Wave: return QObject::tr("Wave");
    case LayerMode::Steps: return QObject::tr("Steps");
    case LayerMode::Form: return QObject::tr("Form");
    }
    return {};
}

QString formTypeName(FormType f)
{
    switch (f) {
    case FormType::Circle: return QObject::tr("Circle");
    case FormType::Figure8: return QObject::tr("Figure 8");
    case FormType::LineHorizontal: return QObject::tr("Pan line");
    case FormType::LineVertical: return QObject::tr("Tilt line");
    case FormType::Square: return QObject::tr("Square");
    case FormType::Triangle: return QObject::tr("Triangle");
    }
    return {};
}

QString spreadDirectionName(SpreadDirection d)
{
    switch (d) {
    case SpreadDirection::Forward: return QObject::tr("Forward");
    case SpreadDirection::Backward: return QObject::tr("Backward");
    case SpreadDirection::Inward: return QObject::tr("Ends to middle");
    case SpreadDirection::Outward: return QObject::tr("Middle to ends");
    case SpreadDirection::Random: return QObject::tr("Random");
    }
    return {};
}

const QVector<ColorFamily> &colorFamilies()
{
    static const QVector<ColorFamily> f = {
        {"red", QObject::tr("Red"), {"ColorAdd_R"}},
        {"green", QObject::tr("Green"), {"ColorAdd_G"}},
        {"blue", QObject::tr("Blue"), {"ColorAdd_B"}},
        {"white", QObject::tr("White"), {"ColorAdd_W", "ColorAdd_WW", "ColorAdd_CW"}},
        {"amber", QObject::tr("Amber"), {"ColorAdd_A"}},
        {"orange", QObject::tr("Orange"), {"ColorAdd_O"}},
        {"uv", QObject::tr("UV"), {"ColorAdd_UV"}},
        {"lime", QObject::tr("Lime"), {"ColorAdd_L"}},
        {"mint", QObject::tr("Mint"), {"ColorAdd_Mint"}},
        {"cyanAdd", QObject::tr("Cyan emitter"), {"ColorAdd_C"}},
        {"indigo", QObject::tr("Indigo"), {"ColorAdd_I"}},
        {"purple", QObject::tr("Purple"), {"ColorAdd_P"}},
        {"magYel", QObject::tr("Magenta/yellow emitters"), {"ColorAdd_M", "ColorAdd_Y"}},
        {"hsb", QObject::tr("Hue/saturation"), {"HSB_Hue", "HSB_Saturation", "HSB_Brightness"}},
        {"cmy", QObject::tr("CMY"), {"ColorSub_C", "ColorSub_M", "ColorSub_Y"}},
        {"cto", QObject::tr("CTO/CTB"), {"CTO", "CTB", "CTC"}},
        {"wheel", QObject::tr("Color wheel"), {"Color1", "Color2"}},
        {"macro", QObject::tr("Macros"), {"ColorMacro"}},
    };
    return f;
}

QStringList EffectLayer::drivenAttributes(const QStringList &fixtureAttributes) const
{
    QStringList out;
    switch (kind) {
    case LayerKind::Dimmer:
        if (fixtureAttributes.contains("Dimmer"))
            out << "Dimmer";
        break;
    case LayerKind::Position:
        for (const char *a : {"Pan", "Tilt"})
            if (fixtureAttributes.contains(a))
                out << a;
        break;
    case LayerKind::Color: {
        QStringList excluded;
        for (const ColorFamily &f : colorFamilies())
            if (excludedFamilies.contains(f.id))
                excluded << f.attributes;
        for (const QString &a : fixtureAttributes)
            if (featureOf(a) == Feature::Color && !excluded.contains(a) && !a.startsWith("Color1W") && !a.endsWith("Spin"))
                out << a;
        break;
    }
    case LayerKind::Attributes:
        for (const QString &a : attributes)
            if (fixtureAttributes.contains(a))
                out << a;
        break;
    }
    return out;
}

QJsonObject EffectLayer::toJson() const
{
    QJsonArray pts;
    for (const EffectPoint &p : points)
        pts.append(p.toJson());
    QJsonObject o{{"name", name}, {"kind", int(kind)}, {"mode", int(mode)}, {"enabled", enabled},
                  {"shape", shapeId}, {"points", pts}, {"width", width}, {"crossfade", crossfade},
                  {"order", int(order)}, {"speed", speed}, {"offset", offset}, {"form", int(form)},
                  {"sizePan", sizePan}, {"sizeTilt", sizeTilt}, {"hasCenter", hasCenter},
                  {"excluded", QJsonArray::fromStringList(excludedFamilies)},
                  {"attributes", QJsonArray::fromStringList(attributes)}};
    if (hasCenter)
        o["center"] = center.toJson();
    if (relative)
        o["relative"] = true;
    return o;
}

EffectLayer EffectLayer::fromJson(const QJsonObject &o)
{
    EffectLayer l;
    l.name = o["name"].toString();
    l.kind = LayerKind(std::clamp(o["kind"].toInt(), 0, 3));
    l.mode = LayerMode(std::clamp(o["mode"].toInt(), 0, 2));
    l.enabled = o["enabled"].toBool(true);
    l.shapeId = o["shape"].toInt(ShapeSine);
    for (const QJsonValue &v : o["points"].toArray())
        l.points.append(EffectPoint::fromJson(v.toObject()));
    l.width = std::clamp(o["width"].toDouble(1.0), 0.01, 1.0);
    l.crossfade = std::clamp(o["crossfade"].toDouble(1.0), 0.0, 1.0);
    l.order = StepOrder(std::clamp(o["order"].toInt(), 0, 2));
    l.speed = o["speed"].toDouble(1.0);
    l.offset = o["offset"].toDouble(0.0);
    l.form = FormType(std::clamp(o["form"].toInt(), 0, 5));
    l.sizePan = o["sizePan"].toDouble(0.12);
    l.sizeTilt = o["sizeTilt"].toDouble(0.12);
    l.hasCenter = o["hasCenter"].toBool(false);
    l.relative = o["relative"].toBool(false);
    if (l.hasCenter)
        l.center = EffectPoint::fromJson(o["center"].toObject());
    for (const QJsonValue &v : o["excluded"].toArray())
        l.excludedFamilies << v.toString();
    for (const QJsonValue &v : o["attributes"].toArray())
        l.attributes << v.toString();
    return l;
}

QList<Feature> Effect::features() const
{
    QList<Feature> out;
    auto add = [&out](Feature f) {
        if (!out.contains(f))
            out << f;
    };
    for (const EffectLayer &l : layers) {
        switch (l.kind) {
        case LayerKind::Dimmer: add(Feature::Dimmer); break;
        case LayerKind::Position: add(Feature::Position); break;
        case LayerKind::Color: add(Feature::Color); break;
        case LayerKind::Attributes:
            for (const QString &a : l.attributes)
                add(featureOf(a));
            break;
        }
    }
    return out;
}

QJsonObject Effect::toJson() const
{
    QJsonArray ls;
    for (const EffectLayer &l : layers)
        ls.append(l.toJson());
    QJsonObject o{{"id", id}, {"name", name}, {"description", description}, {"layers", ls}, {"cycle", cycle},
                  {"spread", spread}, {"groups", groups}, {"blocks", blocks}, {"wings", wings}, {"direction", int(direction)}};
    if (rateMaster)
        o["rateMaster"] = rateMaster;
    if (!axisX)
        o["axisX"] = false;
    if (axisY)
        o["axisY"] = true;
    o["spreadRef"] = std::max(0, spreadId);
    return o;
}

Spread Effect::ownLayout() const
{
    Spread m;
    m.phaseFrom = 0.0;
    m.phaseTo = spread;
    m.blocks = std::max(1, blocks);
    m.groups = std::max(0, groups);
    m.wings = std::max(1, wings);
    m.direction = direction;
    return m;
}

Effect Effect::fromJson(const QJsonObject &o)
{
    Effect e;
    e.id = o["id"].toInt();
    e.name = o["name"].toString();
    e.description = o["description"].toString();
    for (const QJsonValue &v : o["layers"].toArray())
        e.layers.append(EffectLayer::fromJson(v.toObject()));
    e.cycle = std::max(0.05, o["cycle"].toDouble(2.0));
    e.spread = o["spread"].toDouble(0.0);
    e.groups = std::max(0, o["groups"].toInt(0));
    e.blocks = std::max(1, o["blocks"].toInt(1));
    e.wings = std::max(1, o["wings"].toInt(1));
    e.direction = SpreadDirection(std::clamp(o["direction"].toInt(), 0, 4));
    e.rateMaster = std::clamp(o["rateMaster"].toInt(0), 0, RateMaster::kCount);
    e.spreadId = o.contains("spreadRef") ? std::max(0, o["spreadRef"].toInt()) : -1;
    e.axisX = o["axisX"].toBool(true);
    e.axisY = o["axisY"].toBool(false);
    return e;
}

// ---- Rate masters ------------------------------------------------------------

namespace {
// Ratio steps, ×1/16 … ×16, ×1 in the middle.
constexpr double kRatios[] = {1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 2, 1.0, 2.0, 4.0, 8.0, 16.0};
constexpr int kRatioCount = int(sizeof(kRatios) / sizeof(kRatios[0]));
constexpr double kMaxBpm = 240.0;
constexpr double kMaxRate = 2.0;
int nearestRatio(double v)
{
    int best = 4;
    for (int i = 0; i < kRatioCount; ++i)
        if (std::fabs(std::log2(std::max(1e-6, v)) - std::log2(kRatios[i])) < std::fabs(std::log2(std::max(1e-6, v)) - std::log2(kRatios[best])))
            best = i;
    return best;
}
} // namespace

QString rateModeName(RateMode m)
{
    switch (m) {
    case RateMode::Rate: return QStringLiteral("Rate");
    case RateMode::BPM: return QStringLiteral("BPM");
    case RateMode::Ratio: return QStringLiteral("Ratio");
    }
    return QStringLiteral("Rate");
}

RateMode rateModeFromName(const QString &n)
{
    if (n.compare(QLatin1String("BPM"), Qt::CaseInsensitive) == 0)
        return RateMode::BPM;
    if (n.compare(QLatin1String("Ratio"), Qt::CaseInsensitive) == 0)
        return RateMode::Ratio;
    return RateMode::Rate;
}

QString RateMaster::displayName() const
{
    return name.isEmpty() ? QObject::tr("Master %1").arg(id) : name;
}

RateMaster RateMaster::defaultFor(int id)
{
    RateMaster m;
    m.id = id;
    m.mode = id == 1 ? RateMode::BPM : id == 2 ? RateMode::Ratio : RateMode::Rate;
    m.value = m.defaultValue();
    return m;
}

QString RateMaster::valueText() const
{
    QString t;
    switch (mode) {
    case RateMode::Rate: t = QStringLiteral("%1%").arg(std::lround(value * 100)); break;
    case RateMode::BPM: t = QStringLiteral("%1 BPM").arg(std::lround(value)); break;
    case RateMode::Ratio: {
        const double r = kRatios[nearestRatio(value)];
        t = r >= 1.0 ? QStringLiteral("×%1").arg(std::lround(r)) : QStringLiteral("×1/%1").arg(std::lround(1.0 / r));
        break;
    }
    }
    return frozen ? QObject::tr("%1 frozen").arg(t) : t;
}

double RateMaster::defaultValue() const
{
    return mode == RateMode::BPM ? 120.0 : 1.0;
}

double RateMaster::faderPosition() const
{
    switch (mode) {
    case RateMode::Rate: return std::clamp(value / kMaxRate, 0.0, 1.0);
    case RateMode::BPM: return std::clamp(value / kMaxBpm, 0.0, 1.0);
    case RateMode::Ratio: return double(nearestRatio(value)) / (kRatioCount - 1);
    }
    return 0.5;
}

void RateMaster::setFromFader(double f)
{
    f = std::clamp(f, 0.0, 1.0);
    switch (mode) {
    case RateMode::Rate: value = f * kMaxRate; break;
    case RateMode::BPM: value = f * kMaxBpm; break;
    case RateMode::Ratio: value = kRatios[std::clamp(int(std::lround(f * (kRatioCount - 1))), 0, kRatioCount - 1)]; break;
    }
}

void RateMaster::setTyped(double v)
{
    switch (mode) {
    case RateMode::Rate: value = std::clamp(v / 100.0, 0.0, 16.0); break;
    case RateMode::BPM: value = std::clamp(v, 0.0, 999.0); break;
    case RateMode::Ratio: value = kRatios[nearestRatio(v)]; break;
    }
}

double RateMaster::cyclesPerSecond(double cycleSeconds, double instanceRate) const
{
    if (frozen)
        return 0.0;
    switch (mode) {
    case RateMode::BPM: return instanceRate * value / 60.0;   // one cycle per beat
    case RateMode::Ratio: return instanceRate * kRatios[nearestRatio(value)] / std::max(0.05, cycleSeconds);
    case RateMode::Rate: break;
    }
    return instanceRate * value / std::max(0.05, cycleSeconds);
}

bool RateMaster::tap(qint64 msecs)
{
    // A pause longer than two seconds starts counting again.
    if (!m_taps.isEmpty() && msecs - m_taps.last() > 2000)
        m_taps.clear();
    m_taps.append(msecs);
    while (m_taps.size() > 5)
        m_taps.removeFirst();
    if (m_taps.size() < 2)
        return false;
    const double beat = double(m_taps.last() - m_taps.first()) / (m_taps.size() - 1) / 1000.0;
    if (beat <= 0.05)
        return false;
    const double bpm = 60.0 / beat;
    switch (mode) {
    case RateMode::BPM: value = bpm; break;
    // Rate and Ratio: the tapped beat becomes one cycle of a 2-second effect (an effect at its
    // own speed and 100% runs a cycle every 2 s, so 30 BPM = 100%).
    case RateMode::Rate: value = std::clamp(bpm / 30.0, 0.0, 16.0); break;
    case RateMode::Ratio: value = kRatios[nearestRatio(bpm / 30.0)]; break;
    }
    return true;
}

QJsonObject RateMaster::toJson() const
{
    QJsonObject o{{"id", id}, {"mode", rateModeName(mode)}, {"value", value}};
    if (!name.isEmpty())
        o["name"] = name;
    if (frozen)
        o["frozen"] = true;
    if (button)
        o["button"] = true;
    if (!color.isEmpty())
        o["color"] = color;
    if (key != Key::Auto)
        o["key"] = key == Key::Learn ? QStringLiteral("learn") : QStringLiteral("reset");
    return o;
}

RateMaster RateMaster::fromJson(const QJsonObject &o)
{
    RateMaster m;
    m.id = std::clamp(o["id"].toInt(1), 1, kCount);
    m.name = o["name"].toString();
    m.mode = rateModeFromName(o["mode"].toString());
    m.value = o["value"].toDouble(m.defaultValue());
    m.frozen = o["frozen"].toBool(false);
    m.button = o["button"].toBool(false);
    m.color = o["color"].toString();
    const QString k = o["key"].toString();
    m.key = k == QLatin1String("learn") ? Key::Learn : k == QLatin1String("reset") ? Key::Reset : Key::Auto;
    return m;
}

QJsonObject EffectInstance::toJson() const
{
    QJsonArray f;
    for (int fid : fixtures)
        f.append(fid);
    QJsonObject o{{"effect", effectId}, {"fixtures", f}, {"rate", rate}, {"size", size}, {"spread", spread}};
    if (hasLayout)
        o["layout"] = layout.toJson();
    if (hasLayoutY)
        o["layoutY"] = layoutY.toJson();
    if (!grid.isEmpty()) {
        QJsonArray g;
        for (const QPoint &p : grid)
            g.append(QJsonArray{p.x(), p.y()});
        o["grid"] = g;
    }
    if (scope != Universal)
        o["scope"] = scope;
    if (!types.isEmpty())
        o["types"] = QJsonArray::fromStringList(types);
    return o;
}

EffectInstance EffectInstance::fromJson(const QJsonObject &o)
{
    EffectInstance i;
    i.effectId = o["effect"].toInt();
    for (const QJsonValue &v : o["fixtures"].toArray())
        i.fixtures << v.toInt();
    i.rate = o["rate"].toDouble(1.0);
    i.size = o["size"].toDouble(1.0);
    i.spread = o["spread"].toDouble(-1.0);
    if (o.contains("layout")) {
        i.hasLayout = true;
        i.layout = Spread::fromJson(o["layout"].toObject());
    }
    if (o.contains("layoutY")) {
        i.hasLayoutY = true;
        i.layoutY = Spread::fromJson(o["layoutY"].toObject());
        i.layoutY.onX = false;
        i.layoutY.onY = true;
    }
    for (const QJsonValue &v : o["grid"].toArray())
        i.grid << QPoint(v.toArray().at(0).toInt(), v.toArray().at(1).toInt());
    if (i.grid.size() != i.fixtures.size())
        i.grid.clear();
    i.scope = std::clamp(o.value("scope").toInt(Universal), 0, 2);
    for (const QJsonValue &t : o.value("types").toArray())
        i.types << t.toString();
    return i;
}

// ===========================================================================
// Spread
// ===========================================================================

namespace {
struct Layout {
    double spread;   // fraction of a cycle across the fixtures
    double start;    // fraction of a cycle the first one starts at
    int blocks, groups, wings;
    SpreadDirection direction;
    int seed;
};

QVector<double> layoutPositions(const Layout &e, int count);

QVector<double> spreadPositions(const Effect &e, int count, double spreadOverride)
{
    return layoutPositions({(spreadOverride >= 0 ? spreadOverride : e.spread) / 360.0, 0.0, e.blocks, e.groups, e.wings,
                            e.direction, e.id},
                           count);
}

QVector<double> layoutPositions(const Layout &e, int count)
{
    QVector<double> out(count, 0.0);
    const double spread = e.spread;
    if (count <= 0)
        return out;
    const int blocks = std::max(1, e.blocks);
    int n = (count + blocks - 1) / blocks;
    if (e.groups > 0)
        n = std::min(n, e.groups);
    const int wings = std::max(1, e.wings);
    const int wingSize = (n + wings - 1) / wings;
    const int slotCount = wings > 1 ? wingSize : n;

    // Random direction: a fixed shuffle of slotCount, stable for this effect.
    QVector<int> rank(slotCount);
    std::iota(rank.begin(), rank.end(), 0);
    if (e.direction == SpreadDirection::Random) {
        QVector<int> order(slotCount);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&e](int a, int b) { return hash01(quint64(a), quint64(e.seed) + 101) < hash01(quint64(b), quint64(e.seed) + 101); });
        for (int i = 0; i < slotCount; ++i)
            rank[order[i]] = i;
    }
    for (int i = 0; i < count; ++i) {
        int idx = i / blocks;
        if (e.groups > 0)
            idx %= e.groups;
        if (wings > 1) {
            const int w = idx / wingSize;
            int local = idx % wingSize;
            if (w % 2 == 1)
                local = wingSize - 1 - local;
            idx = local;
        }
        double frac = 0.0;
        if (slotCount > 1) {
            switch (e.direction) {
            case SpreadDirection::Forward: frac = double(idx) / slotCount; break;
            case SpreadDirection::Backward: frac = double(slotCount - 1 - idx) / slotCount; break;
            case SpreadDirection::Inward:
            case SpreadDirection::Outward: {
                const int half = (slotCount + 1) / 2;
                const int d = std::min(idx, slotCount - 1 - idx);
                frac = e.direction == SpreadDirection::Inward ? double(d) / half : double(half - 1 - d) / half;
                break;
            }
            case SpreadDirection::Random: frac = double(rank[idx]) / slotCount; break;
            }
        }
        out[i] = e.start + frac * spread;
    }
    return out;
}

bool slotColor(const QString &raw, double *r, double *g, double *b)
{
    static const QList<QPair<QString, QList<double>>> names = {
        {"open", {1, 1, 1}}, {"white", {1, 1, 1}}, {"red", {1, 0.16, 0.16}}, {"blue", {0.2, 0.31, 1}},
        {"green", {0.16, 0.86, 0.31}}, {"amber", {1, 0.7, 0.12}}, {"magenta", {1, 0.16, 0.86}},
        {"yellow", {1, 0.92, 0.16}}, {"cyan", {0.16, 0.86, 1}}, {"orange", {1, 0.47, 0.08}},
        {"pink", {1, 0.47, 0.75}}, {"purple", {0.59, 0.24, 1}}, {"lavender", {0.75, 0.59, 1}},
        {"uv", {0.43, 0.16, 1}}, {"cto", {1, 0.78, 0.55}}, {"ctb", {0.75, 0.84, 1}}, {"lime", {0.7, 1, 0.16}},
    };
    const QString n = raw.trimmed().toLower();
    for (const auto &e : names)
        if (n == e.first || n.contains(e.first)) {
            *r = e.second[0];
            *g = e.second[1];
            *b = e.second[2];
            return true;
        }
    return false;
}


void formOffset(FormType f, double q, double *dx, double *dy)
{
    const double a = 2 * kPi * q;
    switch (f) {
    case FormType::Circle: *dx = std::cos(a); *dy = std::sin(a); return;
    case FormType::Figure8: *dx = std::sin(a); *dy = std::sin(2 * a); return;
    case FormType::LineHorizontal: *dx = std::sin(a); *dy = 0; return;
    case FormType::LineVertical: *dx = 0; *dy = std::sin(a); return;
    case FormType::Square: {
        const double s = q * 4.0;
        const int side = int(s) % 4;
        const double t = s - std::floor(s);
        const double c[5][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}};
        *dx = c[side][0] + (c[side + 1][0] - c[side][0]) * t;
        *dy = c[side][1] + (c[side + 1][1] - c[side][1]) * t;
        return;
    }
    case FormType::Triangle: {
        const double s = q * 3.0;
        const int side = int(s) % 3;
        const double t = s - std::floor(s);
        const double c[4][2] = {{0, -1}, {0.87, 0.5}, {-0.87, 0.5}, {0, -1}};
        *dx = c[side][0] + (c[side + 1][0] - c[side][0]) * t;
        *dy = c[side][1] + (c[side + 1][1] - c[side][1]) * t;
        return;
    }
    }
    *dx = *dy = 0;
}
} // namespace

// ===========================================================================
// spread
// ===========================================================================

bool Spread::isDefault() const
{
    return every <= 1 && offset == 0 && blocks <= 1 && groups <= 0 && wings <= 1 && direction == SpreadDirection::Forward
           && phaseFrom == 0.0 && phaseTo == 360.0;
}

QString Spread::summary() const
{
    QStringList s;
    if (every > 1)
        s << (offset ? QObject::tr("Every %1 from %2").arg(every).arg(offset + 1) : QObject::tr("Every %1").arg(every));
    if (blocks > 1)
        s << QObject::tr("Blocks %1").arg(blocks);
    if (groups > 0)
        s << QObject::tr("Groups %1").arg(groups);
    if (wings > 1)
        s << QObject::tr("Wings %1").arg(wings);
    if (direction != SpreadDirection::Forward)
        s << spreadDirectionName(direction);
    if (phaseFrom != 0.0 || phaseTo != 360.0)
        s << QObject::tr("Phase %1 to %2").arg(phaseFrom).arg(phaseTo);
    if (s.isEmpty())
        return QObject::tr("Off");
    return (onX && onY ? QStringLiteral("XY: ") : onY ? QStringLiteral("Y: ") : QString()) + s.join(QStringLiteral(" · "));
}

QVector<int> Spread::apply(const QVector<int> &fids) const
{
    if (every <= 1)
        return fids;
    QVector<int> out;
    for (int i = 0; i < fids.size(); ++i)
        if (i % every == offset % every)
            out << fids[i];
    return out;
}

QVector<double> Spread::phases(int count) const
{
    return layoutPositions({(phaseTo - phaseFrom) / 360.0, phaseFrom / 360.0, blocks, groups, wings, direction, id + 7}, count);
}

QVector<double> Spread::timeFractions(int count) const
{
    QVector<double> p = layoutPositions({1.0, 0.0, blocks, groups, wings, direction, id + 7}, count);
    double top = 0.0;
    for (double v : p)
        top = std::max(top, v);
    if (top > 1e-9)
        for (double &v : p)
            v /= top;
    return p;
}

GridIndex gridIndex(const QVector<int> &fids, const QVector<QPoint> &grid)
{
    GridIndex g;
    const int n = int(fids.size());
    g.col.resize(n);
    g.row.resize(n);
    if (grid.size() != n) {   // one row, in order
        for (int i = 0; i < n; ++i)
            g.col[i] = i;
        g.cols = std::max(1, n);
        return g;
    }
    // Columns and rows in use, in order: a gap in the matrix doesn't count as a step.
    QVector<int> xs, ys;
    for (const QPoint &p : grid) {
        if (!xs.contains(p.x())) xs << p.x();
        if (!ys.contains(p.y())) ys << p.y();
    }
    std::sort(xs.begin(), xs.end());
    std::sort(ys.begin(), ys.end());
    for (int i = 0; i < n; ++i) {
        g.col[i] = int(xs.indexOf(grid[i].x()));
        g.row[i] = int(ys.indexOf(grid[i].y()));
    }
    g.cols = std::max(1, int(xs.size()));
    g.rows = std::max(1, int(ys.size()));
    return g;
}

QVector<int> applySpreads(const Spread *x, const Spread *y, const QVector<int> &fids, QVector<QPoint> *grid)
{
    const bool thinX = x && x->every > 1, thinY = y && y->every > 1;
    if (!thinX && !thinY)
        return fids;
    const QVector<QPoint> g = grid ? *grid : QVector<QPoint>();
    const GridIndex gi = gridIndex(fids, g);
    QVector<int> out;
    QVector<QPoint> kept;
    for (int i = 0; i < fids.size(); ++i) {
        if (thinX && gi.col[i] % x->every != x->offset % x->every)
            continue;
        if (thinY && gi.row[i] % y->every != y->offset % y->every)
            continue;
        out << fids[i];
        if (g.size() == fids.size())
            kept << g[i];
    }
    if (grid)
        *grid = kept;
    return out;
}

QVector<double> gridTimeFractions(const Spread *x, const Spread *y, const QVector<int> &fids, const QVector<QPoint> &grid)
{
    const GridIndex gi = gridIndex(fids, grid);
    const bool useY = y && !y->isDefault();
    const QVector<double> fx = x ? x->timeFractions(gi.cols) : evenFractions(gi.cols);
    const QVector<double> fy = y ? y->timeFractions(gi.rows) : evenFractions(gi.rows);
    QVector<double> out(fids.size());
    for (int i = 0; i < fids.size(); ++i) {
        const double a = fx.value(gi.col[i]), b = fy.value(gi.row[i]);
        out[i] = useY && gi.rows > 1 ? (gi.cols > 1 ? (a + b) / 2.0 : b) : a;
    }
    return out;
}

double alongPoints(const QVector<double> &points, double fraction)
{
    if (points.isEmpty())
        return 0.0;
    if (points.size() == 1)
        return points.first();
    const double x = std::clamp(fraction, 0.0, 1.0) * (points.size() - 1);
    const int i = std::min(int(x), int(points.size()) - 2);
    return points[i] + (points[i + 1] - points[i]) * (x - i);
}

QVector<double> evenFractions(int count)
{
    QVector<double> out(count, 0.0);
    for (int i = 0; i < count; ++i)
        out[i] = count > 1 ? double(i) / (count - 1) : 0.0;
    return out;
}

QJsonObject Spread::toJson() const
{
    QJsonObject o{{"id", id}, {"name", name}};
    if (every > 1) o["every"] = every;
    if (offset) o["offset"] = offset;
    if (blocks > 1) o["blocks"] = blocks;
    if (groups > 0) o["groups"] = groups;
    if (wings > 1) o["wings"] = wings;
    if (direction != SpreadDirection::Forward) o["direction"] = int(direction);
    if (phaseFrom != 0.0) o["phaseFrom"] = phaseFrom;
    if (phaseTo != 360.0) o["phaseTo"] = phaseTo;
    if (!onX) o["x"] = false;
    if (onY) o["y"] = true;
    return o;
}

Spread Spread::fromJson(const QJsonObject &o)
{
    Spread m;
    m.id = o["id"].toInt();
    m.name = o["name"].toString();
    m.every = std::max(1, o["every"].toInt(1));
    m.offset = std::max(0, o["offset"].toInt(0));
    m.blocks = std::max(1, o["blocks"].toInt(1));
    m.groups = std::max(0, o["groups"].toInt(0));
    m.wings = std::max(1, o["wings"].toInt(1));
    m.direction = SpreadDirection(std::clamp(o["direction"].toInt(), 0, 4));
    m.phaseFrom = std::clamp(o["phaseFrom"].toDouble(0.0), -720.0, 720.0);
    m.phaseTo = std::clamp(o["phaseTo"].toDouble(360.0), -720.0, 720.0);
    m.onX = o["x"].toBool(true);
    m.onY = o["y"].toBool(false);
    if (o["axis"].toString() == QLatin1String("y")) {   // a build that had one axis per spread
        m.onX = false;
        m.onY = true;
    }
    return m;
}

double spreadPosition(const Effect &e, int index, int count)
{
    const QVector<double> p = spreadPositions(e, count, -1);
    return index >= 0 && index < p.size() ? p[index] : 0.0;
}

// ===========================================================================
// Color mapping
// ===========================================================================

RawAttr emitterMix(const FixtureMode *m, double r, double g, double b, const QStringList *allowed)
{
    RawAttr out;
    if (!m)
        return out;
    auto has = [&](const char *a) { return m->channel(a) && (!allowed || allowed->contains(QLatin1String(a))); };
    r = std::max(0.0, r);
    g = std::max(0.0, g);
    b = std::max(0.0, b);
    const double mx = std::max({r, g, b});
    if (mx > 1e-9) {
        r /= mx;
        g /= mx;
        b /= mx;
    } else {
        r = g = b = 0;
    }
    const bool rgb = has("ColorAdd_R") || has("ColorAdd_G") || has("ColorAdd_B");
    if (!rgb) {
        // A virtual-color fixture: hue, saturation and brightness.
        if (has("HSB_Hue") || has("HSB_Saturation")) {
            const double mn = std::min({r, g, b});
            const double d = 1.0 - mn;   // max is 1 after normalizing (or 0)
            double h = 0;
            if (mx > 1e-9 && d > 1e-9) {
                if (r >= 1.0 - 1e-9) h = std::fmod((g - b) / d, 6.0);
                else if (g >= 1.0 - 1e-9) h = (b - r) / d + 2.0;
                else h = (r - g) / d + 4.0;
                h /= 6.0;
                if (h < 0) h += 1.0;
            }
            if (has("HSB_Hue")) out.insert("HSB_Hue", h);
            if (has("HSB_Saturation")) out.insert("HSB_Saturation", mx > 1e-9 ? d : 0.0);
            if (has("HSB_Brightness")) out.insert("HSB_Brightness", mx > 1e-9 ? 1.0 : 0.0);
        }
        return out;
    }
    double t[3] = {r, g, b};
    auto take = [&](const char *attr, double er, double eg, double eb) {
        if (!m->channel(attr))
            return;
        if (allowed && !allowed->contains(QLatin1String(attr)))
            return;   // left alone: not ours to set
        double amount = 1.0;
        const double e[3] = {er, eg, eb};
        for (int c = 0; c < 3; ++c)
            if (e[c] > 0.05)
                amount = std::min(amount, t[c] / e[c]);
        amount = std::clamp(amount, 0.0, 1.0);
        for (int c = 0; c < 3; ++c)
            t[c] = std::max(0.0, t[c] - amount * e[c]);
        out.insert(attr, amount);
    };
    // Whites first (the brightest emitters), then the in-between colors, RGB for what's left.
    if (m->channel("ColorAdd_W")) {
        take("ColorAdd_W", 1.0, 1.0, 1.0);
        for (const char *ww : {"ColorAdd_WW", "ColorAdd_CW"})
            if (has(ww))
                out.insert(ww, 0.0);
    } else {
        take("ColorAdd_WW", 1.0, 0.75, 0.45);
        take("ColorAdd_CW", 0.85, 0.92, 1.0);
    }
    take("ColorAdd_A", 1.0, 0.55, 0.0);
    take("ColorAdd_O", 1.0, 0.35, 0.0);
    take("ColorAdd_Y", 1.0, 1.0, 0.0);
    take("ColorAdd_L", 0.6, 1.0, 0.0);
    take("ColorAdd_Mint", 0.3, 1.0, 0.6);
    take("ColorAdd_C", 0.0, 1.0, 1.0);
    take("ColorAdd_I", 0.25, 0.0, 1.0);
    take("ColorAdd_P", 0.6, 0.0, 1.0);
    take("ColorAdd_M", 1.0, 0.0, 1.0);
    if (has("ColorAdd_UV"))
        out.insert("ColorAdd_UV", 0.0);
    out.insert("ColorAdd_R", t[0]);
    out.insert("ColorAdd_G", t[1]);
    out.insert("ColorAdd_B", t[2]);
    return out;
}

QVector<std::array<double, 3>> emitterColors(const FixtureMode *m)
{
    QVector<std::array<double, 3>> out;
    if (!m)
        return out;
    // The same emitter colors the mix above works with.
    static const QList<QPair<const char *, std::array<double, 3>>> emitters = {
        {"ColorAdd_R", {1.0, 0.0, 0.0}}, {"ColorAdd_G", {0.0, 1.0, 0.0}}, {"ColorAdd_B", {0.0, 0.0, 1.0}},
        {"ColorAdd_W", {1.0, 1.0, 1.0}}, {"ColorAdd_WW", {1.0, 0.75, 0.45}}, {"ColorAdd_CW", {0.85, 0.92, 1.0}},
        {"ColorAdd_A", {1.0, 0.55, 0.0}}, {"ColorAdd_O", {1.0, 0.35, 0.0}}, {"ColorAdd_Y", {1.0, 1.0, 0.0}},
        {"ColorAdd_L", {0.6, 1.0, 0.0}}, {"ColorAdd_Mint", {0.3, 1.0, 0.6}}, {"ColorAdd_C", {0.0, 1.0, 1.0}},
        {"ColorAdd_I", {0.25, 0.0, 1.0}}, {"ColorAdd_P", {0.6, 0.0, 1.0}}, {"ColorAdd_M", {1.0, 0.0, 1.0}},
    };
    for (const auto &e : emitters)
        if (m->channel(e.first))
            out << e.second;
    if (!out.isEmpty())
        return out;
    if (m->channel("ColorSub_C") || m->channel("HSB_Hue")) {
        out << std::array<double, 3>{1.0, 0.0, 0.0} << std::array<double, 3>{0.0, 1.0, 0.0} << std::array<double, 3>{0.0, 0.0, 1.0};
        return out;
    }
    for (const char *wheelAttr : {"Color1", "Color2"})
        if (const ChannelDef *wheel = m->channel(wheelAttr))
            for (const ChannelSet &s : wheel->sets) {
                double sr, sg, sb;
                if (slotColor(s.name, &sr, &sg, &sb))
                    out << std::array<double, 3>{srgbToLinear(sr), srgbToLinear(sg), srgbToLinear(sb)};
            }
    out << std::array<double, 3>{1.0, 1.0, 1.0};   // open, or a white-only fixture
    return out;
}

RawAttr colorAttributes(const Show &show, int fid, double r, double g, double b, const QStringList *allowed)
{
    RawAttr out;
    const FixtureMode *m = show.fixtureMode(fid);
    if (!m)
        return out;
    out = emitterMix(m, r, g, b, allowed);
    r = std::max(0.0, r);
    g = std::max(0.0, g);
    b = std::max(0.0, b);
    const double mx = std::max({r, g, b});
    if (mx > 1e-9) {
        r /= mx;
        g /= mx;
        b /= mx;
    } else {
        r = g = b = 0;
    }
    if (m->channel("ColorSub_C")) {
        out.insert("ColorSub_C", 1 - r);
        out.insert("ColorSub_M", 1 - g);
        out.insert("ColorSub_Y", 1 - b);
    }
    const double sr0 = linearToSrgb(r), sg0 = linearToSrgb(g), sb0 = linearToSrgb(b);
    for (const char *wheelAttr : {"Color1", "Color2"}) {
        const ChannelDef *wheel = m->channel(wheelAttr);
        if (!wheel)
            continue;
        double best = 1e9, value = -1;
        for (const ChannelSet &s : wheel->sets) {
            double sr, sg, sb;
            if (!slotColor(s.name, &sr, &sg, &sb))
                continue;
            const double d = (sr - sr0) * (sr - sr0) + (sg - sg0) * (sg - sg0) + (sb - sb0) * (sb - sb0);
            if (d < best) {
                best = d;
                value = (s.from + s.to) / 2.0;
            }
        }
        if (value >= 0)
            out.insert(wheelAttr, value);
    }
    return out;
}

// ===========================================================================
// Evaluation
// ===========================================================================

namespace {
const Shape &shapeFor(const Show &show, int id)
{
    static const Shape fallback{ShapeSine, "Sine", {}, true};
    if (id < 0) {
        const Shape *s = builtinShape(id);
        return s ? *s : fallback;
    }
    const Shape *s = show.shape(id);
    return s ? *s : fallback;
}

// The attribute values a point stands for on one fixture (only the attributes the layer drives).
RawAttr pointValues(const Show &show, int fid, const EffectPoint &p, const QStringList &attrs)
{
    RawAttr out;
    switch (p.kind) {
    case EffectPoint::Value:
        for (const QString &a : attrs) {
            if (p.values.contains(a))
                out.insert(a, p.values.value(a));
            else if (p.values.contains(QStringLiteral("Level"))) {
                // A level on pan or tilt is a share of its range, in degrees on this fixture.
                const double level = p.values.value(QStringLiteral("Level"));
                const ChannelDef *c = isAngle(a) ? show.channelOf(fid, a) : nullptr;
                out.insert(a, c ? c->fromDmx(level) : level);
            }
        }
        break;
    case EffectPoint::Color: {
        const RawAttr c = colorAttributes(show, fid, p.r, p.g, p.b, &attrs);
        for (const QString &a : attrs)
            if (c.contains(a))
                out.insert(a, c.value(a));
        break;
    }
    case EffectPoint::Preset:
        if (const Preset *pr = show.preset(p.presetType, p.presetId))
            for (const QString &a : attrs) {
                double v;
                if (show.presetValue(*pr, fid, a, &v))
                    out.insert(a, v);
            }
        break;
    }
    return out;
}

double blend(const QString &a, double from, double to, double t)
{
    if (isDiscrete(a))
        return t >= 0.5 ? to : from;
    return from + (to - from) * t;
}
} // namespace

void evaluateEffect(const Show &show, const Effect &effect, const EffectInstance &inst, double phase,
                    const BaseValue &base, RawTable &out)
{
    const int n = int(inst.fixtures.size());
    if (n == 0)
        return;
    // The layout: the instance's (a blueprint line's spread, or the one on the selection), else
    // the effect's spread from the pool, else its own.
    const Spread *pool = effect.spreadId > 0 ? show.spread(effect.spreadId) : nullptr;
    // Across the matrix's columns (X) and down its rows (Y): each axis laid out by its spread,
    // the phases adding up when the effect runs both ways (diagonally).
    const GridIndex gi = gridIndex(inst.fixtures, inst.grid);
    auto along = [&](bool down, int count) {
        if (down && inst.hasLayoutY)
            return inst.layoutY.phases(count);
        if (!down && inst.hasLayout)
            return inst.layout.phases(count);
        return pool && inst.spread < 0 ? pool->phases(count) : spreadPositions(effect, count, inst.spread);
    };
    const QVector<double> px = effect.axisX ? along(false, gi.cols) : QVector<double>();
    const QVector<double> py = effect.axisY ? along(true, gi.rows) : QVector<double>();
    QVector<double> positions(n, 0.0);
    for (int i = 0; i < n; ++i)
        // Both ways: the two phases added, across the effect's range (one sweep corner to corner).
        positions[i] = effect.axisX && effect.axisY ? (px.value(gi.col[i]) + py.value(gi.row[i])) / 2.0
                       : effect.axisX              ? px.value(gi.col[i])
                       : effect.axisY              ? py.value(gi.row[i])
                                                   : 0.0;
    const double size = std::max(0.0, inst.size);
    // Each fixture's attributes, once for all layers.
    QVector<QStringList> fixtureAttrs(n);
    for (int i = 0; i < n; ++i)
        fixtureAttrs[i] = show.fixtureAttributes(inst.fixtures[i]);

    for (const EffectLayer &layer : effect.layers) {
        if (!layer.enabled)
            continue;
        const Shape &shape = shapeFor(show, layer.shapeId);
        // Step order for this layer (forward, or there and back for Bounce).
        QVector<int> seq;
        for (int j = 0; j < int(layer.points.size()); ++j)
            seq << j;
        if (layer.order == StepOrder::Bounce)
            for (int j = int(layer.points.size()) - 2; j >= 1; --j)
                seq << j;
        for (int i = 0; i < n; ++i) {
            const int fid = inst.fixtures[i];
            const QStringList attrs = layer.drivenAttributes(fixtureAttrs[i]);
            if (attrs.isEmpty())
                continue;
            const double p = phase * layer.speed + layer.offset / 360.0 + positions[i];
            const qint64 cycle = qint64(std::floor(p));
            const double q = p - std::floor(p);
            const int seed = i + 1;
            RawAttr &target = out[fid];

            if (layer.mode == LayerMode::Form) {
                double cp = base(fid, "Pan"), ct = base(fid, "Tilt");
                if (layer.hasCenter) {
                    const RawAttr c = pointValues(show, fid, layer.center, {"Pan", "Tilt"});
                    cp = c.value("Pan", cp);
                    ct = c.value("Tilt", ct);
                }
                double dx, dy;
                formOffset(layer.form, q, &dx, &dy);
                // Sizes are shares of each fixture's range, so a circle is the same shape on a 540°
                // and a 630° pan; the values are degrees.
                if (attrs.contains("Pan"))
                    target["Pan"] = show.clampValue(fid, "Pan", cp + dx * layer.sizePan * size * show.valueSpan(fid, "Pan"));
                if (attrs.contains("Tilt"))
                    target["Tilt"] = show.clampValue(fid, "Tilt", ct + dy * layer.sizeTilt * size * show.valueSpan(fid, "Tilt"));
                continue;
            }

            const double width = std::clamp(layer.width, 0.01, 1.0);
            const bool resting = q >= width;
            const double qq = resting ? 0.0 : q / width;

            const bool relative = layer.relative && (layer.kind == LayerKind::Dimmer || layer.kind == LayerKind::Attributes);
            // Relative: a point's amount for one attribute, in its own units (a level on pan or
            // tilt is a share of the range, in degrees). Presets and colors give no amount.
            auto amount = [&](const EffectPoint &pt, const QString &a) {
                if (pt.kind != EffectPoint::Value)
                    return 0.0;
                if (pt.values.contains(a))
                    return pt.values.value(a);
                const double level = pt.values.value(QStringLiteral("Level"), 0.0);
                return isAngle(a) ? level * show.valueSpan(fid, a) : level;
            };
            if (layer.mode == LayerMode::Wave) {
                const EffectPoint low = layer.points.value(0, EffectPoint::level(0.0));
                const EffectPoint high = layer.points.value(1, EffectPoint::level(1.0));
                if (relative) {
                    const double s = std::clamp(resting ? 0.0 : shape.value(qq, cycle, seed), 0.0, 1.0);
                    for (const QString &a : attrs) {
                        const double lo = amount(low, a), hi = amount(high, a);
                        target[a] = show.clampValue(fid, a, base(fid, a) + (lo + (hi - lo) * s) * size);
                    }
                    continue;
                }
                const RawAttr lo = pointValues(show, fid, low, attrs);
                const RawAttr hi = pointValues(show, fid, high, attrs);
                const double s = std::clamp((resting ? 0.0 : shape.value(qq, cycle, seed)) * size, 0.0, 1.0);
                for (const QString &a : attrs) {
                    const double b0 = base(fid, a);
                    target[a] = show.clampValue(fid, a, blend(a, lo.value(a, b0), hi.value(a, b0), s));
                }
                continue;
            }

            // Steps
            const int k = int(layer.points.size());
            if (k == 0)
                continue;
            const int L = int(seq.size());
            const double pos = qq * L;
            const int j = std::min(L - 1, int(std::floor(pos)));
            const double f = pos - j;
            auto pick = [&](qint64 cyc, int step) {
                if (layer.order != StepOrder::Random)
                    return seq[(step % L + L) % L];
                // Random: a different point each step, never the same twice in a row
                const qint64 abs = cyc * L + step;
                const int a = int(hash01(quint64(abs), quint64(seed) * 31 + 7) * k) % k;
                const int prev = int(hash01(quint64(abs - 1), quint64(seed) * 31 + 7) * k) % k;
                return a == prev ? (a + 1) % k : a;
            };
            const int cur = resting ? seq[0] : pick(cycle, j);
            const int prev = resting ? seq[0] : pick(cycle - (j == 0 ? 1 : 0), j == 0 ? L - 1 : j - 1);
            const double t = layer.crossfade <= 0.0 ? 1.0 : smoothstep(f / layer.crossfade);
            if (relative) {
                for (const QString &a : attrs) {
                    const double from = amount(layer.points[prev], a), to = amount(layer.points[cur], a);
                    const double off = isDiscrete(a) ? (t >= 0.5 ? to : from) : from + (to - from) * t;
                    target[a] = show.clampValue(fid, a, base(fid, a) + off * size);
                }
                continue;
            }
            const RawAttr from = pointValues(show, fid, layer.points[prev], attrs);
            const RawAttr to = pointValues(show, fid, layer.points[cur], attrs);
            for (const QString &a : attrs) {
                const double b0 = base(fid, a);
                double v = blend(a, from.value(a, b0), to.value(a, b0), t);
                if (layer.kind == LayerKind::Dimmer)
                    v *= std::min(1.0, size);
                target[a] = show.clampValue(fid, a, v);
            }
        }
    }
}

// ===========================================================================
// Library
// ===========================================================================

QVector<Effect> effectTemplates()
{
    QVector<Effect> out;
    auto make = [&out](const QString &name, const QString &desc, double cycle, double spread) -> Effect & {
        Effect e;
        e.name = name;
        e.description = desc;
        e.cycle = cycle;
        e.spread = spread;
        out << e;
        return out.last();
    };
    auto dimmerWave = [](int shape, double lo, double hi, double width = 1.0) {
        EffectLayer l;
        l.name = QObject::tr("Dimmer");
        l.kind = LayerKind::Dimmer;
        l.mode = LayerMode::Wave;
        l.shapeId = shape;
        l.points = {EffectPoint::level(lo), EffectPoint::level(hi)};
        l.width = width;
        return l;
    };
    auto form = [](FormType f, double sp, double st) {
        EffectLayer l;
        l.name = QObject::tr("Position");
        l.kind = LayerKind::Position;
        l.mode = LayerMode::Form;
        l.form = f;
        l.sizePan = sp;
        l.sizeTilt = st;
        return l;
    };
    auto colorSteps = [](QVector<EffectPoint> pts, double crossfade) {
        EffectLayer l;
        l.name = QObject::tr("Color");
        l.kind = LayerKind::Color;
        l.mode = LayerMode::Steps;
        l.points = pts;
        l.crossfade = crossfade;
        return l;
    };
    auto colorWave = [](int shape, EffectPoint a, EffectPoint b) {
        EffectLayer l;
        l.name = QObject::tr("Color");
        l.kind = LayerKind::Color;
        l.mode = LayerMode::Wave;
        l.shapeId = shape;
        l.points = {a, b};
        return l;
    };

    make(QObject::tr("Dimmer wave"), QObject::tr("A smooth sine rolling across the selection"), 2.0, 360).layers << dimmerWave(ShapeSine, 0, 1);
    make(QObject::tr("Chase"), QObject::tr("One fixture at a time, with a tail"), 1.6, 360).layers << dimmerWave(ShapeRampDown, 0, 1, 0.5);
    make(QObject::tr("Hard chase"), QObject::tr("On/off chase, no fade"), 1.2, 360).layers << dimmerWave(ShapeSquare, 0, 1, 0.5);
    make(QObject::tr("Sparkle"), QObject::tr("Every fixture flickers on its own"), 0.25, 0).layers << dimmerWave(ShapeRandom, 0.05, 1);
    make(QObject::tr("Breathe"), QObject::tr("Slow swell between low and full"), 4.0, 0).layers << dimmerWave(ShapeBreathe, 0.2, 1);
    make(QObject::tr("Flash pulse"), QObject::tr("A hit then a quick decay, all together"), 1.0, 0).layers << dimmerWave(ShapePulse, 0, 1);

    make(QObject::tr("Circle"), QObject::tr("Circles around where each fixture points"), 4.0, 0).layers << form(FormType::Circle, 0.08, 0.08);
    make(QObject::tr("Figure 8"), QObject::tr("Figure 8 around the current position"), 5.0, 0).layers << form(FormType::Figure8, 0.12, 0.06);
    {
        Effect &e = make(QObject::tr("Pan fan"), QObject::tr("Pan sweep that mirrors from the middle"), 4.0, 180);
        e.wings = 2;
        e.layers << form(FormType::LineHorizontal, 0.18, 0);
    }
    make(QObject::tr("Tilt nod"), QObject::tr("Gentle tilt up and down"), 3.0, 90).layers << form(FormType::LineVertical, 0, 0.08);
    {
        Effect &e = make(QObject::tr("Ballyhoo"), QObject::tr("Big searching circles, out of phase"), 2.5, 360);
        e.direction = SpreadDirection::Random;
        e.layers << form(FormType::Circle, 0.25, 0.18);
    }

    {
        // Twelve hues, crossfading the whole way, so it's a smooth trip round the color wheel.
        QVector<EffectPoint> hues;
        for (int i = 0; i < 12; ++i) {
            const double h = i / 12.0 * 6.0;
            const double x = 1.0 - std::abs(std::fmod(h, 2.0) - 1.0);
            const int sector = int(h);
            const double rgb[6][3] = {{1, x, 0}, {x, 1, 0}, {0, 1, x}, {0, x, 1}, {x, 0, 1}, {1, 0, x}};
            hues << EffectPoint::color(rgb[sector][0], rgb[sector][1], rgb[sector][2]);
        }
        make(QObject::tr("Rainbow"), QObject::tr("A smooth trip round the color wheel, rolling across the selection"), 6.0, 360)
                .layers << colorSteps(hues, 1.0);
    }
    make(QObject::tr("Color swap"), QObject::tr("Two colors trading places. Swap the points for presets"), 3.0, 180)
            .layers << colorWave(ShapeSine, EffectPoint::color(1, 0, 0), EffectPoint::color(0, 0.1, 1));
    {
        Effect &e = make(QObject::tr("Fire"), QObject::tr("Flickering red to amber with a dimmer flicker"), 0.5, 0);
        e.layers << colorWave(ShapeSmoothRandom, EffectPoint::color(1, 0.12, 0), EffectPoint::color(1, 0.55, 0.05));
        EffectLayer d = dimmerWave(ShapeSmoothRandom, 0.55, 1);
        d.speed = 1.6;
        e.layers << d;
    }
    make(QObject::tr("Police"), QObject::tr("Hard red and blue alternation"), 0.6, 180)
            .layers << colorSteps({EffectPoint::color(1, 0, 0), EffectPoint::color(0, 0, 1)}, 0.0);
    {
        Effect &e = make(QObject::tr("Zoom breathe"), QObject::tr("Beam size swelling in and out"), 4.0, 0);
        EffectLayer l;
        l.name = QObject::tr("Zoom");
        l.kind = LayerKind::Attributes;
        l.attributes = {"Zoom"};
        l.shapeId = ShapeSine;
        l.points = {EffectPoint::level(0.15), EffectPoint::level(0.85)};
        e.layers << l;
    }
    // Strobes: added last, so effects already numbered from the library keep their numbers.
    make(QObject::tr("Strobe"), QObject::tr("Every fixture flashes together, fast and hard"), 0.15, 0)
            .layers << dimmerWave(ShapeSquare, 0, 1, 0.8);
    {
        Effect &e = make(QObject::tr("Random strobe"), QObject::tr("Hard flashes, every fixture on its own beat"), 0.2, 360);
        e.direction = SpreadDirection::Random;
        e.layers << dimmerWave(ShapeSquare, 0, 1, 0.6);
    }
    {
        // Relative: takes the level that's already up down and back, rolling across.
        Effect &e = make(QObject::tr("Dip"), QObject::tr("Dips whatever level is up by half and back, rolling across"), 2.0, 360);
        EffectLayer l = dimmerWave(ShapeSine, -0.5, 0.0);
        l.relative = true;
        e.layers << l;
    }
    return out;
}

} // namespace lumora
