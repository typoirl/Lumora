// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixturetype.h"
#include "importhelpers.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace lumora {

bool ChannelDef::isAngle() const
{
    return lumora::isAngle(attribute);
}

double ChannelDef::rangeFrom() const
{
    if (physFrom != physTo)
        return physFrom;
    return isAngle() ? -defaultAngleSpan(attribute) / 2 : 0.0;
}

double ChannelDef::rangeTo() const
{
    if (physFrom != physTo)
        return physTo;
    return isAngle() ? defaultAngleSpan(attribute) / 2 : 100.0;
}

double ChannelDef::minValue() const
{
    return isAngle() ? std::min(rangeFrom(), rangeTo()) : 0.0;
}

double ChannelDef::maxValue() const
{
    return isAngle() ? std::max(rangeFrom(), rangeTo()) : 1.0;
}

double ChannelDef::toPhysical(double v) const
{
    return isAngle() ? v : rangeFrom() + (rangeTo() - rangeFrom()) * v;
}

double ChannelDef::fromPhysical(double p) const
{
    if (isAngle())
        return p;
    const double a = rangeFrom(), b = rangeTo();
    return a == b ? 0.0 : (p - a) / (b - a);
}

QString ChannelDef::unit() const
{
    return isAngle() ? QString(QChar(0x00b0)) : QString();
}

double ChannelDef::toDmx(double v) const
{
    double share = v;   // where the value sits in its value range, 0..1
    if (isAngle()) {
        const double a = rangeFrom(), b = rangeTo();
        share = a == b ? 0.5 : (v - a) / (b - a);
    }
    share = std::clamp(share, 0.0, 1.0);
    return dmxFrom + (dmxTo - dmxFrom) * share;
}

double ChannelDef::fromDmx(double level) const
{
    const double share = dmxTo == dmxFrom ? 0.0 : std::clamp((level - dmxFrom) / (dmxTo - dmxFrom), 0.0, 1.0);
    return isAngle() ? rangeFrom() + (rangeTo() - rangeFrom()) * share : share;
}

QString ChannelDef::valueText(double v, int decimals) const
{
    return QString::number(toPhysical(v), 'f', decimals) + unit();
}

void ChannelDef::setPhysicalRange(double from, double to)
{
    const double half = (to - from) / 2.0;
    if (std::abs(half) < 1.0)
        return;
    physFrom = -half;
    physTo = half;
}

const ChannelSet *ChannelDef::setFor(double v) const
{
    v = toDmx(v);   // ranges are DMX ranges; pan and tilt values are degrees
    // Exact containment first, so a value sitting on a boundary picks the range that starts there.
    for (const ChannelSet &s : sets)
        if (v >= s.from && v <= s.to)
            return &s;
    for (const ChannelSet &s : sets)
        if (v >= s.from - 1e-6 && v <= s.to + 1e-6)
            return &s;
    return nullptr;
}

QString ChannelDef::setNameFor(double v) const
{
    const ChannelSet *s = setFor(v);
    return s ? s->name : QString();
}

const ChannelDef *FixtureMode::channel(const QString &attribute) const
{
    for (const ChannelDef &c : channels)
        if (c.attribute == attribute)
            return &c;
    return nullptr;
}

bool FixtureMode::ensureVirtualDimmer()
{
    // Each subfixture (and the fixture itself) on its own: an RGB pixel gets its own virtual Dimmer.
    QVector<QPair<int, int>> nodes;
    for (const ChannelDef &c : channels)
        if (!nodes.contains(qMakePair(c.cell, c.sub)))
            nodes << qMakePair(c.cell, c.sub);
    bool changed = false;
    for (const auto &node : nodes) {
        auto inNode = [&node](const ChannelDef &c) { return c.cell == node.first && c.sub == node.second; };
        bool realDimmer = false, virtualOne = false, colorAdd = false;
        int first = -1;
        for (int i = 0; i < channels.size(); ++i) {
            const ChannelDef &c = channels[i];
            if (!inNode(c))
                continue;
            if (first < 0)
                first = i;
            if (c.attribute == QLatin1String("Dimmer"))
                (c.isVirtual() ? virtualOne : realDimmer) = true;
            colorAdd |= isColorAdd(c.attribute) && !c.isVirtual();
        }
        if (realDimmer && virtualOne) {
            channels.erase(std::remove_if(channels.begin(), channels.end(),
                                          [&](const ChannelDef &c) {
                                              return inNode(c) && c.attribute == QLatin1String("Dimmer") && c.isVirtual();
                                          }),
                           channels.end());
            changed = true;
            continue;
        }
        if (realDimmer || virtualOne || !colorAdd || noAutoDimmer)
            continue;
        ChannelDef d;
        d.attribute = QStringLiteral("Dimmer");
        d.name = QStringLiteral("Dimmer");
        d.coarse = -1;
        d.defaultValue = 0.0;
        d.highlightValue = 1.0;
        d.cell = node.first;
        d.sub = node.second;
        channels.insert(std::max(0, first), d);
        changed = true;
    }
    return changed;
}

const FixtureMode *FixtureMode::part(int slot) const
{
    if (!m_parts)
        return slot == 0 ? this : nullptr;
    return slot >= 0 && slot < int(m_parts->modes.size()) ? &m_parts->modes[slot] : nullptr;
}

QPair<int, int> FixtureMode::partPath(int slot) const
{
    return m_parts && slot >= 0 && slot < m_parts->paths.size() ? m_parts->paths[slot] : QPair<int, int>(0, 0);
}

int FixtureMode::partSlot(int cell, int sub) const
{
    if (!m_parts)
        return cell == 0 && sub == 0 ? 0 : -1;
    return int(m_parts->paths.indexOf(qMakePair(cell, sub)));
}

void FixtureMode::buildParts()
{
    // Every subfixture, in order. A head with only sub-subfixtures still gets its own slot, as
    // the row they nest under.
    QMap<int, QSet<int>> tree;   // cell -> subs
    for (const ChannelDef &c : channels)
        if (c.cell > 0) {
            tree[c.cell].insert(0);
            if (c.sub > 0)
                tree[c.cell].insert(c.sub);
        }
    m_parts.reset();
    QVector<QPair<int, int>> paths{{0, 0}};
    for (auto it = tree.begin(); it != tree.end(); ++it) {
        QList<int> subs = it->values();
        std::sort(subs.begin(), subs.end());
        for (int sub : subs)
            paths << qMakePair(it.key(), sub);
    }
    cellCount = std::min(int(paths.size()) - 1, kCellsPerFixture - 1);
    if (cellCount == 0)
        return;
    auto parts = std::make_shared<Parts>();
    parts->paths = paths.mid(0, cellCount + 1);
    parts->modes.resize(cellCount + 1);
    for (int i = 0; i <= cellCount; ++i) {
        FixtureMode &p = parts->modes[i];
        p.name = name;
        p.footprint = footprint;
        p.noAutoDimmer = noAutoDimmer;
        for (const ChannelDef &c : channels)
            if (c.cell == paths[i].first && (c.cell == 0 || c.sub == paths[i].second))
                p.channels << c;
        const ChannelDef *dim = p.channel(QStringLiteral("Dimmer"));
        p.virtualDimmer = dim && dim->isVirtual();
    }
    m_parts = parts;
}

QStringList FixtureMode::attributes() const
{
    QStringList names;
    names.reserve(channels.size());
    for (const ChannelDef &c : channels)
        if (!names.contains(c.attribute))
            names << c.attribute;
    if (m_sorted && m_sorted->names == names)
        return m_sorted->sorted;
    // By feature, then in the usual order within it; the sort keys worked out once each.
    QVector<std::tuple<int, int, int>> keys;   // feature, sort key, position
    keys.reserve(names.size());
    for (int i = 0; i < names.size(); ++i)
        keys.append({int(featureOf(names[i])), attributeSortKey(names[i]), i});
    std::sort(keys.begin(), keys.end());   // the position keeps it stable
    QStringList out;
    out.reserve(names.size());
    for (const auto &k : keys)
        out << names[std::get<2>(k)];
    m_sorted = std::make_shared<const SortedAttributes>(SortedAttributes{names, out});
    return out;
}

const FixtureMode *FixtureType::mode(const QString &n) const
{
    for (const FixtureMode &m : modes)
        if (m.name == n)
            return &m;
    return modes.isEmpty() ? nullptr : &modes.first();
}

bool FixtureType::snapsByDefault(const QString &a)
{
    static const QStringList wheels = {"Gobo1", "Gobo2", "Gobo3", "Color1", "Color2", "AnimationWheel"};
    static const QStringList macros = {"ColorMacro", "PositionMacro", "Ctl_Macro", "Effects", "Ctl_Mode"};
    return wheels.contains(a) || macros.contains(a);
}

void FixtureType::finalize()
{
    if (!angles) {
        // Pan and tilt rest and highlight values arrive as 0..1 of the DMX range: in degrees now.
        for (FixtureMode &m : modes)
            for (ChannelDef &c : m.channels)
                if (c.isAngle()) {
                    c.defaultValue = c.fromDmx(c.defaultValue);
                    c.highlightValue = c.fromDmx(c.highlightValue);
                }
        angles = true;
    }
    if (!highlightsExplicit) {
        for (FixtureMode &m : modes)
            for (ChannelDef &c : m.channels) {
                const Feature f = featureOf(c.attribute);
                c.hasHighlight = f == Feature::Dimmer || f == Feature::Color || c.attribute == QLatin1String("Shutter");
            }
        highlightsExplicit = true;
    }
    if (!snapsExplicit) {
        for (FixtureMode &m : modes)
            for (ChannelDef &c : m.channels)
                c.snap = snapsByDefault(c.attribute);
        snapsExplicit = true;
    }
    for (FixtureMode &m : modes) {
        m.ensureVirtualDimmer();
        int fp = 0;
        for (ChannelDef &c : m.channels) {
            if (c.isVirtual()) {
                c.coarse = -1;
                c.fine = -1;
                continue;
            }
            fp = std::max(fp, c.coarse + 1);
            if (c.fine >= 0)
                fp = std::max(fp, c.fine + 1);
        }
        if (m.footprint < fp)
            m.footprint = fp;
        const ChannelDef *dim = m.channel(QStringLiteral("Dimmer"));
        m.virtualDimmer = dim && dim->isVirtual() && dim->cell == 0;
        m.buildParts();
    }
}

void FixtureType::applyOpenDefaults()
{
    if (openDefaults)
        return;
    openDefaults = true;
    auto openSet = [](const ChannelDef &c, double *out) {
        static const QRegularExpression open(QStringLiteral("\\b(open|white|no color|no gobo)\\b"),
                                             QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression closed(QStringLiteral("clos|blackout|black out"),
                                               QRegularExpression::CaseInsensitiveOption);
        auto isOpen = [&](const ChannelSet &s) { return open.match(s.name).hasMatch() && !closed.match(s.name).hasMatch(); };
        if (const ChannelSet *cur = c.setFor(c.defaultValue); cur && isOpen(*cur))
            return false;   // the file's own default is already open: keep it
        for (const ChannelSet &s : c.sets)
            if (isOpen(s)) {
                *out = (s.from + s.to) / 2.0;
                return true;
            }
        return false;
    };
    for (FixtureMode &m : modes) {
        // Pixel fixtures: the master's intensity rests at full, so the cells light on their own.
        int cells = 0;
        for (const ChannelDef &c : m.channels)
            cells = std::max(cells, c.cell);
        for (ChannelDef &c : m.channels) {
            const QString &a = c.attribute;
            double v;
            // and every subfixture's own intensity rests at 0.
            if (cells > 0 && a == QLatin1String("Dimmer") && !c.isVirtual())
                c.defaultValue = c.cell == 0 ? 1.0 : 0.0;
            if (a == QLatin1String("ColorAdd_R") || a == QLatin1String("ColorAdd_G") || a == QLatin1String("ColorAdd_B"))
                c.defaultValue = 1.0;   // open white from RGB
            else if (a.startsWith(QLatin1String("ColorAdd_")) || a.startsWith(QLatin1String("ColorSub_")))
                c.defaultValue = 0.0;
            else if (c.isAngle())
                c.defaultValue = 0.0;   // the middle of its range, in degrees
            else if ((a == QLatin1String("Color1") || a == QLatin1String("Color2") || a == QLatin1String("Gobo1")
                      || a == QLatin1String("Gobo2") || a == QLatin1String("Shutter"))
                     && openSet(c, &v))
                c.defaultValue = v;
            if (a == QLatin1String("Shutter"))
                c.highlightValue = c.defaultValue;
        }
    }
}

QJsonObject FixtureType::toJson() const
{
    QJsonArray jm;
    for (const FixtureMode &m : modes) {
        QJsonArray jc;
        for (const ChannelDef &c : m.channels) {
            QJsonObject o{{"attribute", c.attribute}, {"name", c.name}, {"coarse", c.coarse},
                          {"default", c.defaultValue}};
            if (c.hasHighlight)
                o["highlight"] = c.highlightValue;
            if (c.snap)
                o["snap"] = true;
            if (c.dmxFrom != 0.0 || c.dmxTo != 1.0)
                o["dmx"] = QJsonArray{c.dmxFrom, c.dmxTo};
            if (c.fine >= 0)
                o["fine"] = c.fine;
            if (c.cell > 0)
                o["cell"] = c.cell;
            if (c.sub > 0)
                o["sub"] = c.sub;
            if (c.physFrom != c.physTo)
                o["physical"] = QJsonArray{c.physFrom, c.physTo};
            if (!c.sets.isEmpty()) {
                QJsonArray js;
                for (const ChannelSet &s : c.sets) {
                    QJsonObject so{{"from", s.from}, {"to", s.to}, {"name", s.name}};
                    if (!s.media.isEmpty())
                        so["media"] = s.media;
                    js.append(so);
                }
                o["sets"] = js;
            }
            jc.append(o);
        }
        QJsonObject mo{{"name", m.name}, {"footprint", m.footprint}, {"channels", jc}};
        if (m.noAutoDimmer)
            mo["noVirtualDimmer"] = true;
        jm.append(mo);
    }
    QJsonObject o{{"id", id}, {"manufacturer", manufacturer}, {"name", name},
                  {"shortName", shortName}, {"modes", jm}, {"source", source}, {"openDefaults", openDefaults},
                  {"angles", true}, {"highlights", true}, {"snaps", true}};
    if (!media.isEmpty()) {
        QJsonObject jmedia;
        for (auto it = media.begin(); it != media.end(); ++it)
            jmedia[it.key()] = QString::fromLatin1(it.value().toBase64());
        o["media"] = jmedia;
    }
    return o;
}

FixtureType FixtureType::fromJson(const QJsonObject &o)
{
    FixtureType t;
    t.id = o["id"].toString();
    t.manufacturer = o["manufacturer"].toString();
    t.name = o["name"].toString();
    t.shortName = o["shortName"].toString(t.name);
    for (const QJsonValue &mv : o["modes"].toArray()) {
        QJsonObject mo = mv.toObject();
        FixtureMode m;
        m.name = mo["name"].toString();
        m.footprint = mo["footprint"].toInt();
        m.noAutoDimmer = mo["noVirtualDimmer"].toBool();
        for (const QJsonValue &cv : mo["channels"].toArray()) {
            QJsonObject co = cv.toObject();
            ChannelDef c;
            c.attribute = co["attribute"].toString();
            c.name = co["name"].toString(c.attribute);
            c.coarse = co["coarse"].toInt();
            c.fine = co["fine"].toInt(-1);
            c.defaultValue = co["default"].toDouble();
            c.hasHighlight = co.contains("highlight");   // before co["highlight"], which would add it
            c.highlightValue = co.value("highlight").toDouble(1.0);
            c.snap = co["snap"].toBool(false);
            if (const QJsonArray dmx = co["dmx"].toArray(); dmx.size() == 2) {
                c.dmxFrom = std::clamp(dmx[0].toDouble(), 0.0, 1.0);
                c.dmxTo = std::clamp(dmx[1].toDouble(), 0.0, 1.0);
            }
            c.cell = std::max(0, co["cell"].toInt());
            c.sub = c.cell > 0 ? std::max(0, co["sub"].toInt()) : 0;
            const QJsonArray phys = co["physical"].toArray();
            if (phys.size() == 2) {
                c.physFrom = phys[0].toDouble();
                c.physTo = phys[1].toDouble();
            }
            for (const QJsonValue &sv : co["sets"].toArray()) {
                QJsonObject so = sv.toObject();
                c.sets.append({so["from"].toDouble(), so["to"].toDouble(), so["name"].toString(), so["media"].toString()});
            }
            m.channels.append(c);
        }
        t.modes.append(m);
    }
    t.source = o["source"].toString();
    t.openDefaults = o["openDefaults"].toBool();
    t.angles = o["angles"].toBool(false);   // files from before degrees: pan and tilt as 0..1
    t.highlightsExplicit = o["highlights"].toBool(false);
    t.snapsExplicit = o["snaps"].toBool(false);
    const QJsonObject jmedia = o["media"].toObject();
    for (auto it = jmedia.begin(); it != jmedia.end(); ++it)
        t.media.insert(it.key(), QByteArray::fromBase64(it.value().toString().toLatin1()));
    t.finalize();
    return t;
}

// ---------------------------------------------------------------------------
// Open Fixture Library import
// ---------------------------------------------------------------------------

namespace {

using import::attributeSafeName;
using import::colorNameAttribute;

// Parses OFL values like 128, "50%", or "128" into a normalized value.
double parseOflValue(const QJsonValue &v, double maxRaw, double fallback)
{
    if (v.isDouble())
        return std::clamp(v.toDouble() / maxRaw, 0.0, 1.0);
    if (v.isString()) {
        QString s = v.toString().trimmed();
        if (s.endsWith('%'))
            return std::clamp(s.chopped(1).toDouble() / 100.0, 0.0, 1.0);
        bool ok = false;
        double d = s.toDouble(&ok);
        if (ok)
            return std::clamp(d / maxRaw, 0.0, 1.0);
    }
    return fallback;
}

struct CapInfo {
    QString attribute;
    double width = 0;
};

QString wheelSlotName(const QJsonObject &wheels, const QString &wheel, double slotNumber)
{
    QJsonArray wheelSlots = wheels[wheel].toObject()["slots"].toArray();
    int idx = int(std::floor(slotNumber)) - 1;
    if (idx < 0 || idx >= wheelSlots.size())
        return QString();
    QJsonObject slot = wheelSlots[idx].toObject();
    QString n = slot["name"].toString();
    if (n.isEmpty())
        n = slot["type"].toString();
    if (slotNumber != std::floor(slotNumber)) {
        QString next = idx + 1 < wheelSlots.size() ? wheelSlots[idx + 1].toObject()["name"].toString() : QString();
        n = next.isEmpty() ? n + QStringLiteral(" split") : n + QStringLiteral(" / ") + next;
    }
    return n;
}

// Chooses an attribute for one OFL capability. `wheelCounts` tracks how many
// color/gobo wheels we've already seen so the second one becomes Color2/Gobo2.
QString attributeForCapability(const QJsonObject &cap, const QString &channelName)
{
    const QString type = cap["type"].toString();
    QString wheel = cap["wheel"].toString(channelName);
    const bool colorWheel = wheel.contains("Color", Qt::CaseInsensitive);
    const bool goboWheel = wheel.contains("Gobo", Qt::CaseInsensitive);
    const QString wheelIdx = wheel.contains('2') ? QStringLiteral("2") : QStringLiteral("1");

    if (type == "Intensity")
        return "Dimmer";
    if (type == "ColorIntensity")
        return colorNameAttribute(cap["color"].toString());
    const bool endless = channelName.contains(QLatin1String("continuous"), Qt::CaseInsensitive)
                         || channelName.contains(QLatin1String("infinite"), Qt::CaseInsensitive)
                         || channelName.contains(QLatin1String("endless"), Qt::CaseInsensitive);
    const QString second = channelName.contains('2') ? QStringLiteral("2") : QStringLiteral("1");
    if (type == "Pan" || type == "PanContinuous")
        return endless ? "PanRotate" : "Pan";
    if (type == "Tilt" || type == "TiltContinuous")
        return endless ? "TiltRotate" : "Tilt";
    if (type == "PanTiltSpeed")
        return "PTSpeed";
    if (type == "WheelSlot" || type == "WheelShake") {
        if (colorWheel) return "Color" + wheelIdx;
        if (goboWheel) return "Gobo" + wheelIdx;
        return "AnimationWheel";
    }
    if (type == "WheelSlotRotation" || type == "WheelRotation") {
        if (colorWheel) return "Color" + wheelIdx;   // rotating color wheel: keep together
        if (goboWheel) return "Gobo" + wheelIdx + "Pos";
        return "AnimationWheelPos";
    }
    if (type == "ColorPreset")
        return "ColorMacro";
    if (type == "ColorTemperature")
        return "CTO";
    if (type == "ShutterStrobe" || type == "StrobeSpeed" || type == "StrobeDuration")
        return "Shutter";
    if (type == "Prism")
        return "Prism" + second;
    if (type == "PrismRotation")
        return "Prism" + second + "Pos";
    if (type == "Iris" || type == "IrisEffect")
        return "Iris";
    if (type == "Frost" || type == "FrostEffect")
        return "Frost" + second;
    if (type == "BladeInsertion" || type == "BladeRotation") {
        static const QRegularExpression digit(QStringLiteral("([1-4])"));
        const QRegularExpressionMatch m = digit.match(channelName);
        const QString n = m.hasMatch() ? m.captured(1) : QStringLiteral("1");
        return "Blade" + n + (type == "BladeInsertion" ? "A" : "Rot");
    }
    if (type == "BladeSystemRotation")
        return "BladeSystemRot";
    if (type == "Zoom")
        return "Zoom";
    if (type == "Focus")
        return "Focus1";
    if (type == "Effect" || type == "EffectSpeed" || type == "EffectDuration" || type == "EffectParameter")
        return "Effects";
    if (type == "NoFunction")
        return QString();
    return "Ctl_" + attributeSafeName(channelName);
}

QString setLabel(const QJsonObject &cap, const QJsonObject &wheels, const QString &channelName)
{
    if (cap.contains("comment"))
        return cap["comment"].toString();
    const QString type = cap["type"].toString();
    if (type == "WheelSlot" && cap.contains("slotNumber")) {
        QString n = wheelSlotName(wheels, cap["wheel"].toString(channelName), cap["slotNumber"].toDouble());
        if (!n.isEmpty())
            return n;
    }
    if (type == "ShutterStrobe")
        return cap["shutterEffect"].toString(type);
    if (type == "ColorPreset" && cap.contains("colors"))
        return cap["colors"].toArray().isEmpty() ? type : cap["colors"].toArray()[0].toString();
    return type;
}


// ---- OFL matrices --------------------------------------------------------------------------
// Multi-cell fixtures describe per-pixel channels once as "templateChannels" ("Red $pixelKey")
// and insert them into modes with {"insert": "matrixChannels", "repeatFor": ...}. We expand
// them into ordinary channels, each belonging to its pixel's cell (numbered in matrix order).

// Pixel keys of a matrix in X-fastest order, with their coordinates.
struct OflPixel {
    QString key;
    int x = 0, y = 0, z = 0;
};

QVector<OflPixel> oflPixels(const QJsonObject &matrix)
{
    QVector<OflPixel> out;
    if (matrix.contains("pixelKeys")) {
        const QJsonArray zs = matrix["pixelKeys"].toArray();
        for (int z = 0; z < zs.size(); ++z) {
            const QJsonArray ys = zs[z].toArray();
            for (int y = 0; y < ys.size(); ++y) {
                const QJsonArray xs = ys[y].toArray();
                for (int x = 0; x < xs.size(); ++x)
                    if (xs[x].isString())
                        out.append({xs[x].toString(), x, y, z});
            }
        }
        return out;
    }
    const QJsonArray count = matrix["pixelCount"].toArray();
    const int nx = std::max(1, count.size() > 0 ? count[0].toInt() : 1);
    const int ny = std::max(1, count.size() > 1 ? count[1].toInt() : 1);
    const int nz = std::max(1, count.size() > 2 ? count[2].toInt() : 1);
    const int dims = (nx > 1) + (ny > 1) + (nz > 1);
    for (int z = 0; z < nz; ++z)
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                QString key;
                if (dims <= 1)
                    key = QString::number(x + y + z + 1);
                else if (nz == 1)
                    key = QStringLiteral("(%1, %2)").arg(x + 1).arg(y + 1);
                else
                    key = QStringLiteral("(%1, %2, %3)").arg(x + 1).arg(y + 1).arg(z + 1);
                out.append({key, x, y, z});
            }
    return out;
}

// Keys for one "repeatFor": an explicit list, or every pixel in an "eachPixel..." order.
QStringList oflRepeatKeys(const QJsonValue &repeatFor, const QVector<OflPixel> &pixels)
{
    QStringList out;
    if (repeatFor.isArray()) {
        for (const QJsonValue &v : repeatFor.toArray())
            out << v.toString();
        return out;
    }
    const QString order = repeatFor.toString();
    QVector<OflPixel> sorted = pixels;
    if (order == "eachPixelABC") {
        std::stable_sort(sorted.begin(), sorted.end(), [](const OflPixel &a, const OflPixel &b) {
            bool okA = false, okB = false;
            const int na = a.key.toInt(&okA), nb = b.key.toInt(&okB);
            if (okA && okB)
                return na < nb;
            return QString::compare(a.key, b.key, Qt::CaseInsensitive) < 0;
        });
    } else if (order.startsWith("eachPixel") && order.size() == 12) {
        // The first axis letter changes fastest.
        const QString axes = order.mid(9);
        auto coord = [](const OflPixel &p, QChar c) { return c == 'X' ? p.x : c == 'Y' ? p.y : p.z; };
        std::stable_sort(sorted.begin(), sorted.end(), [&](const OflPixel &a, const OflPixel &b) {
            for (int i = 2; i >= 0; --i)
                if (coord(a, axes[i]) != coord(b, axes[i]))
                    return coord(a, axes[i]) < coord(b, axes[i]);
            return false;
        });
    }
    for (const OflPixel &p : sorted)
        out << p.key;
    return out;
}

// "$pixelKey" in any string of a template replaced by the key. Parts without it are kept as
// they are (most of a channel's capabilities), rather than copied.
bool replacePixelKeyIn(QJsonValue &v, const QString &key)
{
    static const QLatin1String token("$pixelKey");
    if (v.isString()) {
        QString str = v.toString();
        if (!str.contains(token))
            return false;
        v = str.replace(token, key);
        return true;
    }
    if (v.isArray()) {
        QJsonArray a = v.toArray();
        bool changed = false;
        for (qsizetype i = 0; i < a.size(); ++i) {
            QJsonValue x = a.at(i);
            if (replacePixelKeyIn(x, key)) {
                a.replace(i, x);
                changed = true;
            }
        }
        if (changed)
            v = a;
        return changed;
    }
    if (v.isObject()) {
        QJsonObject o = v.toObject();
        bool changed = false;
        for (auto it = o.begin(); it != o.end(); ++it) {
            QJsonValue x = it.value();
            if (replacePixelKeyIn(x, key)) {
                it.value() = x;
                changed = true;
            }
        }
        if (changed)
            v = o;
        return changed;
    }
    return false;
}

QJsonValue replacePixelKey(const QJsonValue &v, const QString &key)
{
    QJsonValue out = v;
    replacePixelKeyIn(out, key);
    return out;
}

} // namespace

bool FixtureLibrary::parseOfl(const QJsonObject &ofl, const QString &manufacturerKey, const QString &fixtureKey,
                              FixtureType *out, QString *error)
{
    if ((!ofl.contains("availableChannels") && !ofl.contains("templateChannels")) || !ofl.contains("modes")) {
        if (error)
            *error = QStringLiteral("Not an Open Fixture Library file (missing availableChannels/modes)");
        return false;
    }

    FixtureType t;
    t.angles = false;   // pan and tilt defaults come as 0..1 of the DMX range
    t.name = ofl["name"].toString(fixtureKey);
    t.shortName = ofl["shortName"].toString(t.name);
    t.manufacturer = ofl["manufacturer"].toString(manufacturerKey);   // our own sample files carry this
    if (t.manufacturer.isEmpty())
        t.manufacturer = QStringLiteral("Generic");
    t.id = (manufacturerKey.isEmpty() ? attributeSafeName(t.manufacturer).toLower() : manufacturerKey) + '/'
           + (fixtureKey.isEmpty() ? attributeSafeName(t.name).toLower() : fixtureKey);

    QJsonObject channels = ofl["availableChannels"].toObject();
    const QJsonObject wheels = ofl["wheels"].toObject();
    // Lumora's own addition to the format: wheel slot pictures (gobos), PNG or SVG, base64 in a
    // top-level "images" object and named by a slot's "image".
    const QJsonObject images = ofl["images"].toObject();
    for (auto it = images.begin(); it != images.end(); ++it)
        t.media.insert(it.key(), QByteArray::fromBase64(it.value().toString().toLatin1()));

    // Expand matrix modes into plain channel lists, and template channels into real ones.
    const QVector<OflPixel> pixels = oflPixels(ofl["matrix"].toObject());
    QJsonArray modesIn = ofl["modes"].toArray();
    QSet<QString> pixelKeys;
    for (const OflPixel &p : pixels)
        pixelKeys.insert(p.key);
    for (const QString &g : ofl["matrix"].toObject()["pixelGroups"].toObject().keys())
        pixelKeys.insert(g);
    QVector<QHash<QString, QString>> channelPixel(modesIn.size());   // per mode: channel name -> pixel key
    for (int mi = 0; mi < modesIn.size(); ++mi) {
        QJsonObject mo = modesIn[mi].toObject();
        QJsonArray flat;
        for (const QJsonValue &entry : mo["channels"].toArray()) {
            const QJsonObject ins = entry.toObject();
            if (!entry.isObject() || ins["insert"].toString() != QLatin1String("matrixChannels")) {
                flat.append(entry);
                continue;
            }
            const QStringList keys = oflRepeatKeys(ins["repeatFor"], pixels);
            const QJsonArray templ = ins["templateChannels"].toArray();
            const bool perPixel = ins["channelOrder"].toString() != QLatin1String("perChannel");
            for (const QString &k : keys)
                pixelKeys.insert(k);
            auto add = [&](const QJsonValue &tc, const QString &k) {
                const QJsonValue v = replacePixelKey(tc, k);
                if (v.isString())
                    channelPixel[mi].insert(v.toString(), k);
                flat.append(v);
            };
            if (perPixel) {
                for (const QString &k : keys)
                    for (const QJsonValue &tc : templ)
                        add(tc, k);
            } else {
                for (const QJsonValue &tc : templ)
                    for (const QString &k : keys)
                        add(tc, k);
            }
        }
        mo["channels"] = flat;
        modesIn[mi] = mo;
    }
    // Template channels become one channel per pixel key ("Red $pixelKey" -> "Red 1"). Only the
    // names are made here; a channel is read when a mode uses it (big pixel fixtures name many
    // more than their modes use).
    const QJsonObject templates = ofl["templateChannels"].toObject();
    QHash<QString, QPair<QString, QString>> templated;   // channel name -> (template, pixel key)
    for (auto it = templates.begin(); it != templates.end(); ++it)
        for (const QString &k : pixelKeys) {
            const QString name = QString(it.key()).replace(QLatin1String("$pixelKey"), k);
            if (!channels.contains(name) && !templated.contains(name))
                templated.insert(name, {it.key(), k});
        }

    struct Parsed {
        ChannelDef def;
        bool hasFine = false;
    };
    QHash<QString, Parsed> parsed;
    QHash<QString, QString> fineAlias;   // "Pan fine" -> "Pan"
    for (auto it = channels.begin(); it != channels.end(); ++it)
        for (const QJsonValue &f : it.value().toObject()["fineChannelAliases"].toArray())
            fineAlias.insert(f.toString(), it.key());
    for (auto it = templated.begin(); it != templated.end(); ++it)
        for (const QJsonValue &f : templates[it.value().first].toObject()["fineChannelAliases"].toArray())
            fineAlias.insert(QString(f.toString()).replace(QLatin1String("$pixelKey"), it.value().second), it.key());

    // One channel, read once: its attribute (the capability covering the widest DMX range),
    // physical range, named ranges with their pictures, default and highlight.
    auto parseChannel = [&](const QString &chName) -> const Parsed * {
        if (auto found = parsed.constFind(chName); found != parsed.constEnd())
            return &found.value();
        QJsonObject ch;
        if (channels.contains(chName)) {
            ch = channels[chName].toObject();
        } else if (auto tp = templated.constFind(chName); tp != templated.constEnd()) {
            ch = replacePixelKey(templates[tp->first], tp->second).toObject();
        } else {
            return nullptr;
        }
        const QJsonArray fines = ch["fineChannelAliases"].toArray();
        const double maxRaw = fines.isEmpty() ? 255.0 : 65535.0;

        QVector<QJsonObject> caps;
        if (ch.contains("capability"))
            caps.append(ch["capability"].toObject());
        for (const QJsonValue &c : ch["capabilities"].toArray())
            caps.append(c.toObject());

        // Pick the attribute covering the widest DMX range.
        QHash<QString, double> widths;
        ChannelDef def;
        def.name = chName;
        for (const QJsonObject &cap : caps) {
            QString attr = attributeForCapability(cap, chName);
            double from = 0, to = 1;
            if (cap.contains("dmxRange")) {
                QJsonArray r = cap["dmxRange"].toArray();
                double rangeMax = r[1].toDouble() > 255 ? 65535.0 : 255.0;
                from = r[0].toDouble() / rangeMax;
                to = r[1].toDouble() / rangeMax;
            }
            if (!attr.isEmpty())
                widths[attr] += (to - from) + 1e-3;
            // Pan and tilt: the angles the range covers ("0deg" to "540deg").
            if (isAngle(attr) && cap.contains("angleStart") && cap.contains("angleEnd")) {
                const double a = cap["angleStart"].toString().remove("deg").toDouble();
                const double b = cap["angleEnd"].toString().remove("deg").toDouble();
                def.setPhysicalRange(a, b);
            }
            if (caps.size() > 1) {
                QString media;
                const QString type = cap["type"].toString();
                if ((type == "WheelSlot" || type == "WheelShake") && cap.contains("slotNumber")) {
                    const QJsonArray ws = wheels[cap["wheel"].toString(chName)].toObject()["slots"].toArray();
                    const int idx = int(std::floor(cap["slotNumber"].toDouble())) - 1;
                    if (idx >= 0 && idx < ws.size() && cap["slotNumber"].toDouble() == std::floor(cap["slotNumber"].toDouble())) {
                        const QString key = ws[idx].toObject()["image"].toString();
                        if (t.media.contains(key))
                            media = key;
                    }
                }
                def.sets.append({from, to, setLabel(cap, wheels, chName), media});
            }
        }
        QString best;
        double bestW = -1;
        for (auto w = widths.begin(); w != widths.end(); ++w)
            if (w.value() > bestW) {
                bestW = w.value();
                best = w.key();
            }
        def.attribute = best.isEmpty() ? "Ctl_" + attributeSafeName(chName) : best;
        def.defaultValue = parseOflValue(ch["defaultValue"], maxRaw, 0.0);
        def.highlightValue = parseOflValue(ch["highlightValue"], maxRaw,
                                           (isIntensity(def.attribute) || isColorAdd(def.attribute)) ? 1.0 : def.defaultValue);
        // Position defaults to center if the file doesn't say otherwise.
        if (!ch.contains("defaultValue") && (def.attribute == "Pan" || def.attribute == "Tilt"))
            def.defaultValue = 0.5;
        return &parsed.insert(chName, {def, !fines.isEmpty()}).value();
    };

    QHash<QString, int> pixelOrder;   // matrix order of real pixels (not pixel groups)
    for (int i = 0; i < pixels.size(); ++i)
        pixelOrder.insert(pixels[i].key, i);
    for (int mi = 0; mi < modesIn.size(); ++mi) {
        QJsonObject mo = modesIn[mi].toObject();
        FixtureMode mode;
        mode.name = mo["name"].toString();
        const QJsonArray list = mo["channels"].toArray();
        bool supported = true;
        // Cells: the pixels this mode uses, in matrix order. One pixel (or only groups) isn't a
        // pixel fixture; its channels belong to the fixture.
        QStringList used_pixels;
        for (const QString &k : channelPixel[mi])
            if (pixelOrder.contains(k) && !used_pixels.contains(k))
                used_pixels << k;
        std::sort(used_pixels.begin(), used_pixels.end(),
                  [&](const QString &a, const QString &b) { return pixelOrder.value(a) < pixelOrder.value(b); });
        if (used_pixels.size() < 2)
            used_pixels.clear();
        QHash<int, QSet<QString>> used;   // attributes taken, per cell
        for (int offset = 0; offset < list.size(); ++offset) {
            const QJsonValue entry = list[offset];
            if (entry.isNull())
                continue;   // unused slot keeps its offset
            if (!entry.isString()) {
                supported = false;   // unknown insert kind
                break;
            }
            const QString name = entry.toString();
            if (fineAlias.contains(name)) {
                const QString parent = fineAlias.value(name);
                for (ChannelDef &c : mode.channels)
                    if (c.name == parent)
                        c.fine = offset;
                continue;
            }
            const Parsed *pc = parseChannel(name);
            if (!pc)
                continue;
            ChannelDef def = pc->def;
            def.coarse = offset;
            def.cell = int(used_pixels.indexOf(channelPixel[mi].value(name))) + 1;   // 0: the fixture's own
            QString attr = def.attribute;
            for (int n = 2; used[def.cell].contains(attr); ++n)
                attr = def.attribute + QString::number(n);
            def.attribute = attr;
            used[def.cell].insert(attr);
            mode.channels.append(def);
        }
        mode.footprint = list.size();
        if (supported && !mode.channels.isEmpty())
            t.modes.append(mode);
    }

    if (t.modes.isEmpty()) {
        if (error)
            *error = QStringLiteral("No supported modes in fixture '%1'").arg(t.name);
        return false;
    }
    t.source = QStringLiteral("OFL");
    t.finalize();
    *out = t;
    return true;
}

bool FixtureLibrary::loadOflFile(const QString &path, QString *error, QString *typeId)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull()) {
        if (error)
            *error = pe.errorString();
        return false;
    }
    QFileInfo fi(path);
    FixtureType t;
    const QJsonObject root = doc.object();
    if (root.contains("redirectTo")) {
        // OFL keeps renamed fixtures as a stub pointing at the new file.
        const QString target = root["redirectTo"].toString();
        const QString redirected = QDir(fi.dir().filePath("..")).filePath(target + ".json");
        if (QFileInfo::exists(redirected) && redirected != path)
            return loadOflFile(redirected, error, typeId);
        if (error)
            *error = QStringLiteral("'%1' was renamed to '%2'").arg(fi.completeBaseName(), target);
        return false;
    }
    if (!parseOfl(root, fi.dir().dirName(), fi.completeBaseName(), &t, error))
        return false;
    // OFL files name their manufacturer only by folder; manufacturers.json one level up has the
    // display name ("chauvet-dj" -> "Chauvet DJ").
    if (!root.contains("manufacturer")) {
        static QHash<QString, QJsonObject> manufacturerFiles;
        const QString mf = QDir(fi.dir().filePath("..")).absoluteFilePath("manufacturers.json");
        if (!manufacturerFiles.contains(mf)) {
            QFile m(mf);
            manufacturerFiles.insert(mf, m.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(m.readAll()).object()
                                                                     : QJsonObject());
        }
        const QString display = manufacturerFiles.value(mf)[fi.dir().dirName()].toObject()["name"].toString();
        if (!display.isEmpty())
            t.manufacturer = display;
    }
    if (typeId)
        *typeId = t.id;
    add(t);
    return true;
}

bool FixtureLibrary::loadGdtfFile(const QString &path, QString *error, QString *typeId)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    FixtureType t;
    if (!parseGdtf(f.readAll(), &t, error))
        return false;
    if (typeId)
        *typeId = t.id;
    add(t);
    return true;
}

bool FixtureLibrary::loadFile(const QString &path, QString *error, QString *typeId)
{
    if (path.endsWith(QLatin1String(".gdtf"), Qt::CaseInsensitive))
        return loadGdtfFile(path, error, typeId);
    if (path.endsWith(QLatin1String(".qxf"), Qt::CaseInsensitive))
        return loadQxfFile(path, error, typeId);
    if (path.endsWith(QLatin1String(".lft"), Qt::CaseInsensitive))
        return loadLftFile(path, error, typeId);
    return loadOflFile(path, error, typeId);
}

int FixtureLibrary::loadDirectory(const QString &dir, const std::function<void(int, int, const QString &)> &progress)
{
    QStringList files, later;   // edited types (.lft) load last so they replace the originals
    QDirIterator it(dir, {"*.json", "*.gdtf", "*.qxf", "*.lft"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString fn = QFileInfo(path).fileName();
        if (fn == "manufacturers.json" || fn == "register.json" || fn == "index.json")
            continue;
        (fn.endsWith(QLatin1String(".lft"), Qt::CaseInsensitive) ? later : files) << path;
    }
    files += later;
    int n = 0;
    for (int i = 0; i < files.size(); ++i) {
        if (progress)
            progress(i, int(files.size()), files[i]);
        if (loadFile(files[i]))
            ++n;
    }
    if (progress)
        progress(int(files.size()), int(files.size()), QString());
    return n;
}

QString FixtureLibrary::makeTypeId(const QString &manufacturer, const QString &name)
{
    auto part = [](const QString &s) {
        static const QRegularExpression runs(QStringLiteral("[^a-z0-9]+")), ends(QStringLiteral("^-+|-+$"));
        QString out = s.toLower();
        out.replace(runs, QStringLiteral("-"));
        out.remove(ends);
        return out.isEmpty() ? QStringLiteral("fixture") : out;
    };
    return part(manufacturer) + '/' + part(name);
}

void FixtureLibrary::add(const FixtureType &t)
{
    FixtureType copy = t;
    copy.finalize();   // pan and tilt in degrees before the open defaults
    copy.applyOpenDefaults();
    copy.finalize();   // the cells' parts carry the defaults too
    (m_importing ? m_show : m_types).insert(copy.id, copy);
}

void FixtureLibrary::addToShow(const FixtureType &t)
{
    FixtureType copy = t;
    copy.finalize();
    copy.applyOpenDefaults();
    copy.finalize();
    m_show.insert(copy.id, copy);
}

bool FixtureLibrary::importFile(const QString &path, QString *error, QString *typeId)
{
    m_importing = true;
    const bool ok = loadFile(path, error, typeId);
    m_importing = false;
    return ok;
}

QList<FixtureType> FixtureLibrary::all() const
{
    QMap<QString, FixtureType> merged = m_types;
    for (auto it = m_show.begin(); it != m_show.end(); ++it)
        merged.insert(it.key(), it.value());
    return merged.values();
}

QStringList FixtureLibrary::ids() const
{
    QStringList out = m_types.keys();
    for (const QString &id : m_show.keys())
        if (!m_types.contains(id))
            out << id;
    return out;
}

bool FixtureLibrary::loadLftFile(const QString &path, QString *error, QString *typeId)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o["id"].toString().isEmpty() || o["modes"].toArray().isEmpty()) {
        if (error)
            *error = QStringLiteral("Not a Lumora fixture file");
        return false;
    }
    FixtureType t = FixtureType::fromJson(o);
    if (typeId)
        *typeId = t.id;
    add(t);
    return true;
}

const FixtureType *FixtureLibrary::find(const QString &id) const
{
    auto s = m_show.find(id);
    if (s != m_show.end())
        return &s.value();
    auto it = m_types.find(id);
    return it == m_types.end() ? nullptr : &it.value();
}

} // namespace lumora
