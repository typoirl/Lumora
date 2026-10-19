// SPDX-License-Identifier: GPL-3.0-or-later
#include "plotsui.h"

#include "appcontext.h"
#include "colorpicker.h"
#include "fixturesheet.h"
#include "quickedit.h"
#include "theme.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <numeric>
#include <cmath>

namespace lumora {

namespace {
constexpr double kGrid = 8.0;
double snap(double v) { return std::round(v / kGrid) * kGrid; }

bool isSvg(const QByteArray &b)
{
    return b.left(512).contains("<svg") || b.trimmed().startsWith("<?xml");
}

QByteArray icon(const char *body)
{
    return QByteArray("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64' fill='none' stroke='#ffffff' "
                      "stroke-width='4' stroke-linecap='round' stroke-linejoin='round'>")
           + body + "</svg>";
}
} // namespace

QVector<QPair<QString, QByteArray>> builtinPlotIcons()
{
    return {
        {QObject::tr("Arrow"), icon("<path d='M8 32h44M38 18l14 14-14 14'/>")},
        {QObject::tr("Truss"), icon("<path d='M4 24h56M4 40h56M4 24l8 16 8-16 8 16 8-16 8 16 8-16 8 16'/>")},
        {QObject::tr("Stage"), icon("<rect x='6' y='14' width='52' height='36' rx='2'/><path d='M6 50h52'/>")},
        {QObject::tr("Speaker"), icon("<rect x='16' y='6' width='32' height='52' rx='4'/><circle cx='32' cy='38' r='9'/><circle cx='32' cy='18' r='4'/>")},
        {QObject::tr("Person"), icon("<circle cx='32' cy='14' r='8'/><path d='M18 58V36a14 14 0 0 1 28 0v22'/>")},
        {QObject::tr("Star"), icon("<path d='M32 6l7.6 16.4L58 24.6 44.6 37.2 48 56 32 46.8 16 56l3.4-18.8L6 24.6l18.4-2.2z'/>")},
        {QObject::tr("Circle"), icon("<circle cx='32' cy='32' r='24'/>")},
        {QObject::tr("Square"), icon("<rect x='10' y='10' width='44' height='44' rx='2'/>")},
        {QObject::tr("Triangle"), icon("<path d='M32 8l26 46H6z'/>")},
        {QObject::tr("Door"), icon("<path d='M14 58V6h36v52M8 58h48'/><circle cx='42' cy='34' r='2.5'/>")},
        {QObject::tr("Camera"), icon("<rect x='6' y='18' width='38' height='28' rx='3'/><path d='M44 26l14-8v28l-14-8'/>")},
        {QObject::tr("Fog"), icon("<path d='M8 26c8-8 16 8 24 0s16 8 24 0M8 40c8-8 16 8 24 0s16 8 24 0'/>")},
        // Fixture symbols: a moving wash (round head of LED cells), a moving profile (one big lens)
        // and a conventional ellipsoidal profile from the side, with its shutters and yoke.
        {QObject::tr("Moving wash"), icon("<rect x='18' y='54' width='28' height='6' rx='2'/><path d='M24 54v-4h16v4M10 44V18h6M54 44V18h-6M10 44h44'/>"
                                           "<circle cx='32' cy='28' r='15'/><circle cx='32' cy='28' r='3' fill='#ffffff'/>"
                                           "<circle cx='32' cy='20' r='2.5' fill='#ffffff'/><circle cx='32' cy='36' r='2.5' fill='#ffffff'/>"
                                           "<circle cx='25' cy='24' r='2.5' fill='#ffffff'/><circle cx='39' cy='24' r='2.5' fill='#ffffff'/>"
                                           "<circle cx='25' cy='32' r='2.5' fill='#ffffff'/><circle cx='39' cy='32' r='2.5' fill='#ffffff'/>")},
        {QObject::tr("Moving profile"), icon("<rect x='18' y='54' width='28' height='6' rx='2'/><path d='M24 54v-4h16v4M10 44V16h6M54 44V16h-6M10 44h44'/>"
                                              "<rect x='16' y='8' width='32' height='36' rx='9'/><circle cx='32' cy='26' r='11'/>"
                                              "<circle cx='32' cy='26' r='5'/>")},
        {QObject::tr("Conventional profile"), icon("<path d='M6 22h10v16H6z'/><path d='M8 22v-3M11 22v-3M14 22v-3M8 38v3M11 38v3M14 38v3'/>"
                                                    "<path d='M16 20l10-6h8v32h-8l-10-6z'/><path d='M34 22h20v16H34z'/>"
                                                    "<path d='M54 20h4v20h-4z'/><path d='M38 22v-5M44 22v-5M38 38v5M44 38v5'/>"
                                                    "<path d='M24 46v8h16v-8M32 54v6M26 60h12'/>")},
    };
}

// ===========================================================================
// Pool
// ===========================================================================

PlotsPanel::PlotsPanel(AppContext *ctx, QWidget *parent) : PoolPanel(tr("Plots"), parent), m_ctx(ctx)
{
    Show *show = ctx->show;
    connect(tiles(), &PoolTiles::tileClicked, this, [this, show](int id, bool occupied, Qt::KeyboardModifiers) {
        if (m_ctx->storeArmed()) {
            m_ctx->setStoreArmed(false);
            m_ctx->run(QStringLiteral("Store Plot %1").arg(id));
            m_ctx->run(QStringLiteral("Plot %1").arg(id));
            return;
        }
        if (m_ctx->tapTarget(QStringLiteral("Plot %1").arg(id), show->plot(id) ? show->plot(id)->name : QString()))
            return;
        if (occupied)
            m_ctx->run(QStringLiteral("Plot %1").arg(id));
        else
            emit m_ctx->status(tr("Empty plot slot: press Store and tap it, or right-click it, to make a plot"));
    });
    connect(tiles(), &PoolTiles::tileMenu, this, [this, show](int id, bool occupied, const QPoint &pos) {
        auto *ed = new QuickEditor(occupied ? show->plot(id)->displayName() : tr("Plot %1").arg(id), this);
        if (!occupied) {
            ed->addAction(tr("New plot"), [this, id]() {
                m_ctx->run(QStringLiteral("Store Plot %1").arg(id));
                m_ctx->run(QStringLiteral("Plot %1").arg(id));
            });
            ed->addAction(tr("New plot with the selected fixtures"), [this, id]() {
                m_ctx->run(QStringLiteral("Store Plot %1").arg(id));
                QStringList ids;
                for (int fid : m_ctx->show->selection())
                    ids << m_ctx->show->idText(fid);
                if (!ids.isEmpty())
                    m_ctx->run(QStringLiteral("Assign Fixture %1 At Plot %2").arg(ids.join(QStringLiteral(" + "))).arg(id));
                m_ctx->run(QStringLiteral("Plot %1").arg(id));
            });
            ed->popup(pos);
            return;
        }
        ed->addNameField(tr("Name"), show->plot(id)->name, [this, id](const QString &n) {
            QString q = n.trimmed();
            q.replace('"', '\'');
            m_ctx->run(QStringLiteral("Label Plot %1 \"%2\"").arg(id).arg(q));
        }, true, tr("Plot %1").arg(id));
        ed->addSection(QString());
        ed->addAction(tr("Show it"), [this, id]() { m_ctx->run(QStringLiteral("Plot %1").arg(id)); });
        ed->addAction(tr("Add the selected fixtures"), [this, id]() {
            QStringList ids;
            for (int fid : m_ctx->show->selection())
                ids << m_ctx->show->idText(fid);
            if (ids.isEmpty())
                emit m_ctx->status(tr("Select fixtures first"));
            else
                m_ctx->run(QStringLiteral("Assign Fixture %1 At Plot %2").arg(ids.join(QStringLiteral(" + "))).arg(id));
        });
        ed->addAction(tr("Delete plot"), [this, id]() { m_ctx->run(QStringLiteral("Delete Plot %1").arg(id)); }, QIcon(), true);
        ed->popup(pos);
    });
    connect(show, &Show::changed, this, [this](Show::Parts p) {
        if (p & Show::PlotsPart)
            refresh();
    });
    connect(ctx, &AppContext::uiValueChanged, this, [this](const QString &key) {
        if (key == QLatin1String("plot"))
            refresh();
    });
    followStore(ctx);
    refresh();
}

void PlotsPanel::refresh()
{
    const int current = m_ctx->uiValue("plot", 0).toInt();
    QVector<PoolItem> items;
    for (const Plot &p : m_ctx->show->plots()) {
        PoolItem it;
        it.id = p.id;
        it.label = p.displayName();
        it.tag = theme::muted;
        it.active = p.id == current;
        int fixtures = 0;
        for (const PlotItem &i : p.items)
            fixtures += i.kind == PlotItem::Fixture;
        it.tooltip = tr("%1: %n fixture(s). Tap to show it", nullptr, fixtures).arg(p.displayName());
        items << it;
    }
    tiles()->setItems(items);
}

// ===========================================================================
// Canvas
// ===========================================================================

PlotCanvas::PlotCanvas(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(ctx->show, &Show::changed, this, [this](Show::Parts p) {
        if (p & Show::PlotsPart)
            m_imageCache.clear();
        update();
    });
    // Fixtures follow the output live.
    auto *live = new QTimer(this);
    connect(live, &QTimer::timeout, this, [this]() {
        if (isVisible())
            update();
    });
    live->start(80);
}

const Plot *PlotCanvas::plot() const
{
    if (m_dragging)
        return &m_dragPlot;
    return m_ctx->show->plot(m_plotId);
}

void PlotCanvas::setPlotId(int id)
{
    if (m_plotId == id)
        return;
    m_plotId = id;
    m_picked.clear();
    m_imageCache.clear();
    update();
    emit zoomChanged();
}

void PlotCanvas::setEditing(bool on)
{
    m_editing = on;
    m_picked.clear();
    update();
}

const PlotCanvas::ViewState &PlotCanvas::view() const
{
    auto it = m_views.find(m_plotId);
    if (it == m_views.end()) {
        // First time this window shows the plot: start from what the plot itself kept.
        ViewState v;
        if (const Plot *p = m_ctx->show->plot(m_plotId)) {
            v.zoom = p->zoom;
            v.panX = p->panX;
            v.panY = p->panY;
            v.locked = p->zoomLocked;
        }
        it = m_views.insert(m_plotId, v);
    }
    return *it;
}

void PlotCanvas::setZoomLocked(bool on)
{
    view();
    if (m_views[m_plotId].locked == on)
        return;
    m_views[m_plotId].locked = on;
    emit zoomChanged();
    emit viewChanged();
}

QJsonObject PlotCanvas::saveViews() const
{
    QJsonObject o;
    for (auto it = m_views.begin(); it != m_views.end(); ++it)
        if (m_ctx->show->plot(it.key()))
            o[QString::number(it.key())] = QJsonObject{{"zoom", it->zoom}, {"x", it->panX}, {"y", it->panY}, {"locked", it->locked}};
    return o;
}

void PlotCanvas::loadViews(const QJsonObject &o)
{
    m_views.clear();
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QJsonObject v = it->toObject();
        ViewState st;
        st.zoom = std::clamp(v["zoom"].toDouble(1.0), 0.1, 8.0);
        st.panX = v["x"].toDouble();
        st.panY = v["y"].toDouble();
        st.locked = v["locked"].toBool();
        m_views.insert(it.key().toInt(), st);
    }
    update();
    emit zoomChanged();
}

QPointF PlotCanvas::toPlot(const QPointF &w) const
{
    const ViewState &v = view();
    return {v.panX + w.x() / v.zoom, v.panY + w.y() / v.zoom};
}

QPointF PlotCanvas::toWidget(const QPointF &pt) const
{
    const ViewState &v = view();
    return {(pt.x() - v.panX) * v.zoom, (pt.y() - v.panY) * v.zoom};
}

QRectF PlotCanvas::toWidget(const QRectF &r) const
{
    return {toWidget(r.topLeft()), toWidget(r.bottomRight())};
}

QPointF PlotCanvas::viewCenter() const
{
    return toPlot(QPointF(width() / 2.0, height() / 2.0));
}

void PlotCanvas::commit(const Plot &p)
{
    m_ctx->show->setPlot(p);
}

void PlotCanvas::setView(double zoom, double panX, double panY)
{
    if (!m_ctx->show->plot(m_plotId))
        return;
    view();
    ViewState &v = m_views[m_plotId];
    v.zoom = std::clamp(zoom, 0.1, 8.0);
    v.panX = panX;
    v.panY = panY;
    update();
    emit zoomChanged();
    emit viewChanged();
}

void PlotCanvas::zoomBy(double factor)
{
    const Plot *p = m_ctx->show->plot(m_plotId);
    if (!p || zoomLocked())
        return;
    const QPointF c = viewCenter();
    const double z = std::clamp(zoom() * factor, 0.1, 8.0);
    setView(z, c.x() - width() / 2.0 / z, c.y() - height() / 2.0 / z);
}

void PlotCanvas::fit()
{
    const Plot *p = m_ctx->show->plot(m_plotId);
    if (!p || zoomLocked())
        return;
    QRectF b = p->bounds();
    if (b.isNull())
        b = QRectF(0, 0, 400, 300);
    b.adjust(-24, -24, 24, 24);
    const double z = std::clamp(std::min(width() / b.width(), height() / b.height()), 0.1, 8.0);
    setView(z, b.center().x() - width() / 2.0 / z, b.center().y() - height() / 2.0 / z);
}

QPixmap PlotCanvas::renderImage(const QByteArray &bytes, const QSize &size, const QString &tint)
{
    QPixmap pm(size);
    pm.fill(Qt::transparent);
    if (size.isEmpty())
        return pm;
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (isSvg(bytes)) {
        QSvgRenderer svg(bytes);
        QSizeF s = svg.defaultSize();
        if (s.isEmpty())
            s = size;
        // Fit inside, keeping its proportions.
        const double k = std::min(size.width() / s.width(), size.height() / s.height());
        const QRectF r((size.width() - s.width() * k) / 2, (size.height() - s.height() * k) / 2, s.width() * k, s.height() * k);
        svg.render(&p, r);
    } else {
        const QImage img = QImage::fromData(bytes);
        if (!img.isNull()) {
            const QImage scaled = img.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.drawImage(QPointF((size.width() - scaled.width()) / 2.0, (size.height() - scaled.height()) / 2.0), scaled);
        }
    }
    if (!tint.isEmpty()) {   // one color for a line icon
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), QColor(tint));
    }
    return pm;
}

QString PlotCanvas::objectTitle(const QString &object, QString *kind) const
{
    Show *show = m_ctx->show;
    const QStringList parts = object.split(' ');
    const QString k = parts.value(0);
    const QString num = parts.value(1);
    const int n = num.section('.', -1).toInt();
    *kind = object;
    if (k == QLatin1String("Group")) {
        if (const Group *g = show->group(n)) return g->name;
    } else if (k == QLatin1String("Preset")) {
        const int type = num.section('.', 0, 0).toInt();
        *kind = presetRef(type, n);
        if (const Preset *p = show->preset(type, n)) return p->name;
    } else if (k == QLatin1String("Sequence")) {
        if (const Sequence *s = show->sequence(n)) return s->name;
    } else if (k == QLatin1String("Macro")) {
        if (const Macro *m = show->macro(n)) return m->displayName();
    } else if (k == QLatin1String("Effect")) {
        if (const Effect *e = show->effect(n)) return e->name;
    } else if (k == QLatin1String("View")) {
        if (const StoredView *v = show->view(n)) return v->name;
    } else if (k == QLatin1String("Spread")) {
        if (const Spread *s = show->spread(n)) return s->name;
    } else if (k == QLatin1String("Plot")) {
        if (const Plot *p = show->plot(n)) return p->displayName();
    } else if (k == QLatin1String("RateMaster")) {
        if (const RateMaster *r = show->rateMaster(n)) return r->displayName() + QStringLiteral(" · ") + r->valueText();
    } else if (k == QLatin1String("Executor")) {
        const int page = num.section('.', 0, 0).toInt();
        const Executor *e = show->executor(page, n);
        const Sequence *s = e ? show->sequence(e->sequenceId) : nullptr;
        return s ? s->name : tr("Empty");
    }
    return tr("(missing)");
}

bool PlotCanvas::objectActive(const QString &object) const
{
    Show *show = m_ctx->show;
    const QStringList parts = object.split(' ');
    const QString k = parts.value(0);
    const QString num = parts.value(1);
    const int n = num.section('.', -1).toInt();
    if (k == QLatin1String("Group")) {
        const Group *g = show->group(n);
        return g && !show->selection().isEmpty() && show->selection() == g->fixtures;
    }
    if (k == QLatin1String("Preset")) {
        const int type = num.section('.', 0, 0).toInt();
        for (const AttrValues &a : show->programmerValues())
            for (const ValueRef &v : a)
                if (v.presetType == type && v.presetId == n)
                    return true;
        return false;
    }
    if (k == QLatin1String("Sequence")) {
        const Executor *e = show->executorForSequence(n);
        return e && e->pb().isOn();
    }
    if (k == QLatin1String("Executor")) {
        const Executor *e = show->executor(num.section('.', 0, 0).toInt(), n);
        return e && e->pb().isOn();
    }
    if (k == QLatin1String("Macro"))
        return show->macroRun(n) != nullptr;
    if (k == QLatin1String("Effect")) {
        for (const EffectInstance &i : show->programmerEffects())
            if (i.effectId == n)
                return true;
        return false;
    }
    if (k == QLatin1String("Plot"))
        return m_ctx->uiValue("plot", 0).toInt() == n;
    return false;
}

void PlotCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0x12, 0x15, 0x1a));
    const Plot *pl = plot();
    if (!pl) {
        p.setPen(theme::muted);
        p.drawText(rect(), Qt::AlignCenter, tr("No plot. Tap one in the Plots pool, or right-click an empty slot there to make one."));
        return;
    }
    // A faint grid while editing, so things line up.
    if (m_editing) {
        const double step = kGrid * 4 * view().zoom;
        if (step >= 8) {
            p.setPen(QPen(QColor(255, 255, 255, 14), 1));
            const QPointF o = toWidget(QPointF(0, 0));
            for (double x = std::fmod(o.x(), step); x < width(); x += step)
                p.drawLine(QPointF(x, 0), QPointF(x, height()));
            for (double y = std::fmod(o.y(), step); y < height(); y += step)
                p.drawLine(QPointF(0, y), QPointF(width(), y));
        }
    }
    Show *show = m_ctx->show;
    const QVector<int> &sel = show->selection();
    auto drawImage = [&](const PlotItem &it, int index) {
        const QRectF r = toWidget(it.rect);
        if (it.imageName == QLatin1String("Truss")) {
            // Truss stretches: two chords the length of the item and a cross section for every
            // square of it, so a longer truss gets more sections, not a wider picture.
            const bool across = r.width() >= r.height();
            const double len = across ? r.width() : r.height(), depth = across ? r.height() : r.width();
            const int sections = std::max(1, int(std::lround(len / std::max(4.0, depth))));
            const double step = len / sections;
            p.save();
            p.translate(r.topLeft());
            if (!across) {   // drawn along x, then turned upright
                p.translate(r.width(), 0);
                p.rotate(90);
            }
            const QColor ink = it.color.isEmpty() ? theme::text : QColor(it.color);
            p.setPen(QPen(ink, std::clamp(depth * 0.08, 1.0, 4.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            const double inset = std::clamp(depth * 0.12, 1.0, 6.0);
            const double top = inset, bottom = depth - inset;
            p.drawLine(QPointF(0, top), QPointF(len, top));
            p.drawLine(QPointF(0, bottom), QPointF(len, bottom));
            for (int k = 0; k <= sections; ++k)
                p.drawLine(QPointF(k * step, top), QPointF(k * step, bottom));
            for (int k = 0; k < sections; ++k) {
                const bool up = k % 2 == 0;
                p.drawLine(QPointF(k * step, up ? bottom : top), QPointF((k + 1) * step, up ? top : bottom));
            }
            p.restore();
            return;
        }
        const QSize size(std::max(1, int(r.width())), std::max(1, int(r.height())));
        const QString key = QStringLiteral("%1|%2x%3|%4").arg(index).arg(size.width()).arg(size.height()).arg(it.color);
        auto c = m_imageCache.constFind(key);
        if (c == m_imageCache.constEnd())
            c = m_imageCache.insert(key, renderImage(it.image, size, it.color));
        p.drawPixmap(r.topLeft(), *c);
    };
    // Backgrounds first.
    for (int i = 0; i < pl->items.size(); ++i)
        if (pl->items[i].kind == PlotItem::Image && pl->items[i].background)
            drawImage(pl->items[i], i);
    for (int i = 0; i < pl->items.size(); ++i) {
        const PlotItem &it = pl->items[i];
        const QRectF r = toWidget(it.rect);
        switch (it.kind) {
        case PlotItem::Image:
            if (!it.background)
                drawImage(it, i);
            break;
        case PlotItem::Text: {
            QFont f = theme::uiFont(9, QFont::DemiBold);
            f.setPixelSize(std::max(4, int(it.fontSize * view().zoom)));
            p.setFont(f);
            p.setPen(it.color.isEmpty() ? theme::text : QColor(it.color));
            p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, it.text);
            break;
        }
        case PlotItem::Object: {
            QString kind;
            const QString title = it.text.isEmpty() ? objectTitle(it.object, &kind) : it.text;
            if (!it.text.isEmpty())
                objectTitle(it.object, &kind);
            const bool on = objectActive(it.object);
            p.setPen(QPen(on ? theme::live : theme::line, on ? 2 : 1));
            p.setBrush(on ? theme::mix(theme::raised, theme::live, 0.18) : theme::raised);
            p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
            QFont small = theme::uiFont(7);
            small.setPixelSize(std::clamp(int(10 * view().zoom), 6, 18));
            p.setFont(small);
            p.setPen(theme::muted);
            p.drawText(r.adjusted(6, 3, -4, 0), Qt::AlignLeft | Qt::AlignTop, kind);
            QFont big = theme::uiFont(9, QFont::DemiBold);
            big.setPixelSize(std::clamp(int(13 * view().zoom), 6, 30));
            p.setFont(big);
            p.setPen(on ? theme::live : theme::text);
            p.drawText(r.adjusted(6, 0, -4, -3), Qt::AlignLeft | Qt::AlignBottom | Qt::TextWordWrap, title);
            break;
        }
        case PlotItem::Fixture: {
            const bool exists = show->hasFixture(it.fid);
            const RawAttr out = show->lastOutput().value(it.fid);
            const FixtureMode *mode = show->fixtureMode(it.fid);
            // Intensity and color, linked by default: the symbol lights in the fixture's color, as
            // bright as its intensity, with a level bar along the bottom.
            double level = 0.0;
            if (out.contains(QStringLiteral("Dimmer")))
                level = out.value(QStringLiteral("Dimmer"));
            else
                for (const char *a : {"ColorAdd_R", "ColorAdd_G", "ColorAdd_B", "ColorAdd_W"})
                    level = std::max(level, out.value(QLatin1String(a), 0.0));
            QColor color = approximateColor(out, mode);
            if (!color.isValid() || color == QColor(0, 0, 0))
                color = QColor(255, 244, 224);   // a white fixture
            const QColor body(0x1c, 0x20, 0x27);
            const QColor lit = theme::mix(body, color, std::clamp(level, 0.0, 1.0));
            QPainterPath shape;
            if (it.round)
                shape.addEllipse(r.adjusted(1, 1, -1, -1));
            else
                shape.addRoundedRect(r.adjusted(1, 1, -1, -1), 5, 5);
            p.setPen(Qt::NoPen);
            p.setBrush(lit);
            p.drawPath(shape);
            const bool selected = sel.contains(it.fid);
            const bool programmed = show->programmerValues().contains(it.fid);
            QPen outline(selected ? theme::live : exists ? theme::line : theme::faint, selected ? 2.5 : 1,
                         exists ? Qt::SolidLine : Qt::DashLine);
            p.setPen(outline);
            p.setBrush(Qt::NoBrush);
            p.drawPath(shape);
            if (programmed) {   // in the programmer: a coral dot in the corner
                const double d = std::clamp(std::min(r.width(), r.height()) * 0.16, 3.0, 9.0);
                p.setPen(QPen(QColor(0x12, 0x15, 0x1a), 1));
                p.setBrush(theme::programmer);
                p.drawEllipse(it.round ? QPointF(r.right() - d * 0.6, r.top() + d * 0.6) : QPointF(r.right() - d - 2, r.top() + d + 2), d / 2, d / 2);
            }
            if (!it.round && level > 0.001 && r.height() > 18) {
                const QRectF bar(r.left() + 5, r.bottom() - 6, (r.width() - 10) * level, 3);
                p.setPen(Qt::NoPen);
                p.setBrush(color.lightnessF() > 0.6 ? QColor(0, 0, 0, 120) : QColor(255, 255, 255, 170));
                p.drawRoundedRect(bar, 1.5, 1.5);
            }
            if (r.width() >= 16) {
                QFont f = theme::uiFont(8, QFont::DemiBold);
                f.setPixelSize(std::clamp(int(std::min(r.width(), r.height()) * 0.26), 6, 28));
                p.setFont(f);
                const bool darkText = lit.lightnessF() > 0.6;
                p.setPen(darkText ? QColor(20, 20, 20) : theme::text);
                const QString label = it.text.isEmpty() ? show->idText(it.fid) : it.text;
                p.drawText(it.round ? r : r.adjusted(4, 2, -3, -6), it.round ? Qt::AlignCenter : (Qt::AlignLeft | Qt::AlignTop), label);
            }
            break;
        }
        }
        // A linked item shows what it stands for: the fixture selected, or the object in use.
        if (!it.link.isEmpty() && it.kind != PlotItem::Fixture) {
            bool on = false;
            if (it.link.startsWith(QLatin1String("Fixture "), Qt::CaseInsensitive))
                on = sel.contains(show->parseId(it.link.mid(8).trimmed()));
            else
                on = objectActive(it.link);
            if (on) {
                p.setPen(QPen(theme::live, 2));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r.adjusted(-3, -3, 3, 3), 4, 4);
            }
        }
        if (m_editing && m_picked.contains(i)) {
            p.setPen(QPen(theme::programmer, 1.5, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r.adjusted(-3, -3, 3, 3));
            if (m_picked.size() == 1) {   // the resize handle
                p.setPen(Qt::NoPen);
                p.setBrush(theme::programmer);
                p.drawRect(QRectF(r.right() - 3, r.bottom() - 3, 8, 8));
            }
        }
    }
    if (m_drag == Lasso) {
        p.setPen(QPen(theme::live, 1, Qt::DashLine));
        QColor fill = theme::live;
        fill.setAlpha(30);
        p.setBrush(fill);
        p.drawRect(QRectF(m_pressPos, m_lastPos).normalized());
    }
    if (m_editing) {
        p.setFont(theme::uiFont(8));
        p.setPen(theme::programmer);
        p.drawText(rect().adjusted(8, 0, -8, -6), Qt::AlignLeft | Qt::AlignBottom,
                   tr("Editing: drag to move, the corner square resizes, Delete removes, right-click for settings"));
    }
}

int PlotCanvas::itemAt(const QPointF &w, bool includeBackground) const
{
    const Plot *pl = plot();
    if (!pl)
        return -1;
    for (int i = int(pl->items.size()) - 1; i >= 0; --i) {
        const PlotItem &it = pl->items[i];
        if (it.kind == PlotItem::Image && it.background && !includeBackground)
            continue;
        if (toWidget(it.rect).adjusted(-2, -2, 2, 2).contains(w))
            return i;
    }
    if (includeBackground)
        for (int i = int(pl->items.size()) - 1; i >= 0; --i)
            if (pl->items[i].kind == PlotItem::Image && pl->items[i].background && toWidget(pl->items[i].rect).contains(w))
                return i;
    return -1;
}

void PlotCanvas::tapObject(const QString &object)
{
    Show *show = m_ctx->show;
    const QStringList parts = object.split(' ');
    const QString k = parts.value(0);
    const QString num = parts.value(1);
    const int n = num.section('.', -1).toInt();
    // Move, Copy, Assign… waiting for a target take it.
    if (m_ctx->tapTarget(object))
        return;
    // Otherwise what its pool tile does.
    if (k == QLatin1String("Group")) {
        show->selectGroup(n, Show::SelectMode::Add);
    } else if (k == QLatin1String("Preset")) {
        const int type = num.section('.', 0, 0).toInt();
        if (show->applyPreset(type, n))
            emit m_ctx->status(tr("Preset %1 applied").arg(presetRef(type, n)));
    } else if (k == QLatin1String("Sequence")) {
        show->setSelectedSequence(n);
    } else if (k == QLatin1String("Effect")) {
        if (show->selection().isEmpty())
            m_ctx->requestEditEffect(n);
        else
            m_ctx->run(QStringLiteral("Effect %1").arg(n));
    } else if (k == QLatin1String("Spread")) {
        const Spread *sp = show->spread(n);
        const bool on = sp && show->activeSpread().toJson() == sp->toJson();
        m_ctx->run(on ? QStringLiteral("Spread Off") : QStringLiteral("Spread %1").arg(n));
    } else if (k == QLatin1String("Macro")) {
        m_ctx->run(QStringLiteral("Go Macro %1").arg(n));
    } else if (k == QLatin1String("Executor")) {
        m_ctx->run(QStringLiteral("Go Executor %1").arg(num));
    } else if (k == QLatin1String("RateMaster")) {
        show->tapRateMaster(n);
    } else {
        m_ctx->run(object);   // View 2, Plot 3
    }
}

void PlotCanvas::mousePressEvent(QMouseEvent *ev)
{
    const Plot *pl = m_ctx->show->plot(m_plotId);
    if (!pl)
        return;
    m_pressPos = m_lastPos = ev->position();
    // Middle button (or Space held): move the sheet about, unless the zoom is locked.
    if (ev->button() == Qt::MiddleButton) {
        if (!zoomLocked())
            m_drag = Pan;
        return;
    }
    if (ev->button() != Qt::LeftButton)
        return;
    const int hit = itemAt(ev->position(), m_editing);
    if (!m_editing) {
        if (hit >= 0 && !pl->items[hit].link.isEmpty()) {
            tapLink(pl->items[hit].link);
            return;
        }
        if (hit >= 0 && pl->items[hit].kind == PlotItem::Fixture) {
            // Toggle: in the selection, or out of it again.
            const int fid = pl->items[hit].fid;
            if (m_ctx->tapTarget(QStringLiteral("Fixture %1").arg(m_ctx->show->idText(fid))))
                return;
            m_ctx->show->select({fid}, m_ctx->show->selection().contains(fid) ? Show::SelectMode::Remove : Show::SelectMode::Add);
            return;
        }
        if (hit >= 0 && pl->items[hit].kind == PlotItem::Object) {
            tapObject(pl->items[hit].object);
            return;
        }
        m_drag = Lasso;
        return;
    }
    // Editing
    m_dragPlot = *pl;
    if (m_picked.size() == 1) {
        const QRectF r = toWidget(pl->items[m_picked.first()].rect);
        if (QRectF(r.right() - 6, r.bottom() - 6, 14, 14).contains(ev->position())) {
            m_drag = Resize;
            m_startRects = {pl->items[m_picked.first()].rect};
            m_dragging = true;
            return;
        }
    }
    if (hit < 0) {
        if (!(ev->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)))
            m_picked.clear();
        m_drag = Lasso;
        update();
        return;
    }
    // A grouped item brings its whole group.
    const QVector<int> together = pl->groupOf(hit);
    if (ev->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) {
        if (m_picked.contains(hit)) {
            for (int i : together)
                m_picked.removeAll(i);
        } else {
            for (int i : together)
                if (!m_picked.contains(i))
                    m_picked << i;
        }
    } else if (!m_picked.contains(hit)) {
        m_picked = together;
    }
    m_startRects.clear();
    for (int i : m_picked)
        m_startRects << pl->items[i].rect;
    m_drag = Move;
    m_dragging = true;
    update();
}

void PlotCanvas::mouseMoveEvent(QMouseEvent *ev)
{
    const QPointF pos = ev->position();
    const QPointF delta = pos - m_lastPos;
    m_lastPos = pos;
    const Plot *pl = m_ctx->show->plot(m_plotId);
    if (!pl)
        return;
    switch (m_drag) {
    case Pan:
        setView(view().zoom, view().panX - delta.x() / view().zoom, view().panY - delta.y() / view().zoom);
        break;
    case Lasso:
        update();
        break;
    case Move: {
        const QPointF d = (pos - m_pressPos) / view().zoom;
        for (int k = 0; k < m_picked.size(); ++k) {
            QRectF r = m_startRects.value(k);
            r.moveTopLeft(QPointF(snap(r.left() + d.x()), snap(r.top() + d.y())));
            m_dragPlot.items[m_picked[k]].rect = r;
        }
        update();
        break;
    }
    case Resize: {
        const QPointF d = (pos - m_pressPos) / view().zoom;
        QRectF r = m_startRects.value(0);
        r.setWidth(std::max(kGrid, snap(r.width() + d.x())));
        r.setHeight(std::max(kGrid, snap(r.height() + d.y())));
        if (m_dragPlot.items[m_picked.first()].kind == PlotItem::Fixture && m_dragPlot.items[m_picked.first()].round)
            r.setHeight(r.width());   // circles stay round
        m_dragPlot.items[m_picked.first()].rect = r;
        update();
        break;
    }
    case None:
        break;
    }
}

void PlotCanvas::mouseReleaseEvent(QMouseEvent *ev)
{
    const Drag was = m_drag;
    m_drag = None;
    if (was == Move || was == Resize) {
        m_dragging = false;
        commit(m_dragPlot);
        return;
    }
    if (was != Lasso)
        return;
    const Plot *pl = m_ctx->show->plot(m_plotId);
    if (!pl)
        return;
    const QRectF box = QRectF(m_pressPos, ev->position()).normalized();
    update();
    if (box.width() < 3 && box.height() < 3)
        return;
    if (m_editing) {
        for (int i = 0; i < pl->items.size(); ++i)
            if (!(pl->items[i].kind == PlotItem::Image && pl->items[i].background) && box.intersects(toWidget(pl->items[i].rect)))
                for (int j : pl->groupOf(i))
                    if (!m_picked.contains(j))
                        m_picked << j;
        update();
        return;
    }
    // Fixtures inside the box join the selection, left to right, top to bottom.
    QVector<QPair<QPointF, int>> hits;
    for (const PlotItem &it : pl->items) {
        if (!box.intersects(toWidget(it.rect)))
            continue;
        // A fixture, or a picture or text standing for one.
        int fid = 0;
        if (it.link.startsWith(QLatin1String("Fixture "), Qt::CaseInsensitive))
            fid = m_ctx->show->parseId(it.link.mid(8).trimmed());
        else if (it.link.isEmpty() && it.kind == PlotItem::Fixture)
            fid = it.fid;
        if (fid && m_ctx->show->hasFixture(fid))
            hits.append({it.rect.center(), fid});
    }
    std::sort(hits.begin(), hits.end(), [](const auto &a, const auto &b) {
        if (std::fabs(a.first.y() - b.first.y()) > 12)
            return a.first.y() < b.first.y();
        return a.first.x() < b.first.x();
    });
    QVector<int> fids;
    for (const auto &h : hits)
        fids << h.second;
    if (!fids.isEmpty())
        m_ctx->show->select(fids, Show::SelectMode::Add);
}

void PlotCanvas::mouseDoubleClickEvent(QMouseEvent *ev)
{
    if (!m_editing)
        return;
    const int hit = itemAt(ev->position(), true);
    if (hit >= 0)
        itemMenu(hit, ev->globalPosition().toPoint());
}

void PlotCanvas::wheelEvent(QWheelEvent *ev)
{
    const Plot *pl = m_ctx->show->plot(m_plotId);
    if (!pl || zoomLocked())
        return;
    if (ev->modifiers() & Qt::ControlModifier) {
        // Zoom round the pointer.
        const QPointF at = toPlot(ev->position());
        const double z = std::clamp(view().zoom * (ev->angleDelta().y() > 0 ? 1.15 : 1 / 1.15), 0.1, 8.0);
        setView(z, at.x() - ev->position().x() / z, at.y() - ev->position().y() / z);
        return;
    }
    const QPoint d = ev->angleDelta();
    const bool sideways = ev->modifiers() & Qt::ShiftModifier;
    setView(view().zoom, view().panX - (sideways ? d.y() : d.x()) / 2.0 / view().zoom, view().panY - (sideways ? 0 : d.y()) / 2.0 / view().zoom);
}

void PlotCanvas::keyPressEvent(QKeyEvent *ev)
{
    if (m_editing && (ev->key() == Qt::Key_Delete || ev->key() == Qt::Key_Backspace) && !m_picked.isEmpty()) {
        const Plot *pl = m_ctx->show->plot(m_plotId);
        if (!pl)
            return;
        Plot p = *pl;
        QVector<int> gone = m_picked;
        std::sort(gone.begin(), gone.end(), std::greater<int>());
        for (int i : gone)
            if (i < p.items.size())
                p.items.removeAt(i);
        m_picked.clear();
        commit(p);
        return;
    }
    QWidget::keyPressEvent(ev);
}

void PlotCanvas::contextMenuEvent(QContextMenuEvent *ev)
{
    const int hit = itemAt(ev->pos(), m_editing);
    if (hit < 0) {
        emit backgroundMenu(ev->globalPos());
        return;
    }
    if (!m_editing) {
        // Out of Edit, right-click gives the item's settings too; Edit moves things.
        itemMenu(hit, ev->globalPos());
        return;
    }
    itemMenu(hit, ev->globalPos());
}

QString PlotCanvas::normalizeLink(const QString &text) const
{
    // "Fixture 21.3", "F 101", "101" (a fixture), "Group 3", "Color.1", "Preset 4.1", "Macro 2"…
    QString t = text.simplified();
    if (t.isEmpty())
        return QString();
    static const QRegularExpression poolDot(QStringLiteral("^([A-Za-z]+)\\.(\\d+)$"));
    if (const auto mt = poolDot.match(t); mt.hasMatch())
        if (const int type = presetTypeFromName(mt.captured(1)))
            return QStringLiteral("Preset %1.%2").arg(type).arg(mt.captured(2));
    QStringList parts = t.split(' ');
    if (parts.size() == 1)
        parts.prepend(QStringLiteral("Fixture"));
    static const QStringList kinds = {"Fixture", "Group", "Preset", "Sequence", "Executor", "Macro", "Effect",
                                      "View", "Spread", "Plot", "RateMaster"};
    QString kind;
    for (const QString &k : kinds)
        if (k.startsWith(parts[0], Qt::CaseInsensitive) && parts[0].size() >= 1)
            if (kind.isEmpty() || parts[0].compare(k, Qt::CaseInsensitive) == 0)
                kind = k;
    if (parts[0].compare(QLatin1String("f"), Qt::CaseInsensitive) == 0 || parts[0].compare(QLatin1String("fix"), Qt::CaseInsensitive) == 0)
        kind = QStringLiteral("Fixture");
    if (kind.isEmpty() || parts.size() != 2)
        return QString();
    if (kind == QLatin1String("Fixture"))
        return m_ctx->show->parseId(parts[1]) ? QStringLiteral("Fixture %1").arg(parts[1]) : QString();
    bool ok = false;
    parts[1].section('.', -1).toInt(&ok);
    return ok ? kind + ' ' + parts[1] : QString();
}

void PlotCanvas::tapLink(const QString &link)
{
    if (link.startsWith(QLatin1String("Fixture "), Qt::CaseInsensitive)) {
        const int fid = m_ctx->show->parseId(link.mid(8).trimmed());
        if (!fid || !m_ctx->show->hasFixture(fid))
            return;
        if (m_ctx->tapTarget(QStringLiteral("Fixture %1").arg(m_ctx->show->idText(fid))))
            return;
        m_ctx->show->select({fid}, m_ctx->show->selection().contains(fid) ? Show::SelectMode::Remove : Show::SelectMode::Add);
        return;
    }
    tapObject(link);
}

void PlotCanvas::itemMenu(int index, const QPoint &global)
{
    const Plot *pl = m_ctx->show->plot(m_plotId);
    if (!pl || index < 0 || index >= pl->items.size())
        return;
    const int id = m_plotId;
    Show *show = m_ctx->show;
    auto change = [show, id, index](std::function<void(PlotItem &)> fn) {
        const Plot *cur = show->plot(id);
        if (!cur || index >= cur->items.size())
            return;
        Plot p = *cur;
        fn(p.items[index]);
        show->setPlot(p);
    };
    const PlotItem &it = pl->items[index];
    QString title;
    switch (it.kind) {
    case PlotItem::Fixture: title = tr("Fixture %1").arg(show->idText(it.fid)); break;
    case PlotItem::Object: title = it.object; break;
    case PlotItem::Text: title = tr("Text"); break;
    case PlotItem::Image: title = it.background ? tr("Background") : tr("Picture"); break;
    }
    auto *ed = new QuickEditor(title, this);
    if (it.kind == PlotItem::Text) {
        ed->addNameField(tr("Text"), it.text, [change](const QString &t) { change([t](PlotItem &x) { x.text = t; }); });
        ed->addNumber(tr("Size"), 6, 200, int(it.fontSize), [change](int v) { change([v](PlotItem &x) { x.fontSize = v; }); });
    } else {
        ed->addNameField(it.kind == PlotItem::Image ? tr("Label") : tr("Label (empty: its own name)"), it.text,
                         [change](const QString &t) { change([t](PlotItem &x) { x.text = t.trimmed(); }); }, true);
    }
    if (it.kind == PlotItem::Fixture)
        ed->addSegmented(tr("Shape"), {tr("Square"), tr("Circle")}, it.round ? 1 : 0, [change](int i) {
            change([i](PlotItem &x) {
                x.round = i == 1;
                if (x.round)
                    x.rect.setHeight(x.rect.width());
            });
        });
    if (it.kind == PlotItem::Image)
        ed->addSegmented(tr("Layer"), {tr("Picture"), tr("Background")}, it.background ? 1 : 0,
                         [change](int i) { change([i](PlotItem &x) { x.background = i == 1; }); });
    if (it.kind == PlotItem::Text || it.kind == PlotItem::Image)
        ed->addAction(it.kind == PlotItem::Text ? tr("Color…") : tr("Tint (one-color icons)…"), [this, change, it]() {
            if (const auto c = pickAppearanceColor(this, tr("Color"), it.color))
                change([c](PlotItem &x) { x.color = *c; });
        });
    if (it.kind != PlotItem::Object) {
        // Linked: tapped, it acts like the fixture or pool object it stands for.
        ed->addAction(it.link.isEmpty() ? tr("Link to a fixture or pool object…") : tr("Linked to %1: change…").arg(it.link),
                      [this, change, current = it.link]() {
            bool ok = false;
            const QString t = QInputDialog::getText(this, tr("Link"),
                                                    tr("Tapping it acts like: a fixture (Fixture 21.3, or just 101) or a pool object "
                                                       "(Group 3, Color.1, Preset 4.1, Sequence 2, Executor 1.201, Macro 1, Effect 5, "
                                                       "View 2, Spread 1, Plot 2, RateMaster 3). Empty: no link."),
                                                    QLineEdit::Normal, current, &ok);
            if (!ok)
                return;
            const QString link = normalizeLink(t);
            if (!t.trimmed().isEmpty() && link.isEmpty()) {
                emit m_ctx->status(tr("Not a fixture or pool object: %1").arg(t.trimmed()));
                return;
            }
            change([link](PlotItem &x) { x.link = link; });
        });
        if (!it.link.isEmpty())
            ed->addAction(tr("Unlink"), [change]() { change([](PlotItem &x) { x.link.clear(); }); });
    }
    // The items this acts on: the pick when it's among them, else its group.
    QVector<int> set = m_picked.contains(index) ? m_picked : pl->groupOf(index);
    std::sort(set.begin(), set.end());
    ed->addSection(tr("Arrange"));
    auto arrange = [this, show, id, set](int how) {
        const Plot *cur = show->plot(id);
        if (!cur)
            return;
        Plot p = *cur;
        QVector<int> order(p.items.size());
        std::iota(order.begin(), order.end(), 0);
        auto in = [&set](int i) { return set.contains(i); };
        switch (how) {
        case 0: {   // to front: the set last, keeping its order
            QVector<int> rest, mine;
            for (int i : order) (in(i) ? mine : rest) << i;
            order = rest + mine;
            break;
        }
        case 1:   // forward one: each past the next item above it that isn't in the set
            for (int k = int(order.size()) - 2; k >= 0; --k)
                if (in(order[k]) && !in(order[k + 1]))
                    std::swap(order[k], order[k + 1]);
            break;
        case 2:   // backward one
            for (int k = 1; k < order.size(); ++k)
                if (in(order[k]) && !in(order[k - 1]))
                    std::swap(order[k], order[k - 1]);
            break;
        case 3: {   // to back (backgrounds stay behind regardless)
            QVector<int> rest, mine;
            for (int i : order) (in(i) ? mine : rest) << i;
            order = mine + rest;
            break;
        }
        }
        QVector<PlotItem> items;
        QVector<int> picked;
        for (int k = 0; k < order.size(); ++k) {
            items << p.items[order[k]];
            if (in(order[k]))
                picked << k;
        }
        p.items = items;
        m_picked = m_editing ? picked : QVector<int>();
        show->setPlot(p);
    };
    ed->addAction(tr("Bring to front"), [arrange]() { arrange(0); });
    ed->addAction(tr("Bring forward"), [arrange]() { arrange(1); });
    ed->addAction(tr("Send backward"), [arrange]() { arrange(2); });
    ed->addAction(tr("Send to back"), [arrange]() { arrange(3); });
    if (m_editing && m_picked.contains(index) && pickedUnits() >= 2) {
        ed->addSection(tr("Align"));
        const AlignTo to = AlignTo(std::clamp(m_ctx->uiValue("plotAlignTo", 0).toInt(), 0, int(WholePlot)));
        QVector<QuickEditor::Choice> choices;
        QVector<Align> list = {Left, CenterX, Right, Top, CenterY, Bottom};
        if (pickedUnits() >= 3)
            list << SpreadCenterX << SpreadGapX << SpreadCenterY << SpreadGapY;
        for (Align a : list)
            choices.append({alignName(a), alignIcon(a), [this, a, to]() { align(a, to); }});
        ed->addChoices(choices);
    }
    if (set.size() > 1 || it.group) {
        ed->addSection(tr("Group"));
        bool allOne = it.group != 0;
        for (int i : set)
            allOne = allOne && i < pl->items.size() && pl->items[i].group == it.group;
        if (set.size() > 1 && !allOne)
            ed->addAction(tr("Group these %1").arg(set.size()), [this, show, id, set]() {
                const Plot *cur = show->plot(id);
                if (!cur)
                    return;
                Plot p = *cur;
                const int g = p.nextGroup();
                for (int i : set)
                    if (i < p.items.size())
                        p.items[i].group = g;
                show->setPlot(p);
            });
        if (it.group)
            ed->addAction(tr("Ungroup"), [show, id, group = it.group]() {
                const Plot *cur = show->plot(id);
                if (!cur)
                    return;
                Plot p = *cur;
                for (PlotItem &x : p.items)
                    if (x.group == group)
                        x.group = 0;
                show->setPlot(p);
            });
    }
    ed->addSection(QString());
    ed->addAction(set.size() > 1 ? tr("Remove these %1 from the plot").arg(set.size()) : tr("Remove from the plot"),
                  [this, show, id, set]() {
        const Plot *cur = show->plot(id);
        if (!cur)
            return;
        Plot p = *cur;
        for (int k = int(set.size()) - 1; k >= 0; --k)
            if (set[k] < p.items.size())
                p.items.removeAt(set[k]);
        m_picked.clear();
        show->setPlot(p);
    }, QIcon(), true);
    ed->popup(global);
}

// ===========================================================================
// Align and distribute
// ===========================================================================

namespace {
// The picked items as units: a group moves as one, everything else on its own. In pick order.
struct Unit {
    QVector<int> items;
    QRectF box;
};
QVector<Unit> unitsOf(const Plot &p, const QVector<int> &picked)
{
    QVector<Unit> units;
    QHash<int, int> byGroup;   // group -> unit index
    for (int i : picked) {
        if (i < 0 || i >= p.items.size())
            continue;
        const PlotItem &it = p.items[i];
        int u = -1;
        if (it.group && byGroup.contains(it.group)) {
            u = byGroup.value(it.group);
        } else {
            units.append(Unit());
            u = int(units.size()) - 1;
            if (it.group)
                byGroup.insert(it.group, u);
        }
        units[u].items << i;
        units[u].box = units[u].box.isNull() ? it.rect : units[u].box.united(it.rect);
    }
    return units;
}
} // namespace

int PlotCanvas::pickedUnits() const
{
    const Plot *p = m_ctx->show->plot(m_plotId);
    return p ? int(unitsOf(*p, m_picked).size()) : 0;
}

QString PlotCanvas::alignName(Align how)
{
    switch (how) {
    case Left: return tr("Align left edges");
    case CenterX: return tr("Center on vertical axis");
    case Right: return tr("Align right edges");
    case Top: return tr("Align top edges");
    case CenterY: return tr("Center on horizontal axis");
    case Bottom: return tr("Align bottom edges");
    case SpreadLeft: return tr("Distribute left edges equally");
    case SpreadCenterX: return tr("Distribute centers equally, across");
    case SpreadRight: return tr("Distribute right edges equally");
    case SpreadGapX: return tr("Make horizontal gaps equal");
    case SpreadTop: return tr("Distribute top edges equally");
    case SpreadCenterY: return tr("Distribute centers equally, down");
    case SpreadBottom: return tr("Distribute bottom edges equally");
    case SpreadGapY: return tr("Make vertical gaps equal");
    }
    return QString();
}

QIcon PlotCanvas::alignIcon(Align how)
{
    // Little pictures in the style of a drawing program: boxes and the line they meet on.
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor box = theme::text, line = theme::programmer;
    auto rect = [&](double x, double y, double w, double h) {
        p.setPen(Qt::NoPen);
        p.setBrush(box);
        p.drawRoundedRect(QRectF(x, y, w, h), 1.5, 1.5);
    };
    auto vline = [&](double x) { p.setPen(QPen(line, 2.5)); p.drawLine(QPointF(x, 3), QPointF(x, 37)); };
    auto hline = [&](double y) { p.setPen(QPen(line, 2.5)); p.drawLine(QPointF(3, y), QPointF(37, y)); };
    switch (how) {
    case Left: rect(8, 8, 24, 9); rect(8, 23, 14, 9); vline(6); break;
    case CenterX: rect(8, 8, 24, 9); rect(13, 23, 14, 9); vline(20); break;
    case Right: rect(8, 8, 24, 9); rect(18, 23, 14, 9); vline(34); break;
    case Top: rect(8, 8, 9, 24); rect(23, 8, 9, 14); hline(6); break;
    case CenterY: rect(8, 8, 9, 24); rect(23, 13, 9, 14); hline(20); break;
    case Bottom: rect(8, 8, 9, 24); rect(23, 18, 9, 14); hline(34); break;
    case SpreadLeft: case SpreadCenterX: case SpreadRight: {
        const double xs[3] = {5, 16, 27};
        for (double x : xs) {
            rect(x, 14, 8, 12);
            p.setPen(QPen(line, 1.6));
            const double at = how == SpreadLeft ? x : how == SpreadRight ? x + 8 : x + 4;
            p.drawLine(QPointF(at, 6), QPointF(at, 34));
        }
        break;
    }
    case SpreadTop: case SpreadCenterY: case SpreadBottom: {
        const double ys[3] = {5, 16, 27};
        for (double y : ys) {
            rect(14, y, 12, 8);
            p.setPen(QPen(line, 1.6));
            const double at = how == SpreadTop ? y : how == SpreadBottom ? y + 8 : y + 4;
            p.drawLine(QPointF(6, at), QPointF(34, at));
        }
        break;
    }
    case SpreadGapX:
        rect(4, 12, 8, 16); rect(16, 12, 8, 16); rect(28, 12, 8, 16);
        p.setPen(QPen(line, 1.6));
        p.drawLine(QPointF(12.5, 34), QPointF(15.5, 34));
        p.drawLine(QPointF(24.5, 34), QPointF(27.5, 34));
        break;
    case SpreadGapY:
        rect(12, 4, 16, 8); rect(12, 16, 16, 8); rect(12, 28, 16, 8);
        p.setPen(QPen(line, 1.6));
        p.drawLine(QPointF(34, 12.5), QPointF(34, 15.5));
        p.drawLine(QPointF(34, 24.5), QPointF(34, 27.5));
        break;
    }
    p.end();
    return QIcon(pm);
}

void PlotCanvas::align(Align how, AlignTo to)
{
    const Plot *cur = m_ctx->show->plot(m_plotId);
    if (!cur)
        return;
    Plot p = *cur;
    QVector<Unit> units = unitsOf(p, m_picked);
    const bool spread = how >= SpreadLeft;
    if (units.isEmpty() || (spread && units.size() < 3) || (!spread && units.size() < 2 && to != WholePlot)) {
        emit m_ctx->status(spread ? tr("Pick three or more things to distribute") : tr("Pick two or more things to align"));
        return;
    }
    auto moveUnit = [&p](const Unit &u, double dx, double dy) {
        for (int i : u.items)
            p.items[i].rect.translate(dx, dy);
    };
    if (!spread) {
        // The anchor everything lines up on; the anchor itself stays put.
        QRectF anchor;
        int anchorUnit = -1;
        auto area = [](const QRectF &r) { return r.width() * r.height(); };
        switch (to) {
        case LastPicked: anchorUnit = int(units.size()) - 1; break;
        case FirstPicked: anchorUnit = 0; break;
        case Biggest:
        case Smallest:
            anchorUnit = 0;
            for (int k = 1; k < units.size(); ++k)
                if (to == Biggest ? area(units[k].box) > area(units[anchorUnit].box) : area(units[k].box) < area(units[anchorUnit].box))
                    anchorUnit = k;
            break;
        case Selection:
            for (const Unit &u : units)
                anchor = anchor.isNull() ? u.box : anchor.united(u.box);
            break;
        case WholePlot: {
            for (const PlotItem &it : p.items)
                if (!(it.kind == PlotItem::Image && it.background))
                    anchor = anchor.isNull() ? it.rect : anchor.united(it.rect);
            break;
        }
        }
        if (anchorUnit >= 0)
            anchor = units[anchorUnit].box;
        for (int k = 0; k < units.size(); ++k) {
            if (k == anchorUnit)
                continue;
            const QRectF b = units[k].box;
            double dx = 0, dy = 0;
            switch (how) {
            case Left: dx = anchor.left() - b.left(); break;
            case CenterX: dx = anchor.center().x() - b.center().x(); break;
            case Right: dx = anchor.right() - b.right(); break;
            case Top: dy = anchor.top() - b.top(); break;
            case CenterY: dy = anchor.center().y() - b.center().y(); break;
            case Bottom: dy = anchor.bottom() - b.bottom(); break;
            default: break;
            }
            moveUnit(units[k], dx, dy);
        }
    } else {
        const bool across = how <= SpreadGapX;
        auto key = [&](const QRectF &b) {
            switch (how) {
            case SpreadLeft: return b.left();
            case SpreadRight: return b.right();
            case SpreadTop: return b.top();
            case SpreadBottom: return b.bottom();
            case SpreadCenterY: case SpreadGapY: return b.center().y();
            default: return b.center().x();
            }
        };
        std::sort(units.begin(), units.end(), [&](const Unit &a, const Unit &b) { return key(a.box) < key(b.box); });
        const int n = int(units.size());
        if (how == SpreadGapX || how == SpreadGapY) {
            // The ends stay; the gaps between them come out equal.
            double total = 0;
            for (const Unit &u : units)
                total += across ? u.box.width() : u.box.height();
            const double first = across ? units.first().box.left() : units.first().box.top();
            const double last = across ? units.last().box.right() : units.last().box.bottom();
            const double gap = (last - first - total) / (n - 1);
            double at = first;
            for (const Unit &u : units) {
                const double was = across ? u.box.left() : u.box.top();
                moveUnit(u, across ? at - was : 0, across ? 0 : at - was);
                at += (across ? u.box.width() : u.box.height()) + gap;
            }
        } else {
            const double a = key(units.first().box), b = key(units.last().box);
            for (int k = 1; k < n - 1; ++k) {
                const double want = a + (b - a) * k / (n - 1);
                const double d = want - key(units[k].box);
                moveUnit(units[k], across ? d : 0, across ? 0 : d);
            }
        }
    }
    commit(p);
}

// ===========================================================================
// Window
// ===========================================================================

PlotView::PlotView(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    setWindowTitle(tr("Plot"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(6, 6, 6, 6);
    v->setSpacing(6);
    m_head = new QWidget(this);
    auto *head = new QHBoxLayout(m_head);
    head->setContentsMargins(0, 0, 0, 0);
    m_title = new QLabel(this);
    m_title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    auto button = [this](const QString &text, const QString &tip, bool checkable = false) {
        auto *b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setCheckable(checkable);
        return b;
    };
    m_edit = button(tr("Edit"), tr("Move, resize and add things. Off: tap fixtures to select them, tap objects to use them"), true);
    m_add = button(tr("Add ▾"), tr("Add selected fixtures, a pool object, text, an icon or a picture"));
    m_align = button(tr("Align ▾"), tr("Line the picked things up or space them out evenly"));
    m_minus = button(QStringLiteral("−"), tr("Zoom out"));
    m_plus = button(QStringLiteral("+"), tr("Zoom in"));
    m_fit = button(tr("Fit"), tr("Everything on the plot in view"));
    m_lock = button(tr("Lock"), tr("Lock the zoom and position: +, −, Fit, the wheel and dragging leave it where it is"), true);
    m_zoomLabel = new QLabel(this);
    m_zoomLabel->setProperty("role", "muted");
    m_zoomLabel->setMinimumWidth(44);
    head->addWidget(m_title, 1);
    head->addWidget(m_edit);
    head->addWidget(m_add);
    head->addWidget(m_align);
    head->addSpacing(10);
    head->addWidget(m_minus);
    head->addWidget(m_zoomLabel);
    head->addWidget(m_plus);
    head->addWidget(m_fit);
    head->addWidget(m_lock);
    v->addWidget(m_head);
    m_canvas = new PlotCanvas(ctx, this);
    v->addWidget(m_canvas, 1);

    connect(m_edit, &QToolButton::toggled, this, [this](bool on) {
        m_canvas->setEditing(on);
        refreshHeader();
    });
    connect(m_add, &QToolButton::clicked, this, &PlotView::addMenu);
    connect(m_align, &QToolButton::clicked, this, [this]() {
        QMenu menu(this);
        fillAlignMenu(&menu);
        menu.exec(m_align->mapToGlobal(QPoint(0, m_align->height())));
    });
    connect(m_canvas, &PlotCanvas::backgroundMenu, this, &PlotView::backgroundMenu);
    // Zooming and panning change the view; tell it once things settle, not on every pixel.
    auto *viewTimer = new QTimer(this);
    viewTimer->setSingleShot(true);
    viewTimer->setInterval(300);
    connect(viewTimer, &QTimer::timeout, this, [this]() { panelStateChanged(this); });
    connect(m_canvas, &PlotCanvas::viewChanged, viewTimer, qOverload<>(&QTimer::start));
    connect(m_minus, &QToolButton::clicked, this, [this]() { m_canvas->zoomBy(1 / 1.25); });
    connect(m_plus, &QToolButton::clicked, this, [this]() { m_canvas->zoomBy(1.25); });
    connect(m_fit, &QToolButton::clicked, this, [this]() { m_canvas->fit(); });
    connect(m_lock, &QToolButton::toggled, this, [this](bool on) { m_canvas->setZoomLocked(on); });
    connect(m_canvas, &PlotCanvas::zoomChanged, this, &PlotView::refreshHeader);
    connect(ctx->show, &Show::changed, this, [this](Show::Parts p) {
        if (p & Show::PlotsPart) {
            if (!m_ctx->show->plot(m_canvas->plotId()))
                follow();
            refreshHeader();
        }
    });
    connect(ctx, &AppContext::uiValueChanged, this, [this](const QString &key) {
        if (key == QLatin1String("plot"))
            follow();
    });
    follow();
}

void PlotView::follow()
{
    int id = m_pinned ? m_pinned : m_ctx->uiValue("plot", 0).toInt();
    if (!m_ctx->show->plot(id))
        id = m_ctx->show->plots().isEmpty() ? 0 : m_ctx->show->plots().first().id;
    const bool fresh = id != m_canvas->plotId() && !m_canvas->hasView(id);
    m_canvas->setPlotId(id);
    refreshHeader();
    // A plot this window shows for the first time (and never zoomed) starts with everything in view.
    if (fresh)
        if (const Plot *p = m_ctx->show->plot(id); p && p->zoom == 1.0 && p->panX == 0 && p->panY == 0 && !p->items.isEmpty())
            QTimer::singleShot(0, this, [this]() { m_canvas->fit(); });
}

void PlotView::refreshHeader()
{
    const Plot *p = m_ctx->show->plot(m_canvas->plotId());
    m_title->setText(p ? QStringLiteral("%1  %2").arg(p->id).arg(p->displayName()) : tr("No plot"));
    setWindowTitle(p ? tr("Plot · %1").arg(p->displayName()) : tr("Plot"));
    const bool locked = p && m_canvas->zoomLocked();
    {
        const QSignalBlocker b(m_lock);
        m_lock->setChecked(locked);
    }
    m_lock->setText(locked ? tr("Locked") : tr("Lock"));
    for (QToolButton *b : {m_minus, m_plus, m_fit})
        b->setEnabled(p && !locked);
    m_edit->setEnabled(p);
    m_add->setEnabled(p);
    m_align->setVisible(m_canvas->editing());
    m_zoomLabel->setText(p ? QStringLiteral("%1%").arg(std::lround(m_canvas->zoom() * 100)) : QString());
}

void PlotView::addMenu()
{
    if (!m_canvas->plotId())
        return;
    QMenu menu(this);
    fillAddMenu(&menu);
    menu.exec(m_add->mapToGlobal(QPoint(0, m_add->height())));
}

void PlotView::fillAlignMenu(QMenu *menu)
{
    const int units = m_canvas->pickedUnits();
    const auto to = PlotCanvas::AlignTo(std::clamp(m_ctx->uiValue("plotAlignTo", 0).toInt(), 0, int(PlotCanvas::WholePlot)));
    QMenu *rel = menu->addMenu(tr("Relative to"));
    const QStringList names = {tr("Last picked"), tr("First picked"), tr("Biggest"), tr("Smallest"), tr("The picked things together"),
                               tr("The whole plot")};
    for (int i = 0; i < names.size(); ++i) {
        QAction *a = rel->addAction(names[i], this, [this, i]() { m_ctx->setUiValue("plotAlignTo", i); });
        a->setCheckable(true);
        a->setChecked(i == int(to));
    }
    menu->addSeparator();
    using A = PlotCanvas::Align;
    auto add = [&](A how, bool enabled) {
        QAction *a = menu->addAction(PlotCanvas::alignIcon(how), PlotCanvas::alignName(how), this, [this, how, to]() { m_canvas->align(how, to); });
        a->setEnabled(enabled);
    };
    const bool canAlign = units >= 2 || (units == 1 && to == PlotCanvas::WholePlot);
    for (A how : {A::Left, A::CenterX, A::Right, A::Top, A::CenterY, A::Bottom})
        add(how, canAlign);
    menu->addSeparator();
    for (A how : {A::SpreadLeft, A::SpreadCenterX, A::SpreadRight, A::SpreadGapX, A::SpreadTop, A::SpreadCenterY, A::SpreadBottom,
                  A::SpreadGapY})
        add(how, units >= 3);
    if (units == 0) {
        menu->addSeparator();
        menu->addAction(tr("Pick things first (in Edit)"))->setEnabled(false);
    }
}

void PlotView::backgroundMenu(const QPoint &global)
{
    QMenu menu(this);
    const bool has = m_canvas->plotId() != 0;
    QAction *edit = menu.addAction(tr("Edit"), this, [this]() { m_edit->toggle(); });
    edit->setCheckable(true);
    edit->setChecked(m_canvas->editing());
    edit->setEnabled(has);
    if (has) {
        fillAddMenu(menu.addMenu(tr("Add")));
        if (m_canvas->editing())
            fillAlignMenu(menu.addMenu(tr("Align")));
    }
    menu.addSeparator();
    const bool locked = m_canvas->zoomLocked();
    menu.addAction(tr("Zoom in"), this, [this]() { m_canvas->zoomBy(1.25); })->setEnabled(has && !locked);
    menu.addAction(tr("Zoom out"), this, [this]() { m_canvas->zoomBy(1 / 1.25); })->setEnabled(has && !locked);
    menu.addAction(tr("Fit"), this, [this]() { m_canvas->fit(); })->setEnabled(has && !locked);
    QAction *lock = menu.addAction(tr("Lock zoom and position"), this, [this, locked]() {
        m_canvas->setZoomLocked(!locked);
    });
    lock->setCheckable(true);
    lock->setChecked(locked);
    lock->setEnabled(has);
    menu.addSeparator();
    menu.addAction(m_barHidden ? tr("Show the top bar") : tr("Hide the top bar"), this, [this]() { setBarHidden(!m_barHidden); });
    menu.exec(global);
}

void PlotView::setBarHidden(bool hidden)
{
    m_barHidden = hidden;
    m_head->setVisible(!hidden);
    layout()->setContentsMargins(hidden ? 0 : 6, hidden ? 0 : 6, hidden ? 0 : 6, hidden ? 0 : 6);
    panelStateChanged(this);
}

void PlotView::fillAddMenu(QMenu *menuPtr)
{
    const int id = m_canvas->plotId();
    QMenu &menu = *menuPtr;
    menu.addAction(tr("Selected fixtures"), this, [this, id]() {
        QStringList ids;
        for (int fid : m_ctx->show->selection())
            ids << m_ctx->show->idText(fid);
        if (ids.isEmpty()) {
            emit m_ctx->status(tr("Select fixtures (or subfixtures) first"));
            return;
        }
        m_ctx->run(QStringLiteral("Assign Fixture %1 At Plot %2").arg(ids.join(QStringLiteral(" + "))).arg(id));
    });
    menu.addAction(tr("Pool object…"), this, [this, id]() {
        bool ok = false;
        const QString object = QInputDialog::getText(this, tr("Add a pool object"),
                                                     tr("As the command line writes it: Group 3, Preset 4.1 (or Color.1), Sequence 2, "
                                                        "Macro 1, Effect 5, View 2, Spread 1, Plot 2, Executor 1.201, RateMaster 3"),
                                                     QLineEdit::Normal, QString(), &ok);
        if (ok && !object.trimmed().isEmpty())
            m_ctx->run(QStringLiteral("Assign %1 At Plot %2").arg(object.trimmed()).arg(id));
    });
    menu.addAction(tr("Text"), this, [this, id]() {
        bool ok = false;
        const QString text = QInputDialog::getText(this, tr("Add text"), tr("Text"), QLineEdit::Normal, QString(), &ok);
        const Plot *cur = m_ctx->show->plot(id);
        if (!ok || text.trimmed().isEmpty() || !cur)
            return;
        Plot p = *cur;
        PlotItem it;
        it.kind = PlotItem::Text;
        it.text = text.trimmed();
        const QPointF c = m_canvas->viewCenter();
        it.rect = QRectF(snap(c.x() - 80), snap(c.y() - 12), 160, 24);
        p.items << it;
        m_ctx->show->setPlot(p);
    });
    QMenu *icons = menu.addMenu(tr("Icon"));
    for (const auto &ic : builtinPlotIcons()) {
        const QPixmap pm = PlotCanvas::renderImage(ic.second, QSize(20, 20), theme::text.name());
        icons->addAction(QIcon(pm), ic.first, this, [this, id, ic]() {
            const Plot *cur = m_ctx->show->plot(id);
            if (!cur)
                return;
            Plot p = *cur;
            PlotItem it;
            it.kind = PlotItem::Image;
            it.image = ic.second;
            it.imageName = ic.first;
            it.color = theme::text.name();
            const QPointF c = m_canvas->viewCenter();
            const bool truss = ic.first == QLatin1String("Truss");
            it.rect = truss ? QRectF(snap(c.x() - 96), snap(c.y() - 12), 192, 24) : QRectF(snap(c.x() - 24), snap(c.y() - 24), 48, 48);
            p.items << it;
            m_ctx->show->setPlot(p);
        });
    }
    menu.addAction(tr("Picture or SVG file…"), this, [this]() { addPicture(false); });
    menu.addAction(tr("Background (SVG or picture)…"), this, [this]() { addPicture(true); });
}

void PlotView::addPicture(bool background)
{
    const int id = m_canvas->plotId();
    const QString path = QFileDialog::getOpenFileName(this, background ? tr("Background") : tr("Picture"), QString(),
                                                      tr("Pictures (*.svg *.png *.jpg *.jpeg *.bmp *.gif)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QByteArray bytes = f.readAll();
    QSizeF natural(160, 160);
    if (isSvg(bytes)) {
        QSvgRenderer svg(bytes);
        if (!svg.isValid()) {
            emit m_ctx->status(tr("Couldn't read %1 as an SVG").arg(QFileInfo(path).fileName()));
            return;
        }
        if (!svg.defaultSize().isEmpty())
            natural = svg.defaultSize();
    } else {
        const QImage img = QImage::fromData(bytes);
        if (img.isNull()) {
            emit m_ctx->status(tr("Couldn't read %1 as a picture").arg(QFileInfo(path).fileName()));
            return;
        }
        natural = img.size();
    }
    const Plot *cur = m_ctx->show->plot(id);
    if (!cur)
        return;
    Plot p = *cur;
    PlotItem it;
    it.kind = PlotItem::Image;
    it.image = bytes;
    it.imageName = QFileInfo(path).fileName();
    it.background = background;
    if (background) {
        // Under everything already there, as big as it.
        QRectF b = p.bounds();
        if (b.isNull())
            b = QRectF(0, 0, 800, 600);
        const double k = std::max(b.width() / natural.width(), b.height() / natural.height());
        it.rect = QRectF(b.left(), b.top(), natural.width() * k, natural.height() * k);
        p.items.prepend(it);
    } else {
        const double k = std::min(1.0, 240.0 / std::max(natural.width(), natural.height()));
        const QPointF c = m_canvas->viewCenter();
        it.rect = QRectF(snap(c.x() - natural.width() * k / 2), snap(c.y() - natural.height() * k / 2), natural.width() * k,
                         natural.height() * k);
        p.items << it;
    }
    m_ctx->show->setPlot(p);
}

QJsonObject PlotView::saveOptions() const
{
    return QJsonObject{{"plot", m_pinned}, {"views", m_canvas->saveViews()}, {"hideBar", m_barHidden}};
}

void PlotView::loadOptions(const QJsonObject &o)
{
    m_pinned = std::max(0, o["plot"].toInt(0));
    if (o.contains("views"))
        m_canvas->loadViews(o["views"].toObject());
    m_barHidden = o["hideBar"].toBool(false);
    m_head->setVisible(!m_barHidden);
    layout()->setContentsMargins(m_barHidden ? 0 : 6, m_barHidden ? 0 : 6, m_barHidden ? 0 : 6, m_barHidden ? 0 : 6);
    follow();
    refreshHeader();
}

void PlotView::buildOptions(QuickEditor *editor)
{
    editor->addNumber(tr("Plot (0 shows the one chosen last)"), 0, 9999, m_pinned, [this](int n) {
        m_pinned = n;
        follow();
        panelStateChanged(this);
    });
    editor->addSegmented(tr("Top bar"), {tr("Show"), tr("Hide")}, m_barHidden ? 1 : 0, [this](int i) { setBarHidden(i == 1); });
    editor->addSegmented(tr("Zoom"), {tr("Free"), tr("Locked")}, m_canvas->zoomLocked() ? 1 : 0,
                         [this](int i) { m_canvas->setZoomLocked(i == 1); });
}

} // namespace lumora
