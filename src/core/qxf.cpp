// SPDX-License-Identifier: GPL-3.0-or-later
//
// QLC+ fixture definitions (.qxf).
//
// A .qxf is a small XML file:
//   <Manufacturer>, <Model>, <Type>
//   <Channel Name="..." Preset="PositionPan"/>                 (a channel described by a preset), or
//   <Channel Name="..."><Group Byte="0|1">Colour</Group>       (a channel described by its group,
//       <Colour>Red</Colour>                                     Byte 1 meaning it is a fine channel)
//       <Capability Min Max Preset Res1>Name</Capability>...</Channel>
//   <Mode Name="..."><Channel Number="n">Channel name</Channel>...<Head>...</Head></Mode>
//
// Capabilities become named channel sets. Res1 carries either a color ("#rrggbb") or a gobo image
// path relative to QLC+'s resources/gobos folder ("Robe/gobo00010.svg"); we load those images
// through a resolver so the library browser can supply the files it downloaded.
#include "fixturetype.h"
#include "importhelpers.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

#include <algorithm>

namespace lumora {

namespace {

struct QxfCap {
    int min = 0, max = 255;
    QString preset, res1, name;
};

struct QxfChannel {
    QString name;
    QString preset;    // channel preset, e.g. "IntensityRed"
    QString group;     // e.g. "Colour"
    int byte = 0;      // 1 = fine
    QString colour;    // for Intensity groups: "Red", "Green", ...
    QVector<QxfCap> caps;
};

QString sanitizeName(const QString &s) { return import::attributeSafeName(s); }
QString colourAttr(const QString &c) { return import::colorNameAttribute(c); }

// Attribute for a channel described by a QLC+ channel preset. Sets *fine for "...Fine" presets.
QString attrFromPreset(const QString &preset, bool *fine)
{
    QString p = preset;
    *fine = p.endsWith(QLatin1String("Fine"));
    if (*fine)
        p.chop(4);
    if (p == "IntensityMasterDimmer" || p == "IntensityDimmer")
        return "Dimmer";
    if (p.startsWith("Intensity")) {
        const QString a = colourAttr(p.mid(9));
        if (!a.isEmpty())
            return a;
        return "Ctl_" + p.mid(9);   // Hue/Saturation/Lightness/Value
    }
    if (p == "PositionPan" || p == "PositionXAxis")
        return "Pan";
    if (p == "PositionTilt" || p == "PositionYAxis")
        return "Tilt";
    if (p.startsWith("SpeedPan") || p.startsWith("SpeedTilt"))
        return "PTSpeed";
    if (p == "ColorMacro" || p == "ColorWheel")
        return "Color1";
    if (p == "ColorCTOMixer" || p == "ColorCTCMixer")
        return "CTO";
    if (p == "ColorCTBMixer")
        return "CTB";
    if (p == "GoboWheel")
        return "Gobo1";
    if (p == "GoboIndex")
        return "Gobo1Pos";
    if (p.startsWith("ShutterStrobe"))
        return "Shutter";
    if (p.startsWith("ShutterIris"))
        return "Iris";
    if (p.startsWith("BeamFocus"))
        return "Focus1";
    if (p.startsWith("BeamZoom"))
        return "Zoom";
    if (p.startsWith("PrismRotation"))
        return "Prism1Pos";
    if (p == "NoFunction")
        return QString();
    return "Ctl_" + p;
}

bool nameHas(const QString &name, const char *re)
{
    return name.contains(QRegularExpression(QString::fromLatin1(re), QRegularExpression::CaseInsensitiveOption));
}

// Attribute for a channel described by its group. `wheels` counts gobo/color wheels seen so far
// in this mode, so a second wheel becomes Gobo2/Color2.
QString attrFromGroup(const QxfChannel &c, QHash<QString, int> &wheels)
{
    const QString &g = c.group;
    const QString &n = c.name;
    if (g == "Intensity") {
        const QString a = colourAttr(c.colour);
        if (!a.isEmpty())
            return a;
        const QString byName = colourAttr(n);
        return byName.isEmpty() ? QStringLiteral("Dimmer") : byName;
    }
    if (g == "Pan")
        return "Pan";
    if (g == "Tilt")
        return "Tilt";
    if (g == "Colour") {
        if (nameHas(n, "\\bcto\\b|temperature|\\bctc\\b"))
            return "CTO";
        if (nameHas(n, "\\bctb\\b"))
            return "CTB";
        const QString cmy = colourAttr(n);
        if (cmy.startsWith("ColorSub"))
            return cmy;
        if (nameHas(n, "macro"))
            return "ColorMacro";
        if (nameHas(n, "speed|rotation|scroll"))
            return "Color1Pos";
        const int w = ++wheels["Color"];
        return w > 1 ? "Color2" : "Color1";
    }
    if (g == "Gobo") {
        const int cur = std::max(1, wheels.value("Gobo"));
        if (nameHas(n, "index|rotation|rot\\.|spin|indexing") && !nameHas(n, "rotating gobo|gobo wheel|select"))
            return QStringLiteral("Gobo%1Pos").arg(cur);
        const int w = ++wheels["Gobo"];
        return QStringLiteral("Gobo%1").arg(std::min(w, 2));
    }
    if (g == "Shutter")
        return nameHas(n, "iris") ? QStringLiteral("Iris") : QStringLiteral("Shutter");
    if (g == "Prism")
        return nameHas(n, "rot|index") ? QStringLiteral("Prism1Pos") : QStringLiteral("Prism1");
    if (g == "Beam") {
        if (nameHas(n, "zoom")) return "Zoom";
        if (nameHas(n, "focus")) return "Focus1";
        if (nameHas(n, "iris")) return "Iris";
        if (nameHas(n, "frost|diffus")) return "Frost1";
        return "Ctl_" + sanitizeName(n);
    }
    if (g == "Speed")
        return nameHas(n, "pan|tilt|p/t|p&t|movement") ? QStringLiteral("PTSpeed") : "Ctl_" + sanitizeName(n);
    if (g == "Effect")
        return "Effects";
    if (g == "Nothing")
        return QString();
    return "Ctl_" + sanitizeName(n);   // Maintenance and anything new
}

} // namespace

bool FixtureLibrary::parseQxf(const QByteArray &xml, FixtureType *out, QString *error,
                              const std::function<QByteArray(const QString &)> &mediaResolver)
{
    QXmlStreamReader r(xml);
    FixtureType t;
    t.angles = false;   // pan and tilt defaults come as 0..1 of the DMX range
    QVector<QxfChannel> channels;
    struct ModeEntry { int number; QString channel; };
    struct ModeDef { QString name; QVector<ModeEntry> entries; QHash<int, int> head; int heads = 0; };   // channel -> head (1…)
    QVector<ModeDef> modes;
    bool isQxf = false;
    double panMax = 0, tiltMax = 0;

    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement())
            continue;
        const auto name = r.name();
        if (name == QLatin1String("FixtureDefinition")) {
            isQxf = true;
        } else if (name == QLatin1String("Manufacturer") && t.manufacturer.isEmpty()) {
            t.manufacturer = r.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
        } else if (name == QLatin1String("Model") && t.name.isEmpty()) {
            t.name = r.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
        } else if (name == QLatin1String("Channel") && r.attributes().hasAttribute("Name")) {
            QxfChannel c;
            c.name = r.attributes().value("Name").toString();
            c.preset = r.attributes().value("Preset").toString();
            while (!(r.isEndElement() && r.name() == QLatin1String("Channel")) && !r.atEnd()) {
                r.readNext();
                if (!r.isStartElement())
                    continue;
                if (r.name() == QLatin1String("Group")) {
                    c.byte = r.attributes().value("Byte").toInt();
                    c.group = r.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
                } else if (r.name() == QLatin1String("Colour")) {
                    c.colour = r.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
                } else if (r.name() == QLatin1String("Capability")) {
                    QxfCap cap;
                    cap.min = r.attributes().value("Min").toInt();
                    cap.max = r.attributes().value("Max").toInt();
                    cap.preset = r.attributes().value("Preset").toString();
                    cap.res1 = r.attributes().value("Res1").toString();
                    if (cap.res1.isEmpty())
                        cap.res1 = r.attributes().value("Res").toString();   // older files
                    cap.name = r.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
                    c.caps.append(cap);
                }
            }
            channels.append(c);
        } else if (name == QLatin1String("Focus") && r.attributes().hasAttribute("PanMax")) {
            // <Physical><Focus PanMax="540" TiltMax="270"/>: the degrees pan and tilt cover.
            if (!panMax)
                panMax = r.attributes().value("PanMax").toDouble();
            if (!tiltMax)
                tiltMax = r.attributes().value("TiltMax").toDouble();
        } else if (name == QLatin1String("Mode")) {
            ModeDef m;
            m.name = r.attributes().value("Name").toString();
            while (!(r.isEndElement() && r.name() == QLatin1String("Mode")) && !r.atEnd()) {
                r.readNext();
                if (r.isStartElement() && r.name() == QLatin1String("Channel") && r.attributes().hasAttribute("Number")) {
                    const int num = r.attributes().value("Number").toInt();
                    m.entries.append({num, r.readElementText(QXmlStreamReader::IncludeChildElements).trimmed()});
                } else if (r.isStartElement() && r.name() == QLatin1String("Head")) {
                    // A head lists its channels; with two or more, each head is a cell.
                    ++m.heads;
                    while (!(r.isEndElement() && r.name() == QLatin1String("Head")) && !r.atEnd()) {
                        r.readNext();
                        if (r.isStartElement() && r.name() == QLatin1String("Channel"))
                            m.head.insert(r.readElementText(QXmlStreamReader::IncludeChildElements).trimmed().toInt(), m.heads);
                    }
                }
            }
            modes.append(m);
        }
    }
    if (r.hasError() || !isQxf) {
        if (error)
            *error = r.hasError() ? QStringLiteral("The QLC+ file couldn't be read (line %1: %2)").arg(r.lineNumber()).arg(r.errorString())
                                  : QStringLiteral("Not a QLC+ fixture definition");
        return false;
    }
    if (t.name.isEmpty() || modes.isEmpty()) {
        if (error)
            *error = QStringLiteral("Fixture definition has no model name or modes");
        return false;
    }
    if (t.manufacturer.isEmpty())
        t.manufacturer = QStringLiteral("Generic");
    t.shortName = t.name;
    t.id = FixtureLibrary::makeTypeId(t.manufacturer, t.name);
    t.source = QStringLiteral("QLC+");

    QHash<QString, const QxfChannel *> byName;
    for (const QxfChannel &c : channels)
        byName.insert(c.name, &c);

    for (const ModeDef &md : modes) {
        FixtureMode mode;
        mode.name = md.name.isEmpty() ? QStringLiteral("Default") : md.name;
        QHash<QString, int> wheels;
        QHash<int, QSet<QString>> usedPerCell;
        int footprint = 0;
        for (const ModeEntry &e : md.entries) {
            footprint = std::max(footprint, e.number + 1);
            const QxfChannel *c = byName.value(e.channel);
            if (!c)
                continue;
            const int cell = md.heads >= 2 ? md.head.value(e.number, 0) : 0;
            QSet<QString> &used = usedPerCell[cell];
            bool fine = false;
            QString attr;
            if (!c->preset.isEmpty()) {
                attr = attrFromPreset(c->preset, &fine);
            } else {
                fine = c->byte == 1;
                QHash<QString, int> probe = wheels;   // fine channels must not count as a new wheel
                attr = attrFromGroup(*c, fine ? probe : wheels);
                if (fine && (attr == "Gobo2" || attr == "Color2") && !used.contains(attr))
                    attr.chop(1), attr += '1';
            }
            if (attr.isEmpty())
                continue;
            if (fine) {
                // Attach to the most recent coarse channel with this attribute.
                bool attached = false;
                for (int i = mode.channels.size() - 1; i >= 0 && !attached; --i) {
                    ChannelDef &cd = mode.channels[i];
                    const bool same = cd.attribute == attr
                                      || (cd.attribute.startsWith(attr) && cd.attribute.mid(attr.size()).toInt() > 1);
                    if (same && cd.fine < 0 && cd.cell == cell) {
                        cd.fine = e.number;
                        attached = true;
                    }
                }
                if (attached)
                    continue;
                attr += QStringLiteral("Fine");   // fine without a coarse: keep it addressable
            }

            ChannelDef def;
            def.name = c->name;
            def.coarse = e.number;
            def.cell = cell;
            QString unique = attr;
            for (int n = 2; used.contains(unique); ++n)
                unique = attr + QString::number(n);
            def.attribute = unique;
            used.insert(unique);
            if (def.attribute == "Pan" || def.attribute == "Tilt")
                def.defaultValue = 0.5;
            def.highlightValue = (isIntensity(def.attribute) || isColorAdd(def.attribute)) ? 1.0 : def.defaultValue;

            if (c->caps.size() > 1) {
                for (const QxfCap &cap : c->caps) {
                    ChannelSet s;
                    s.from = cap.min / 255.0;
                    s.to = cap.max / 255.0;
                    s.name = cap.name;
                    if (!cap.res1.isEmpty() && !cap.res1.startsWith('#') && cap.preset.startsWith("Gobo")) {
                        if (!t.media.contains(cap.res1) && mediaResolver) {
                            const QByteArray bytes = mediaResolver(cap.res1);
                            if (!bytes.isEmpty())
                                t.media.insert(cap.res1, bytes);
                        }
                        if (t.media.contains(cap.res1))
                            s.media = cap.res1;
                    }
                    def.sets.append(s);
                }
                // Shutters rest open, not closed: start on the first "open" range when there is one.
                if (def.attribute == "Shutter")
                    for (const QxfCap &cap : c->caps)
                        if (cap.preset == "ShutterOpen" || cap.name.contains("open", Qt::CaseInsensitive)) {
                            def.defaultValue = (cap.min + cap.max) / 2.0 / 255.0;
                            def.highlightValue = def.defaultValue;
                            break;
                        }
            }
            mode.channels.append(def);
        }
        mode.footprint = footprint;
        if (!mode.channels.isEmpty())
            t.modes.append(mode);
    }
    if (t.modes.isEmpty()) {
        if (error)
            *error = QStringLiteral("No usable modes in '%1'").arg(t.name);
        return false;
    }
    for (FixtureMode &m : t.modes)
        for (ChannelDef &c : m.channels)
            if (c.attribute.startsWith("Pan") && c.isAngle() && panMax > 0)
                c.setPhysicalRange(0, panMax);
            else if (c.attribute.startsWith("Tilt") && c.isAngle() && tiltMax > 0)
                c.setPhysicalRange(0, tiltMax);
    t.finalize();
    *out = t;
    return true;
}

bool FixtureLibrary::loadQxfFile(const QString &path, QString *error, QString *typeId)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    // Gobo images live in a "gobos" folder beside the definition (or one level up, the way
    // QLC+ lays out resources/fixtures and resources/gobos).
    const QDir here = QFileInfo(path).dir();
    const QStringList roots = {here.filePath("gobos"), here.filePath("../gobos"), here.filePath("../../gobos")};
    auto resolver = [&](const QString &rel) -> QByteArray {
        for (const QString &root : roots) {
            QFile img(QDir(root).filePath(rel));
            if (img.open(QIODevice::ReadOnly))
                return img.readAll();
        }
        return {};
    };
    FixtureType t;
    if (!parseQxf(f.readAll(), &t, error, resolver))
        return false;
    if (typeId)
        *typeId = t.id;
    add(t);
    return true;
}

} // namespace lumora
