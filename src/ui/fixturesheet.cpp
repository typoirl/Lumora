// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixturesheet.h"

#include "appcontext.h"
#include "media.h"
#include "quickedit.h"
#include "theme.h"

#include <QApplication>
#include <QHeaderView>
#include <QIcon>
#include <QJsonArray>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QScrollArea>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <cmath>

namespace lumora {

QColor colorForName(const QString &raw)
{
    static const QHash<QString, QColor> names = {
        {"open", QColor(255, 255, 255)}, {"white", QColor(255, 255, 255)}, {"red", QColor(255, 40, 40)},
        {"blue", QColor(50, 80, 255)}, {"green", QColor(40, 220, 80)}, {"amber", QColor(255, 180, 30)},
        {"magenta", QColor(255, 40, 220)}, {"yellow", QColor(255, 235, 40)}, {"cyan", QColor(40, 220, 255)},
        {"orange", QColor(255, 120, 20)}, {"pink", QColor(255, 120, 190)}, {"purple", QColor(150, 60, 255)},
        {"lavender", QColor(190, 150, 255)}, {"uv", QColor(110, 40, 255)}, {"cto", QColor(255, 200, 140)},
        {"ctb", QColor(190, 215, 255)}, {"lime", QColor(180, 255, 40)},
    };
    const QString n = raw.trimmed().toLower();
    if (names.contains(n))
        return names.value(n);
    // Longest name first, so "orange red" is the same color on every run.
    static const QStringList byLength = [] {
        QStringList k = names.keys();
        std::sort(k.begin(), k.end(), [](const QString &a, const QString &b) { return a.size() != b.size() ? a.size() > b.size() : a < b; });
        return k;
    }();
    for (const QString &k : byLength)
        if (n.contains(k))
            return names.value(k);
    return QColor();
}

QColor approximateColor(const RawAttr &v, const FixtureMode *mode)
{
    auto has = [&](const char *a) { return v.contains(QLatin1String(a)); };
    if (has("ColorAdd_R") || has("ColorAdd_G") || has("ColorAdd_B") || has("ColorAdd_W") || has("ColorAdd_A")) {
        const double w = v.value("ColorAdd_W", 0) + v.value("ColorAdd_WW", 0) * 0.9 + v.value("ColorAdd_CW", 0);
        const double a = v.value("ColorAdd_A", 0);
        const double uv = v.value("ColorAdd_UV", 0);
        double r = v.value("ColorAdd_R", 0) + w + a + uv * 0.3;
        double g = v.value("ColorAdd_G", 0) + w + a * 0.7 + v.value("ColorAdd_L", 0);
        double b = v.value("ColorAdd_B", 0) + w + uv * 0.8;
        const double m = std::max({r, g, b});
        if (m < 1e-3)
            return QColor(0, 0, 0);
        return QColor::fromRgbF(float(r / m), float(g / m), float(b / m));
    }
    if (has("ColorSub_C") || has("ColorSub_M") || has("ColorSub_Y"))
        return QColor::fromRgbF(float(1 - v.value("ColorSub_C", 0)), float(1 - v.value("ColorSub_M", 0)),
                                float(1 - v.value("ColorSub_Y", 0)));
    if (has("Color1") && mode) {
        if (const ChannelDef *c = mode->channel("Color1"))
            return colorForName(c->setNameFor(v.value("Color1")));
    }
    return QColor();
}

// ===========================================================================

FixtureSheetModel::FixtureSheetModel(AppContext *ctx, QObject *parent) : QAbstractTableModel(parent), m_ctx(ctx)
{
    reloadPatch();
    connect(ctx->show, &Show::changed, this, [this](Show::Parts p) {
        if (p & (Show::SequencesPart | Show::PresetsPart | Show::GroupsPart | Show::PatchPart | Show::EffectsPart)) {
            m_presetCache.clear();
            m_effectCache.clear();
        }
    });
}

void FixtureSheetModel::collectPlaybacks()
{
    m_sources.clear();
    QList<Executor *> on;
    for (Executor *e : m_ctx->show->allExecutors())
        if (e->pb().isOn() && e->sequenceId > 0)
            on << e;
    std::sort(on.begin(), on.end(), [](const Executor *a, const Executor *b) { return a->pb().stamp > b->pb().stamp; });
    for (const Executor *e : on)
        m_sources.append({e->sequenceId, e->pb().cueIndex, e->pb().to, e->pb().current});
}

QString FixtureSheetModel::playbackPresetName(int fid, Feature f) const
{
    if (m_sources.isEmpty())
        return QString();
    Show *show = m_ctx->show;
    const FixtureMode *mode = show->fixtureMode(fid);
    const RawAttr out = show->lastOutput().value(fid);
    for (const ChannelDef &c : mode->channels) {
        if (featureOf(c.attribute) != f)
            continue;
        const QString &a = c.attribute;
        // The playback that wins this attribute: newest (LTP), or the highest for Dimmer (HTP).
        const Source *win = nullptr;
        double best = -1;
        for (const Source &src : m_sources) {
            const auto fx = src.current.constFind(fid);
            if (fx == src.current.constEnd() || !fx->contains(a))
                continue;
            if (a != QLatin1String("Dimmer")) {
                win = &src;
                break;
            }
            if (fx->value(a) > best) {
                best = fx->value(a);
                win = &src;
            }
        }
        if (!win)
            continue;
        const qint64 key = (qint64(win->sequenceId) << 32) | quint32(win->cueIndex);
        auto cached = m_presetCache.constFind(key);
        if (cached == m_presetCache.constEnd()) {
            const Sequence *seq = show->sequence(win->sequenceId);
            cached = m_presetCache.insert(key, seq ? show->trackedPresets(*seq, win->cueIndex)
                                                   : QMap<int, QHash<QString, PresetRef>>());
        }
        const PresetRef ref = cached->value(fid).value(a);
        if (ref.second <= 0)
            continue;
        // Only while the output is that preset: not mid-fade, pulled down by a master or overridden.
        if (std::fabs(out.value(a, -1) - win->to.value(fid).value(a, -2)) > 0.005)
            continue;
        if (const Preset *p = show->preset(ref.first, ref.second))
            return p->name;
    }
    return QString();
}

QString FixtureSheetModel::effectNames(int fid, Feature f) const
{
    Show *show = m_ctx->show;
    const FixtureMode *mode = show->fixtureMode(fid);
    if (!mode)
        return QString();
    const QStringList attrs = mode->attributes();
    QStringList names;
    auto consider = [&](const EffectInstance &inst) {
        if (!inst.fixtures.contains(fid))
            return;
        const Effect *e = show->effect(inst.effectId);
        if (!e || names.contains(e->name))
            return;
        for (const EffectLayer &l : e->layers) {
            if (!l.enabled)
                continue;
            for (const QString &a : l.drivenAttributes(attrs))
                if (featureOf(a) == f) {
                    names << (e->name.isEmpty() ? tr("Effect %1").arg(e->id) : e->name);
                    return;
                }
        }
    };
    // The programmer's effects, then those of running cues (newest first).
    for (const EffectInstance &inst : show->programmerEffects())
        consider(inst);
    for (const Source &src : m_sources) {
        const qint64 key = (qint64(src.sequenceId) << 32) | quint32(src.cueIndex);
        auto cached = m_effectCache.constFind(key);
        if (cached == m_effectCache.constEnd()) {
            const Sequence *seq = show->sequence(src.sequenceId);
            const bool valid = seq && src.cueIndex >= 0 && src.cueIndex < seq->cues.size();
            cached = m_effectCache.insert(key, valid ? show->cueEffects(seq->cues[src.cueIndex]) : QVector<EffectInstance>());
        }
        for (const EffectInstance &inst : *cached)
            consider(inst);
    }
    return names.join(QStringLiteral(" + "));
}

void FixtureSheetModel::reloadPatch()
{
    QVector<int> fids;
    Show *show = m_ctx->show;
    auto passes = [&](const Fixture &f) {
        switch (m_filter) {
        case Filter::All: return true;
        case Filter::Patched: return f.patched();
        case Filter::Selected: return show->selection().contains(f.fid);
        case Filter::Programmed: return show->programmerValues().contains(f.fid);
        }
        return true;
    };
    // A row stays when anything nested under it is listed, so that has something to sit in.
    std::function<bool(int, QVector<int> &)> collect = [&](int fid, QVector<int> &out) {
        QVector<int> inside;
        bool any = false;
        for (int c : show->childrenOf(fid))
            any |= collect(c, inside);
        if (!any && !passes(*show->fixture(fid)))
            return false;
        out << fid;
        if (m_expanded.contains(fid))
            out << inside;
        return true;
    };
    for (const Fixture &f : show->fixtures())
        collect(f.fid, fids);
    if (fids == m_fids && !m_fids.isEmpty())
        return;
    beginResetModel();
    m_fids = fids;
    endResetModel();
}

bool FixtureSheetModel::hasCells(int fid) const
{
    return !m_ctx->show->childrenOf(fid).isEmpty();
}

int FixtureSheetModel::depthOf(int fid) const
{
    const Fixture *f = m_ctx->show->fixture(fid);
    return f ? f->depth() : 0;
}

void FixtureSheetModel::setExpanded(int master, bool on)
{
    if (on == m_expanded.contains(master))
        return;
    if (on)
        m_expanded.insert(master);
    else
        m_expanded.remove(master);
    m_fids.clear();   // force the reset
    reloadPatch();
}

void FixtureSheetModel::setExpandedAll(const QList<int> &masters)
{
    m_expanded = QSet<int>(masters.begin(), masters.end());
    m_fids.clear();
    reloadPatch();
}

bool FixtureSheetModel::isLabelRow(int fid) const
{
    if (!hasCells(fid))
        return false;
    const FixtureMode *m = m_ctx->show->fixtureMode(fid);
    return !m || m->channels.isEmpty();
}

bool FixtureSheetModel::hidesSelection(int fid) const
{
    if (isExpanded(fid) || !hasCells(fid))
        return false;
    const QVector<int> &sel = m_ctx->show->selection();
    const QVector<int> inside = isCellId(fid) ? m_ctx->show->childrenOf(fid) : m_ctx->show->cellsOf(fid);
    for (int c : inside)
        if (sel.contains(c))
            return true;
    return false;
}

void FixtureSheetModel::setFilter(Filter f)
{
    m_filter = f;
    m_fids.clear();
    reloadPatch();
}

void FixtureSheetModel::refreshValues()
{
    collectPlaybacks();
    if (!m_fids.isEmpty())
        emit dataChanged(index(0, Dimmer), index(m_fids.size() - 1, Control));
}

QVariant FixtureSheetModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal)
        return {};
    if (role == Qt::DisplayRole) {
        switch (section) {
        case Fid: return tr("ID");
        case Name: return tr("Name");
        case Patch: return tr("Patch");
        case Dimmer: return tr("Dimmer");
        case Position: return tr("Position");
        case Gobo: return tr("Gobo");
        case Color: return tr("Color");
        case Beam: return tr("Beam");
        case Focus: return tr("Focus");
        case Control: return tr("Control");
        }
    }
    if (role == Qt::ForegroundRole && section >= Dimmer)
        return theme::featureColor(static_cast<Feature>(section - Dimmer + 1));
    if (role == Qt::TextAlignmentRole)
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    return {};
}

void FixtureSheetModel::cellState(int fid, Feature f, bool *programmed, bool *changed) const
{
    Show *show = m_ctx->show;
    *programmed = false;
    *changed = false;
    const FixtureMode *mode = show->fixtureMode(fid);
    if (!mode)
        return;
    const auto out = show->lastOutput().constFind(fid);
    const auto prog = show->programmerValues().constFind(fid);
    for (const ChannelDef &c : mode->channels) {
        if (featureOf(c.attribute) != f)
            continue;
        if (prog != show->programmerValues().constEnd() && prog->contains(c.attribute))
            *programmed = true;
        if (out != show->lastOutput().constEnd())
            if (auto v = out->constFind(c.attribute); v != out->constEnd()
                && std::fabs(v.value() - show->defaultValue(fid, c.attribute)) > 1e-3)
                *changed = true;
    }
}

QString FixtureSheetModel::cellText(int fid, Feature f) const
{
    Show *show = m_ctx->show;
    const FixtureMode *mode = show->fixtureMode(fid);
    if (!mode)
        return QString();
    const RawAttr values = show->lastOutput().value(fid);   // shared, not copied
    const AttrValues prog = show->programmerValues().value(fid);
    const QStringList attrs = mode->attributes();
    bool anyAttr = false;
    for (const QString &a : attrs)
        anyAttr |= featureOf(a) == f;
    if (!anyAttr)
        return QString();
    auto pct = [&](const QString &a) {
        const auto v = values.constFind(a);
        return QString::number(std::lround((v != values.constEnd() ? v.value() : show->defaultValue(fid, a)) * 100));
    };
    auto setName = [&](const QString &a) -> QString {
        const ChannelDef *c = mode ? mode->channel(a) : nullptr;
        if (!c)
            return QString();
        QString n = c->setNameFor(values.value(a, c->defaultValue));
        return n.isEmpty() ? pct(a) : n;
    };
    // Preset names win over numbers: the programmer's preset references first, then (when the
    // programmer has nothing for this feature) the presets running playbacks put there. Effects
    // running on the feature show by name the same way, after the preset they run over.
    const QString fx = effectNames(fid, f);
    auto withFx = [&](const QString &name) { return fx.isEmpty() ? name : name + QStringLiteral(" · ") + fx; };
    bool inProgrammer = false;
    for (const QString &a : attrs) {
        if (featureOf(a) != f || !prog.contains(a))
            continue;
        inProgrammer = true;
        if (prog.value(a).isPreset()) {
            const ValueRef r = prog.value(a);
            if (const Preset *p = show->preset(r.presetType, r.presetId))
                return withFx(p->name);
        }
    }
    if (!inProgrammer)
        if (const QString n = playbackPresetName(fid, f); !n.isEmpty())
            return withFx(n);
    if (!fx.isEmpty())
        return fx;
    switch (f) {
    case Feature::Dimmer: return pct("Dimmer");
    case Feature::Position:
    {
        // In degrees, 0 the middle of the range. A head may only tilt.
        auto deg = [&](const QString &a) {
            const auto v = values.constFind(a);
            return QString::number(std::lround(v != values.constEnd() ? v.value() : show->defaultValue(fid, a))) + QChar(0x00b0);
        };
        QStringList s;
        if (attrs.contains("Pan"))
            s << "P " + deg("Pan");
        if (attrs.contains("Tilt"))
            s << "T " + deg("Tilt");
        return s.join("  ");
    }
    case Feature::Color: {
        if (attrs.contains("Color1"))
            return setName("Color1");
        QStringList c;
        for (const QString &a : attrs)
            if (featureOf(a) == Feature::Color && values.value(a, 0) > 0.004)
                c << attributeLabel(a).left(1) + pct(a);
        return c.isEmpty() ? tr("–") : c.join(' ');
    }
    case Feature::Gobo:
        return attrs.contains("Gobo1") ? setName("Gobo1") : QString();
    case Feature::Beam:
        if (attrs.contains("Shutter"))
            return setName("Shutter");
        return attrs.contains("Prism1") ? setName("Prism1") : QString();
    case Feature::Focus: {
        QStringList s;
        if (attrs.contains("Zoom"))
            s << "Z " + pct("Zoom");
        if (attrs.contains("Focus1"))
            s << "F " + pct("Focus1");
        return s.join("  ");
    }
    case Feature::Control: {
        // Control channels by name: range names where there are some ("Reset"), else levels.
        QStringList s;
        for (const QString &a : attrs)
            if (featureOf(a) == Feature::Control && s.size() < 3) {
                const QString v = setName(a);
                s << (v.isEmpty() ? QStringLiteral("%1 %2").arg(attributeLabel(a).remove(QStringLiteral("Ctl_")), pct(a)) : v);
            }
        return s.join(QStringLiteral(" · "));
    }
    }
    return QString();
}

QVariant FixtureSheetModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_fids.size())
        return {};
    const int fid = m_fids[index.row()];
    Show *show = m_ctx->show;
    const Fixture *fx = show->fixture(fid);
    if (!fx)
        return {};
    const int col = index.column();

    if (role == Qt::BackgroundRole && hidesSelection(fid))
        return QColor(0x3a, 0x32, 0x20);   // collapsed, with selected cells inside
    if (col <= Patch) {
        if (role == Qt::DisplayRole) {
            if (col == Fid) return m_ctx->show->idText(fid);
            if (col == Name) return fx->name;
            if (!fx->patched())
                return tr("unpatched");
            int address = fx->address;
            if (fx->cell) {   // a cell: where its first channel is
                int first = 512;
                if (const FixtureMode *m = show->fixtureMode(fid))
                    for (const ChannelDef &c : m->channels)
                        if (!c.isVirtual())
                            first = std::min(first, c.coarse);
                address += first < 512 ? first : 0;
            }
            return QStringLiteral("%1.%2").arg(fx->universe).arg(address, 3, 10, QChar('0'));
        }
        if (role == Qt::ForegroundRole)
            return col == Name && !fx->cell ? theme::text : theme::muted;
        return {};
    }

    const Feature f = static_cast<Feature>(col - Dimmer + 1);
    if (role == Qt::DisplayRole)
        return cellText(fid, f);
    if (role == Qt::ForegroundRole) {
        bool programmed = false, changed = false;
        cellState(fid, f, &programmed, &changed);
        if (programmed)
            return theme::programmer;
        return changed ? theme::text : theme::faint;
    }
    if (role == Qt::DecorationRole && f == Feature::Gobo) {
        const RawAttr v = show->lastOutput().value(fid);
        if (v.contains("Gobo1")) {
            const QImage img = slotImage(show, fid, "Gobo1", v.value("Gobo1"));
            if (!img.isNull())
                return QIcon(goboPixmap(img, 18));
        }
    }
    if (role == Qt::DecorationRole && f == Feature::Color) {
        QColor c = approximateColor(show->lastOutput().value(fid), show->fixtureMode(fid));
        if (c.isValid() && c != QColor(0, 0, 0))
            return c;
    }
    return {};
}

// ===========================================================================
// Tiles
// ===========================================================================

namespace {
constexpr int kTileW = 104;
constexpr int kTileH = 74;
constexpr int kTileGap = 3;
} // namespace

FixtureTiles::FixtureTiles(AppContext *ctx, FixtureSheetModel *model, QWidget *parent)
    : QWidget(parent), m_ctx(ctx), m_model(model)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(model, &QAbstractItemModel::modelReset, this, [this]() {
        updateGeometry();
        update();
    });
    connect(model, &QAbstractItemModel::dataChanged, this, qOverload<>(&QWidget::update));
}

int FixtureTiles::columns(int w) const
{
    return std::max(1, (w + kTileGap) / (kTileW + kTileGap));
}

int FixtureTiles::heightForWidth(int w) const
{
    const int n = int(m_model->fids().size());
    const int rows = (n + columns(w) - 1) / columns(w);
    return std::max(kTileH, rows * (kTileH + kTileGap));
}

QRectF FixtureTiles::arrowRect(const QRect &tile)
{
    return {tile.right() - 16.0, tile.bottom() - 20.0, 12.0, 12.0};
}

QRect FixtureTiles::tileRect(int index) const
{
    const int cols = columns(width());
    const int w = (width() - (cols - 1) * kTileGap) / cols;   // stretch to fill the row
    return {(index % cols) * (w + kTileGap), (index / cols) * (kTileH + kTileGap), w, kTileH};
}

void FixtureTiles::paintEvent(QPaintEvent *ev)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    Show *show = m_ctx->show;
    const QVector<int> &fids = m_model->fids();
    const QVector<int> &sel = show->selection();

    // An open fixture and its subfixtures sit together on a tray, row by row, so the cells
    // visibly hang off their fixture.
    const QColor tray = theme::mix(theme::raised, theme::live, 0.18);
    for (int i = 0; i < fids.size(); ++i) {
        if (m_model->depthOf(fids[i]) != 0 || !m_model->hasCells(fids[i]) || !m_model->isExpanded(fids[i]))
            continue;
        int last = i;
        while (last + 1 < fids.size() && m_model->depthOf(fids[last + 1]) > 0)
            ++last;
        int start = i;
        for (int k = i; k <= last; ++k) {
            const bool rowEnds = k == last || tileRect(k + 1).top() != tileRect(k).top();
            if (!rowEnds)
                continue;
            const QRect a = tileRect(start), b = tileRect(k);
            p.setPen(Qt::NoPen);
            p.setBrush(tray);
            p.drawRoundedRect(QRectF(a.left() - 1.5, a.top() - 1.5, b.right() - a.left() + 4, a.height() + 3), 7, 7);
            start = k + 1;
        }
    }

    for (int i = 0; i < fids.size(); ++i) {
        QRect r = tileRect(i);
        const int depth = m_model->depthOf(fids.value(i));
        if (depth > 0)
            r.adjust(0, 5 * depth, 0, -3);   // subfixtures sit lower and smaller on the tray
        if (!r.intersects(ev->rect()))
            continue;
        const int fid = fids[i];
        const Fixture *fx = show->fixture(fid);
        if (!fx)
            continue;
        const RawAttr out = show->lastOutput().value(fid);
        const bool selected = sel.contains(fid);
        const bool programmed = show->programmerValues().contains(fid);
        const double level = out.value("Dimmer", 0.0);

        // Body, with a glow at the bottom as bright as the fixture's intensity, in its color.
        QColor c = approximateColor(out, show->fixtureMode(fid));
        if (!c.isValid() || c == QColor(0, 0, 0))
            c = QColor(255, 236, 200);
        p.setPen(QPen(selected ? theme::live : theme::line, selected ? 1.6 : 1));
        p.setBrush(theme::panel);
        p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
        if (level > 0.003) {
            QColor glow = c;
            glow.setAlphaF(float(0.18 + 0.55 * level));
            const int gh = 6;
            p.setPen(Qt::NoPen);
            p.setBrush(glow);
            p.drawRoundedRect(QRectF(r.left() + 3, r.bottom() - gh - 2, (r.width() - 6) * std::max(0.08, level), gh), 2, 2);
        }
        // ID and name
        p.setPen(selected ? theme::live : theme::text);
        p.setFont(theme::uiFont(11, QFont::DemiBold));
        p.drawText(r.adjusted(7, 4, -7, 0), Qt::AlignLeft | Qt::AlignTop, m_ctx->show->idText(fid));
        p.setFont(theme::uiFont(8));
        p.setPen(theme::muted);
        p.drawText(r.adjusted(7, 23, -7, 0), Qt::AlignLeft | Qt::AlignTop,
                   p.fontMetrics().elidedText(fx->name, Qt::ElideRight, r.width() - 14));
        // Level, coral when it's in the programmer
        bool prog = false, changed = false;
        m_model->cellState(fid, Feature::Dimmer, &prog, &changed);
        const QString dim = m_model->cellText(fid, Feature::Dimmer);
        p.setFont(theme::uiFont(10, QFont::DemiBold));
        p.setPen(prog ? theme::programmer : (level > 0.003 ? theme::text : theme::faint));
        p.drawText(r.adjusted(7, 4, -7, 0), Qt::AlignRight | Qt::AlignTop, dim.isEmpty() ? QStringLiteral("–") : dim);
        // Color chip and gobo
        int x = r.left() + 7;
        const int y = r.top() + 42;
        if (c.isValid()) {
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawEllipse(QRectF(x, y, 12, 12));
            x += 17;
        }
        if (out.contains("Gobo1")) {
            const QImage img = slotImage(show, fid, "Gobo1", out.value("Gobo1"));
            if (!img.isNull()) {
                p.drawPixmap(x, y - 1, goboPixmap(img, 14));
                x += 18;
            }
        }
        if (out.contains("Pan")) {
            p.setFont(theme::uiFont(7));
            p.setPen(theme::faint);
            p.drawText(QRect(x, y - 1, r.right() - x - 5, 14), Qt::AlignLeft | Qt::AlignVCenter,
                       m_model->cellText(fid, Feature::Position));
        }
        if (programmed) {
            p.setPen(Qt::NoPen);
            p.setBrush(theme::programmer);
            p.drawEllipse(QPointF(r.right() - (m_model->hasCells(fid) ? 22 : 6), r.bottom() - 13), 2.5, 2.5);
        }
        // Fixtures with subfixtures: an arrow to open and close them.
        if (m_model->hasCells(fid)) {
            const QRectF a = arrowRect(r);
            const bool open = m_model->isExpanded(fid);
            QPainterPath tri;
            const QPointF c = a.center();
            if (open) {
                tri.moveTo(c.x() - 4, c.y() - 2);
                tri.lineTo(c.x() + 4, c.y() - 2);
                tri.lineTo(c.x(), c.y() + 3);
            } else {
                tri.moveTo(c.x() - 2, c.y() - 4);
                tri.lineTo(c.x() + 3, c.y());
                tri.lineTo(c.x() - 2, c.y() + 4);
            }
            tri.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(open ? theme::live : theme::muted);
            p.drawPath(tri);
        }
        if (!fx->patched()) {
            p.setFont(theme::uiFont(7));
            p.setPen(theme::faint);
            p.drawText(r.adjusted(0, 0, -14, -15), Qt::AlignRight | Qt::AlignBottom, tr("unpatched"));
        }
    }
}

void FixtureTiles::mousePressEvent(QMouseEvent *ev)
{
    const QVector<int> &fids = m_model->fids();
    int hit = -1;
    for (int i = 0; i < fids.size(); ++i)
        if (tileRect(i).contains(ev->position().toPoint()))
            hit = i;
    Show *show = m_ctx->show;
    if (hit < 0)
        return;   // the selection only empties with Clear
    const int fid = fids[hit];
    // The arrow opens or closes a fixture's subfixtures.
    if (m_model->hasCells(fid) && arrowRect(tileRect(hit)).adjusted(-4, -4, 4, 4).contains(ev->position())) {
        m_model->setExpanded(fid, !m_model->isExpanded(fid));
        emit expandedChanged();
        return;
    }
    if (ev->modifiers() & Qt::ShiftModifier && m_anchor >= 0) {
        QVector<int> range;
        for (int i = std::min(m_anchor, hit); i <= std::max(m_anchor, hit); ++i)
            range << fids[i];
        show->select(range, Show::SelectMode::Add);
    } else {
        // Every tap adds; tapping a selected fixture drops it.
        show->select({fid}, show->selection().contains(fid) ? Show::SelectMode::Remove : Show::SelectMode::Add);
        m_anchor = hit;
    }
    update();
}

// ===========================================================================

namespace {
// Keeps the model's text color (programmer coral, faint defaults) on selected rows too.
constexpr int kArrowWidth = 16;   // the expand arrow in front of a pixel fixture's ID
constexpr int kCellIndent = 14;   // how far its cells' IDs sit in

class KeepColorDelegate : public QStyledItemDelegate {
public:
    KeepColorDelegate(FixtureSheetModel *model, QObject *parent) : QStyledItemDelegate(parent), m_model(model) {}

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (index.column() != FixtureSheetModel::Fid) {
            QStyledItemDelegate::paint(p, option, index);
            return;
        }
        // ID column: an arrow for fixtures with cells, cells indented under them.
        const int fid = m_model->fidAt(index.row());
        const bool parent = m_model->hasCells(fid);
        const int level = m_model->depthOf(fid) * kCellIndent;   // heads in one step, their parts two
        const int indent = level + kArrowWidth;
        // Background and selection across the whole cell, then the ID where it belongs.
        QStyleOptionViewItem bg = option;
        initStyleOption(&bg, index);
        const QString text = bg.text;
        bg.text.clear();
        QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &bg, p);
        const QVariant fg = index.data(Qt::ForegroundRole);
        p->save();
        p->setFont(bg.font);
        p->setPen(fg.canConvert<QColor>() ? fg.value<QColor>() : bg.palette.color(QPalette::Text));
        const QRect tr = option.rect.adjusted(indent + 2, 0, -2, 0);
        p->drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, p->fontMetrics().elidedText(text, Qt::ElideRight, tr.width()));
        p->restore();
        if (parent) {
            p->save();
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(Qt::NoPen);
            p->setBrush(m_model->hidesSelection(fid) ? theme::live : theme::muted);
            const QPointF c(option.rect.left() + level + 9, option.rect.center().y() + 0.5);
            QPolygonF tri;
            if (m_model->isExpanded(fid))
                tri << c + QPointF(-4, -2) << c + QPointF(4, -2) << c + QPointF(0, 3);
            else
                tri << c + QPointF(-2, -4) << c + QPointF(-2, 4) << c + QPointF(3, 0);
            p->drawPolygon(tri);
            p->restore();
        }
    }

protected:
    void initStyleOption(QStyleOptionViewItem *opt, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(opt, index);
        const QVariant fg = index.data(Qt::ForegroundRole);
        if (fg.canConvert<QColor>())
            opt->palette.setColor(QPalette::HighlightedText, fg.value<QColor>());
    }

private:
    FixtureSheetModel *m_model = nullptr;
};
} // namespace

FixtureSheet::FixtureSheet(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);
    auto *title = new QLabel(tr("Fixtures"), this);
    title->setProperty("role", "title");
    v->addWidget(title);

    m_model = new FixtureSheetModel(ctx, this);
    m_view = new QTableView(this);
    m_view->setModel(m_model);
    m_view->setItemDelegate(new KeepColorDelegate(m_model, m_view));
    // The arrow in front of a pixel fixture's ID opens and closes its cells (double-click the ID too).
    m_view->viewport()->installEventFilter(this);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Clicks add to the selection (click a selected row again to drop it); Clear empties it.
    m_view->setSelectionMode(QAbstractItemView::MultiSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->verticalHeader()->hide();
    m_view->verticalHeader()->setDefaultSectionSize(26);
    m_view->horizontalHeader()->setHighlightSections(false);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->setShowGrid(false);
    m_view->setAlternatingRowColors(true);
    m_view->setWordWrap(false);
    m_view->setColumnWidth(FixtureSheetModel::Fid, 96);   // room for 101.12.2 at the second level
    m_view->setColumnWidth(FixtureSheetModel::Name, 130);
    m_view->setColumnWidth(FixtureSheetModel::Patch, 70);
    m_view->setColumnWidth(FixtureSheetModel::Dimmer, 70);
    m_view->setColumnWidth(FixtureSheetModel::Position, 100);
    m_view->setColumnWidth(FixtureSheetModel::Gobo, 90);
    m_view->setColumnWidth(FixtureSheetModel::Color, 130);
    m_view->setColumnWidth(FixtureSheetModel::Beam, 80);
    m_view->setColumnWidth(FixtureSheetModel::Focus, 110);
    // Drag a heading to put the columns in another order (saved with the view).
    m_view->horizontalHeader()->setSectionsMovable(true);
    connect(m_view->horizontalHeader(), &QHeaderView::sectionMoved, this, [this]() { panelStateChanged(this); });
    v->addWidget(m_view, 1);
    m_tiles = new FixtureTiles(ctx, m_model, this);
    connect(m_tiles, &FixtureTiles::expandedChanged, this, [this]() {
        syncSelectionFromShow();
        panelStateChanged(this);
    });
    m_tileScroll = new QScrollArea(this);
    m_tileScroll->setWidget(m_tiles);
    m_tileScroll->setWidgetResizable(true);
    m_tileScroll->setFrameShape(QFrame::NoFrame);
    m_tileScroll->hide();
    v->addWidget(m_tileScroll, 1);

    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection &selected, const QItemSelection &deselected) {
        if (m_syncing)
            return;
        // Only what changed: fixtures selected elsewhere (hidden by the filter, other windows,
        // the command line) stay selected.
        QVector<int> add, drop;
        for (const QModelIndex &i : selected.indexes())
            if (i.column() == 0)
                add << m_model->fidAt(i.row());
        for (const QModelIndex &i : deselected.indexes())
            if (i.column() == 0)
                drop << m_model->fidAt(i.row());
        m_syncing = true;
        if (!drop.isEmpty())
            m_ctx->show->select(drop, Show::SelectMode::Remove);
        if (!add.isEmpty())
            m_ctx->show->select(add, Show::SelectMode::Add);
        m_syncing = false;
    });
    connect(ctx->show, &Show::changed, this, [this](Show::Parts parts) {
        if (parts & Show::PatchPart
            || ((parts & Show::ProgrammerPart) && m_model->filter() != FixtureSheetModel::Filter::All))
            m_model->reloadPatch();
        if (parts & Show::ProgrammerPart)
            m_tiles->update();
        if (parts & (Show::ProgrammerPart | Show::PatchPart))
            syncSelectionFromShow();
    });
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, m_model, &FixtureSheetModel::refreshValues);
    timer->start(100);
}

bool FixtureSheet::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_view->viewport() && (ev->type() == QEvent::MouseButtonPress || ev->type() == QEvent::MouseButtonDblClick)) {
        auto *me = static_cast<QMouseEvent *>(ev);
        const QModelIndex i = m_view->indexAt(me->pos());
        if (i.isValid() && i.column() == FixtureSheetModel::Fid) {
            const int fid = m_model->fidAt(i.row());
            const QRect r = m_view->visualRect(i);
            const int x = me->pos().x() - r.left() - m_model->depthOf(fid) * kCellIndent;
            const bool onArrow = x >= 0 && x < kArrowWidth + 4;
            if (m_model->hasCells(fid) && (onArrow || ev->type() == QEvent::MouseButtonDblClick)) {
                m_model->setExpanded(fid, !m_model->isExpanded(fid));
                syncSelectionFromShow();
                panelStateChanged(this);
                return true;   // opening a fixture doesn't select it
            }
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void FixtureSheet::setTiles(bool on)
{
    m_view->setVisible(!on);
    m_tileScroll->setVisible(on);
}

QJsonObject FixtureSheet::saveOptions() const
{
    QJsonObject o;
    if (m_model->filter() != FixtureSheetModel::Filter::All)
        o["show"] = int(m_model->filter());
    if (m_tileScroll->isVisibleTo(const_cast<FixtureSheet *>(this)))
        o["layout"] = QStringLiteral("tiles");
    // Column order, left to right, when it isn't the standard one
    const QHeaderView *h = m_view->horizontalHeader();
    QJsonArray order;
    bool moved = false;
    for (int visual = 0; visual < h->count(); ++visual) {
        const int logical = h->logicalIndex(visual);
        order.append(logical);
        moved |= logical != visual;
    }
    if (moved)
        o["columns"] = order;
    QJsonArray open;
    QList<int> ids = m_model->expanded();
    std::sort(ids.begin(), ids.end());
    for (int id : ids)
        open.append(id);
    if (!open.isEmpty())
        o["expanded"] = open;
    return o;
}

void FixtureSheet::updateTitle()
{
    static const char *titles[] = {QT_TR_NOOP("Fixtures"), QT_TR_NOOP("Patched fixtures"), QT_TR_NOOP("Selected fixtures"),
                                   QT_TR_NOOP("Programmed fixtures")};
    setWindowTitle(tr(titles[std::clamp(int(m_model->filter()), 0, 3)]));
}

void FixtureSheet::loadOptions(const QJsonObject &o)
{
    QList<int> open;
    for (const QJsonValue &v : o["expanded"].toArray())
        open << v.toInt();
    m_model->setExpandedAll(open);
    m_model->setFilter(static_cast<FixtureSheetModel::Filter>(std::clamp(o["show"].toInt(0), 0, 3)));
    setTiles(o["layout"].toString() == QLatin1String("tiles"));
    QHeaderView *h = m_view->horizontalHeader();
    const QSignalBlocker block(h);
    const QJsonArray order = o["columns"].toArray();
    for (int visual = 0; visual < h->count(); ++visual) {
        // Missing or short lists keep the remaining columns in their standard order.
        const int logical = visual < order.size() ? order[visual].toInt(-1) : -1;
        if (logical >= 0 && logical < h->count())
            h->moveSection(h->visualIndex(logical), visual);
    }
    if (order.isEmpty())
        for (int i = 0; i < h->count(); ++i)
            h->moveSection(h->visualIndex(i), i);
    syncSelectionFromShow();
    updateTitle();
}

void FixtureSheet::buildOptions(QuickEditor *ed)
{
    ed->addSegmented(tr("Layout"), {tr("Table"), tr("Tiles")}, m_tileScroll->isVisibleTo(this) ? 1 : 0,
                     [this](int i) { setTiles(i == 1); });
    ed->addSegmented(tr("Show"), {tr("All"), tr("Patched"), tr("Selected"), tr("Programmed")}, int(m_model->filter()),
                     [this](int i) {
                         m_model->setFilter(static_cast<FixtureSheetModel::Filter>(i));
                         syncSelectionFromShow();
                         updateTitle();
                     });
}

void FixtureSheet::syncSelectionFromShow()
{
    if (m_syncing)
        return;
    m_syncing = true;
    QItemSelection sel;
    auto selectRow = [&](int row) {
        if (row >= 0)
            sel.select(m_model->index(row, 0), m_model->index(row, FixtureSheetModel::ColumnCount - 1));
    };
    const QVector<int> &chosen = m_ctx->show->selection();
    for (int fid : chosen)
        selectRow(m_model->rowOf(fid));
    // A label row (a fixture or head that is only its subfixtures) is selected when they all are.
    for (int fid : m_model->fids())
        if (m_model->isLabelRow(fid)) {
            const QVector<int> cells = m_ctx->show->expandMasters({fid});
            if (std::all_of(cells.begin(), cells.end(), [&](int c) { return chosen.contains(c); }))
                selectRow(m_model->rowOf(fid));
        }
    m_view->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
    m_syncing = false;
}

} // namespace lumora
