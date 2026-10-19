// SPDX-License-Identifier: GPL-3.0-or-later
//
// GDTF (General Device Type Format, DIN SPEC 15800) import.
//
// A .gdtf file is a zip archive holding description.xml plus resources such as
// wheel slot images (wheels/<MediaFileName>.png). We read:
//   FixtureType  name, short name, manufacturer
//   Wheels       slot names and images
//   DMXModes     each DMXChannel's offsets (1.1+ "Offset", or 1.0 "Coarse"/"Fine"),
//                its logical channel attribute, default and highlight values,
//                and the ranges of its channel functions and channel sets.
// Pixel and multi-head fixtures become subfixtures: channels of a geometry that
// GeometryReferences repeat are laid out once per reference (at its DMX offset), with the
// geometries inside it (a head's ring and beam) one level down. Channels listed per geometry
// ("Pixel 1", "Pixel 2"… with the same attributes) are grouped by geometry.
// Not yet supported: channels in DMX breaks other than 1, and mode-master dependencies.

#include "fixturetype.h"
#include "zipreader.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

#include <algorithm>
#include <functional>
#include <cmath>
#include <memory>

namespace lumora {

namespace {

struct XNode {
    QString name;
    QHash<QString, QString> attrs;
    std::vector<std::unique_ptr<XNode>> children;

    QString attr(const char *key, const QString &fallback = QString()) const
    {
        return attrs.value(QString::fromLatin1(key), fallback);
    }
    const XNode *child(const char *n) const
    {
        for (const auto &c : children)
            if (c->name == QLatin1String(n))
                return c.get();
        return nullptr;
    }
    std::vector<const XNode *> all(const char *n) const
    {
        std::vector<const XNode *> out;
        for (const auto &c : children)
            if (c->name == QLatin1String(n))
                out.push_back(c.get());
        return out;
    }
};

std::unique_ptr<XNode> parseXml(const QByteArray &xml, QString *error)
{
    QXmlStreamReader r(xml);
    auto root = std::make_unique<XNode>();
    std::vector<XNode *> stack{root.get()};
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            auto n = std::make_unique<XNode>();
            n->name = r.name().toString();
            for (const QXmlStreamAttribute &a : r.attributes())
                n->attrs.insert(a.name().toString(), a.value().toString());
            XNode *raw = n.get();
            stack.back()->children.push_back(std::move(n));
            stack.push_back(raw);
        } else if (r.isEndElement()) {
            stack.pop_back();
        }
    }
    if (r.hasError()) {
        if (error)
            *error = QStringLiteral("description.xml: %1 (line %2)").arg(r.errorString()).arg(r.lineNumber());
        return nullptr;
    }
    return root;
}

// "128/1" or "32768/2" (value/bytes) → 0..1. A bare number is read at the channel's resolution.
double dmxValue(const QString &raw, int channelBytes, bool *ok = nullptr)
{
    const QString s = raw.trimmed();
    if (ok)
        *ok = !s.isEmpty() && s.compare(QLatin1String("None"), Qt::CaseInsensitive) != 0;
    if (ok && !*ok)
        return 0.0;
    int bytes = channelBytes;
    double v = 0;
    const int slash = s.indexOf('/');
    if (slash >= 0) {
        v = s.left(slash).toDouble();
        bytes = std::max(1, s.mid(slash + 1).toInt());
    } else {
        v = s.toDouble();
    }
    const double max = std::pow(2.0, 8.0 * std::min(bytes, 4)) - 1.0;
    return std::clamp(v / max, 0.0, 1.0);
}

// GDTF attribute names mostly match ours (we follow GDTF naming); a few differ.
QString mapAttribute(const QString &gdtf)
{
    static const QHash<QString, QString> map = {
        {"Shutter1", "Shutter"}, {"ColorAdd_RY", "ColorAdd_A"}, {"ColorAdd_GY", "ColorAdd_L"},
        {"ColorRGB_Red", "ColorAdd_R"}, {"ColorRGB_Green", "ColorAdd_G"}, {"ColorRGB_Blue", "ColorAdd_B"},
        {"Gobo1Rotate", "Gobo1Pos"},
    };
    return map.value(gdtf, gdtf);
}

struct WheelSlot {
    QString name;
    QString media;
};

// The geometry tree of one DMX mode, with GeometryReferences expanded: each reference becomes a
// copy of the geometry it points to, shifted to its DMX offset. Channels are attached by
// geometry name, so a channel on a referenced geometry lands in every copy.
struct GeoNode {
    QString name;
    int shift = 0;    // DMX slots this copy sits from the fixture's start
    int parent = -1;
    QVector<int> kids;
};

struct GeoTree {
    QVector<GeoNode> nodes;
    QHash<QString, QVector<int>> byName;   // in tree order
    QHash<QString, const XNode *> top;     // top-level geometries, by name
    QSet<QString> templates;               // geometries something references

    int add(const QString &name, int shift, int parent)
    {
        nodes.append({name, shift, parent, {}});
        const int i = int(nodes.size()) - 1;
        if (parent >= 0)
            nodes[parent].kids << i;
        byName[name] << i;
        return i;
    }
    void expand(const XNode *x, int node, int shift, int depth)
    {
        if (depth > 24)
            return;
        for (const auto &c : x->children) {
            const QString name = c->attr("Name");
            if (c->name == QLatin1String("Break") || name.isEmpty())
                continue;
            if (c->name == QLatin1String("GeometryReference")) {
                const XNode *t = top.value(c->attr("Geometry"));
                if (!t)
                    continue;
                int offset = 1;
                for (const XNode *b : c->all("Break"))
                    if (b->attr("DMXBreak", QStringLiteral("1")) == QLatin1String("1"))
                        offset = std::max(1, b->attr("DMXOffset", QStringLiteral("1")).toInt());
                const int copy = add(t->attr("Name"), offset - 1, node);
                expand(t, copy, offset - 1, depth + 1);
                continue;
            }
            if (templates.contains(name))
                continue;   // a referenced geometry's own definition: it lives in its copies
            expand(c.get(), add(name, shift, node), shift, depth + 1);
        }
    }
};

void collectTemplates(const XNode *n, QSet<QString> &out)
{
    for (const auto &c : n->children) {
        if (c->name == QLatin1String("GeometryReference"))
            out.insert(c->attr("Geometry"));
        collectTemplates(c.get(), out);
    }
}

} // namespace

bool FixtureLibrary::parseGdtf(const QByteArray &archive, FixtureType *out, QString *error)
{
    ZipArchive zip;
    if (!zip.open(archive, error))
        return false;
    bool ok = false;
    const QByteArray xml = zip.file(QStringLiteral("description.xml"), &ok);
    if (!ok) {
        if (error)
            *error = QStringLiteral("Not a GDTF file (no description.xml inside)");
        return false;
    }
    std::unique_ptr<XNode> doc = parseXml(xml, error);
    if (!doc)
        return false;
    const XNode *gdtf = doc->child("GDTF");
    const XNode *ft = gdtf ? gdtf->child("FixtureType") : nullptr;
    if (!ft) {
        if (error)
            *error = QStringLiteral("description.xml has no FixtureType");
        return false;
    }

    FixtureType t;
    t.angles = false;   // pan and tilt defaults come as 0..1 of the DMX range
    t.source = QStringLiteral("GDTF");
    t.name = ft->attr("LongName").isEmpty() ? ft->attr("Name") : ft->attr("LongName");
    if (t.name.isEmpty())
        t.name = ft->attr("Name", QStringLiteral("Fixture"));
    t.shortName = ft->attr("ShortName", ft->attr("Name", t.name));
    if (t.shortName.isEmpty())
        t.shortName = t.name;
    t.manufacturer = ft->attr("Manufacturer", QStringLiteral("Unknown"));
    t.id = FixtureLibrary::makeTypeId(t.manufacturer, ft->attr("Name", t.name));

    // ---- Wheels and their images
    QHash<QString, QVector<WheelSlot>> wheels;
    if (const XNode *ws = ft->child("Wheels")) {
        for (const XNode *w : ws->all("Wheel")) {
            QVector<WheelSlot> slots_;
            for (const XNode *s : w->all("Slot")) {
                WheelSlot slot{s->attr("Name"), s->attr("MediaFileName")};
                if (!slot.media.isEmpty() && !t.media.contains(slot.media)) {
                    for (const QString &candidate : {QStringLiteral("wheels/%1.png").arg(slot.media),
                                                     QStringLiteral("wheels/%1").arg(slot.media)}) {
                        bool found = false;
                        const QByteArray img = zip.file(candidate, &found);
                        if (found) {
                            t.media.insert(slot.media, img);
                            break;
                        }
                    }
                    if (!t.media.contains(slot.media))
                        slot.media.clear();
                }
                slots_.append(slot);
            }
            wheels.insert(w->attr("Name"), slots_);
        }
    }
    auto slotOf = [&](const QString &wheel, int index) -> WheelSlot {
        const QVector<WheelSlot> s = wheels.value(wheel);
        return index >= 1 && index <= s.size() ? s[index - 1] : WheelSlot{};
    };

    const XNode *geometries = ft->child("Geometries");
    QSet<QString> templates;
    if (geometries)
        collectTemplates(geometries, templates);

    // ---- DMX modes
    const XNode *modes = ft->child("DMXModes");
    if (!modes) {
        if (error)
            *error = QStringLiteral("%1 has no DMX modes").arg(t.name);
        return false;
    }
    for (const XNode *m : modes->all("DMXMode")) {
        FixtureMode mode;
        mode.name = m->attr("Name", QStringLiteral("Mode %1").arg(t.modes.size() + 1));
        const XNode *chans = m->child("DMXChannels");
        if (!chans)
            continue;
        int footprint = 0;
        struct Parsed {
            ChannelDef def;
            QString geometry;
        };
        QVector<Parsed> parsed;
        for (const XNode *ch : chans->all("DMXChannel")) {
            const QString brk = ch->attr("DMXBreak", QStringLiteral("1"));
            if (brk != QLatin1String("1") && brk != QLatin1String("Overwrite"))
                continue;

            // Offsets: "1,2" (1.1+) or Coarse/Fine (1.0). "None" = virtual channel, nothing on the wire.
            QList<int> offsets;
            const QString offsetAttr = ch->attr("Offset");
            if (!offsetAttr.isEmpty() && offsetAttr != QLatin1String("None")) {
                for (const QString &o : offsetAttr.split(',', Qt::SkipEmptyParts))
                    offsets << o.trimmed().toInt();
            } else if (offsetAttr.isEmpty() && !ch->attr("Coarse").isEmpty() && ch->attr("Coarse") != QLatin1String("None")) {
                offsets << ch->attr("Coarse").toInt();
                if (!ch->attr("Fine").isEmpty() && ch->attr("Fine") != QLatin1String("None"))
                    offsets << ch->attr("Fine").toInt();
            }
            // Offset="None": a virtual channel (often an RGB fixture's dimmer). Kept, without DMX.
            const bool isVirtual = offsets.isEmpty() && offsetAttr.compare(QLatin1String("None"), Qt::CaseInsensitive) == 0;
            if (!isVirtual && (offsets.isEmpty() || offsets.first() < 1))
                continue;
            const int bytes = std::max(1, int(offsets.size()));

            const XNode *logical = ch->child("LogicalChannel");
            if (!logical)
                continue;
            std::vector<const XNode *> functions = logical->all("ChannelFunction");
            QString attr = logical->attr("Attribute");
            if (attr.isEmpty() && !functions.empty())
                attr = functions.front()->attr("Attribute");
            if (attr.isEmpty() || attr == QLatin1String("NoFeature"))
                attr = QStringLiteral("Ctl_") + QString(ch->attr("Name", QStringLiteral("Channel"))).remove(QRegularExpression("[^A-Za-z0-9]"));
            attr = mapAttribute(attr);

            ChannelDef def;
            def.name = attr;
            def.coarse = isVirtual ? -1 : offsets[0] - 1;
            def.fine = offsets.size() > 1 ? offsets[1] - 1 : -1;
            // Pan and tilt: the degrees the channel function covers.
            if (isAngle(attr))
                for (const XNode *f : functions)
                    if (mapAttribute(f->attr("Attribute")) == attr && !f->attr("PhysicalTo").isEmpty()) {
                        def.setPhysicalRange(f->attr("PhysicalFrom").toDouble(), f->attr("PhysicalTo").toDouble());
                        break;
                    }

            // Defaults: 1.0 keeps it on the channel, 1.1+ on the initial (or first) function.
            bool hasDefault = false;
            double def0 = dmxValue(ch->attr("Default"), bytes, &hasDefault);
            if (!hasDefault && !functions.empty()) {
                const QString initial = ch->attr("InitialFunction").section('.', -1);
                const XNode *f0 = functions.front();
                for (const XNode *f : functions)
                    if (!initial.isEmpty() && f->attr("Name") == initial)
                        f0 = f;
                def0 = dmxValue(f0->attr("Default"), bytes, &hasDefault);
            }
            def.defaultValue = hasDefault ? def0 : ((attr == "Pan" || attr == "Tilt") ? 0.5 : 0.0);
            bool hasHighlight = false;
            const double hl = dmxValue(ch->attr("Highlight"), bytes, &hasHighlight);
            def.highlightValue = hasHighlight ? hl : ((isIntensity(attr) || isColorAdd(attr)) ? 1.0 : def.defaultValue);

            // Named ranges: channel sets inside each function, or the functions themselves.
            std::sort(functions.begin(), functions.end(), [bytes](const XNode *a, const XNode *b) {
                return dmxValue(a->attr("DMXFrom", "0/1"), bytes) < dmxValue(b->attr("DMXFrom", "0/1"), bytes);
            });
            QVector<ChannelSet> sets;
            for (size_t i = 0; i < functions.size(); ++i) {
                const XNode *f = functions[i];
                const double fFrom = dmxValue(f->attr("DMXFrom", "0/1"), bytes);
                const double fTo = i + 1 < functions.size()
                                       ? std::max(fFrom, dmxValue(functions[i + 1]->attr("DMXFrom", "0/1"), bytes) - 1e-9)
                                       : 1.0;
                std::vector<const XNode *> csets = f->all("ChannelSet");
                std::sort(csets.begin(), csets.end(), [bytes](const XNode *a, const XNode *b) {
                    return dmxValue(a->attr("DMXFrom", "0/1"), bytes) < dmxValue(b->attr("DMXFrom", "0/1"), bytes);
                });
                const QString wheel = f->attr("Wheel");
                bool addedAny = false;
                for (size_t k = 0; k < csets.size(); ++k) {
                    const XNode *cs = csets[k];
                    const double from = std::max(fFrom, dmxValue(cs->attr("DMXFrom", "0/1"), bytes));
                    const double to = k + 1 < csets.size()
                                          ? std::max(from, dmxValue(csets[k + 1]->attr("DMXFrom", "0/1"), bytes) - 1e-9)
                                          : fTo;
                    QString name = cs->attr("Name");
                    QString media;
                    const int slotIndex = cs->attr("WheelSlotIndex").toInt();
                    if (slotIndex > 0 && !wheel.isEmpty()) {
                        const WheelSlot slot = slotOf(wheel, slotIndex);
                        if (name.isEmpty())
                            name = slot.name;
                        media = slot.media;
                    }
                    if (name.isEmpty())
                        continue;   // unnamed sets add nothing an operator can pick
                    sets.append({from, to, name, media});
                    addedAny = true;
                }
                if (!addedAny) {
                    const QString fname = f->attr("Name", f->attr("Attribute"));
                    if (!fname.isEmpty())
                        sets.append({fFrom, fTo, fname, QString()});
                }
            }
            if (sets.size() > 1)
                def.sets = sets;
            def.attribute = attr;
            parsed.append({def, ch->attr("Geometry")});
        }

        // Subfixtures. The mode's geometry tree (references expanded) is
        // searched from the top for the first set of sibling geometries that carry the same
        // channels: those are the heads (or pixels). Inside a head, geometries that carry channels
        // and have none below them (its ring, its main beam) are its parts when there are two or
        // more; the rest (its yoke's tilt, its pan) belong to the head. Everything else belongs to
        // the fixture.
        GeoTree tree;
        tree.templates = templates;
        // Referenced geometries are top-level in GDTF; some files define them deeper.
        std::function<void(const XNode *)> index = [&](const XNode *x) {
            for (const auto &g : x->children) {
                if (g->name != QLatin1String("GeometryReference") && !g->attr("Name").isEmpty() && !tree.top.contains(g->attr("Name")))
                    tree.top.insert(g->attr("Name"), g.get());
                index(g.get());
            }
        };
        if (geometries)
            index(geometries);
        const int root = tree.add(m->attr("Geometry"), 0, -1);
        if (const XNode *r = tree.top.value(m->attr("Geometry")))
            tree.expand(r, root, 0, 0);
        else if (geometries)
            tree.expand(geometries, root, 0, 0);   // no mode geometry: everything that isn't a template
        // Where each channel goes: every copy of its geometry (the root when it isn't in the tree).
        QVector<QVector<int>> placed(parsed.size());
        QVector<QStringList> attrsIn(tree.nodes.size());   // attributes in each node's subtree
        for (int i = 0; i < parsed.size(); ++i) {
            placed[i] = tree.byName.value(parsed[i].geometry);
            // A channel on a referenced geometry that sits further out than the copies are apart
            // (a shutter at 169 on an LED repeated every 3 slots) is one channel for all of them,
            // not one per copy.
            QVector<int> shifts;
            for (int n : placed[i])
                shifts << tree.nodes[n].shift;
            std::sort(shifts.begin(), shifts.end());
            shifts.erase(std::unique(shifts.begin(), shifts.end()), shifts.end());
            int spacing = 1 << 20;
            for (int k = 1; k < shifts.size(); ++k)
                spacing = std::min(spacing, shifts[k] - shifts[k - 1]);
            if (placed[i].isEmpty()
                || (shifts.size() > 1 && !parsed[i].def.isVirtual() && std::max(parsed[i].def.coarse, parsed[i].def.fine) >= spacing))
                placed[i] = {root};
            for (int n : placed[i])
                for (int up = n; up >= 0; up = tree.nodes[up].parent)
                    attrsIn[up] << parsed[i].def.attribute;
        }
        auto signature = [&](int n) {
            QStringList a = attrsIn[n];
            a.sort();
            return a.join('|');
        };
        std::function<QVector<int>(int)> findHeads = [&](int n) -> QVector<int> {
            QVector<QString> order;
            QHash<QString, QVector<int>> groups;
            for (int k : tree.nodes[n].kids)
                if (!attrsIn[k].isEmpty()) {
                    const QString sig = signature(k);
                    if (!groups.contains(sig))
                        order << sig;
                    groups[sig] << k;
                }
            QVector<int> best;
            for (const QString &sig : order)
                if (groups[sig].size() >= 2 && groups[sig].size() > best.size())
                    best = groups[sig];
            if (!best.isEmpty())
                return best;
            for (int k : tree.nodes[n].kids)
                if (!attrsIn[k].isEmpty())
                    if (QVector<int> h = findHeads(k); !h.isEmpty())
                        return h;
            return {};
        };
        const QVector<int> heads = findHeads(root);
        QVector<int> cellOf(tree.nodes.size(), 0), subOf(tree.nodes.size(), 0);
        QVector<bool> carries(tree.nodes.size(), false);
        for (const QVector<int> &p : placed)
            for (int n : p)
                carries[n] = true;
        for (int h = 0; h < heads.size(); ++h) {
            // The head's subtree, in tree order, and its leaves that carry channels.
            QVector<int> inside{heads[h]};
            for (int i = 0; i < inside.size(); ++i)
                inside << tree.nodes[inside[i]].kids;
            std::sort(inside.begin(), inside.end());
            QVector<int> leaves;
            for (int n : inside) {
                if (!carries[n])
                    continue;
                bool below = false;
                for (int k : inside)
                    if (k != n && carries[k])
                        for (int up = tree.nodes[k].parent; up >= 0 && !below; up = tree.nodes[up].parent)
                            below = up == n;
                if (!below)
                    leaves << n;
            }
            for (int n : inside) {
                cellOf[n] = h + 1;
                subOf[n] = leaves.size() >= 2 ? int(leaves.indexOf(n)) + 1 : 0;
            }
        }
        QVector<ChannelDef> laid;
        for (int i = 0; i < parsed.size(); ++i)
            for (int n : placed[i]) {
                ChannelDef c = parsed[i].def;
                if (!c.isVirtual()) {
                    c.coarse += tree.nodes[n].shift;
                    if (c.fine >= 0)
                        c.fine += tree.nodes[n].shift;
                }
                c.cell = cellOf[n];
                c.sub = c.cell ? subOf[n] : 0;
                laid.append(c);
            }
        QHash<int, QSet<QString>> used;   // attribute names are unique per subfixture
        for (ChannelDef def : laid) {
            const int node = def.cell * 1024 + def.sub;
            QString unique = def.attribute;
            for (int n = 2; used[node].contains(unique); ++n)
                unique = def.attribute + QString::number(n);
            used[node].insert(unique);
            def.attribute = unique;
            if (!def.isVirtual())
                footprint = std::max({footprint, def.coarse + 1, def.fine + 1});
            mode.channels.append(def);
        }
        if (mode.channels.isEmpty())
            continue;
        mode.footprint = footprint;
        t.modes.append(mode);
    }
    if (t.modes.isEmpty()) {
        if (error)
            *error = QStringLiteral("%1 has no DMX modes Lumora can use").arg(t.name);
        return false;
    }
    t.finalize();
    *out = t;
    return true;
}

} // namespace lumora
