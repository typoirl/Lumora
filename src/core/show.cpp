// SPDX-License-Identifier: GPL-3.0-or-later
#include "show.h"

#include "commandline.h"

#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>

#include <algorithm>
#include <cmath>

namespace lumora {

namespace {
constexpr double kEps = 1e-4;
int execKey(int page, int number) { return page * 1000 + number; }
} // namespace

Show::Show(QObject *parent) : QObject(parent)
{
    resetRateMasters();
}

void Show::resetRateMasters()
{
    m_rateMasters.clear();
    for (int i = 1; i <= RateMaster::kCount; ++i) {
        RateMaster m;
        m.id = i;
        m_rateMasters.append(m);
    }
}

void Show::setName(const QString &n)
{
    m_name = n;
    emit changed(SettingsPart);
}

void Show::clearShow()
{
    m_name = QStringLiteral("Untitled Show");
    m_library.clearShowTypes();
    m_fixtures.clear();
    m_cells.clear();
    m_groups.clear();
    m_presets.clear();
    m_sequences.clear();
    m_effects.clear();
    m_shapes.clear();
    m_progEffects.clear();
    m_executors.clear();
    resetRateMasters();
    m_macros.clear();
    m_plots.clear();
    m_macroRuns.clear();
    m_macroVars.clear();
    m_lastGoSequence = 0;
    m_selection.clear();
    m_programmer.clear();
    m_defaults.clear();
    m_lastOutput.clear();
    m_extra = QJsonObject();
    m_page = 1;
    m_pageNames.clear();
    m_views.clear();
    m_spreads.clear();
    m_activeSpread = Spread();
    m_parked.clear();
    m_lastSelection.clear();
    m_stepList.clear();
    m_stepIndex = -1;
    m_selectedSequence = 1;
    m_grandMaster = 1.0;
    m_blackout = m_highlight = false;
    emit changed(AllParts);
}

// ===========================================================================
// Patch
// ===========================================================================

const Fixture *Show::fixture(int fid) const
{
    const QMap<int, Fixture> &map = isCellId(fid) ? m_cells : m_fixtures;
    auto it = map.constFind(fid);
    return it == map.constEnd() ? nullptr : &it.value();
}

QVector<int> Show::cellsOf(int master) const
{
    QVector<int> out;
    for (auto it = m_cells.lowerBound(cellId(master, 1)); it != m_cells.constEnd() && it->master == master; ++it)
        out << it.key();
    return out;
}

QVector<int> Show::fixtureIds() const
{
    QVector<int> out;
    out.reserve(m_fixtures.size() + m_cells.size());
    for (auto it = m_fixtures.constBegin(); it != m_fixtures.constEnd(); ++it) {
        out << it.key();
        out << cellsOf(it.key());
    }
    return out;
}

QVector<int> Show::childrenOf(int fid) const
{
    QVector<int> out;
    if (!isCellId(fid)) {   // the heads (or pixels)
        for (auto it = m_cells.lowerBound(cellId(fid, 1)); it != m_cells.constEnd() && it->master == fid; ++it)
            if (it->sub == 0)
                out << it.key();
        return out;
    }
    // A head's subs follow it directly.
    auto it = m_cells.constFind(fid);
    if (it == m_cells.constEnd() || it->sub != 0)
        return out;
    for (++it; it != m_cells.constEnd() && it->master == masterOf(fid) && it->sub != 0; ++it)
        out << it.key();
    return out;
}

QString Show::idText(int fid) const
{
    if (!isCellId(fid))
        return QString::number(fid);
    const Fixture *f = fixture(fid);
    if (!f)
        return QStringLiteral("%1.%2").arg(masterOf(fid)).arg(cellOf(fid));
    return f->sub ? QStringLiteral("%1.%2.%3").arg(f->master).arg(f->head).arg(f->sub)
                  : QStringLiteral("%1.%2").arg(f->master).arg(f->head);
}

int Show::parseId(const QString &text) const
{
    const QStringList parts = text.trimmed().split('.');
    if (parts.isEmpty() || parts.size() > 3)
        return 0;
    QVector<int> n;
    for (const QString &p : parts) {
        bool ok = false;
        const int v = p.toInt(&ok);
        if (!ok || v < (n.isEmpty() ? 1 : 0))
            return 0;
        n << v;
    }
    if (n[0] > kMaxFixtureId)
        return 0;
    if (n.size() == 1)
        return n[0];
    const int head = n[1], sub = n.size() > 2 ? n[2] : 0;
    if (head < 1)
        return 0;
    const FixtureMode *full = nullptr;
    if (const Fixture *f = fixture(n[0]))
        if (const FixtureType *t = m_library.find(f->typeId))
            full = t->mode(f->mode);
    if (full && full->cellCount) {
        const int slot = full->partSlot(head, sub);
        return slot > 0 ? cellId(n[0], slot) : 0;
    }
    // Not patched (or no subfixtures): a single level, written as the slot.
    return sub == 0 && head < kCellsPerFixture ? cellId(n[0], head) : 0;
}

QVector<int> Show::expandMasters(const QVector<int> &fids) const
{
    QVector<int> out;
    for (int fid : fids) {
        const FixtureMode *own = fixtureMode(fid);
        const QVector<int> children = own && own->channels.isEmpty() ? childrenOf(fid) : QVector<int>();
        if (children.isEmpty())
            out << fid;
        else
            out << expandMasters(children);   // a head with no channels of its own: its subs
    }
    return out;
}

void Show::rebuildCells()
{
    m_cells.clear();
    for (const Fixture &f : m_fixtures) {
        const FixtureType *t = m_library.find(f.typeId);
        const FixtureMode *m = t ? t->mode(f.mode) : nullptr;
        if (!m)
            continue;
        for (int slot = 1; slot <= m->cellCount; ++slot) {
            const QPair<int, int> path = m->partPath(slot);
            Fixture cell = f;
            cell.fid = cellId(f.fid, slot);
            cell.name = f.name;   // subfixtures go by their fixture's name
            cell.master = f.fid;
            cell.cell = slot;
            cell.head = path.first;
            cell.sub = path.second;
            cell.parent = path.second ? cellId(f.fid, m->partSlot(path.first, 0)) : f.fid;
            m_cells.insert(cell.fid, cell);
        }
    }
}

const FixtureType *Show::fixtureType(int fid) const
{
    const Fixture *f = fixture(fid);
    return f ? m_library.find(f->typeId) : nullptr;
}

const FixtureMode *Show::fixtureMode(int fid) const
{
    const Fixture *f = fixture(fid);
    const FixtureType *t = f ? m_library.find(f->typeId) : nullptr;
    const FixtureMode *m = t ? t->mode(f->mode) : nullptr;
    return m ? m->part(f->cell) : nullptr;   // pixel fixtures: the master's or the cell's channels
}

QStringList Show::fixtureAttributes(int fid) const
{
    const FixtureMode *m = fixtureMode(fid);
    return m ? m->attributes() : QStringList();
}

bool Show::fixtureHas(int fid, const QString &attribute) const
{
    const FixtureMode *m = fixtureMode(fid);
    if (!m)
        return false;
    if (attribute == QLatin1String("Dimmer") && m->virtualDimmer)
        return true;
    return m->channel(attribute) != nullptr;
}

double Show::defaultValue(int fid, const QString &attribute) const
{
    auto f = m_defaults.constFind(fid);
    if (f != m_defaults.constEnd()) {
        auto a = f->constFind(attribute);
        if (a != f->constEnd())
            return resolveValue(fid, attribute, a.value());
    }
    return typeDefault(fid, attribute);
}

double Show::typeDefault(int fid, const QString &attribute) const
{
    const FixtureMode *m = fixtureMode(fid);
    if (!m)
        return 0.0;
    if (const ChannelDef *c = m->channel(attribute))
        return c->defaultValue;
    return 0.0;
}

const ChannelDef *Show::channelOf(int fid, const QString &attribute) const
{
    const FixtureMode *m = fixtureMode(fid);
    return m ? m->channel(attribute) : nullptr;
}

double Show::clampValue(int fid, const QString &attribute, double value) const
{
    if (const ChannelDef *c = channelOf(fid, attribute))
        return std::clamp(value, c->minValue(), c->maxValue());
    if (isAngle(attribute))
        return std::clamp(value, -defaultAngleSpan(attribute) / 2, defaultAngleSpan(attribute) / 2);
    return std::clamp(value, 0.0, 1.0);
}

double Show::valueSpan(int fid, const QString &attribute) const
{
    if (const ChannelDef *c = channelOf(fid, attribute))
        return c->span();
    return isAngle(attribute) ? defaultAngleSpan(attribute) : 1.0;
}

int Show::nextFreeFid() const
{
    return m_fixtures.isEmpty() ? 1 : m_fixtures.lastKey() + 1;
}

int Show::nextFreeAddress(int universe, int footprint) const
{
    int addr = 1;
    bool moved = true;
    while (moved && addr + footprint - 1 <= 512) {
        moved = false;
        for (const Fixture &f : m_fixtures) {
            if (f.universe != universe || !f.patched())
                continue;
            const FixtureMode *m = fixtureMode(f.fid);
            int fp = m ? m->footprint : 1;
            if (addr <= f.address + fp - 1 && f.address <= addr + footprint - 1) {
                addr = f.address + fp;
                moved = true;
            }
        }
    }
    return addr + footprint - 1 <= 512 ? addr : -1;
}

int Show::footprintOf(const Fixture &f) const
{
    const FixtureType *t = m_library.find(f.typeId);
    const FixtureMode *m = t ? t->mode(f.mode) : nullptr;
    return m ? std::max(1, m->footprint) : 1;
}

bool Show::layOut(QVector<Fixture> &list, int universe, int address, QString *error) const
{
    QSet<int> moving;
    for (const Fixture &f : list)
        moving.insert(f.fid);
    // What's taken, per universe: [first, last] slot ranges.
    QHash<int, QVector<QPair<int, int>>> taken;
    for (const Fixture &f : m_fixtures)
        if (f.patched() && !moving.contains(f.fid))
            taken[f.universe].append({f.address, f.address + footprintOf(f) - 1});
    int u = std::max(1, universe), addr = std::clamp(address, 1, 512);
    for (Fixture &f : list) {
        const int fp = footprintOf(f);
        if (fp > 512) {
            if (error)
                *error = tr("Fixture %1 needs %2 channels: more than a universe").arg(f.fid).arg(fp);
            return false;
        }
        bool placed = false;
        while (!placed) {
            if (addr + fp - 1 > 512) {   // doesn't fit: the next universe, from the top
                ++u;
                addr = 1;
            }
            if (u > kMaxUniverses) {
                if (error)
                    *error = tr("Ran out of universes at fixture %1").arg(f.fid);
                return false;
            }
            placed = true;
            for (const auto &r : std::as_const(taken[u]))
                if (addr <= r.second && r.first <= addr + fp - 1) {
                    addr = r.second + 1;
                    placed = false;
                    break;
                }
        }
        f.universe = u;
        f.address = addr;
        taken[u].append({addr, addr + fp - 1});
        addr += fp;
    }
    return true;
}

QString Show::addressConflict(const Fixture &nf) const
{
    return conflictIn(m_fixtures, nf);
}

QString Show::conflictIn(const QMap<int, Fixture> &patch, const Fixture &nf) const
{
    const FixtureType *t = m_library.find(nf.typeId);
    const FixtureMode *nm = t ? t->mode(nf.mode) : nullptr;
    if (!nm)
        return tr("Unknown fixture type %1").arg(nf.typeId);
    if (!nf.mode.isEmpty() && nm->name != nf.mode)
        return tr("%1 has no mode \"%2\"").arg(t->name, nf.mode);
    if (!nf.patched())
        return QString();   // unpatched fixtures can be programmed, they just don't output
    if (nf.universe < 1 || nf.universe > kMaxUniverses)
        return tr("Universe %1 is out of range (1–%2)").arg(nf.universe).arg(kMaxUniverses);
    if (nf.address < 1 || nf.address + nm->footprint - 1 > 512)
        return tr("Address %1 with footprint %2 doesn't fit in a universe").arg(nf.address).arg(nm->footprint);
    for (const Fixture &f : patch) {
        if (f.fid == nf.fid || f.universe != nf.universe || !f.patched())
            continue;
        const FixtureType *ft = m_library.find(f.typeId);
        const FixtureMode *m = ft ? ft->mode(f.mode) : nullptr;
        int fp = m ? m->footprint : 1;
        if (nf.address <= f.address + fp - 1 && f.address <= nf.address + nm->footprint - 1)
            return tr("Overlaps fixture %1 (%2.%3)").arg(f.fid).arg(f.universe).arg(f.address, 3, 10, QChar('0'));
    }
    return QString();
}

bool Show::addFixture(const Fixture &f, QString *error)
{
    if (f.fid <= 0 || f.fid > kMaxFixtureId || m_fixtures.contains(f.fid)) {
        if (error)
            *error = f.fid > kMaxFixtureId ? tr("Fixture IDs go up to %1").arg(kMaxFixtureId)
                                           : tr("Fixture ID %1 is already used").arg(f.fid);
        return false;
    }
    QString conflict = addressConflict(f);
    if (!conflict.isEmpty()) {
        if (error)
            *error = conflict;
        return false;
    }
    Fixture copy = f;
    adoptType(f.typeId);
    if (copy.mode.isEmpty())
        copy.mode = m_library.find(f.typeId)->modes.first().name;
    if (copy.name.isEmpty())
        copy.name = QStringLiteral("%1 %2").arg(m_library.find(f.typeId)->shortName).arg(f.fid);
    copy.master = copy.cell = copy.head = copy.sub = copy.parent = 0;
    m_fixtures.insert(f.fid, copy);
    rebuildCells();
    emit changed(PatchPart);
    return true;
}

bool Show::importFixtureFile(const QString &path, QString *error, QString *typeId)
{
    if (!m_library.importFile(path, error, typeId))
        return false;
    emit changed(PatchPart);
    return true;
}

void Show::adoptType(const QString &typeId)
{
    if (!m_library.isShowType(typeId))
        if (const FixtureType *t = m_library.find(typeId))
            m_library.addToShow(*t);
}

void Show::updateFixtureType(const FixtureType &type)
{
    m_library.addToShow(type);   // edits belong to this show
    const FixtureType *t = m_library.find(type.id);
    for (Fixture &f : m_fixtures)
        if (f.typeId == type.id && t && !t->modes.isEmpty() && t->mode(f.mode)->name != f.mode)
            f.mode = t->modes.first().name;
    rebuildCells();
    emit changed(PatchPart | ProgrammerPart);
}

bool Show::updateFixture(const Fixture &f, QString *error)
{
    return updateFixtures({f}, error);
}

bool Show::updateFixtures(const QVector<Fixture> &list, QString *error)
{
    QMap<int, Fixture> next = m_fixtures;
    for (Fixture f : list) {
        if (!m_fixtures.contains(f.fid)) {
            if (error)
                *error = tr("No fixture %1").arg(f.fid);
            return false;
        }
        const FixtureType *t = m_library.find(f.typeId);
        if (!t) {
            if (error)
                *error = tr("Unknown fixture type %1").arg(f.typeId);
            return false;
        }
        if (f.mode.isEmpty() || !t->mode(f.mode) || t->mode(f.mode)->name != f.mode)
            f.mode = t->modes.first().name;
        next[f.fid] = f;
    }
    // Validate against the patch as it will be, so fixtures can swap or shift together.
    for (const Fixture &f : list) {
        const QString conflict = conflictIn(next, next[f.fid]);
        if (!conflict.isEmpty()) {
            if (error)
                *error = tr("Fixture %1: %2").arg(f.fid).arg(conflict);
            return false;
        }
    }
    m_fixtures = next;
    for (const Fixture &f : list)
        adoptType(f.typeId);
    rebuildCells();
    emit changed(PatchPart | ProgrammerPart);
    return true;
}

void Show::removeFixture(int fid)
{
    const QVector<int> gone = QVector<int>{fid} + cellsOf(fid);
    if (!m_fixtures.remove(fid))
        return;
    rebuildCells();
    for (int id : gone) {
        m_selection.removeAll(id);
        m_stepList.removeAll(id);
        m_lastSelection.removeAll(id);
        m_parked.remove(id);
        m_programmer.remove(id);
        m_defaults.remove(id);
        for (Group &g : m_groups)
            g.fixtures.removeAll(id);
    }
    emit changed(PatchPart | GroupsPart | ProgrammerPart);
}

// ===========================================================================
// Groups
// ===========================================================================

const Group *Show::group(int id) const
{
    auto it = m_groups.constFind(id);
    return it == m_groups.constEnd() ? nullptr : &it.value();
}

bool Show::storeGroup(int id, const QString &name)
{
    if (m_selection.isEmpty()) {
        emit message(tr("Nothing selected to store in Group %1").arg(id));
        return false;
    }
    Group g = m_groups.value(id);
    g.id = id;
    g.fixtures = m_selection;
    if (!name.isEmpty())
        g.name = name;
    if (g.name.isEmpty())
        g.name = tr("Group %1").arg(id);
    m_groups.insert(id, g);
    emit changed(GroupsPart);
    return true;
}

void Show::setGroup(const Group &g)
{
    m_groups.insert(g.id, g);
    emit changed(GroupsPart);
}

void Show::deleteGroup(int id)
{
    if (m_groups.remove(id))
        emit changed(GroupsPart | SequencesPart | ExecutorsPart);
}

// ===========================================================================
// Presets
// ===========================================================================

const QMap<int, Preset> &Show::presets(int type) const
{
    static const QMap<int, Preset> empty;
    auto it = m_presets.constFind(type);
    return it == m_presets.constEnd() ? empty : it.value();
}

const Preset *Show::preset(int type, int id) const
{
    auto t = m_presets.constFind(type);
    if (t == m_presets.constEnd())
        return nullptr;
    auto p = t->constFind(id);
    return p == t->constEnd() ? nullptr : &p.value();
}

bool Show::storePreset(int type, int id, StoreMode mode, const QString &name, std::optional<PresetScope> scope)
{
    if (type < 1 || type > kPresetTypeAll)
        return false;
    Preset p;
    const bool existed = preset(type, id) != nullptr;
    if (mode == StoreMode::Merge && existed)
        p = *preset(type, id);
    p.type = type;
    p.id = id;
    if (!name.isEmpty())
        p.name = name;
    const PresetScope layer = scope ? *scope : defaultPresetLayer(type);
    if (mode == StoreMode::Overwrite) {
        p.universal.clear();
        p.perType.clear();
        p.perFixture.clear();
    }

    // Write one layer. For Universal and Global the first fixture (in selection order) with a
    // value sets it.
    bool any = false;
    QVector<QPair<int, QString>> stored;
    QSet<QString> universalSet;
    QSet<QString> typeSet;
    QVector<int> order = m_selection;
    for (auto f = m_programmer.begin(); f != m_programmer.end(); ++f)
        if (!order.contains(f.key()))
            order << f.key();
    for (int fid : order) {
        auto f = m_programmer.constFind(fid);
        if (f == m_programmer.constEnd())
            continue;
        const Fixture *fx = fixture(fid);
        const QString typeId = fx ? fx->typeId : QString();
        for (auto a = f->begin(); a != f->end(); ++a) {
            if (type != kPresetTypeAll && int(featureOf(a.key())) != type)
                continue;
            const double v = resolveValue(fid, a.key(), a.value());
            if (layer == PresetScope::Fixture) {
                p.perFixture[fid][a.key()] = v;
            } else if (layer == PresetScope::Universal) {
                if (!universalSet.contains(a.key())) {
                    p.universal.insert(a.key(), v);
                    universalSet.insert(a.key());
                }
            } else {
                const QString typeKey = typeId + '\n' + a.key();
                if (!typeId.isEmpty() && !typeSet.contains(typeKey)) {
                    p.perType[typeId].insert(a.key(), v);
                    typeSet.insert(typeKey);
                }
            }
            stored.append({fid, a.key()});
            any = true;
        }
    }
    // Running effects that drive this preset's feature go into the preset too (without fixtures).
    if (mode == StoreMode::Overwrite)
        p.effects.clear();
    // Their spread (phase layout) goes with them, and the preset's layer says where they play:
    // Universal on whatever the preset is applied to, Global on fixtures of the same types,
    // Selective on these fixtures.
    for (const EffectInstance &inst : m_progEffects) {
        const Effect *e = effect(inst.effectId);
        if (!e || (type != kPresetTypeAll && !e->features().contains(static_cast<Feature>(type))))
            continue;
        EffectInstance spec = inst;
        if (layer == PresetScope::Fixture) {
            spec.scope = EffectInstance::Selective;
        } else if (layer == PresetScope::Type) {
            spec.scope = EffectInstance::Global;
            for (int fid : inst.fixtures)
                if (const Fixture *fx = fixture(fid); fx && !spec.types.contains(fx->typeId))
                    spec.types << fx->typeId;
            spec.fixtures.clear();
        } else {
            spec.scope = EffectInstance::Universal;
            spec.fixtures.clear();
        }
        for (int i = p.effects.size() - 1; i >= 0; --i)
            if (p.effects[i].effectId == spec.effectId && p.effects[i].scope == spec.scope)
                p.effects.removeAt(i);
        p.effects.append(spec);
        any = true;
    }
    if (!any) {
        emit message(tr("No %1 values in the programmer to store").arg(presetTypeName(type)));
        return false;
    }
    if (p.name.isEmpty() && type == int(Feature::Color))
        p.name = colorName();   // picked from the color picker
    if (p.name.isEmpty())
        p.name = tr("%1 %2").arg(presetTypeName(type)).arg(id);
    m_presets[type].insert(id, p);

    // The programmer now references the preset it just created.
    for (const auto &s : stored) {
        ValueRef &r = m_programmer[s.first][s.second];
        r.presetType = type;
        r.presetId = id;
    }
    emit changed(PresetsPart | ProgrammerPart);
    return true;
}

void Show::setPreset(const Preset &p)
{
    m_presets[p.type].insert(p.id, p);
    emit changed(PresetsPart | ProgrammerPart);
}

void Show::deletePreset(int type, int id)
{
    if (m_presets[type].remove(id))
        emit changed(PresetsPart | ProgrammerPart | SequencesPart);
}

bool Show::resolvePreset(int type, int id, int fid, const QString &attribute, double *out) const
{
    const Preset *p = preset(type, id);
    return p && presetValue(*p, fid, attribute, out);
}

bool Show::presetValue(const Preset &p, int fid, const QString &attribute, double *out) const
{
    const Fixture *f = fixture(fid);
    return p.lookup(fid, f ? f->typeId : QString(), attribute, out);
}

// ===========================================================================
// Programmer
// ===========================================================================

void Show::select(const QVector<int> &fids, SelectMode mode)
{
    m_stepList.clear();
    m_stepIndex = -1;
    if (mode == SelectMode::Replace) {
        rememberSelection();
        m_selection.clear();
    }
    for (int fid : expandMasters(fids)) {
        if (!hasFixture(fid))
            continue;
        if (mode == SelectMode::Remove)
            m_selection.removeAll(fid);
        else if (!m_selection.contains(fid))
            m_selection.append(fid);
    }
    emit changed(ProgrammerPart);
}

void Show::putProgrammer(int fid, const QString &attribute, ValueRef r)
{
    // A new value keeps the attribute's individual fade and delay.
    AttrValues &attrs = m_programmer[fid];
    if (!r.hasTiming()) {
        const auto old = attrs.constFind(attribute);
        if (old != attrs.constEnd()) {
            r.fade = old->fade;
            r.delay = old->delay;
        }
    }
    attrs[attribute] = r;
}

void Show::selectGroup(int id, SelectMode mode)
{
    if (const Group *g = group(id))
        select(g->fixtures, mode);
}

void Show::setAttribute(const QString &attribute, double value)
{
    for (int fid : workingSelection())
        if (fixtureHas(fid, attribute))
            putProgrammer(fid, attribute, ValueRef{clampValue(fid, attribute, value), 0, 0});
    emit changed(ProgrammerPart);
}

void Show::setAttribute(int fid, const QString &attribute, double value)
{
    if (!fixtureHas(fid, attribute))
        return;
    putProgrammer(fid, attribute, ValueRef{clampValue(fid, attribute, value), 0, 0});
    emit changed(ProgrammerPart);
}

void Show::nudgeAttribute(const QString &attribute, double delta)
{
    for (int fid : workingSelection()) {
        if (!fixtureHas(fid, attribute))
            continue;
        double base;
        auto f = m_programmer.constFind(fid);
        if (f != m_programmer.constEnd() && f->contains(attribute))
            base = resolveValue(fid, attribute, f->value(attribute));
        else
            base = m_lastOutput.value(fid).value(attribute, defaultValue(fid, attribute));
        putProgrammer(fid, attribute, ValueRef{clampValue(fid, attribute, base + delta * valueSpan(fid, attribute)), 0, 0});
    }
    emit changed(ProgrammerPart);
}

int Show::flip(const QVector<int> &fidsIn)
{
    const QVector<int> fids = fidsIn.isEmpty() ? workingSelection() : fidsIn;
    auto current = [this](int fid, const QString &a) {
        auto f = m_programmer.constFind(fid);
        if (f != m_programmer.constEnd() && f->contains(a))
            return resolveValue(fid, a, f->value(a));
        return m_lastOutput.value(fid).value(a, defaultValue(fid, a));
    };
    int n = 0;
    for (int fid : fids) {
        const ChannelDef *pan = channelOf(fid, QStringLiteral("Pan"));
        const ChannelDef *tilt = channelOf(fid, QStringLiteral("Tilt"));
        if (!pan || !tilt)
            continue;
        // Pointing the same way: pan half a turn round (whichever way stays in range), tilt
        // mirrored through straight up.
        const double p = current(fid, QStringLiteral("Pan")), t = current(fid, QStringLiteral("Tilt"));
        auto fits = [](const ChannelDef *c, double v) { return v >= c->minValue() - 1e-6 && v <= c->maxValue() + 1e-6; };
        const bool up = fits(pan, p + 180.0), down = fits(pan, p - 180.0);
        const double nt = -t;
        if ((!up && !down) || !fits(tilt, nt))
            continue;   // no room to go round
        // Of the two ways round, the one nearer the middle of the range.
        const double np = up && down ? (std::abs(p + 180.0) <= std::abs(p - 180.0) ? p + 180.0 : p - 180.0)
                                     : (up ? p + 180.0 : p - 180.0);
        putProgrammer(fid, QStringLiteral("Pan"), ValueRef{np, 0, 0});
        putProgrammer(fid, QStringLiteral("Tilt"), ValueRef{nt, 0, 0});
        ++n;
    }
    if (n)
        emit changed(ProgrammerPart);
    return n;
}

bool Show::applyPreset(int type, int id)
{
    const Preset *p = preset(type, id);
    if (!p)
        return false;
    if (m_selection.isEmpty()) {
        emit message(tr("Select fixtures before applying a preset"));
        return false;
    }
    const QStringList attrs = p->attributes();
    bool any = false;
    QHash<int, QSet<Feature>> stomped;   // fixture -> features the preset gave a value
    for (int fid : workingSelection()) {
        for (const QString &a : attrs) {
            double v;
            if (fixtureHas(fid, a) && presetValue(*p, fid, a, &v)) {
                putProgrammer(fid, a, ValueRef{v, type, id});
                stomped[fid].insert(featureOf(a));
                any = true;
            }
        }
    }
    // Stomp: a preset's values stop the programmer's effects on those features of
    // those fixtures (the preset's own effects go back on just below).
    for (int i = m_progEffects.size() - 1; i >= 0; --i) {
        EffectInstance &inst = m_progEffects[i];
        const Effect *e = effect(inst.effectId);
        if (!e)
            continue;
        const QList<Feature> features = e->features();
        for (int k = inst.fixtures.size() - 1; k >= 0; --k) {
            const QSet<Feature> fs = stomped.value(inst.fixtures[k]);
            for (Feature f : features)
                if (fs.contains(f)) {
                    inst.fixtures.removeAt(k);
                    break;
                }
        }
        if (inst.fixtures.isEmpty())
            m_progEffects.removeAt(i);
    }
    for (const EffectInstance &spec : p->effects)
        any |= applyEffectInstance(spec, presetEffectFixtures(spec, workingSelection()));
    emit changed(ProgrammerPart);
    return any;
}

QHash<QString, double> Show::colorSnapshot() const
{
    QHash<QString, double> snap;
    for (auto f = m_programmer.begin(); f != m_programmer.end(); ++f)
        for (auto a = f->begin(); a != f->end(); ++a)
            if (featureOf(a.key()) == Feature::Color)
                snap.insert(QString::number(f.key()) + '\n' + a.key(), resolveValue(f.key(), a.key(), a.value()));
    return snap;
}

void Show::setColorName(const QString &name)
{
    m_colorName = name;
    m_colorSnapshot = name.isEmpty() ? QHash<QString, double>() : colorSnapshot();
}

QString Show::colorName() const
{
    if (m_colorName.isEmpty() || m_colorSnapshot.isEmpty())
        return QString();
    const QHash<QString, double> now = colorSnapshot();
    if (now.size() != m_colorSnapshot.size())
        return QString();
    for (auto it = now.begin(); it != now.end(); ++it) {
        auto was = m_colorSnapshot.constFind(it.key());
        if (was == m_colorSnapshot.constEnd() || std::abs(*was - it.value()) > 1e-6)
            return QString();
    }
    return m_colorName;
}

void Show::removeProgrammerValues(const QVector<int> &fids)
{
    for (int fid : fids)
        m_programmer.remove(fid);
    removeEffectsFrom(fids);
    emit changed(ProgrammerPart);
}

void Show::clearSelection()
{
    rememberSelection();
    m_selection.clear();
    m_stepList.clear();
    m_stepIndex = -1;
    m_activeSpread = Spread();   // spread belong to the selection
    emit changed(ProgrammerPart);
}

void Show::clearProgrammer()
{
    m_programmer.clear();
    m_progEffects.clear();
    emit changed(ProgrammerPart | EffectsPart);
}

void Show::clearAll()
{
    rememberSelection();
    m_selection.clear();
    m_stepList.clear();
    m_stepIndex = -1;
    m_activeSpread = Spread();
    m_programmer.clear();
    m_progEffects.clear();
    emit changed(ProgrammerPart | EffectsPart);
}

QString Show::clearStep()
{
    if (!m_selection.isEmpty()) {
        clearSelection();
        return tr("Selection cleared");
    }
    clearProgrammer();
    return tr("Programmer cleared");
}

void Show::rememberSelection()
{
    const QVector<int> &whole = m_stepList.isEmpty() ? m_selection : m_stepList;
    if (!whole.isEmpty())
        m_lastSelection = whole;
}

QString Show::stepSelection(int direction)
{
    if (m_stepList.isEmpty()) {
        if (m_selection.size() < 2)
            return m_selection.isEmpty() ? tr("Select fixtures first, then Next steps through them")
                                         : tr("Only one fixture is selected");
        m_stepList = m_selection;
        m_stepIndex = -1;
    }
    const int n = int(m_stepList.size());
    // -1 (the whole selection) sits between the last and the first.
    m_stepIndex += direction > 0 ? 1 : -1;
    if (m_stepIndex >= n)
        m_stepIndex = -1;
    else if (m_stepIndex < -1)
        m_stepIndex = n - 1;
    m_selection = m_stepIndex < 0 ? m_stepList : QVector<int>{m_stepList[m_stepIndex]};
    emit changed(ProgrammerPart);
    if (m_stepIndex < 0)
        return tr("All %1 fixtures").arg(n);
    return tr("Fixture %1  (%2 of %3)").arg(idText(m_stepList[m_stepIndex])).arg(m_stepIndex + 1).arg(n);
}

QString Show::wholeSelection()
{
    if (m_stepList.isEmpty())
        return tr("%1 selected").arg(m_selection.size());
    m_selection = m_stepList;
    m_stepList.clear();
    m_stepIndex = -1;
    emit changed(ProgrammerPart);
    return tr("All %1 fixtures").arg(m_selection.size());
}

bool Show::reselectLast()
{
    QVector<int> last;
    for (int fid : m_lastSelection)
        if (hasFixture(fid))
            last << fid;
    if (last.isEmpty())
        return false;
    const QVector<int> now = m_stepList.isEmpty() ? m_selection : m_stepList;
    m_selection = last;
    m_stepList.clear();
    m_stepIndex = -1;
    if (!now.isEmpty())
        m_lastSelection = now;
    emit changed(ProgrammerPart);
    return true;
}

QVector<int> Show::withSubfixtures(const QVector<int> &fids) const
{
    QVector<int> out;
    for (int fid : fids) {
        if (!out.contains(fid))
            out << fid;
        for (int c : cellsOf(fid))
            if (!out.contains(c))
                out << c;
    }
    return out;
}

QStringList Show::matchingAttributes(int fid, const QString &attribute, int feature) const
{
    QStringList out;
    const FixtureMode *m = fixtureMode(fid);
    if (!m)
        return out;
    for (const ChannelDef &c : m->channels) {
        if (!attribute.isEmpty() && c.attribute.compare(attribute, Qt::CaseInsensitive) != 0
            && attributeLabel(c.attribute).compare(attribute, Qt::CaseInsensitive) != 0)
            continue;
        if (feature && int(featureOf(c.attribute)) != feature)
            continue;
        if (!out.contains(c.attribute))
            out << c.attribute;
    }
    return out;
}

int Show::park(const QVector<int> &fids, const QString &attribute, int feature)
{
    int n = 0;
    for (int fid : withSubfixtures(fids))
        for (const QString &a : matchingAttributes(fid, attribute, feature)) {
            // Where it is on stage right now.
            const double v = m_lastOutput.value(fid).value(a, defaultValue(fid, a));
            m_parked[fid][a] = v;
            ++n;
        }
    if (n)
        emit changed(ParkPart);
    return n;
}

int Show::unpark(const QVector<int> &fids, const QString &attribute, int feature)
{
    int n = 0;
    const QVector<int> targets = fids.isEmpty() ? QVector<int>(m_parked.keyBegin(), m_parked.keyEnd()) : withSubfixtures(fids);
    for (int fid : targets) {
        auto f = m_parked.find(fid);
        if (f == m_parked.end())
            continue;
        for (auto a = f->begin(); a != f->end();) {
            const bool match = (attribute.isEmpty() || a.key().compare(attribute, Qt::CaseInsensitive) == 0
                                || attributeLabel(a.key()).compare(attribute, Qt::CaseInsensitive) == 0)
                               && (!feature || int(featureOf(a.key())) == feature);
            if (match) {
                a = f->erase(a);
                ++n;
            } else {
                ++a;
            }
        }
        if (f->isEmpty())
            m_parked.erase(f);
    }
    if (n)
        emit changed(ParkPart);
    return n;
}

bool Show::storeDefaults(StoreMode mode)
{
    if (m_programmer.isEmpty()) {
        emit message(tr("Programmer is empty: set the values to rest at first"));
        return false;
    }
    if (mode == StoreMode::Overwrite)
        m_defaults.clear();
    for (auto f = m_programmer.begin(); f != m_programmer.end(); ++f)
        for (auto a = f->begin(); a != f->end(); ++a)
            m_defaults[f.key()][a.key()] = a.value();
    emit changed(SettingsPart | ProgrammerPart);
    return true;
}

void Show::clearDefaults(const QVector<int> &fids)
{
    if (fids.isEmpty())
        m_defaults.clear();
    else
        for (int fid : fids)
            m_defaults.remove(fid);
    emit changed(SettingsPart | ProgrammerPart);
}

int Show::home(const QVector<int> &fidsIn, const QStringList &attributes)
{
    QVector<int> fids = fidsIn;
    if (fids.isEmpty())
        fids = fixtureIds();
    int n = 0;
    for (int fid : fids) {
        const FixtureMode *m = fixtureMode(fid);
        if (!m)
            continue;
        for (const QString &a : m->attributes()) {
            if (!attributes.isEmpty() && !attributes.contains(a))
                continue;
            // Keep a stored default's preset reference; otherwise the plain value.
            ValueRef r;
            auto f = m_defaults.constFind(fid);
            if (f != m_defaults.constEnd() && f->contains(a))
                r = f->value(a);
            else
                r.value = typeDefault(fid, a);
            putProgrammer(fid, a, r);
            ++n;
        }
    }
    if (n)
        emit changed(ProgrammerPart);
    return n;
}

double Show::resolveValue(int fid, const QString &attribute, const ValueRef &v) const
{
    double out;
    if (v.isPreset() && resolvePreset(v.presetType, v.presetId, fid, attribute, &out))
        return out;
    return v.value;
}

RawTable Show::programmerOutput() const
{
    RawTable out;
    for (auto f = m_programmer.begin(); f != m_programmer.end(); ++f)
        for (auto a = f->begin(); a != f->end(); ++a)
            out[f.key()][a.key()] = resolveValue(f.key(), a.key(), a.value());
    return out;
}

// ===========================================================================
// Sequences, cues, blueprints
// ===========================================================================

const Sequence *Show::sequence(int id) const
{
    auto it = m_sequences.constFind(id);
    return it == m_sequences.constEnd() ? nullptr : &it.value();
}

Sequence *Show::sequenceMutable(int id)
{
    auto it = m_sequences.find(id);
    return it == m_sequences.end() ? nullptr : &it.value();
}

Sequence &Show::ensureSequence(int id)
{
    auto it = m_sequences.find(id);
    if (it == m_sequences.end()) {
        Sequence s;
        s.id = id;
        s.name = tr("Sequence %1").arg(id);
        it = m_sequences.insert(id, s);
        emit changed(SequencesPart);
    }
    return it.value();
}

void Show::setSequence(const Sequence &s)
{
    m_sequences.insert(s.id, s);
    emit changed(SequencesPart | ExecutorsPart);
}

void Show::deleteSequence(int id)
{
    if (!m_sequences.remove(id))
        return;
    for (Executor &e : m_executors)
        if (e.sequenceId == id) {
            e.sequenceId = 0;
            e.state = std::make_shared<PlaybackState>();
        }
    emit changed(SequencesPart | ExecutorsPart);
}

void Show::setSelectedSequence(int id)
{
    m_selectedSequence = id;
    emit changed(SequencesPart);
}

bool Show::storeCue(int sequenceId, double number, StoreMode mode, double *storedNumber)
{
    Sequence &s = ensureSequence(sequenceId);
    if (number < 0)
        number = s.cues.isEmpty() ? 1.0 : std::floor(s.cues.last().number) + 1.0;

    int idx = s.indexOf(number);
    Cue cue;
    if (idx >= 0) {
        cue = s.cues[idx];
        if (mode == StoreMode::Overwrite) {
            cue.values.clear();
            cue.blueprints.clear();
            cue.effects.clear();
        }
    } else {
        cue.number = number;
        cue.name = tr("Cue %1").arg(cue.displayNumber());
    }
    for (auto f = m_programmer.begin(); f != m_programmer.end(); ++f)
        for (auto a = f->begin(); a != f->end(); ++a)
            cue.values[f.key()][a.key()] = a.value();
    for (const EffectInstance &inst : m_progEffects) {
        for (int i = cue.effects.size() - 1; i >= 0; --i)
            if (cue.effects[i].effectId == inst.effectId)
                cue.effects.removeAt(i);
        cue.effects.append(inst);
    }
    s.insertSorted(cue);
    if (storedNumber)
        *storedNumber = number;
    emit changed(SequencesPart | ExecutorsPart);
    return true;
}

bool Show::addEmptyCue(int sequenceId, double number, double *storedNumber)
{
    Sequence &s = ensureSequence(sequenceId);
    if (number < 0)
        number = s.cues.isEmpty() ? 1.0 : std::floor(s.cues.last().number) + 1.0;
    if (s.indexOf(number) >= 0)
        return false;
    Cue cue;
    cue.number = number;
    cue.name = tr("Cue %1").arg(cue.displayNumber());
    s.insertSorted(cue);
    if (storedNumber)
        *storedNumber = number;
    emit changed(SequencesPart | ExecutorsPart);
    return true;
}

bool Show::deleteCue(int sequenceId, double number)
{
    Sequence *s = sequenceMutable(sequenceId);
    if (!s)
        return false;
    int idx = s->indexOf(number);
    if (idx < 0)
        return false;
    s->cues.removeAt(idx);
    for (Executor &e : m_executors)
        if (e.sequenceId == sequenceId && e.pb().cueIndex >= s->cues.size())
            e.pb().cueIndex = s->cues.size() - 1;
    emit changed(SequencesPart | ExecutorsPart);
    return true;
}

bool Show::addBlueprintLine(int sequenceId, double number, const BlueprintLine &line)
{
    Sequence &s = ensureSequence(sequenceId);
    int idx = s.indexOf(number);
    if (idx < 0) {
        Cue c;
        c.number = number;
        c.name = tr("Cue %1").arg(c.displayNumber());
        s.insertSorted(c);
        idx = s.indexOf(number);
    }
    s.cues[idx].blueprints.append(line);
    emit changed(SequencesPart);
    return true;
}

QVector<int> Show::blueprintFixtures(const BlueprintLine &line) const
{
    QVector<int> out;
    const Group *g = group(line.groupId);
    if (!g)
        return out;
    const int every = std::max(1, line.every);
    for (int i = 0; i < g->fixtures.size(); ++i)
        if (i % every == line.offset % every && hasFixture(g->fixtures[i]))
            out.append(g->fixtures[i]);
    if (const Spread *m = spread(line.spreadId))
        out = m->apply(out);
    return out;
}

TimingTable Show::cueTiming(const Cue &cue) const
{
    TimingTable out;
    for (const BlueprintLine &line : cue.blueprints) {
        if (line.fadeTimes.isEmpty() && line.delayTimes.isEmpty())
            continue;
        const Spread *m = spread(line.spreadId);
        const QVector<int> fids = blueprintFixtures(line);
        // In the group's order, or the line's spread's layout (blocks, wings, direction).
        const QVector<double> frac = m ? m->timeFractions(int(fids.size())) : evenFractions(int(fids.size()));
        QStringList attrs;
        for (const PresetRef &ref : line.presets)
            if (const Preset *p = preset(ref.first, ref.second))
                for (const QString &a : p->attributes())
                    if (!attrs.contains(a))
                        attrs << a;
        if (line.dimmer >= 0 && !attrs.contains(QStringLiteral("Dimmer")))
            attrs << QStringLiteral("Dimmer");
        attrs << QStringLiteral("*");   // the line's effects
        for (int i = 0; i < fids.size(); ++i) {
            AttrTiming t;
            if (!line.fadeTimes.isEmpty())
                t.fade = alongPoints(line.fadeTimes, frac[i]);
            if (!line.delayTimes.isEmpty())
                t.delay = alongPoints(line.delayTimes, frac[i]);
            for (const QString &a : attrs)
                if (a == QLatin1String("*") || fixtureHas(fids[i], a))
                    out[fids[i]][a] = t;
        }
    }
    // The cue's own values with individual times win, as their values do.
    for (auto f = cue.values.begin(); f != cue.values.end(); ++f)
        for (auto a = f->begin(); a != f->end(); ++a) {
            if (!a->hasTiming())
                continue;
            const AttrTiming t{a->fade, a->delay};
            out[f.key()][a.key()] = t;
            if (!out[f.key()].contains(QStringLiteral("*")))   // its effects fade with its values
                out[f.key()][QStringLiteral("*")] = t;
        }
    return out;
}

int Show::dimmerPresetFor(double level, bool create)
{
    level = std::clamp(level, 0.0, 1.0);
    const int dimmer = int(Feature::Dimmer);
    for (const Preset &p : m_presets.value(dimmer))
        if (p.perFixture.isEmpty() && p.perType.isEmpty() && p.universal.size() == 1 && p.universal.contains("Dimmer")
            && std::fabs(p.universal.value("Dimmer") - level) < 0.0005)
            return p.id;
    if (!create)
        return 0;
    int id = 1;
    while (m_presets.value(dimmer).contains(id))
        ++id;
    Preset p;
    p.type = dimmer;
    p.id = id;
    p.name = level >= 0.9995 ? tr("Full") : level <= 0.0005 ? tr("Out") : QStringLiteral("%1%").arg(std::lround(level * 100));
    p.universal.insert("Dimmer", level);
    m_presets[dimmer].insert(id, p);
    emit changed(PresetsPart);
    return id;
}

RawTable Show::evaluateCue(const Cue &cue) const
{
    RawTable out;
    for (const BlueprintLine &line : cue.blueprints) {
        const QVector<int> fids = blueprintFixtures(line);
        for (const PresetRef &ref : line.presets) {
            const Preset *p = preset(ref.first, ref.second);
            if (!p)
                continue;
            const QStringList attrs = p->attributes();
            for (int fid : fids)
                for (const QString &a : attrs) {
                    double v;
                    if (fixtureHas(fid, a) && presetValue(*p, fid, a, &v))
                        out[fid][a] = v;
                }
        }
        if (line.dimmer >= 0)
            for (int fid : fids)
                if (fixtureHas(fid, "Dimmer"))
                    out[fid]["Dimmer"] = line.dimmer;
    }
    for (auto f = cue.values.begin(); f != cue.values.end(); ++f)
        for (auto a = f->begin(); a != f->end(); ++a)
            out[f.key()][a.key()] = resolveValue(f.key(), a.key(), a.value());
    return out;
}

RawTable Show::trackedState(const Sequence &s, int index) const
{
    RawTable out;
    if (index < 0 || index >= s.cues.size())
        return out;
    const int first = s.tracking ? 0 : index;
    for (int i = first; i <= index; ++i) {
        RawTable c = evaluateCue(s.cues[i]);
        for (auto f = c.begin(); f != c.end(); ++f)
            for (auto a = f->begin(); a != f->end(); ++a)
                out[f.key()][a.key()] = a.value();
    }
    return out;
}

QMap<int, QHash<QString, PresetRef>> Show::trackedPresets(const Sequence &s, int index) const
{
    QMap<int, QHash<QString, PresetRef>> out;
    if (index < 0 || index >= s.cues.size())
        return out;
    for (int i = s.tracking ? 0 : index; i <= index; ++i) {
        const Cue &cue = s.cues[i];
        // Same order as evaluateCue: blueprint presets, blueprint dimmer, then the cue's own values.
        for (const BlueprintLine &line : cue.blueprints) {
            const QVector<int> fids = blueprintFixtures(line);
            for (const PresetRef &ref : line.presets) {
                const Preset *p = preset(ref.first, ref.second);
                if (!p)
                    continue;
                const QStringList attrs = p->attributes();
                for (int fid : fids)
                    for (const QString &a : attrs) {
                        double v;
                        if (fixtureHas(fid, a) && presetValue(*p, fid, a, &v))
                            out[fid][a] = ref;
                    }
            }
            if (line.dimmer >= 0)
                for (int fid : fids)
                    if (fixtureHas(fid, "Dimmer"))
                        out[fid]["Dimmer"] = PresetRef(0, 0);
        }
        for (auto f = cue.values.begin(); f != cue.values.end(); ++f)
            for (auto a = f->begin(); a != f->end(); ++a)
                out[f.key()][a.key()] = a->isPreset() ? PresetRef(a->presetType, a->presetId) : PresetRef(0, 0);
    }
    return out;
}

int Show::removeFromCue(int sequenceId, double number, const QVector<int> &fids)
{
    Sequence *s = sequenceMutable(sequenceId);
    int idx = s ? s->indexOf(number) : -1;
    if (idx < 0)
        return 0;
    int removed = 0;
    for (int fid : fids)
        removed += s->cues[idx].values.remove(fid);
    if (removed)
        emit changed(SequencesPart);
    return removed;
}

bool Show::renumberSequence(int from, int to)
{
    if (from == to || !m_sequences.contains(from) || m_sequences.contains(to) || to <= 0)
        return false;
    Sequence s = m_sequences.take(from);
    s.id = to;
    m_sequences.insert(to, s);
    for (Executor &e : m_executors)
        if (e.sequenceId == from)
            e.sequenceId = to;
    if (m_selectedSequence == from)
        m_selectedSequence = to;
    emit changed(SequencesPart | ExecutorsPart);
    return true;
}

bool Show::buildCue(int sequenceId, double number)
{
    Sequence *s = sequenceMutable(sequenceId);
    if (!s)
        return false;
    int idx = s->indexOf(number);
    if (idx < 0)
        return false;
    Cue &cue = s->cues[idx];
    if (cue.blueprints.isEmpty())
        return false;
    // Blueprint output becomes hard values; preset references are kept so the cue still follows preset edits.
    ValueTable built;
    for (const BlueprintLine &line : cue.blueprints) {
        const QVector<int> fids = blueprintFixtures(line);
        for (const PresetRef &ref : line.presets) {
            const Preset *p = preset(ref.first, ref.second);
            if (!p)
                continue;
            for (int fid : fids)
                for (const QString &a : p->attributes()) {
                    double v;
                    if (fixtureHas(fid, a) && presetValue(*p, fid, a, &v))
                        built[fid][a] = ValueRef{v, ref.first, ref.second};
                }
        }
        if (line.dimmer >= 0)
            for (int fid : fids)
                if (fixtureHas(fid, "Dimmer"))
                    built[fid]["Dimmer"] = ValueRef{line.dimmer, 0, 0};
    }
    // A line's spread times become the values' own individual times.
    const TimingTable times = cueTiming(cue);
    for (auto f = built.begin(); f != built.end(); ++f) {
        const QHash<QString, AttrTiming> ft = times.value(f.key());
        for (auto a = f->begin(); a != f->end(); ++a) {
            const auto t = ft.constFind(a.key());
            if (t != ft.constEnd()) {
                a->fade = t->fade;
                a->delay = t->delay;
            }
        }
    }
    // Existing hard values win, as they did during playback.
    for (auto f = cue.values.begin(); f != cue.values.end(); ++f)
        for (auto a = f->begin(); a != f->end(); ++a)
            built[f.key()][a.key()] = a.value();
    cue.values = built;
    cue.blueprints.clear();
    emit changed(SequencesPart);
    return true;
}

// ===========================================================================
// Effects
// ===========================================================================

const Effect *Show::effect(int id) const
{
    auto it = m_effects.constFind(id);
    return it == m_effects.constEnd() ? nullptr : &it.value();
}

void Show::setEffect(const Effect &e)
{
    Effect copy = e;
    if (copy.spreadId < 0)
        copy.spreadId = spreadForLayout(copy.ownLayout(), QString());
    m_effects.insert(copy.id, copy);
    emit changed(EffectsPart);
}

int Show::spreadForLayout(const Spread &layout, const QString &name)
{
    auto same = [](const Spread &a, const Spread &b) {
        return a.every == b.every && a.offset == b.offset && a.blocks == b.blocks && a.groups == b.groups && a.wings == b.wings
               && a.direction == b.direction && std::fabs(a.phaseFrom - b.phaseFrom) < 1e-6 && std::fabs(a.phaseTo - b.phaseTo) < 1e-6;
    };
    for (const Spread &m : m_spreads)
        if (same(m, layout))
            return m.id;
    Spread m = layout;
    m.id = 1;
    while (m_spreads.contains(m.id))
        ++m.id;
    if (!name.isEmpty()) {
        m.name = name;
    } else {
        QStringList parts;
        parts << (layout.phaseTo == layout.phaseFrom ? tr("Together") : tr("Chase %1°").arg(layout.phaseTo - layout.phaseFrom));
        if (layout.blocks > 1)
            parts << tr("Blocks %1").arg(layout.blocks);
        if (layout.groups > 0)
            parts << tr("Groups %1").arg(layout.groups);
        if (layout.wings > 1)
            parts << tr("Wings %1").arg(layout.wings);
        if (layout.direction != SpreadDirection::Forward)
            parts << spreadDirectionName(layout.direction);
        m.name = parts.join(QStringLiteral(" · "));
    }
    m_spreads.insert(m.id, m);
    emit changed(SpreadsPart);
    return m.id;
}

void Show::deleteEffect(int id)
{
    if (!m_effects.remove(id))
        return;
    removeEffect(id);
    emit changed(EffectsPart | SequencesPart);
}

QStringList Show::blueprintProblems(const BlueprintLine &line) const
{
    QStringList out;
    if (line.groupId && !m_groups.contains(line.groupId))
        out << tr("Group %1 was deleted").arg(line.groupId);
    for (const PresetRef &r : line.presets)
        if (!preset(r.first, r.second))
            out << tr("Preset %1 was deleted").arg(presetRef(r.first, r.second));
    for (int id : line.effects)
        if (!m_effects.contains(id))
            out << tr("Effect %1 was deleted").arg(id);
    if (line.spreadId && !m_spreads.contains(line.spreadId))
        out << tr("Spread %1 was deleted").arg(line.spreadId);
    return out;
}

QStringList Show::cueProblems(const Cue &cue) const
{
    QStringList out;
    for (int i = 0; i < cue.blueprints.size(); ++i)
        for (const QString &p : blueprintProblems(cue.blueprints[i]))
            out << tr("Blueprint line %1: %2").arg(i + 1).arg(p);
    QSet<PresetRef> missing;
    for (const AttrValues &attrs : cue.values)
        for (const ValueRef &v : attrs)
            if (v.isPreset() && !preset(v.presetType, v.presetId))
                missing.insert({v.presetType, v.presetId});
    for (const PresetRef &r : missing)
        out << tr("Values use Preset %1, which was deleted").arg(presetRef(r.first, r.second));
    for (const EffectInstance &e : cue.effects)
        if (!m_effects.contains(e.effectId))
            out << tr("Effect %1 was deleted").arg(e.effectId);
    return out;
}

bool Show::renumberEffect(int from, int to)
{
    if (from == to || !m_effects.contains(from) || m_effects.contains(to) || to < 1)
        return false;
    Effect e = m_effects.take(from);
    e.id = to;
    m_effects.insert(to, e);
    auto fix = [&](QVector<EffectInstance> &list) {
        for (EffectInstance &i : list)
            if (i.effectId == from)
                i.effectId = to;
    };
    for (auto &pool : m_presets)
        for (Preset &p : pool)
            fix(p.effects);
    for (Sequence &sq : m_sequences)
        for (Cue &c : sq.cues) {
            fix(c.effects);
            for (BlueprintLine &r : c.blueprints)
                for (int &id : r.effects)
                    if (id == from)
                        id = to;
        }
    fix(m_progEffects);
    emit changed(EffectsPart | PresetsPart | SequencesPart | ProgrammerPart);
    return true;
}

bool Show::renumberPreset(int type, int from, int to)
{
    auto &pool = m_presets[type];
    if (from == to || !pool.contains(from) || pool.contains(to) || to < 1)
        return false;
    Preset p = pool.take(from);
    p.id = to;
    pool.insert(to, p);
    auto fixTable = [&](ValueTable &table) {
        for (auto &attrs : table)
            for (ValueRef &v : attrs)
                if (v.presetType == type && v.presetId == from)
                    v.presetId = to;
    };
    for (Sequence &sq : m_sequences)
        for (Cue &c : sq.cues) {
            fixTable(c.values);
            for (BlueprintLine &r : c.blueprints)
                for (PresetRef &ref : r.presets)
                    if (ref.first == type && ref.second == from)
                        ref.second = to;
        }
    for (Effect &e : m_effects)
        for (EffectLayer &l : e.layers)
            for (EffectPoint &pt : l.points)
                if (pt.kind == EffectPoint::Preset && pt.presetType == type && pt.presetId == from)
                    pt.presetId = to;
    fixTable(m_programmer);
    fixTable(m_defaults);
    emit changed(PresetsPart | SequencesPart | EffectsPart | ProgrammerPart);
    return true;
}

namespace {
template <typename Table>
Table remapKeys(const Table &t, const QMap<int, int> &map)
{
    Table out;
    for (auto it = t.begin(); it != t.end(); ++it)
        out.insert(map.value(it.key(), it.key()), it.value());
    return out;
}
void remapList(QVector<int> &list, const QMap<int, int> &map)
{
    for (int &fid : list)
        fid = map.value(fid, fid);
}
} // namespace

bool Show::renumberFixtures(const QMap<int, int> &oldToNew, QString *error)
{
    QMap<int, int> map;
    for (auto it = oldToNew.begin(); it != oldToNew.end(); ++it)
        if (it.key() != it.value() && m_fixtures.contains(it.key()))
            map.insert(it.key(), it.value());
    if (map.isEmpty())
        return true;
    QSet<int> taken;
    for (int fid : m_fixtures.keys())
        if (!map.contains(fid))
            taken.insert(fid);
    for (int to : map) {
        if (to < 1 || to > kMaxFixtureId || taken.contains(to)) {
            if (error)
                *error = to < 1 ? tr("IDs start at 1")
                                : to > kMaxFixtureId ? tr("Fixture IDs go up to %1").arg(kMaxFixtureId)
                                                     : tr("ID %1 is already used").arg(to);
            return false;
        }
        taken.insert(to);
    }
    QMap<int, Fixture> fixtures;
    for (Fixture f : m_fixtures) {
        f.fid = map.value(f.fid, f.fid);
        fixtures.insert(f.fid, f);
    }
    // Cells move with their master: 101.3 becomes 205.3.
    for (const Fixture &c : std::as_const(m_cells))
        if (map.contains(c.master))
            map.insert(c.fid, cellId(map.value(c.master), c.cell));
    m_fixtures = fixtures;
    rebuildCells();
    for (Group &g : m_groups)
        remapList(g.fixtures, map);
    for (auto &pool : m_presets)
        for (Preset &p : pool) {
            p.perFixture = remapKeys(p.perFixture, map);
            for (EffectInstance &e : p.effects)
                remapList(e.fixtures, map);
        }
    for (Sequence &sq : m_sequences)
        for (Cue &c : sq.cues) {
            c.values = remapKeys(c.values, map);
            for (EffectInstance &e : c.effects)
                remapList(e.fixtures, map);
        }
    for (EffectInstance &e : m_progEffects)
        remapList(e.fixtures, map);
    for (Executor &e : m_executors) {
        e.pb().from = remapKeys(e.pb().from, map);
        e.pb().to = remapKeys(e.pb().to, map);
        e.pb().current = remapKeys(e.pb().current, map);
    }
    remapList(m_selection, map);
    remapList(m_stepList, map);
    remapList(m_lastSelection, map);
    m_parked = remapKeys(m_parked, map);
    m_programmer = remapKeys(m_programmer, map);
    m_defaults = remapKeys(m_defaults, map);
    m_lastOutput = remapKeys(m_lastOutput, map);
    emit changed(PatchPart | GroupsPart | PresetsPart | SequencesPart | ProgrammerPart | EffectsPart);
    return true;
}

bool Show::renumberGroup(int from, int to)
{
    if (from == to || !m_groups.contains(from) || m_groups.contains(to) || to < 1)
        return false;
    Group g = m_groups.take(from);
    g.id = to;
    m_groups.insert(to, g);
    for (Sequence &sq : m_sequences)
        for (Cue &c : sq.cues)
            for (BlueprintLine &r : c.blueprints)
                if (r.groupId == from)
                    r.groupId = to;
    emit changed(GroupsPart | SequencesPart);
    return true;
}

int Show::nextFreeEffectId() const
{
    int id = 1;
    while (m_effects.contains(id))
        ++id;
    return id;
}

const Shape *Show::shape(int id) const
{
    auto it = m_shapes.constFind(id);
    return it == m_shapes.constEnd() ? nullptr : &it.value();
}

void Show::setShape(const Shape &s)
{
    m_shapes.insert(s.id, s);
    emit changed(EffectsPart);
}

int Show::nextFreeShapeId() const
{
    int id = 1;
    while (m_shapes.contains(id))
        ++id;
    return id;
}

bool Show::applyEffect(int effectId, double rate, double size, double spread)
{
    const Effect *e = effect(effectId);
    if (!e)
        return false;
    const QVector<int> sel = workingSelection();
    if (sel.isEmpty()) {
        emit message(tr("Select fixtures before running an effect"));
        return false;
    }
    const QList<Feature> features = e->features();
    // One effect per feature per fixture: the new one takes these fixtures from older ones.
    for (int i = m_progEffects.size() - 1; i >= 0; --i) {
        EffectInstance &other = m_progEffects[i];
        const Effect *oe = effect(other.effectId);
        bool overlaps = other.effectId == effectId;
        if (oe)
            for (Feature f : oe->features())
                overlaps |= features.contains(f);
        if (!overlaps)
            continue;
        for (int fid : sel)
            other.fixtures.removeAll(fid);
        if (other.fixtures.isEmpty())
            m_progEffects.removeAt(i);
    }
    EffectInstance inst;
    inst.effectId = effectId;
    inst.fixtures = sel;
    if (!m_activeSpread.isDefault()) {   // the spread lays the effect out
        inst.hasLayout = true;
        inst.layout = m_activeSpread;
    }
    inst.rate = rate;
    inst.size = size;
    inst.spread = spread;
    m_progEffects.append(inst);
    emit changed(ProgrammerPart | EffectsPart);
    return true;
}

QVector<int> Show::presetEffectFixtures(const EffectInstance &spec, const QVector<int> &candidates) const
{
    QVector<int> out;
    for (int fid : candidates) {
        if (spec.scope == EffectInstance::Selective && !spec.fixtures.contains(fid))
            continue;
        if (spec.scope == EffectInstance::Global) {
            const Fixture *fx = fixture(fid);
            if (!fx || !spec.types.contains(fx->typeId))
                continue;
        }
        out << fid;
    }
    return out;
}

bool Show::applyEffectInstance(const EffectInstance &spec, const QVector<int> &fids)
{
    const Effect *e = effect(spec.effectId);
    if (!e || fids.isEmpty())
        return false;
    const QList<Feature> features = e->features();
    for (int i = m_progEffects.size() - 1; i >= 0; --i) {
        EffectInstance &other = m_progEffects[i];
        const Effect *oe = effect(other.effectId);
        bool overlaps = other.effectId == spec.effectId;
        if (oe)
            for (Feature f : oe->features())
                overlaps |= features.contains(f);
        if (!overlaps)
            continue;
        for (int fid : fids)
            other.fixtures.removeAll(fid);
        if (other.fixtures.isEmpty())
            m_progEffects.removeAt(i);
    }
    EffectInstance inst = spec;
    inst.fixtures = fids;
    inst.scope = EffectInstance::Universal;
    inst.types.clear();
    if (!inst.hasLayout && !m_activeSpread.isDefault()) {   // the preset's own spread wins
        inst.hasLayout = true;
        inst.layout = m_activeSpread;
    }
    m_progEffects.append(inst);
    emit changed(ProgrammerPart | EffectsPart);
    return true;
}

void Show::setProgrammerEffect(int index, const EffectInstance &inst)
{
    if (index < 0 || index >= m_progEffects.size())
        return;
    m_progEffects[index] = inst;
    emit changed(ProgrammerPart | EffectsPart);
}

void Show::removeProgrammerEffect(int index)
{
    if (index < 0 || index >= m_progEffects.size())
        return;
    m_progEffects.removeAt(index);
    emit changed(ProgrammerPart | EffectsPart);
}

void Show::removeEffectsFrom(const QVector<int> &fids)
{
    for (int i = m_progEffects.size() - 1; i >= 0; --i) {
        for (int fid : fids)
            m_progEffects[i].fixtures.removeAll(fid);
        if (m_progEffects[i].fixtures.isEmpty())
            m_progEffects.removeAt(i);
    }
    emit changed(ProgrammerPart | EffectsPart);
}

void Show::removeEffect(int effectId)
{
    for (int i = m_progEffects.size() - 1; i >= 0; --i)
        if (m_progEffects[i].effectId == effectId)
            m_progEffects.removeAt(i);
    emit changed(ProgrammerPart | EffectsPart);
}

QVector<EffectInstance> Show::cueEffects(const Cue &cue) const
{
    QVector<EffectInstance> out = cue.effects;
    for (const BlueprintLine &line : cue.blueprints) {
        const QVector<int> fids = blueprintFixtures(line);
        if (fids.isEmpty())
            continue;
        const Spread *m = spread(line.spreadId);
        // Effects stored in the line's presets play on the line's fixtures (as far as each
        // preset's layer reaches), with their own spread unless the line gives one.
        for (const auto &ref : line.presets) {
            const Preset *pr = preset(ref.first, ref.second);
            if (!pr)
                continue;
            for (const EffectInstance &spec : pr->effects) {
                EffectInstance inst = spec;
                inst.fixtures = presetEffectFixtures(spec, fids);
                if (inst.fixtures.isEmpty())
                    continue;
                inst.scope = EffectInstance::Universal;
                inst.types.clear();
                if (m) {
                    inst.hasLayout = true;
                    inst.layout = *m;
                }
                out.append(inst);
            }
        }
        for (int id : line.effects) {
            EffectInstance inst;
            inst.effectId = id;
            inst.fixtures = fids;
            if (m) {   // the line's spread lays the effect out
                inst.hasLayout = true;
                inst.layout = *m;
            }
            out.append(inst);
        }
    }
    return out;
}

// ===========================================================================
// Executors and playback
// ===========================================================================

void Show::setPage(int page)
{
    m_page = std::max(1, page);
    emit changed(ExecutorsPart);
}

QString Show::pageLabel(int page) const
{
    const QString n = m_pageNames.value(page);
    return n.isEmpty() ? tr("Page %1").arg(page) : n;
}

void Show::setPageName(int page, const QString &name)
{
    if (page < 1)
        return;
    if (name.trimmed().isEmpty())
        m_pageNames.remove(page);
    else
        m_pageNames.insert(page, name.trimmed());
    emit changed(ExecutorsPart);
}

QList<int> Show::pages() const
{
    QSet<int> set{m_page, 1};
    for (auto it = m_pageNames.begin(); it != m_pageNames.end(); ++it)
        set.insert(it.key());
    for (const Executor &e : m_executors)
        set.insert(e.page);
    QList<int> out = set.values();
    std::sort(out.begin(), out.end());
    return out;
}

Executor *Show::executor(int page, int number)
{
    auto it = m_executors.find(execKey(page, number));
    return it == m_executors.end() ? nullptr : &it.value();
}

Executor &Show::ensureExecutor(int page, int number)
{
    auto it = m_executors.find(execKey(page, number));
    if (it == m_executors.end()) {
        Executor e;
        e.page = page;
        e.number = number;
        if (executorKind(number) == ExecKind::Encoder)
            e.buttons[0] = ButtonFn::Toggle;   // the push: on and off
        // Faders and buttons: one key, Go.
        it = m_executors.insert(execKey(page, number), e);
    }
    return it.value();
}

QList<Executor *> Show::allExecutors()
{
    QList<Executor *> out;
    for (Executor &e : m_executors)
        out << &e;
    return out;
}

bool Show::assignSequence(int sequenceId, int page, int number)
{
    ensureSequence(sequenceId);
    Executor &e = ensureExecutor(page, number);
    if (e.sequenceId != sequenceId)
        e.state = std::make_shared<PlaybackState>();
    e.sequenceId = sequenceId;
    e.rateMaster = 0;
    linkPlaybacks();
    emit changed(ExecutorsPart);
    return true;
}

// ---- Rate masters -------------------------------------------------------------

const RateMaster *Show::rateMaster(int id) const
{
    return id >= 1 && id <= m_rateMasters.size() ? &m_rateMasters[id - 1] : nullptr;
}

void Show::setRateMaster(const RateMaster &m)
{
    if (m.id < 1 || m.id > m_rateMasters.size())
        return;
    m_rateMasters[m.id - 1] = m;
    for (Executor &e : m_executors)
        if (e.rateMaster == m.id)
            e.fader = m.faderPosition();
    emit changed(EffectsPart | ExecutorsPart);
}

bool Show::assignRateMaster(int id, int page, int number)
{
    const RateMaster *m = rateMaster(id);
    if (!m)
        return false;
    Executor &e = ensureExecutor(page, number);
    if (e.sequenceId && e.pb().isOn())
        off(e, true);
    e.state = std::make_shared<PlaybackState>();
    e.sequenceId = 0;
    e.rateMaster = id;
    e.fader = m->faderPosition();
    linkPlaybacks();
    emit changed(ExecutorsPart);
    return true;
}

void Show::tapRateMaster(int id)
{
    if (id < 1 || id > m_rateMasters.size())
        return;
    RateMaster m = m_rateMasters[id - 1];
    if (m.tap(QDateTime::currentMSecsSinceEpoch()))
        m.frozen = false;
    setRateMaster(m);
}

void Show::resetRateMaster(int id)
{
    if (const RateMaster *cur = rateMaster(id)) {
        RateMaster m = *cur;
        m.value = m.defaultValue();
        m.frozen = false;
        setRateMaster(m);
    }
}

void Show::toggleRateMasterFreeze(int id)
{
    if (const RateMaster *cur = rateMaster(id)) {
        RateMaster m = *cur;
        m.frozen = !m.frozen;
        setRateMaster(m);
    }
}

void Show::linkPlaybacks()
{
    // One playback per sequence: the running one if any, so a second executor joins in.
    QHash<int, std::shared_ptr<PlaybackState>> bySeq;
    for (Executor &e : m_executors)
        if (e.sequenceId && e.pb().isOn() && !bySeq.contains(e.sequenceId))
            bySeq.insert(e.sequenceId, e.state);
    for (Executor &e : m_executors) {
        if (!e.sequenceId)
            continue;
        std::shared_ptr<PlaybackState> &shared = bySeq[e.sequenceId];
        if (!shared)
            shared = e.state;
        e.state = shared;
    }
}

QList<Executor *> Show::executorsOf(int sequenceId)
{
    QList<Executor *> out;
    for (Executor &e : m_executors)
        if (sequenceId && e.sequenceId == sequenceId)
            out << &e;
    return out;
}

bool Show::anySwopHeld() const
{
    for (const Executor &e : m_executors)
        if (e.swopHeld && e.pb().isOn())
            return true;
    return false;
}

bool Show::setExecutorKind(int page, int number, ExecKind kind)
{
    if (!executorKindAssignable(number))
        return false;
    Executor &e = ensureExecutor(page, number);
    if (e.kind == kind)
        return true;
    e.kind = kind;
    if (kind == ExecKind::Fader) {
        e.buttons[0] = ButtonFn::Go;   // a fader has one key
    } else {
        e.buttons[0] = kind == ExecKind::Button ? ButtonFn::Go : ButtonFn::Toggle;
        if (kind == ExecKind::Button) {
            e.fader = 1.0;   // nothing to pull it down with
            e.faderFn = FaderFn::Master;
        }
    }
    emit changed(ExecutorsPart);
    return true;
}

void Show::unassign(int page, int number)
{
    if (Executor *e = executor(page, number)) {
        e->sequenceId = 0;
        e->rateMaster = 0;
        e->state = std::make_shared<PlaybackState>();   // the sequence plays on elsewhere
        emit changed(ExecutorsPart);
    }
}

Executor *Show::executorForSequence(int sequenceId)
{
    for (Executor &e : m_executors)
        if (e.page == m_page && e.sequenceId == sequenceId)
            return &e;
    for (Executor &e : m_executors)
        if (e.sequenceId == sequenceId)
            return &e;
    return nullptr;
}

double Show::cueValueLookupFrom(const Executor &e, int fid, const QString &attr) const
{
    auto f = e.pb().current.constFind(fid);
    if (f != e.pb().current.constEnd() && f->contains(attr))
        return f->value(attr);
    if (isIntensity(attr))
        return 0.0;
    auto o = m_lastOutput.constFind(fid);
    if (o != m_lastOutput.constEnd() && o->contains(attr))
        return o->value(attr);
    return defaultValue(fid, attr);
}

void Show::startFade(Executor &e, int targetIndex, double fade, double delay, bool individual)
{
    const Sequence *s = sequence(e.sequenceId);
    if (!s || targetIndex < 0 || targetIndex >= s->cues.size())
        return;
    PlaybackState &pb = e.pb();
    RawTable to = trackedState(*s, targetIndex);
    RawTable from = pb.current;
    for (auto f = to.begin(); f != to.end(); ++f)
        for (auto a = f->begin(); a != f->end(); ++a)
            if (!from.value(f.key()).contains(a.key()))
                from[f.key()][a.key()] = cueValueLookupFrom(e, f.key(), a.key());
    pb.from = from;
    pb.to = to;
    pb.fromCueIndex = pb.cueIndex;
    pb.cueIndex = targetIndex;
    pb.fadeTime = std::max(0.0, fade);
    pb.delayTime = std::max(0.0, delay);
    pb.timing = individual ? cueTiming(s->cues[targetIndex]) : TimingTable();
    pb.duration = pb.fadeTime + pb.delayTime;
    for (const auto &f : std::as_const(pb.timing))
        for (const AttrTiming &t : f)
            pb.duration = std::max(pb.duration, (t.delay >= 0 ? t.delay : pb.delayTime) + (t.fade >= 0 ? t.fade : pb.fadeTime));
    pb.elapsed = 0.0;
    pb.fading = true;
    pb.paused = false;
    pb.goingOff = false;
    pb.xfadeTarget = -1;
    pb.sinceGo = 0.0;
    pb.stamp = ++m_stamp;
    emit changed(ExecutorsPart);
}

void Show::go(Executor &e)
{
    const Sequence *s = sequence(e.sequenceId);
    if (!s || s->cues.isEmpty())
        return;
    m_lastGoSequence = s->id;
    if (e.pb().paused && e.pb().fading) {   // Go resumes a paused fade
        e.pb().paused = false;
        emit changed(ExecutorsPart);
        return;
    }
    int next = e.pb().cueIndex + 1;
    if (next >= s->cues.size()) {
        if (!s->wrap) {
            // Go on the last cue with the Off line waiting for Go: that Go turns it off.
            if (s->offTrigger == CueTrigger::Go && e.pb().isOn() && !e.pb().goingOff)
                off(e);
            return;
        }
        next = 0;
    }
    startFade(e, next, s->cues[next].fade, s->cues[next].delay);
}

void Show::goBack(Executor &e)
{
    const Sequence *s = sequence(e.sequenceId);
    if (!s || e.pb().cueIndex <= 0)
        return;
    int prev = e.pb().cueIndex - 1;
    startFade(e, prev, std::min(s->cues[prev].fade, 2.0), 0.0, false);
}

void Show::gotoCue(Executor &e, int index, bool useFade)
{
    const Sequence *s = sequence(e.sequenceId);
    if (!s || index < 0 || index >= s->cues.size())
        return;
    startFade(e, index, useFade ? s->cues[index].fade : 0.0, useFade ? s->cues[index].delay : 0.0, useFade);
}

void Show::off(Executor &e, bool immediately)
{
    // The sequence's Off line: fade out (intensity down, the rest holds) over its time.
    const Sequence *s = sequence(e.sequenceId);
    if (!immediately && s && e.pb().isOn() && !e.pb().goingOff && (s->offFade > 0 || s->offDelay > 0)) {
        PlaybackState &pb = e.pb();
        pb.from = pb.current;
        pb.to.clear();
        pb.fromCueIndex = pb.cueIndex;
        pb.fadeTime = s->offFade;
        pb.delayTime = s->offDelay;
        pb.timing.clear();
        pb.duration = s->offFade + s->offDelay;
        pb.elapsed = 0.0;
        pb.fading = true;
        pb.paused = false;
        pb.xfadeTarget = -1;
        pb.goingOff = true;
        emit changed(ExecutorsPart);
        return;
    }
    quint64 stamp = e.pb().stamp;
    e.pb() = PlaybackState();
    e.pb().stamp = stamp;
    e.flashStartedPlayback = false;
    emit changed(ExecutorsPart);
}

void Show::togglePause(Executor &e)
{
    if (!e.pb().fading)
        return;
    e.pb().paused = !e.pb().paused;
    emit changed(ExecutorsPart);
}

void Show::releaseFlash(Executor &e, bool useOffTime)
{
    // Flash and Swop let go at once; Temp goes out with the sequence's Off line times.
    if (e.flashStartedPlayback && !e.flashHeld && !e.tempHeld && !e.swopHeld)
        off(e, !useOffTime);
}

void Show::pressButton(Executor &e, int button, bool down)
{
    if (button < 0 || button > 2)
        return;
    e.buttonDown[button] = down;
    if (e.rateMaster) {
        // A rate master's buttons: Learn (tap tempo), Reset, Freeze.
        if (down) {
            if (button == 0)
                tapRateMaster(e.rateMaster);
            else if (button == 1)
                resetRateMaster(e.rateMaster);
            else
                toggleRateMasterFreeze(e.rateMaster);
        }
        emit changed(ExecutorsPart);
        return;
    }
    const ButtonFn fn = e.buttons[button];
    auto startForHold = [&]() {
        if (!e.pb().isOn()) {
            gotoCue(e, 0, false);
            e.flashStartedPlayback = true;
        }
    };
    switch (fn) {
    case ButtonFn::Go:
        if (down) go(e);
        break;
    case ButtonFn::GoBack:
        if (down) goBack(e);
        break;
    case ButtonFn::Pause:
        if (down) togglePause(e);
        break;
    case ButtonFn::Flash:
        e.flashHeld = down;
        if (down) startForHold(); else releaseFlash(e);
        break;
    case ButtonFn::Temp:
        // Like Flash, but in and out with the times: the first cue's fade, then the Off line's.
        e.tempHeld = down;
        if (down) {
            if (!e.pb().isOn() || e.pb().goingOff) {
                gotoCue(e, 0, true);
                e.flashStartedPlayback = true;
            }
        } else {
            releaseFlash(e, true);
        }
        break;
    case ButtonFn::Swop:
        e.swopHeld = down;
        if (down) startForHold(); else releaseFlash(e);
        break;
    case ButtonFn::Toggle:
        if (down) {
            if (e.pb().isOn()) off(e); else go(e);
        }
        break;
    case ButtonFn::On:
        if (down && !e.pb().isOn()) go(e);
        break;
    case ButtonFn::Off:
        if (down) off(e);
        break;
    case ButtonFn::Top:   // back to the first cue, in its fade
        if (down) gotoCue(e, 0, true);
        break;
    case ButtonFn::Select:
        if (down) {
            QVector<int> fids;
            const Sequence *s = sequence(e.sequenceId);
            if (s) {
                RawTable st = trackedState(*s, std::max(0, e.pb().cueIndex));
                fids = st.keys().toVector();
            }
            select(fids);
        }
        break;
    case ButtonFn::None:
        break;
    }
    emit changed(ExecutorsPart);
}

void Show::setFader(Executor &e, double v)
{
    v = std::clamp(v, 0.0, 1.0);
    if (e.rateMaster) {
        if (const RateMaster *cur = rateMaster(e.rateMaster)) {
            RateMaster m = *cur;
            m.setFromFader(v);
            m_rateMasters[m.id - 1] = m;
            for (Executor &x : m_executors)
                if (x.rateMaster == m.id)
                    x.fader = v;
            emit changed(EffectsPart | ExecutorsPart);
        }
        return;
    }
    const double old = e.fader;
    e.fader = v;
    const Sequence *s = sequence(e.sequenceId);

    if (e.faderFn == FaderFn::XFade) {
        if (!s || s->cues.isEmpty())
            return;
        if (!e.pb().isOn()) {
            gotoCue(e, 0, false);
            e.pb().xfadeStart = old;
        }
        double prog = std::fabs(v - e.pb().xfadeStart);
        if (e.pb().xfadeTarget < 0 && prog > kEps) {
            int next = e.pb().cueIndex + 1;
            if (next >= s->cues.size())
                next = s->wrap ? 0 : -1;
            if (next < 0)
                return;
            double start = e.pb().xfadeStart;
            // The fader runs the whole fade: individual times keep their proportions.
            if (cueTiming(s->cues[next]).isEmpty())
                startFade(e, next, 1.0, 0.0, false);
            else
                startFade(e, next, s->cues[next].fade, s->cues[next].delay);
            e.pb().xfadeTarget = next;
            e.pb().xfadeStart = start;
        }
        if (e.pb().xfadeTarget >= 0) {
            e.pb().elapsed = prog;
            if (prog >= 1.0 - kEps) {
                e.pb().elapsed = 1.0;
                e.pb().xfadeTarget = -1;
                e.pb().xfadeStart = v > 0.5 ? 1.0 : 0.0;
            }
        }
        emit changed(ExecutorsPart);
        return;
    }

    // Master faders of one sequence move together (it's one playback).
    if (e.faderFn == FaderFn::Master && e.sequenceId)
        for (Executor *o : executorsOf(e.sequenceId))
            if (o != &e && o->faderFn == FaderFn::Master && executorKind(o->number, o) == ExecKind::Fader)
                o->fader = v;

    // Master / Temp: optional auto start and stop
    const bool temp = e.faderFn == FaderFn::Temp;
    if ((e.autoStart || temp) && old <= kEps && v > kEps && !e.pb().isOn() && s && !s->cues.isEmpty())
        gotoCue(e, 0, false);
    else if ((e.autoStop || temp) && v <= kEps && e.pb().isOn() && !e.flashHeld)
        off(e, true);
    emit changed(ExecutorsPart);
}

// ===========================================================================
// Masters
// ===========================================================================

void Show::setGrandMaster(double v)
{
    m_grandMaster = std::clamp(v, 0.0, 1.0);
    emit changed(SettingsPart);
}

void Show::setBlackout(bool on)
{
    m_blackout = on;
    emit changed(SettingsPart);
}

void Show::setHighlight(bool on)
{
    m_highlight = on;
    emit changed(SettingsPart);
}

// ===========================================================================
// Persistence
// ===========================================================================

QJsonObject Show::toJson() const
{
    QSet<QString> typeIds;
    QJsonArray fixtures;
    for (const Fixture &f : m_fixtures) {
        fixtures.append(f.toJson());
        typeIds.insert(f.typeId);
    }
    // Every fixture type the show uses or brought in, so the file opens anywhere.
    for (const FixtureType &t : m_library.showTypes())
        typeIds.insert(t.id);
    QJsonArray types;
    for (const QString &id : typeIds)
        if (const FixtureType *t = m_library.find(id))
            types.append(t->toJson());
    QJsonArray groups;
    for (const Group &g : m_groups)
        groups.append(g.toJson());
    QJsonArray presets;
    for (const auto &pool : m_presets)
        for (const Preset &p : pool)
            presets.append(p.toJson());
    QJsonArray sequences;
    for (const Sequence &s : m_sequences)
        sequences.append(s.toJson());
    QJsonArray executors;
    for (const Executor &e : m_executors)
        if (!e.isEmpty() || e.kind != ExecKind::Fader)
            executors.append(e.toJson());
    QJsonArray effects;
    for (const Effect &e : m_effects)
        effects.append(e.toJson());
    QJsonArray shapes;
    for (const Shape &sh : m_shapes)
        shapes.append(sh.toJson());
    QJsonArray pages;
    for (auto it = m_pageNames.begin(); it != m_pageNames.end(); ++it)
        pages.append(QJsonObject{{"page", it.key()}, {"name", it.value()}});
    QJsonArray views;
    for (const StoredView &v : m_views)
        views.append(v.toJson());
    QJsonArray spreads;
    for (const Spread &m : m_spreads)
        spreads.append(m.toJson());
    QJsonArray rateMasters;
    for (const RateMaster &m : m_rateMasters)
        if (m.mode != RateMode::Rate || !m.name.isEmpty() || m.frozen || m.button || std::fabs(m.value - m.defaultValue()) > 1e-9)
            rateMasters.append(m.toJson());

    QJsonArray macros;
    for (const Macro &m : m_macros)
        macros.append(m.toJson());
    QJsonArray plots;
    for (const Plot &p : m_plots)
        plots.append(p.toJson());

    QJsonArray parked;
    for (auto f = m_parked.begin(); f != m_parked.end(); ++f)
        for (auto a = f->begin(); a != f->end(); ++a)
            parked.append(QJsonObject{{"fixture", f.key()}, {"attribute", a.key()}, {"value", a.value()}});

    QJsonObject o{
        {"format", "lumora-show"}, {"version", 2}, {"name", m_name},
        {"fixtureTypes", types}, {"patch", fixtures}, {"groups", groups},
        {"presets", presets}, {"sequences", sequences}, {"executors", executors},
        {"defaults", valueTableToJson(m_defaults)},
        {"effects", effects}, {"shapes", shapes}, {"pages", pages},
        {"views", views}, {"spreads", spreads}, {"parked", parked}, {"rateMasters", rateMasters}, {"macros", macros}, {"plots", plots},
        {"settings", QJsonObject{{"page", m_page}, {"grandMaster", m_grandMaster},
                                 {"selectedSequence", m_selectedSequence}}},
    };
    for (auto it = m_extra.begin(); it != m_extra.end(); ++it)
        o[it.key()] = it.value();
    return o;
}

bool Show::fromJson(const QJsonObject &o, QString *error)
{
    if (o["format"].toString() != QLatin1String("lumora-show")) {
        if (error)
            *error = tr("Not a Lumora show file");
        return false;
    }
    clearShow();
    m_name = o["name"].toString(m_name);
    for (const QJsonValue &v : o["fixtureTypes"].toArray()) {
        FixtureType t = FixtureType::fromJson(v.toObject());
        if (!t.id.isEmpty())
            m_library.addToShow(t);   // the show's own copy wins over the shipped library
    }
    for (const QJsonValue &v : o["patch"].toArray()) {
        Fixture f = Fixture::fromJson(v.toObject());
        if (m_library.find(f.typeId) && f.fid > 0 && f.fid <= kMaxFixtureId)
            m_fixtures.insert(f.fid, f);
    }
    rebuildCells();
    QHash<int, QPair<QVector<double>, QVector<double>>> spreadTimes;   // a.7.1.10 kept times in spreads
    for (const QJsonValue &v : o["spreads"].toArray()) {
        const QJsonObject so = v.toObject();
        const Spread m = Spread::fromJson(so);
        if (m.id > 0)
            m_spreads.insert(m.id, m);
        QVector<double> f, d;
        if (so.contains("fadeFrom"))
            f = {so["fadeFrom"].toDouble(), so["fadeTo"].toDouble()};
        if (so.contains("delayFrom"))
            d = {so["delayFrom"].toDouble(), so["delayTo"].toDouble()};
        if (!f.isEmpty() || !d.isEmpty())
            spreadTimes.insert(m.id, {f, d});
    }
    for (const QJsonValue &v : o["parked"].toArray()) {
        const QJsonObject po = v.toObject();
        const int fid = po["fixture"].toInt();
        if (hasFixture(fid))
            m_parked[fid][po["attribute"].toString()] = po["value"].toDouble();
    }
    for (const QJsonValue &v : o["groups"].toArray()) {
        Group g = Group::fromJson(v.toObject());
        m_groups.insert(g.id, g);
    }
    for (const QJsonValue &v : o["presets"].toArray()) {
        Preset p = Preset::fromJson(v.toObject());
        m_presets[p.type].insert(p.id, p);
    }
    for (const QJsonValue &v : o["sequences"].toArray()) {
        Sequence s = Sequence::fromJson(v.toObject());
        m_sequences.insert(s.id, s);
    }
    for (const QJsonValue &v : o["effects"].toArray()) {
        Effect e = Effect::fromJson(v.toObject());
        if (e.spreadId < 0) {   // older shows: each effect references the pool spread for its layout
            const QSignalBlocker quiet(this);
            e.spreadId = spreadForLayout(e.ownLayout(), QString());
        }
        m_effects.insert(e.id, e);
    }
    for (const QJsonValue &v : o["shapes"].toArray()) {
        Shape sh = Shape::fromJson(v.toObject());
        m_shapes.insert(sh.id, sh);
    }
    for (const QJsonValue &v : o["executors"].toArray()) {
        Executor e = Executor::fromJson(v.toObject());
        m_executors.insert(execKey(e.page, e.number), e);
    }
    for (const QJsonValue &v : o["rateMasters"].toArray()) {
        const RateMaster m = RateMaster::fromJson(v.toObject());
        m_rateMasters[m.id - 1] = m;
    }
    for (const QJsonValue &v : o["plots"].toArray()) {
        const Plot p = Plot::fromJson(v.toObject());
        if (p.id > 0)
            m_plots.insert(p.id, p);
    }
    for (const QJsonValue &v : o["macros"].toArray()) {
        const Macro m = Macro::fromJson(v.toObject());
        if (m.id > 0)
            m_macros.insert(m.id, m);
    }
    m_defaults = valueTableFromJson(o["defaults"].toObject());
    for (const QJsonValue &v : o["pages"].toArray())
        m_pageNames.insert(v["page"].toInt(), v["name"].toString());
    for (const QJsonValue &v : o["views"].toArray()) {
        StoredView sv = StoredView::fromJson(v.toObject());
        if (sv.id > 0)
            m_views.insert(sv.id, sv);
    }
    // Spreads used to carry fade and delay ranges: those are the lines' own times now.
    if (!spreadTimes.isEmpty())
        for (Sequence &seq : m_sequences)
            for (Cue &c : seq.cues)
                for (BlueprintLine &l : c.blueprints)
                    if (spreadTimes.contains(l.spreadId)) {
                        if (l.fadeTimes.isEmpty())
                            l.fadeTimes = spreadTimes[l.spreadId].first;
                        if (l.delayTimes.isEmpty())
                            l.delayTimes = spreadTimes[l.spreadId].second;
                    }
    // Blueprint lines used to carry their own dimmer level; that's a universal Dimmer preset now.
    // Added last, so it still wins over the line's other presets.
    {
        const QSignalBlocker quiet(this);
        for (Sequence &seq : m_sequences)
            for (Cue &c : seq.cues)
                for (BlueprintLine &l : c.blueprints)
                    if (l.dimmer >= 0) {
                        l.presets.append({int(Feature::Dimmer), dimmerPresetFor(l.dimmer, true)});
                        l.dimmer = -1.0;
                    }
    }
    // Shows from before version 2 kept pan and tilt as 0..1 of the DMX range: degrees now.
    if (o["version"].toInt(1) < 2)
        anglesToDegrees();
    QJsonObject settings = o["settings"].toObject();
    m_page = settings["page"].toInt(1);
    m_grandMaster = settings["grandMaster"].toDouble(1.0);
    m_selectedSequence = settings["selectedSequence"].toInt(1);
    static const QStringList known = {"format", "version", "name", "fixtureTypes", "patch", "groups",
                                      "presets", "sequences", "executors", "settings", "effects", "shapes", "macros", "plots",
                                      "pages", "defaults", "views", "spreads", "parked", "rateMasters"};
    for (auto it = o.begin(); it != o.end(); ++it)
        if (!known.contains(it.key()))
            m_extra[it.key()] = it.value();
    linkPlaybacks();
    emit changed(AllParts);
    return true;
}

// ===========================================================================
// spread
// ===========================================================================

const Spread *Show::spread(int id) const
{
    auto it = m_spreads.constFind(id);
    return it == m_spreads.constEnd() ? nullptr : &it.value();
}

void Show::setSpread(const Spread &m)
{
    m_spreads.insert(m.id, m);
    emit changed(SpreadsPart | SequencesPart);
}

void Show::deleteSpread(int id)
{
    if (m_spreads.remove(id))
        emit changed(SpreadsPart | SequencesPart);
}

bool Show::storeSpread(int id, const QString &name)
{
    if (id < 1)
        return false;
    Spread m = m_activeSpread;
    m.id = id;
    m.name = !name.isEmpty() ? name : m_spreads.contains(id) ? m_spreads[id].name : QString();
    if (m.name.isEmpty())
        m.name = m.isDefault() ? tr("Spread %1").arg(id) : m.summary();
    setSpread(m);
    return true;
}

bool Show::recallSpread(int id)
{
    const Spread *m = spread(id);
    if (!m)
        return false;
    setActiveSpread(*m);
    return true;
}

void Show::setActiveSpread(const Spread &m)
{
    m_activeSpread = m;
    emit changed(ProgrammerPart | SpreadsPart);
}

QVector<int> Show::workingSelection() const
{
    return m_activeSpread.apply(m_selection);
}

int Show::setProgrammerTiming(bool delay, const QVector<double> &points, int feature, const QString &attribute)
{
    const QVector<int> fids = workingSelection();
    const QVector<double> frac = m_activeSpread.timeFractions(int(fids.size()));
    int n = 0;
    for (int i = 0; i < fids.size(); ++i) {
        auto f = m_programmer.find(fids[i]);
        if (f == m_programmer.end())
            continue;
        const double v = points.isEmpty() ? -1.0 : std::max(0.0, alongPoints(points, frac[i]));
        for (auto a = f->begin(); a != f->end(); ++a) {
            if (!attribute.isEmpty() ? a.key() != attribute : feature && int(featureOf(a.key())) != feature)
                continue;
            (delay ? a->delay : a->fade) = v;
            ++n;
        }
    }
    if (n)
        emit changed(ProgrammerPart);
    return n;
}

// ===========================================================================
// Plots
// ===========================================================================

const Plot *Show::plot(int id) const
{
    auto it = m_plots.constFind(id);
    return it == m_plots.constEnd() ? nullptr : &it.value();
}

void Show::setPlot(const Plot &p)
{
    if (p.id <= 0)
        return;
    m_plots.insert(p.id, p);
    emit changed(PlotsPart);
}

bool Show::storePlot(int id, const QString &name)
{
    if (id <= 0)
        return false;
    Plot p = m_plots.value(id);
    p.id = id;
    if (!name.isEmpty())
        p.name = name;
    m_plots.insert(id, p);
    emit changed(PlotsPart);
    return true;
}

void Show::deletePlot(int id)
{
    if (m_plots.remove(id))
        emit changed(PlotsPart);
}

bool Show::movePlot(int from, int to, bool copy)
{
    if (!m_plots.contains(from) || to <= 0 || from == to || m_plots.contains(to))
        return false;
    Plot p = m_plots.value(from);
    p.id = to;
    if (!copy)
        m_plots.remove(from);
    m_plots.insert(to, p);
    emit changed(PlotsPart);
    return true;
}

int Show::addFixturesToPlot(int id, const QVector<int> &fids)
{
    if (id <= 0)
        return 0;
    Plot p = m_plots.value(id);
    p.id = id;
    // A new row under everything already there, wrapping every 12.
    const QRectF b = p.bounds();
    const double top = b.isNull() ? 0.0 : b.bottom() + 24.0;
    const double left = b.isNull() ? 0.0 : b.left();
    int n = 0;
    for (int fid : fids) {
        if (!hasFixture(fid) || p.indexOfFixture(fid) >= 0)
            continue;
        PlotItem it;
        it.kind = PlotItem::Fixture;
        it.fid = fid;
        it.round = isCellId(fid);   // subfixtures as dots
        it.rect = QRectF(left + (n % 12) * 60.0, top + (n / 12) * 72.0, 48, 48);
        p.items << it;
        ++n;
    }
    m_plots.insert(id, p);
    emit changed(PlotsPart);
    return n;
}

bool Show::addObjectToPlot(int id, const QString &object)
{
    if (id <= 0 || object.isEmpty())
        return false;
    Plot p = m_plots.value(id);
    p.id = id;
    const QRectF b = p.bounds();
    PlotItem it;
    it.kind = PlotItem::Object;
    it.object = object;
    it.rect = QRectF(b.isNull() ? 0.0 : b.left(), b.isNull() ? 0.0 : b.bottom() + 24.0, 120, 56);
    p.items << it;
    m_plots.insert(id, p);
    emit changed(PlotsPart);
    return true;
}

// ===========================================================================
// Macros
// ===========================================================================

const Macro *Show::macro(int id) const
{
    auto it = m_macros.constFind(id);
    return it == m_macros.constEnd() ? nullptr : &it.value();
}

void Show::setMacro(const Macro &m)
{
    if (m.id <= 0)
        return;
    m_macros.insert(m.id, m);
    emit changed(MacrosPart);
}

bool Show::storeMacro(int id, const QString &name)
{
    if (id <= 0)
        return false;
    Macro m = m_macros.value(id);
    m.id = id;
    if (!name.isEmpty())
        m.name = name;
    m_macros.insert(id, m);
    emit changed(MacrosPart);
    return true;
}

void Show::deleteMacro(int id)
{
    offMacro(id);
    if (m_macros.remove(id))
        emit changed(MacrosPart);
}

bool Show::moveMacro(int from, int to, bool copy)
{
    if (!m_macros.contains(from) || to <= 0 || from == to || m_macros.contains(to))
        return false;
    Macro m = m_macros.value(from);
    m.id = to;
    if (!copy) {
        offMacro(from);
        m_macros.remove(from);
    }
    m_macros.insert(to, m);
    emit changed(MacrosPart);
    return true;
}

const MacroRun *Show::macroRun(int id) const
{
    for (const MacroRun &r : m_macroRuns)
        if (r.id == id)
            return &r;
    return nullptr;
}

void Show::goMacro(int id)
{
    const Macro *m = macro(id);
    if (!m || m->lines.isEmpty())
        return;
    if (m_executingMacro == id) {   // "Go Macro 3" inside macro 3: from the top again, a loop
        m_macroRestart.insert(id);
        return;
    }
    bool running = false;
    for (MacroRun &r : m_macroRuns)
        if (r.id == id) {   // a tap goes on: from a Wait, or now instead of after the Follow time
            r.waiting = false;
            r.timeLeft = 0.0;
            running = true;
        }
    if (!running)
        m_macroRuns.append({id, 0, 0.0, false});
    processMacros();
    emit changed(MacrosPart);
}

void Show::offMacro(int id)
{
    const int before = int(m_macroRuns.size());
    for (int i = int(m_macroRuns.size()) - 1; i >= 0; --i)
        if (id == 0 || m_macroRuns[i].id == id)
            m_macroRuns.removeAt(i);
    if (int(m_macroRuns.size()) != before)
        emit changed(MacrosPart);
}

void Show::tickMacros(double dt)
{
    bool due = false;
    for (MacroRun &r : m_macroRuns)
        if (!r.waiting && r.timeLeft <= 0)
            due = true;   // left over from a long loop with no time in it
        else if (!r.waiting && r.timeLeft > 0) {
            r.timeLeft = std::max(0.0, r.timeLeft - dt);
            due |= r.timeLeft <= 0;
        }
    if (due) {
        processMacros();
        emit changed(MacrosPart);
    }
}

void Show::processMacros()
{
    if (m_processingMacros)   // a line started a macro: the loop below gets to it
        return;
    m_processingMacros = true;
    auto find = [this](int id) -> MacroRun * {
        for (MacroRun &r : m_macroRuns)
            if (r.id == id)
                return &r;
        return nullptr;
    };
    int budget = 2000;   // a macro that loops with no time in it can't hang the console
    bool progress = true;
    while (progress && budget > 0) {
        progress = false;
        QVector<int> ids;
        for (const MacroRun &r : m_macroRuns)
            ids << r.id;
        for (int id : ids) {
            MacroRun *r = find(id);
            if (!r || r->waiting || r->timeLeft > 0)
                continue;
            const Macro *m = macro(id);
            if (!m || r->line >= m->lines.size()) {
                offMacro(id);
                progress = true;
                continue;
            }
            const Macro copy = *m;   // the line may change or delete its own macro
            const int index = r->line;
            r->line = index + 1;
            const MacroLine &line = copy.lines[index];
            bool keepGoing = true;
            if (line.enabled && !line.command.trimmed().isEmpty()) {
                m_executingMacro = id;
                keepGoing = runMacroLine(copy, line);
                m_executingMacro = 0;
                --budget;
            }
            progress = true;
            r = find(id);   // "Off Macro" in the line, or anything else, may have stopped it
            if (!r)
                continue;
            if (!keepGoing) {
                offMacro(id);
                continue;
            }
            if (m_macroRestart.remove(id)) {
                r->line = 0;
                r->waiting = false;
                r->timeLeft = line.wait == MacroWait::Follow ? line.time : 0.0;
                continue;
            }
            if (r->line >= copy.lines.size()) {   // that was the last line
                offMacro(id);
                continue;
            }
            if (line.wait == MacroWait::Wait)
                r->waiting = true;
            else
                r->timeLeft = line.time;
        }
    }
    m_processingMacros = false;
}

bool Show::runMacroLine(const Macro &m, const MacroLine &line)
{
    const QString command = expandVariables(line.command);
    if (m_macroExec)
        return m_macroExec(m, line, command);
    QString cmd = command.trimmed();
    if (cmd.endsWith('@'))   // "finish it yourself": nobody's there, so it runs as it is
        cmd.chop(1);
    if (cmd.startsWith('@'))
        cmd.remove(0, 1);
    if (cmd.contains('('))   // a popup needs someone to answer it
        return false;
    CommandLine cl(this);
    cl.execute(cmd);
    return true;
}

void Show::setMacroVariable(const QString &name, const QString &value)
{
    m_macroVars.insert(name.toLower(), value);
}

QString Show::expandVariables(const QString &command) const
{
    if (!command.contains('$'))
        return command;
    static const QRegularExpression var(QStringLiteral("\\$([A-Za-z_][A-Za-z0-9_]*)"));
    QString out;
    int last = 0;
    auto it = var.globalMatch(command);
    while (it.hasNext()) {
        const QRegularExpressionMatch mt = it.next();
        out += command.mid(last, mt.capturedStart() - last);
        const QString name = mt.captured(1);
        out += hasMacroVariable(name) ? macroVariable(name) : mt.captured(0);
        last = mt.capturedEnd();
    }
    out += command.mid(last);
    return out;
}

bool Show::setAppearance(const QString &what, int id, const QString &color)
{
    if (what == QLatin1String("sequence")) {
        Sequence *seq = sequenceMutable(id);
        if (!seq)
            return false;
        seq->color = color;
        emit changed(SequencesPart | ExecutorsPart);
        return true;
    }
    if (what == QLatin1String("macro")) {
        auto it = m_macros.find(id);
        if (it == m_macros.end())
            return false;
        it->color = color;
        emit changed(MacrosPart);
        return true;
    }
    return false;
}

void Show::anglesToDegrees()
{
    // A fixture's own range when the value belongs to one; otherwise the first fixture of that
    // type, or any fixture, with the attribute; otherwise the usual 540/270.
    auto any = [this](const QString &typeId, const QString &a) -> const ChannelDef * {
        for (const Fixture &f : m_fixtures)
            if (typeId.isEmpty() || f.typeId == typeId)
                if (const ChannelDef *c = channelOf(f.fid, a))
                    return c;
        return nullptr;
    };
    auto convert = [&](int fid, const QString &typeId, const QString &a, double v) {
        const ChannelDef *c = fid ? channelOf(fid, a) : nullptr;
        if (!c)
            c = any(typeId, a);
        if (c)
            return c->fromDmx(v);
        return -defaultAngleSpan(a) / 2 + v * defaultAngleSpan(a);
    };
    auto rawAttr = [&](RawAttr &attrs, int fid, const QString &typeId) {
        for (auto it = attrs.begin(); it != attrs.end(); ++it)
            if (isAngle(it.key()))
                it.value() = convert(fid, typeId, it.key(), it.value());
    };
    auto table = [&](ValueTable &t) {
        for (auto f = t.begin(); f != t.end(); ++f)
            for (auto a = f->begin(); a != f->end(); ++a)
                if (isAngle(a.key()))
                    a->value = convert(f.key(), QString(), a.key(), a->value);
    };
    for (auto &pool : m_presets)
        for (Preset &p : pool) {
            rawAttr(p.universal, 0, QString());
            for (auto it = p.perType.begin(); it != p.perType.end(); ++it)
                rawAttr(it.value(), 0, it.key());
            for (auto it = p.perFixture.begin(); it != p.perFixture.end(); ++it)
                rawAttr(it.value(), it.key(), QString());
        }
    for (Sequence &seq : m_sequences)
        for (Cue &c : seq.cues)
            table(c.values);
    table(m_defaults);
    for (Effect &e : m_effects)
        for (EffectLayer &l : e.layers) {
            for (EffectPoint &p : l.points)
                rawAttr(p.values, 0, QString());
            rawAttr(l.center.values, 0, QString());
        }
}

// ---------------------------------------------------------------------------
// Views
// ---------------------------------------------------------------------------

const StoredView *Show::view(int id) const
{
    auto it = m_views.constFind(id);
    return it == m_views.cend() ? nullptr : &it.value();
}

bool Show::storeView(int id, const QString &name)
{
    if (id <= 0 || !m_layoutProvider)
        return false;
    const std::optional<QJsonArray> layout = m_layoutProvider();
    if (!layout)
        return false;
    StoredView v = m_views.value(id);
    v.id = id;
    v.layout = *layout;
    if (!name.isEmpty())
        v.name = name;
    if (v.name.isEmpty())
        v.name = tr("View %1").arg(id);
    m_views.insert(id, v);
    emit changed(ViewsPart);
    return true;
}

void Show::setView(const StoredView &v)
{
    if (v.id <= 0)
        return;
    m_views.insert(v.id, v);
    emit changed(ViewsPart);
}

void Show::setViews(const QList<StoredView> &views)
{
    m_views.clear();
    for (const StoredView &v : views)
        if (v.id > 0)
            m_views.insert(v.id, v);
    emit changed(ViewsPart);
}

void Show::deleteView(int id)
{
    if (m_views.remove(id))
        emit changed(ViewsPart);
}

int Show::nextFreeViewId() const
{
    int id = 1;
    while (m_views.contains(id))
        ++id;
    return id;
}

} // namespace lumora
