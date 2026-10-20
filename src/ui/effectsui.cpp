// SPDX-License-Identifier: GPL-3.0-or-later
#include "effectsui.h"

#include "appcontext.h"
#include "colorpicker.h"
#include "fixturesheet.h"
#include "quickedit.h"
#include "theme.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace lumora {

namespace {
QColor kindColor(LayerKind k)
{
    switch (k) {
    case LayerKind::Dimmer: return theme::featureColor(Feature::Dimmer);
    case LayerKind::Position: return theme::featureColor(Feature::Position);
    case LayerKind::Color: return theme::featureColor(Feature::Color);
    case LayerKind::Attributes: return theme::featureColor(Feature::Beam);
    }
    return theme::muted;
}

QString kindsLabel(const Effect &e)
{
    QStringList k;
    for (const EffectLayer &l : e.layers)
        if (!k.contains(layerKindName(l.kind)))
            k << layerKindName(l.kind);
    return k.join('+');
}

const Shape &resolveShape(Show *show, int id)
{
    static const Shape sine{ShapeSine, "Sine", {}, true};
    if (id < 0) {
        const Shape *s = builtinShape(id);
        return s ? *s : sine;
    }
    const Shape *s = show->shape(id);
    return s ? *s : sine;
}

QLabel *muted(const QString &t, QWidget *parent)
{
    auto *l = new QLabel(t, parent);
    l->setProperty("role", "muted");
    return l;
}

QToolButton *chip(const QString &text, bool checked, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setText(text);
    b->setCheckable(true);
    b->setChecked(checked);
    b->setStyleSheet(QStringLiteral("QToolButton { padding: 2px 10px; border-radius: 11px; }"));
    return b;
}

Effect blankEffect(int id)
{
    Effect e;
    e.id = id;
    e.name = QObject::tr("Effect %1").arg(id);
    EffectLayer l;
    l.name = QObject::tr("Dimmer");
    l.points = {EffectPoint::level(0.0), EffectPoint::level(1.0)};
    e.layers << l;
    return e;
}
} // namespace

QPixmap shapeIcon(const Shape &shape, int w, int h)
{
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    for (int i = 0; i <= 40; ++i) {
        const double q = i / 40.0;
        // Random shapes: show a few cycles worth of steps
        const double v = shape.value(q * 0.999, qint64(q * 4), 3);
        const QPointF pt(2 + q * (w - 4), h - 2 - v * (h - 4));
        if (i == 0)
            path.moveTo(pt);
        else
            path.lineTo(pt);
    }
    p.setPen(QPen(theme::text, 1.6));
    p.drawPath(path);
    return pm;
}

// ===========================================================================
// Effects pool
// ===========================================================================

EffectsPanel::EffectsPanel(AppContext *ctx, QWidget *parent) : PoolPanel(tr("Effects"), parent), m_ctx(ctx)
{
    auto *v = static_cast<QVBoxLayout *>(layout());
    auto *runTitle = muted(tr("Running in the programmer"), this);
    v->addWidget(runTitle);
    m_running = new QWidget(this);
    m_runningLayout = new QVBoxLayout(m_running);
    m_runningLayout->setContentsMargins(0, 0, 0, 0);
    m_runningLayout->setSpacing(4);
    v->addWidget(m_running);

    auto *library = new QToolButton(this);
    library->setText(tr("Library…"));
    library->setToolTip(tr("Load a ready-made effect into the first free slot"));
    headerLayout()->addWidget(library);
    connect(library, &QToolButton::clicked, this, [this]() {
        EffectLibraryDialog d(this);
        if (d.exec() != QDialog::Accepted)
            return;
        Effect e = d.chosen();
        e.id = m_ctx->show->nextFreeEffectId();
        m_ctx->show->setEffect(e);
        emit m_ctx->status(tr("%1 loaded into effect %2").arg(e.name).arg(e.id));
    });

    Show *show = ctx->show;
    connect(tiles(), &PoolTiles::tileClicked, this, [this, show](int id, bool occupied, Qt::KeyboardModifiers) {
        if (!m_ctx->storeArmed()
            && m_ctx->tapTarget(QStringLiteral("Effect %1").arg(id), show->effect(id) ? show->effect(id)->name : QString()))
            return;
        if (!occupied || m_ctx->storeArmed()) {
            m_ctx->setStoreArmed(false);
            createAt(id, QCursor::pos());
            return;
        }
        // Nothing selected: open it in the effect editor. With a selection: run it on them.
        if (show->selection().isEmpty()) {
            m_ctx->chooseEffect(id);
            emit m_ctx->status(tr("Editing %1. Select fixtures first to run an effect instead").arg(show->effect(id)->name));
            return;
        }
        if (show->applyEffect(id))
            emit m_ctx->status(tr("%1 running on %2").arg(show->effect(id)->name,
                                                           countOf(int(show->selection().size()), tr("fixture"), tr("fixtures"))));
    });
    connect(tiles(), &PoolTiles::tileMenu, this, [this, show](int id, bool occupied, const QPoint &pos) {
        if (!occupied) {
            createAt(id, pos);
            return;
        }
        auto *ed = new QuickEditor(tr("Effect %1").arg(id), this);
        ed->addNameField(tr("Name"), show->effect(id)->name, [show, id](const QString &n) {
            if (const Effect *e0 = show->effect(id)) {
                Effect e = *e0;
                e.name = n;
                show->setEffect(e);
            }
        });
        ed->addSection(QString());
        ed->addAction(tr("Edit effect…"), [this, id]() { m_ctx->requestEditEffect(id); });
        ed->addAction(tr("Run on selection"), [show, id]() { show->applyEffect(id); });
        ed->addAction(tr("Stop"), [show, id]() { show->removeEffect(id); });
        ed->addAction(tr("Duplicate"), [this, show, id]() {
            Effect e = *show->effect(id);
            e.id = show->nextFreeEffectId();
            e.name = tr("%1 copy").arg(e.name);
            show->setEffect(e);
            m_ctx->requestEditEffect(e.id);
        });
        ed->addAction(tr("Delete"), [show, id]() { show->deleteEffect(id); }, QIcon(), true);
        ed->popup(pos);
    });
    connect(show, &Show::changed, this, [this](Show::Parts p) {
        if (p & (Show::EffectsPart | Show::ProgrammerPart)) {
            refresh();
            refreshRunning();
        }
    });
    followStore(ctx);
    refresh();
    refreshRunning();
}

void EffectsPanel::createAt(int id, const QPoint &globalPos)
{
    QMenu menu(this);
    menu.addAction(tr("New blank effect"), this, [this, id]() {
        m_ctx->show->setEffect(blankEffect(id));
        m_ctx->requestEditEffect(id);
    });
    QMenu *lib = menu.addMenu(tr("From the library"));
    const QVector<Effect> templates = effectTemplates();
    for (const Effect &t : templates) {
        QAction *a = lib->addAction(t.name, this, [this, id, t]() {
            Effect e = t;
            e.id = id;
            m_ctx->show->setEffect(e);
            emit m_ctx->status(tr("%1 loaded into effect %2").arg(e.name).arg(id));
        });
        a->setToolTip(t.description);
    }
    menu.exec(globalPos);
}

void EffectsPanel::refresh()
{
    Show *show = m_ctx->show;
    QSet<int> running;
    for (const EffectInstance &i : show->programmerEffects())
        running.insert(i.effectId);
    QVector<PoolItem> items;
    for (const Effect &e : show->effects()) {
        PoolItem it;
        it.id = e.id;
        it.label = e.name;
        it.badge = kindsLabel(e);
        it.tag = e.layers.isEmpty() ? QColor() : kindColor(e.layers.first().kind);
        it.active = running.contains(e.id);
        it.tooltip = e.description.isEmpty() ? tr("Tap to run on the selection. Right-click to edit.") : e.description;
        items << it;
    }
    tiles()->setItems(items);
}

void EffectsPanel::refreshRunning()
{
    Show *show = m_ctx->show;
    const QVector<EffectInstance> &fx = show->programmerEffects();
    QString sig;
    for (const EffectInstance &i : fx)
        sig += QStringLiteral("%1:%2;").arg(i.effectId).arg(i.fixtures.size());
    if (sig == m_runningSignature)
        return;
    m_runningSignature = sig;
    while (QLayoutItem *item = m_runningLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
    if (fx.isEmpty()) {
        m_runningLayout->addWidget(muted(tr("Nothing running. Select fixtures and tap an effect."), m_running));
        return;
    }
    for (int i = 0; i < fx.size(); ++i) {
        const Effect *e = show->effect(fx[i].effectId);
        if (!e)
            continue;
        auto *row = new QWidget(m_running);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        auto *name = new QLabel(QStringLiteral("%1  %2").arg(e->name, countOf(int(fx[i].fixtures.size()), tr("fixture"), tr("fixtures"))), row);
        name->setMinimumWidth(170);
        h->addWidget(name);
        auto slider = [&](const QString &label, int lo, int hi, int value, auto apply) {
            h->addWidget(muted(label, row));
            auto *s = new QSlider(Qt::Horizontal, row);
            s->setRange(lo, hi);
            s->setValue(value);
            s->setMinimumWidth(90);
            auto *val = new QLabel(QStringLiteral("%1%").arg(value), row);
            val->setMinimumWidth(40);
            h->addWidget(s, 1);
            h->addWidget(val);
            connect(s, &QSlider::valueChanged, row, [this, i, val, apply](int v) {
                val->setText(QStringLiteral("%1%").arg(v));
                if (i >= m_ctx->show->programmerEffects().size())
                    return;
                EffectInstance inst = m_ctx->show->programmerEffects()[i];
                apply(inst, v);
                m_ctx->show->setProgrammerEffect(i, inst);
            });
        };
        slider(tr("Rate"), 10, 400, int(std::lround(fx[i].rate * 100)), [](EffectInstance &in, int v) { in.rate = v / 100.0; });
        slider(tr("Size"), 0, 200, int(std::lround(fx[i].size * 100)), [](EffectInstance &in, int v) { in.size = v / 100.0; });
        if (fx[i].hasLayout) {   // the layout it runs with
            auto *mtx = muted(fx[i].layout.summary(), row);
            mtx->setToolTip(tr("Spread this effect runs with"));
            h->addWidget(mtx);
        }
        auto *stop = new QToolButton(row);
        stop->setText(tr("Stop"));
        connect(stop, &QToolButton::clicked, row, [this, i]() { m_ctx->show->removeProgrammerEffect(i); });
        h->addWidget(stop);
        m_runningLayout->addWidget(row);
    }
}

// ===========================================================================
// Preview
// ===========================================================================

EffectPreview::EffectPreview(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    setMinimumHeight(140);
    // Every frame of output, so the preview runs with the lights.
    connect(ctx->engine, &Engine::frameDone, this, [this]() {
        if (!isVisible())
            return;
        const double cycle = std::max(0.05, m_effect.cycle);
        m_playhead = std::fmod(QDateTime::currentMSecsSinceEpoch() / 1000.0 / cycle, 1.0);
        update();
    });
}

void EffectPreview::setEffect(const Effect &e)
{
    m_effect = e;
    m_dirty = true;
    update();
}

void EffectPreview::paintEvent(QPaintEvent *)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // The engine shares this thread: re-render the rows only when needed (and twice a second
    // to pick up selection and preset changes), not on every playhead frame.
    if (m_dirty || m_cache.size() != size() || now - m_cacheTime > 500) {
        renderCache();
        m_cacheTime = now;
        m_dirty = false;
    }
    QPainter p(this);
    p.drawImage(0, 0, m_cache);
    const QRectF area = QRectF(rect()).adjusted(0, 18, 0, 0);
    const double x = area.left() + m_playhead * area.width();
    p.setPen(QPen(theme::text, 1.5));
    p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
}

void EffectPreview::renderCache()
{
    m_cache = QImage(size(), QImage::Format_ARGB32_Premultiplied);
    m_cache.fill(Qt::transparent);
    QPainter p(&m_cache);
    p.setRenderHint(QPainter::Antialiasing);
    Show *show = m_ctx->show;
    QVector<int> fids = show->selection();
    if (fids.isEmpty())
        fids = show->fixtureIds();
    fids = fids.mid(0, 10);
    const QRectF area = QRectF(rect()).adjusted(0, 18, 0, 0);
    p.setFont(theme::uiFont(8));
    p.setPen(theme::muted);
    p.drawText(QRectF(0, 0, width(), 16), Qt::AlignLeft | Qt::AlignVCenter,
               show->selection().isEmpty() ? tr("Preview on the first fixtures in the patch: select fixtures to preview on them")
                                           : tr("Preview on the selection, one row per fixture, one cycle across"));
    if (fids.isEmpty() || m_effect.layers.isEmpty()) {
        p.drawText(area, Qt::AlignCenter, tr("Nothing to preview"));
        return;
    }
    bool hasColor = false, hasDimmer = false, hasPosition = false;
    for (const EffectLayer &l : m_effect.layers) {
        if (!l.enabled)
            continue;
        hasColor |= l.kind == LayerKind::Color;
        hasDimmer |= l.kind == LayerKind::Dimmer;
        hasPosition |= l.kind == LayerKind::Position;
    }
    EffectInstance inst;
    inst.effectId = m_effect.id;
    inst.fixtures = fids;
    // Relative layers move what's underneath: preview them over the live output (or full, so a
    // dip shows when nothing's up yet).
    bool anyRelative = false;
    for (const EffectLayer &l : m_effect.layers)
        anyRelative |= l.enabled && l.relative;
    auto base = [show, anyRelative](int fid, const QString &a) {
        if (anyRelative) {
            const RawAttr o = show->lastOutput().value(fid);
            if (o.contains(a) && (!isIntensity(a) || o.value(a) > 0.001))
                return o.value(a);
            if (isIntensity(a))
                return 1.0;
        }
        return show->defaultValue(fid, a);
    };
    const int samples = std::clamp(int(area.width() / 5), 16, 120);
    const double rowH = area.height() / fids.size();
    QVector<QVector<RawAttr>> grid(fids.size(), QVector<RawAttr>(samples));
    for (int j = 0; j < samples; ++j) {
        RawTable out;
        evaluateEffect(*show, m_effect, inst, (j + 0.5) / samples, base, out);
        for (int i = 0; i < fids.size(); ++i)
            grid[i][j] = out.value(fids[i]);
    }
    const double colW = area.width() / samples;
    for (int i = 0; i < fids.size(); ++i) {
        const QRectF row(area.left(), area.top() + i * rowH, area.width(), rowH - 2);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x10, 0x13, 0x18));
        p.drawRoundedRect(row, 3, 3);
        const FixtureMode *mode = show->fixtureMode(fids[i]);
        if (hasColor || hasDimmer || !hasPosition) {
            for (int j = 0; j < samples; ++j) {
                const RawAttr &v = grid[i][j];
                QColor c = hasColor ? approximateColor(v, mode) : QColor();
                if (!c.isValid())
                    c = theme::live;
                double level = 1.0;
                if (hasDimmer)
                    level = v.value("Dimmer", 0.0);
                else if (!hasColor && !v.isEmpty()) {
                    double sum = 0;
                    for (double x : v)
                        sum += x;
                    level = sum / v.size();
                }
                p.setBrush(theme::mix(QColor(0x10, 0x13, 0x18), c, std::clamp(level, 0.0, 1.0)));
                p.drawRect(QRectF(row.left() + j * colW, row.top(), colW + 0.6, row.height()));
            }
        }
        if (hasPosition) {
            for (const char *attr : {"Pan", "Tilt"}) {
                QPainterPath path;
                for (int j = 0; j < samples; ++j) {
                    // Degrees, drawn across the fixture's range.
                    const ChannelDef *ch = m_ctx->show->channelOf(fids[i], attr);
                    const double v = ch ? ch->toDmx(grid[i][j].value(attr, 0.0)) : 0.5;
                    const QPointF pt(row.left() + (j + 0.5) * colW, row.bottom() - 2 - v * (row.height() - 4));
                    if (j == 0)
                        path.moveTo(pt);
                    else
                        path.lineTo(pt);
                }
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(QString(attr) == "Pan" ? theme::featureColor(Feature::Position) : theme::featureColor(Feature::Focus), 1.4));
                p.drawPath(path);
            }
        }
        p.setPen(theme::faint);
        p.drawText(row.adjusted(4, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, m_ctx->show->idText(fids[i]));
    }
}

// ===========================================================================
// Shape editor
// ===========================================================================

ShapeCanvas::ShapeCanvas(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(360, 200);
    setMouseTracking(true);
    setToolTip(tr("Drag points to reshape. Double-click to add a point, right-click a point to remove it."));
}

QRectF ShapeCanvas::plot() const { return QRectF(rect()).adjusted(14, 12, -14, -14); }

QPointF ShapeCanvas::toWidget(const ShapePoint &pt) const
{
    const QRectF r = plot();
    return {r.left() + pt.x * r.width(), r.bottom() - pt.y * r.height()};
}

ShapePoint ShapeCanvas::fromWidget(const QPointF &w) const
{
    const QRectF r = plot();
    return {std::clamp((w.x() - r.left()) / r.width(), 0.0, 1.0), std::clamp((r.bottom() - w.y()) / r.height(), 0.0, 1.0)};
}

int ShapeCanvas::hit(const QPointF &p) const
{
    for (int i = 0; i < shape.points.size(); ++i)
        if (QLineF(toWidget(shape.points[i]), p).length() < 9)
            return i;
    return -1;
}

void ShapeCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = plot();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x10, 0x13, 0x18));
    p.drawRoundedRect(r.adjusted(-6, -6, 6, 6), 6, 6);
    p.setPen(QPen(theme::line, 1, Qt::DotLine));
    for (int i = 1; i < 4; ++i) {
        p.drawLine(QPointF(r.left() + r.width() * i / 4, r.top()), QPointF(r.left() + r.width() * i / 4, r.bottom()));
        p.drawLine(QPointF(r.left(), r.top() + r.height() * i / 4), QPointF(r.right(), r.top() + r.height() * i / 4));
    }
    QPainterPath path;
    for (int i = 0; i <= 200; ++i) {
        const double q = i / 200.0;
        const QPointF pt = toWidget({q, shape.value(std::min(q, 0.9999))});
        if (i == 0)
            path.moveTo(pt);
        else
            path.lineTo(pt);
    }
    p.setPen(QPen(theme::live, 2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    for (int i = 0; i < shape.points.size(); ++i) {
        p.setPen(QPen(theme::bg, 2));
        p.setBrush(i == m_drag ? theme::live : theme::text);
        p.drawEllipse(toWidget(shape.points[i]), 5, 5);
    }
}

void ShapeCanvas::mousePressEvent(QMouseEvent *ev)
{
    const int h = hit(ev->position());
    if (ev->button() == Qt::RightButton) {
        if (h >= 0 && shape.points.size() > 2) {
            shape.points.removeAt(h);
            update();
        }
        return;
    }
    m_drag = h;
    update();
}

void ShapeCanvas::mouseMoveEvent(QMouseEvent *ev)
{
    setCursor(hit(ev->position()) >= 0 ? Qt::OpenHandCursor : Qt::CrossCursor);
    if (m_drag < 0 || !(ev->buttons() & Qt::LeftButton))
        return;
    shape.points[m_drag] = fromWidget(ev->position());
    update();
}

void ShapeCanvas::mouseReleaseEvent(QMouseEvent *)
{
    if (m_drag >= 0)
        std::sort(shape.points.begin(), shape.points.end(), [](const ShapePoint &a, const ShapePoint &b) { return a.x < b.x; });
    m_drag = -1;
    update();
}

void ShapeCanvas::mouseDoubleClickEvent(QMouseEvent *ev)
{
    if (hit(ev->position()) >= 0)
        return;
    shape.points.append(fromWidget(ev->position()));
    std::sort(shape.points.begin(), shape.points.end(), [](const ShapePoint &a, const ShapePoint &b) { return a.x < b.x; });
    update();
}

ShapeEditorDialog::ShapeEditorDialog(const Shape &shape, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Shape"));
    auto *v = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_name = new QLineEdit(shape.name, this);
    form->addRow(tr("Name"), m_name);
    v->addLayout(form);
    m_canvas = new ShapeCanvas(this);
    m_canvas->shape = shape;
    v->addWidget(m_canvas, 1);
    auto *row = new QHBoxLayout;
    m_smooth = new QCheckBox(tr("Smooth between points"), this);
    m_smooth->setChecked(shape.smooth);
    row->addWidget(m_smooth);
    row->addStretch(1);
    row->addWidget(muted(tr("Start from"), this));
    auto *start = new QComboBox(this);
    start->addItem(tr("…"));
    for (const Shape &b : builtinShapes())
        if (b.id != ShapeRandom && b.id != ShapeSmoothRandom)
            start->addItem(QIcon(shapeIcon(b)), b.name, b.id);
    row->addWidget(start);
    v->addLayout(row);
    v->addWidget(muted(tr("Drag points to reshape. Double-click to add a point, right-click a point to remove it. "
                          "The curve wraps from the last point back to the first."), this));
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(bb);
    connect(m_smooth, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->shape.smooth = on;
        m_canvas->update();
    });
    connect(start, &QComboBox::activated, this, [this, start](int i) {
        if (i == 0)
            return;
        const Shape *b = builtinShape(start->itemData(i).toInt());
        QVector<ShapePoint> pts;
        for (int k = 0; k < 8; ++k)
            pts.append({k / 8.0, b->value(k / 8.0)});
        m_canvas->shape.points = pts;
        m_canvas->update();
    });
    resize(520, 420);
}

Shape ShapeEditorDialog::result() const
{
    Shape s = m_canvas->shape;
    s.name = m_name->text().trimmed().isEmpty() ? tr("Custom shape") : m_name->text().trimmed();
    s.smooth = m_smooth->isChecked();
    return s;
}

// ===========================================================================
// Library
// ===========================================================================

EffectLibraryDialog::EffectLibraryDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Effect library"));
    resize(520, 520);
    auto *v = new QVBoxLayout(this);
    v->addWidget(muted(tr("Ready-made effects. Load one, then make it yours in the effect editor."), this));
    m_list = new QListWidget(this);
    m_list->setIconSize(QSize(40, 22));
    m_list->setSpacing(2);
    m_templates = effectTemplates();
    for (const Effect &e : m_templates) {
        const Shape *s = !e.layers.isEmpty() && e.layers.first().mode != LayerMode::Form ? builtinShape(e.layers.first().shapeId) : nullptr;
        auto *item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(e.name, e.description), m_list);
        if (s)
            item->setIcon(QIcon(shapeIcon(*s)));
        item->setForeground(theme::text);
    }
    m_list->setCurrentRow(0);
    v->addWidget(m_list, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *load = bb->addButton(tr("Load effect"), QDialogButtonBox::AcceptRole);
    load->setProperty("role", "primary");
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    v->addWidget(bb);
}

Effect EffectLibraryDialog::chosen() const
{
    return m_templates.value(std::max(0, m_list->currentRow()));
}

// ===========================================================================
// Editor
// ===========================================================================

EffectEditor::EffectEditor(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);

    auto *head = new QHBoxLayout;
    auto *title = new QLabel(tr("Effect editor"), this);
    title->setProperty("role", "title");
    head->addWidget(title);
    head->addSpacing(10);
    // Which effect is open: pick it by tapping it in the Effects pool (with nothing selected).
    m_picker = new QLabel(this);
    m_picker->setFont(theme::uiFont(10, QFont::DemiBold));
    head->addWidget(m_picker);
    auto *newBtn = new QToolButton(this);
    newBtn->setText(tr("New"));
    newBtn->setPopupMode(QToolButton::InstantPopup);
    auto *newMenu = new QMenu(newBtn);
    newMenu->addAction(tr("Blank effect"), this, [this]() {
        const int id = m_ctx->show->nextFreeEffectId();
        m_ctx->show->setEffect(blankEffect(id));
        edit(id);
    });
    newMenu->addAction(tr("From the library…"), this, [this]() {
        EffectLibraryDialog d(this);
        if (d.exec() != QDialog::Accepted)
            return;
        Effect e = d.chosen();
        e.id = m_ctx->show->nextFreeEffectId();
        m_ctx->show->setEffect(e);
        edit(e.id);
    });
    newBtn->setMenu(newMenu);
    head->addWidget(newBtn);
    head->addStretch(1);
    auto *run = new QPushButton(tr("Run on selection"), this);
    run->setProperty("role", "primary");
    head->addWidget(run);
    connect(run, &QPushButton::clicked, this, [this]() {
        if (m_effectId && m_ctx->show->applyEffect(m_effectId))
            emit m_ctx->status(tr("%1 running").arg(m_effect.name));
    });
    v->addLayout(head);

    m_empty = muted(tr("Tap an effect in the Effects pool (with no fixtures selected) to edit it, tap New, or right-click an empty slot in the pool."), this);
    v->addWidget(m_empty);

    m_body = new QWidget(this);
    auto *bv = new QVBoxLayout(m_body);
    bv->setContentsMargins(0, 0, 0, 0);
    bv->setSpacing(10);

    // General settings
    auto *general = new QGridLayout;
    general->setHorizontalSpacing(10);
    general->setVerticalSpacing(8);
    m_name = new QLineEdit(m_body);
    m_cycle = new QDoubleSpinBox(m_body);
    m_cycle->setRange(0.05, 600);
    m_cycle->setDecimals(2);
    m_cycle->setSingleStep(0.1);
    m_bpm = muted(QString(), m_body);
    general->addWidget(muted(tr("Name"), m_body), 0, 0);
    general->addWidget(m_name, 0, 1, 1, 3);
    general->addWidget(muted(tr("Seconds per cycle"), m_body), 0, 4);
    general->addWidget(m_cycle, 0, 5);
    general->addWidget(m_bpm, 0, 6);
    m_rateMaster = new QComboBox(m_body);
    m_rateMaster->setToolTip(tr("Follow a rate master: its fader (or tapped tempo) sets this effect's speed"));
    general->addWidget(muted(tr("Rate master"), m_body), 0, 7);
    general->addWidget(m_rateMaster, 0, 8);
    m_spread = new QComboBox(m_body);
    m_spread->setToolTip(tr("The spread from the pool that lays the effect out: phase, blocks, groups, wings and direction. "
                            "Effects sharing a spread change together when you edit it."));
    general->addWidget(muted(tr("Spread"), m_body), 1, 0);
    general->addWidget(m_spread, 1, 1, 1, 3);
    // Which way it runs over the selection matrix: across, down, or both (diagonally).
    m_axisX = new QCheckBox(QStringLiteral("X"), m_body);
    m_axisY = new QCheckBox(QStringLiteral("Y"), m_body);
    m_axisX->setToolTip(tr("The phase runs along the selection matrix's X"));
    m_axisY->setToolTip(tr("The phase runs along its Y. With X too, it runs diagonally."));
    auto *axes = new QHBoxLayout;
    axes->addWidget(m_axisX);
    axes->addWidget(m_axisY);
    general->addWidget(muted(tr("Runs"), m_body), 1, 4);
    general->addLayout(axes, 1, 5, 1, 2);
    auto *hint = muted(tr("A spread on the selection when you run it, or on a blueprint line, lays it out instead."), m_body);
    hint->setWordWrap(true);
    general->addWidget(hint, 1, 7, 1, 2);
    connect(m_axisX, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading)
            return;
        m_effect.axisX = on;
        commit();
    });
    connect(m_axisY, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading)
            return;
        m_effect.axisY = on;
        commit();
    });
    general->setColumnStretch(1, 1);
    general->setColumnStretch(3, 1);
    bv->addLayout(general);

    auto *split = new QSplitter(Qt::Horizontal, m_body);
    split->setHandleWidth(12);
    // Layers
    auto *layerCol = new QWidget(split);
    auto *lv = new QVBoxLayout(layerCol);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->setSpacing(6);
    lv->addWidget(muted(tr("Layers"), layerCol));
    m_layers = new QListWidget(layerCol);
    lv->addWidget(m_layers, 1);
    auto *addRow = new QGridLayout;
    addRow->setSpacing(4);
    const QList<LayerKind> kinds = {LayerKind::Dimmer, LayerKind::Position, LayerKind::Color, LayerKind::Attributes};
    for (int i = 0; i < kinds.size(); ++i) {
        const LayerKind k = kinds[i];
        auto *b = new QToolButton(layerCol);
        b->setText(QStringLiteral("+ %1").arg(layerKindName(k)));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(b, &QToolButton::clicked, this, [this, k]() {
            EffectLayer l;
            l.kind = k;
            l.name = layerKindName(k);
            switch (k) {
            case LayerKind::Dimmer: l.points = {EffectPoint::level(0), EffectPoint::level(1)}; break;
            case LayerKind::Position: l.mode = LayerMode::Form; break;
            case LayerKind::Color:
                l.mode = LayerMode::Steps;
                l.points = {EffectPoint::color(1, 0, 0), EffectPoint::color(0, 0, 1)};
                break;
            case LayerKind::Attributes:
                l.points = {EffectPoint::level(0.2), EffectPoint::level(0.8)};
                l.attributes = {"Zoom"};
                break;
            }
            m_effect.layers << l;
            m_layer = int(m_effect.layers.size()) - 1;
            commit();
            refreshLayers();
            buildLayerDetails();
        });
        addRow->addWidget(b, i / 2, i % 2);
    }
    lv->addLayout(addRow);
    auto *removeLayer = new QToolButton(layerCol);
    removeLayer->setText(tr("Remove layer"));
    removeLayer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    lv->addWidget(removeLayer);
    connect(removeLayer, &QToolButton::clicked, this, [this]() {
        if (m_layer < 0 || m_layer >= m_effect.layers.size())
            return;
        m_effect.layers.removeAt(m_layer);
        m_layer = std::max(0, m_layer - 1);
        commit();
        refreshLayers();
        buildLayerDetails();
    });
    layerCol->setMinimumWidth(190);
    split->addWidget(layerCol);

    m_scroll = new QScrollArea(split);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_details = nullptr;
    m_detailsLayout = nullptr;
    split->addWidget(m_scroll);
    split->setStretchFactor(1, 3);
    bv->addWidget(split, 1);

    m_preview = new EffectPreview(ctx, m_body);
    bv->addWidget(m_preview);
    v->addWidget(m_body, 1);

    // General edits
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString &t) {
        m_effect.name = t;
        commit();
    });
    connect(m_cycle, &QDoubleSpinBox::valueChanged, this, [this](double s) {
        m_bpm->setText(tr("%1 BPM").arg(std::lround(60.0 / s)));
        if (m_loading)
            return;
        m_effect.cycle = s;
        commit();
    });
    connect(m_spread, &QComboBox::activated, this, [this](int index) {
        if (m_loading || !m_effectId)
            return;
        m_effect.spreadId = m_spread->itemData(index).toInt();
        commit();
    });
    connect(m_rateMaster, &QComboBox::activated, this, [this](int index) {
        if (m_loading)
            return;
        m_effect.rateMaster = m_rateMaster->itemData(index).toInt();
        m_cycle->setEnabled(true);
        commit();
    });
    connect(m_layers, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_loading || row < 0)
            return;
        m_layer = row;
        buildLayerDetails();
    });
    connect(ctx, &AppContext::editEffect, this, &EffectEditor::edit);
    connect(ctx, &AppContext::effectChosen, this, &EffectEditor::edit);
    connect(ctx->show, &Show::changed, this, [this](Show::Parts p) {
        if (m_loading)
            return;
        if (p & Show::EffectsPart) {
            refreshPicker();
            const Effect *e = m_ctx->show->effect(m_effectId);
            if (!e) {
                // Nothing (or a deleted effect) open: show the first effect there is.
                m_effectId = m_ctx->show->effects().isEmpty() ? 0 : m_ctx->show->effects().firstKey();
                m_layer = 0;
                refreshPicker();
                loadEffect();
            } else if (e->toJson() != m_effect.toJson()) {
                loadEffect();   // changed elsewhere (rename in the pool, command line...)
            }
        }
        if (p & (Show::ProgrammerPart | Show::PresetsPart))
            m_preview->update();
    });

    refreshPicker();
    int start = ctx->editingEffect();
    if (!ctx->show->effect(start) && !ctx->show->effects().isEmpty())
        start = ctx->show->effects().firstKey();
    edit(start);
}

void EffectEditor::edit(int effectId)
{
    m_effectId = m_ctx->show->effect(effectId) ? effectId : 0;
    m_layer = 0;
    refreshPicker();
    loadEffect();
}

void EffectEditor::refreshPicker()
{
    const Effect *e = m_ctx->show->effect(m_effectId);
    m_picker->setText(e ? QStringLiteral("Effect %1  ·  %2").arg(e->id).arg(e->name) : QString());
    setWindowTitle(e ? tr("Effect editor · %1").arg(e->name) : tr("Effect editor"));

}

void EffectEditor::loadEffect()
{
    const Effect *e = m_ctx->show->effect(m_effectId);
    m_body->setVisible(e != nullptr);
    m_empty->setVisible(e == nullptr);
    if (!e)
        return;
    m_loading = true;
    m_effect = *e;
    if (m_name->text() != m_effect.name)
        m_name->setText(m_effect.name);
    m_cycle->setValue(m_effect.cycle);
    m_bpm->setText(tr("%1 BPM").arg(std::lround(60.0 / m_effect.cycle)));
    m_spread->clear();
    m_spread->addItem(tr("None (its own layout)"), 0);
    for (const Spread &sp : m_ctx->show->spreads())
        m_spread->addItem(sp.isDefault() ? QStringLiteral("%1  %2").arg(sp.id).arg(sp.name)
                                         : QStringLiteral("%1  %2  ·  %3").arg(sp.id).arg(sp.name, sp.summary()),
                          sp.id);
    m_spread->setCurrentIndex(std::max(0, m_spread->findData(std::max(0, m_effect.spreadId))));
    m_rateMaster->clear();
    m_rateMaster->addItem(tr("None (own speed)"), 0);
    for (const RateMaster &rm : m_ctx->show->rateMasters())
        m_rateMaster->addItem(QStringLiteral("%1  %2  ·  %3").arg(rm.id).arg(rm.displayName(), rm.valueText()), rm.id);
    m_rateMaster->setCurrentIndex(std::clamp(m_effect.rateMaster, 0, RateMaster::kCount));
    m_axisX->setChecked(m_effect.axisX);
    m_axisY->setChecked(m_effect.axisY);
    m_layer = std::clamp(m_layer, 0, std::max(0, int(m_effect.layers.size()) - 1));
    m_loading = false;
    refreshLayers();
    buildLayerDetails();
    m_preview->setEffect(m_effect);
}

void EffectEditor::refreshLayers()
{
    m_loading = true;
    const int listScroll = m_layers->verticalScrollBar()->value();
    m_layers->clear();
    for (const EffectLayer &l : m_effect.layers) {
        auto *item = new QListWidgetItem(QStringLiteral("%1\n%2%3").arg(l.name.isEmpty() ? layerKindName(l.kind) : l.name,
                                                                          l.mode == LayerMode::Form ? formTypeName(l.form) : layerModeName(l.mode),
                                                                          l.enabled ? QString() : tr(", off")),
                                         m_layers);
        QPixmap dot(10, 10);
        dot.fill(Qt::transparent);
        QPainter p(&dot);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(l.enabled ? kindColor(l.kind) : theme::faint);
        p.setPen(Qt::NoPen);
        p.drawEllipse(1, 1, 8, 8);
        p.end();
        item->setIcon(QIcon(dot));
    }
    if (!m_effect.layers.isEmpty())
        m_layers->setCurrentRow(std::clamp(m_layer, 0, int(m_effect.layers.size()) - 1));
    m_layers->verticalScrollBar()->setValue(listScroll);
    m_loading = false;
}

void EffectEditor::commit()
{
    if (!m_effectId)
        return;
    m_loading = true;
    m_ctx->show->setEffect(m_effect);
    // What the show keeps (a new effect gets its pool spread): the same, so later changes
    // elsewhere don't look like an edit and reload the editor.
    if (const Effect *kept = m_ctx->show->effect(m_effectId))
        m_effect = *kept;
    m_loading = false;
    m_preview->setEffect(m_effect);
    refreshPicker();
}

QStringList EffectEditor::candidateAttributes() const
{
    QStringList out;
    for (int fid : m_ctx->show->fixtureIds())
        for (const QString &a : m_ctx->show->fixtureAttributes(fid)) {
            const Feature ft = featureOf(a);
            if (ft == Feature::Dimmer || ft == Feature::Position || ft == Feature::Color || out.contains(a))
                continue;
            out << a;
        }
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
        const int fa = int(featureOf(a)), fb = int(featureOf(b));
        return fa != fb ? fa < fb : attributeSortKey(a) < attributeSortKey(b);
    });
    return out;
}

namespace {
// The wave between its low and high points on a scale from -100 to 100, zero marked: shows a
// relative layer dipping below what's underneath as well as adding to it.
class RangePlot : public QWidget {
public:
    RangePlot(const Shape &shape, double low, double high, QWidget *parent)
        : QWidget(parent), m_shape(shape), m_low(low), m_high(high)
    {
        setFixedHeight(64);
        setToolTip(tr("The wave from low to high: above the line adds to the value underneath, below it takes away"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(34, 4, -4, -4);
        p.setPen(QPen(theme::line, 1));
        p.setBrush(QColor(0x14, 0x17, 0x1c));
        p.drawRoundedRect(r, 3, 3);
        auto yOf = [&](double v) { return r.top() + (1.0 - (std::clamp(v, -1.0, 1.0) + 1.0) / 2.0) * r.height(); };
        p.setFont(theme::uiFont(7));
        p.setPen(theme::muted);
        for (double v : {1.0, 0.0, -1.0})
            p.drawText(QRectF(0, yOf(v) - 7, 30, 14), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("%1").arg(int(v * 100)));
        p.setPen(QPen(theme::muted, 1, Qt::DashLine));
        p.drawLine(QPointF(r.left(), yOf(0)), QPointF(r.right(), yOf(0)));
        QPainterPath path;
        const int n = 96;
        for (int i = 0; i <= n; ++i) {
            const double q = double(i) / n;
            const double v = m_low + (m_high - m_low) * std::clamp(m_shape.value(q), 0.0, 1.0);
            const QPointF pt(r.left() + q * r.width(), yOf(v));
            if (i == 0)
                path.moveTo(pt);
            else
                path.lineTo(pt);
        }
        p.setPen(QPen(theme::featureColor(Feature::Dimmer), 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }

private:
    Shape m_shape;
    double m_low, m_high;
};
} // namespace

QWidget *EffectEditor::pointEditor(int index)
{
    EffectLayer &layer = m_effect.layers[m_layer];
    EffectPoint &pt = layer.points[index];
    auto *w = new QWidget(m_details);
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    QString label;
    if (layer.mode == LayerMode::Wave)
        label = index == 0 ? tr("Low") : tr("High");
    else
        label = tr("Step %1").arg(index + 1);
    auto *lab = new QLabel(label, w);
    lab->setMinimumWidth(52);
    h->addWidget(lab);

    // What a point can be depends on the layer
    auto *kind = new QComboBox(w);
    const bool position = layer.kind == LayerKind::Position;
    const bool color = layer.kind == LayerKind::Color;
    kind->addItem(color ? tr("Color") : position ? tr("Pan/Tilt") : tr("Level"), int(color ? EffectPoint::Color : EffectPoint::Value));
    kind->addItem(tr("Preset"), int(EffectPoint::Preset));
    kind->setCurrentIndex(pt.kind == EffectPoint::Preset ? 1 : 0);
    h->addWidget(kind);
    connect(kind, &QComboBox::activated, this, [this, index, kind, color, position]() {
        EffectPoint &p = m_effect.layers[m_layer].points[index];
        const auto k = EffectPoint::Kind(kind->currentData().toInt());
        if (k == p.kind)
            return;
        if (k == EffectPoint::Preset)
            p = EffectPoint::preset(0, 0);
        else if (color)
            p = EffectPoint::color(1, 1, 1);
        else if (position)
            p = EffectPoint::panTilt(0.0, 0.0);   // degrees: the middle
        else
            p = EffectPoint::level(1.0);
        commit();
        QTimer::singleShot(0, this, &EffectEditor::buildLayerDetails);
    });

    if (pt.kind == EffectPoint::Preset) {
        auto *presets = new QComboBox(w);
        presets->addItem(tr("Choose a preset"), QString());
        QList<int> types;
        switch (layer.kind) {
        case LayerKind::Dimmer: types = {1, kPresetTypeAll}; break;
        case LayerKind::Position: types = {2, kPresetTypeAll}; break;
        case LayerKind::Color: types = {4, kPresetTypeAll}; break;
        case LayerKind::Attributes: types = {3, 5, 6, 7, kPresetTypeAll}; break;
        }
        for (int t : types)
            for (const Preset &p : m_ctx->show->presets(t))
                presets->addItem(QStringLiteral("%1  %2").arg(presetRef(t, p.id), p.name), QStringLiteral("%1.%2").arg(t).arg(p.id));
        presets->setCurrentIndex(std::max(0, presets->findData(QStringLiteral("%1.%2").arg(pt.presetType).arg(pt.presetId))));
        h->addWidget(presets, 1);
        connect(presets, &QComboBox::activated, this, [this, index, presets]() {
            const QStringList parts = presets->currentData().toString().split('.');
            EffectPoint &p = m_effect.layers[m_layer].points[index];
            p.presetType = parts.value(0).toInt();
            p.presetId = parts.value(1).toInt();
            commit();
        });
    } else if (color) {
        auto *swatch = new QPushButton(w);
        const QColor c = toDisplay({pt.r, pt.g, pt.b});
        swatch->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: 1px solid %2; min-width: 90px; }").arg(c.name(), theme::line.name()));
        swatch->setToolTip(tr("Pick a color"));
        h->addWidget(swatch, 1);
        connect(swatch, &QPushButton::clicked, this, [this, index, c]() {
            const QColor picked = QColorDialog::getColor(c, this, tr("Point color"));
            if (!picked.isValid())
                return;
            const LinearRgb lin = toLinear(picked);
            EffectPoint &p = m_effect.layers[m_layer].points[index];
            p.r = lin.r;
            p.g = lin.g;
            p.b = lin.b;
            commit();
            QTimer::singleShot(0, this, &EffectEditor::buildLayerDetails);
        });
    } else if (position) {
        for (const char *attr : {"Pan", "Tilt"}) {
            h->addWidget(muted(attr, w));
            auto *spin = new QSpinBox(w);   // degrees, 0 the middle
            spin->setRange(-360, 360);
            spin->setSuffix(QStringLiteral("°"));
            spin->setValue(int(std::lround(pt.values.value(attr, 0.0))));
            h->addWidget(spin);
            const QString a = attr;
            connect(spin, &QSpinBox::valueChanged, this, [this, index, a](int v) {
                m_effect.layers[m_layer].points[index].values[a] = v;
                commit();
            });
        }
        h->addStretch(1);
    } else {
        // Relative layers add to the value underneath: below zero takes it down.
        auto *spin = new QSpinBox(w);
        spin->setRange(layer.relative ? -100 : 0, 100);
        spin->setValue(int(std::lround(pt.values.value("Level", 0.0) * 100)));
        if (layer.relative)
            spin->setPrefix(QString());
        h->addWidget(spin);
        h->addWidget(muted(layer.relative ? tr("percent, added to the value underneath") : tr("percent"), w));
        h->addStretch(1);
        connect(spin, &QSpinBox::valueChanged, this, [this, index](int v) {
            m_effect.layers[m_layer].points[index].values["Level"] = v / 100.0;
            commit();
            if (m_effect.layers[m_layer].relative && m_effect.layers[m_layer].mode == LayerMode::Wave)
                QTimer::singleShot(0, this, &EffectEditor::buildLayerDetails);   // the plot follows
        });
    }
    if (layer.mode == LayerMode::Steps && layer.points.size() > 2) {
        auto *rm = new QToolButton(w);
        rm->setText(QStringLiteral("✕"));
        rm->setToolTip(tr("Remove this step"));
        h->addWidget(rm);
        connect(rm, &QToolButton::clicked, this, [this, index]() {
            m_effect.layers[m_layer].points.removeAt(index);
            commit();
            QTimer::singleShot(0, this, &EffectEditor::buildLayerDetails);
        });
    }
    return w;
}

void EffectEditor::buildLayerDetails()
{
    // A fresh details page each time; the old one goes once any event it's handling is done.
    // Rebuilding the same layer (a color picked, a step added) keeps the page where it was
    // scrolled to.
    const int keepScroll = m_builtLayer == m_layer && m_builtEffect == m_effectId ? m_scroll->verticalScrollBar()->value() : 0;
    m_builtLayer = m_layer;
    m_builtEffect = m_effectId;
    if (keepScroll > 0)
        QTimer::singleShot(0, this, [this, keepScroll]() { m_scroll->verticalScrollBar()->setValue(keepScroll); });
    if (QWidget *old = m_scroll->takeWidget()) {
        old->hide();
        old->deleteLater();
    }
    m_details = new QWidget(m_scroll);
    m_detailsLayout = new QVBoxLayout(m_details);
    m_detailsLayout->setContentsMargins(4, 0, 8, 0);
    m_detailsLayout->setSpacing(8);
    m_scroll->setWidget(m_details);
    if (m_layer < 0 || m_layer >= m_effect.layers.size()) {
        m_detailsLayout->addWidget(muted(tr("Add a layer: Dimmer, Position, Color or Attributes."), m_details));
        m_detailsLayout->addStretch(1);
        return;
    }
    EffectLayer &layer = m_effect.layers[m_layer];
    auto *form = new QFormLayout;
    form->setVerticalSpacing(8);
    form->setLabelAlignment(Qt::AlignLeft);

    auto *name = new QLineEdit(layer.name, m_details);
    form->addRow(tr("Layer name"), name);
    connect(name, &QLineEdit::textEdited, this, [this](const QString &t) {
        m_effect.layers[m_layer].name = t;
        commit();
        refreshLayers();
    });
    auto *enabled = new QCheckBox(tr("Layer is on"), m_details);
    enabled->setChecked(layer.enabled);
    form->addRow(QString(), enabled);
    connect(enabled, &QCheckBox::toggled, this, [this](bool on) {
        m_effect.layers[m_layer].enabled = on;
        commit();
        refreshLayers();
    });

    if (layer.kind == LayerKind::Dimmer || layer.kind == LayerKind::Attributes) {
        auto *relative = new QCheckBox(tr("Relative: adds to what's underneath (can go below zero)"), m_details);
        relative->setChecked(layer.relative);
        relative->setToolTip(tr("On: the effect moves whatever value is already there (a running executor, the programmer) "
                                "up or down by its points, so -40 to 0 dips a level that's up. Off: the points are the values."));
        form->addRow(QString(), relative);
        connect(relative, &QCheckBox::toggled, this, [this](bool on) {
            EffectLayer &l = m_effect.layers[m_layer];
            l.relative = on;
            if (on && l.mode != LayerMode::Form)   // a dip to start from: down 50% and back
                for (int i = 0; i < l.points.size(); ++i)
                    if (l.points[i].kind == EffectPoint::Value)
                        l.points[i].values["Level"] = i == 0 ? -0.5 : 0.0;
            if (!on)
                for (EffectPoint &pt : l.points)
                    if (pt.kind == EffectPoint::Value)
                        pt.values["Level"] = std::clamp(pt.values.value("Level", 0.0), 0.0, 1.0);
            commit();
            refreshLayers();
            QTimer::singleShot(0, this, &EffectEditor::buildLayerDetails);
        });
    }

    auto *mode = new QComboBox(m_details);
    mode->addItem(tr("Wave: between low and high along a shape"), int(LayerMode::Wave));
    mode->addItem(tr("Steps: through a list of points"), int(LayerMode::Steps));
    if (layer.kind == LayerKind::Position)
        mode->addItem(tr("Form: a figure around the current position"), int(LayerMode::Form));
    mode->setCurrentIndex(std::max(0, mode->findData(int(layer.mode))));
    form->addRow(tr("Mode"), mode);
    connect(mode, &QComboBox::activated, this, [this, mode]() {
        EffectLayer &l = m_effect.layers[m_layer];
        l.mode = LayerMode(mode->currentData().toInt());
        if (l.mode != LayerMode::Form && l.points.size() < 2) {
            if (l.kind == LayerKind::Position)
                l.points = {EffectPoint::panTilt(-45.0, 0.0), EffectPoint::panTilt(45.0, 0.0)};   // degrees
            else if (l.kind == LayerKind::Color)
                l.points = {EffectPoint::color(1, 0, 0), EffectPoint::color(0, 0, 1)};
            else
                l.points = {EffectPoint::level(0), EffectPoint::level(1)};
        }
        if (l.mode == LayerMode::Wave && l.points.size() > 2)
            l.points.resize(2);
        commit();
        refreshLayers();
        QTimer::singleShot(0, this, &EffectEditor::buildLayerDetails);
    });

    if (layer.mode == LayerMode::Wave) {
        auto *shapeRow = new QHBoxLayout;
        auto *shape = new QComboBox(m_details);
        shape->setIconSize(QSize(40, 22));
        for (const Shape &s : builtinShapes())
            shape->addItem(QIcon(shapeIcon(s)), s.name, s.id);
        for (const Shape &s : m_ctx->show->shapes())
            shape->addItem(QIcon(shapeIcon(s)), s.name, s.id);
        shape->setCurrentIndex(std::max(0, shape->findData(layer.shapeId)));
        shapeRow->addWidget(shape, 1);
        auto *newShape = new QToolButton(m_details);
        newShape->setText(tr("Draw new…"));
        auto *editShape = new QToolButton(m_details);
        editShape->setText(tr("Edit…"));
        editShape->setEnabled(layer.shapeId > 0);
        editShape->setToolTip(tr("Edits the shape everywhere it's used"));
        shapeRow->addWidget(newShape);
        shapeRow->addWidget(editShape);
        form->addRow(tr("Shape"), shapeRow);
        connect(shape, &QComboBox::activated, this, [this, shape, editShape]() {
            m_effect.layers[m_layer].shapeId = shape->currentData().toInt();
            editShape->setEnabled(m_effect.layers[m_layer].shapeId > 0);
            commit();
        });
        connect(newShape, &QToolButton::clicked, this, [this]() {
            const Shape &current = resolveShape(m_ctx->show, m_effect.layers[m_layer].shapeId);
            Shape s;
            s.id = m_ctx->show->nextFreeShapeId();
            s.name = tr("Shape %1").arg(s.id);
            for (int k = 0; k < 8; ++k)
                s.points.append({k / 8.0, current.value(k / 8.0)});
            ShapeEditorDialog d(s, this);
            if (d.exec() != QDialog::Accepted)
                return;
            Shape r = d.result();
            r.id = s.id;
            m_ctx->show->setShape(r);
            m_effect.layers[m_layer].shapeId = r.id;
            commit();
            QTimer::singleShot(0, this, &EffectEditor::buildLayerDetails);
        });
        connect(editShape, &QToolButton::clicked, this, [this]() {
            const Shape *s = m_ctx->show->shape(m_effect.layers[m_layer].shapeId);
            if (!s)
                return;
            ShapeEditorDialog d(*s, this);
            if (d.exec() != QDialog::Accepted)
                return;
            Shape r = d.result();
            r.id = s->id;
            m_ctx->show->setShape(r);
            m_preview->update();
            QTimer::singleShot(0, this, &EffectEditor::buildLayerDetails);
        });
    }
    if (layer.mode == LayerMode::Steps) {
        auto *fade = new QSpinBox(m_details);
        fade->setRange(0, 100);
        fade->setValue(int(std::lround(layer.crossfade * 100)));
        fade->setToolTip(tr("0 snaps between steps, 100 fades the whole step"));
        form->addRow(tr("Crossfade (percent of each step)"), fade);
        connect(fade, &QSpinBox::valueChanged, this, [this](int v) {
            m_effect.layers[m_layer].crossfade = v / 100.0;
            commit();
        });
        auto *order = new QComboBox(m_details);
        order->addItem(tr("In order, then loop"), int(StepOrder::Loop));
        order->addItem(tr("There and back"), int(StepOrder::Bounce));
        order->addItem(tr("Random"), int(StepOrder::Random));
        order->setCurrentIndex(int(layer.order));
        form->addRow(tr("Step order"), order);
        connect(order, &QComboBox::activated, this, [this, order]() {
            m_effect.layers[m_layer].order = StepOrder(order->currentData().toInt());
            commit();
        });
    }
    if (layer.mode != LayerMode::Form) {
        auto *width = new QSpinBox(m_details);
        width->setRange(1, 100);
        width->setValue(int(std::lround(layer.width * 100)));
        width->setToolTip(tr("Squeezes the movement into part of the cycle and rests on the first point for the remainder"));
        form->addRow(tr("Width (percent of the cycle)"), width);
        connect(width, &QSpinBox::valueChanged, this, [this](int v) {
            m_effect.layers[m_layer].width = v / 100.0;
            commit();
        });
    }
    auto *speed = new QDoubleSpinBox(m_details);
    speed->setRange(0.05, 16);
    speed->setSingleStep(0.25);
    speed->setValue(layer.speed);
    form->addRow(tr("Speed (times the effect's rate)"), speed);
    connect(speed, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        m_effect.layers[m_layer].speed = v;
        commit();
    });
    auto *offset = new QSpinBox(m_details);
    offset->setRange(0, 359);
    offset->setSingleStep(15);
    offset->setValue(int(layer.offset));
    form->addRow(tr("Phase offset (degrees)"), offset);
    connect(offset, &QSpinBox::valueChanged, this, [this](int v) {
        m_effect.layers[m_layer].offset = v;
        commit();
    });
    m_detailsLayout->addLayout(form);

    // ---- Form
    if (layer.mode == LayerMode::Form) {
        auto *ff = new QFormLayout;
        ff->setVerticalSpacing(8);
        auto *formType = new QComboBox(m_details);
        for (FormType f : {FormType::Circle, FormType::Figure8, FormType::LineHorizontal, FormType::LineVertical,
                           FormType::Square, FormType::Triangle})
            formType->addItem(formTypeName(f), int(f));
        formType->setCurrentIndex(int(layer.form));
        ff->addRow(tr("Figure"), formType);
        connect(formType, &QComboBox::activated, this, [this, formType]() {
            m_effect.layers[m_layer].form = FormType(formType->currentData().toInt());
            commit();
            refreshLayers();
        });
        for (int axis = 0; axis < 2; ++axis) {
            auto *size = new QSpinBox(m_details);
            size->setRange(0, 100);
            size->setValue(int(std::lround((axis == 0 ? layer.sizePan : layer.sizeTilt) * 100)));
            ff->addRow(axis == 0 ? tr("Pan size (percent of range)") : tr("Tilt size (percent of range)"), size);
            connect(size, &QSpinBox::valueChanged, this, [this, axis](int v) {
                (axis == 0 ? m_effect.layers[m_layer].sizePan : m_effect.layers[m_layer].sizeTilt) = v / 100.0;
                commit();
            });
        }
        auto *center = new QComboBox(m_details);
        center->addItem(tr("Wherever each fixture points"), QString());
        for (int t : {2, kPresetTypeAll})
            for (const Preset &p : m_ctx->show->presets(t))
                center->addItem(QStringLiteral("%1  %2").arg(presetRef(t, p.id), p.name), QStringLiteral("%1.%2").arg(t).arg(p.id));
        if (layer.hasCenter && layer.center.kind == EffectPoint::Preset)
            center->setCurrentIndex(std::max(0, center->findData(QStringLiteral("%1.%2").arg(layer.center.presetType).arg(layer.center.presetId))));
        ff->addRow(tr("Around"), center);
        connect(center, &QComboBox::activated, this, [this, center]() {
            EffectLayer &l = m_effect.layers[m_layer];
            const QStringList parts = center->currentData().toString().split('.');
            l.hasCenter = parts.size() == 2;
            if (l.hasCenter)
                l.center = EffectPoint::preset(parts[0].toInt(), parts[1].toInt());
            commit();
        });
        m_detailsLayout->addLayout(ff);
    } else {
        // ---- Points
        m_detailsLayout->addWidget(muted(layer.mode == LayerMode::Wave ? tr("Moves between") : tr("Steps"), m_details));
        for (int i = 0; i < layer.points.size(); ++i)
            m_detailsLayout->addWidget(pointEditor(i));
        if (layer.relative && layer.mode == LayerMode::Wave && layer.points.size() >= 2
            && layer.points[0].kind == EffectPoint::Value && layer.points[1].kind == EffectPoint::Value)
            m_detailsLayout->addWidget(new RangePlot(resolveShape(m_ctx->show, layer.shapeId),
                                                     layer.points[0].values.value("Level", 0.0),
                                                     layer.points[1].values.value("Level", 0.0), m_details));
        if (layer.mode == LayerMode::Steps) {
            auto *add = new QToolButton(m_details);
            add->setText(tr("+ Add step"));
            m_detailsLayout->addWidget(add, 0, Qt::AlignLeft);
            connect(add, &QToolButton::clicked, this, [this]() {
                EffectLayer &l = m_effect.layers[m_layer];
                l.points.append(l.points.isEmpty() ? EffectPoint::level(1) : l.points.last());
                commit();
                QTimer::singleShot(0, this, &EffectEditor::buildLayerDetails);
            });
        }
    }

    // ---- Color families
    if (layer.kind == LayerKind::Color) {
        m_detailsLayout->addWidget(muted(tr("Drives these color parameters (all by default; switch off any you want left alone)"), m_details));
        auto *grid = new QGridLayout;
        grid->setSpacing(4);
        int n = 0;
        for (const ColorFamily &f : colorFamilies()) {
            auto *c = chip(f.label, !layer.excludedFamilies.contains(f.id), m_details);
            const QString id = f.id;
            connect(c, &QToolButton::toggled, this, [this, id](bool on) {
                QStringList &ex = m_effect.layers[m_layer].excludedFamilies;
                ex.removeAll(id);
                if (!on)
                    ex << id;
                commit();
            });
            grid->addWidget(c, n / 6, n % 6);
            ++n;
        }
        m_detailsLayout->addLayout(grid);
    }

    // ---- Attributes
    if (layer.kind == LayerKind::Attributes) {
        m_detailsLayout->addWidget(muted(tr("Drives these attributes"), m_details));
        const QStringList candidates = candidateAttributes();
        auto *grid = new QGridLayout;
        grid->setSpacing(4);
        int n = 0;
        for (const QString &a : candidates) {
            auto *c = chip(attributeLabel(a), layer.attributes.contains(a), m_details);
            connect(c, &QToolButton::toggled, this, [this, a](bool on) {
                QStringList &list = m_effect.layers[m_layer].attributes;
                list.removeAll(a);
                if (on)
                    list << a;
                commit();
            });
            grid->addWidget(c, n / 5, n % 5);
            ++n;
        }
        if (candidates.isEmpty())
            m_detailsLayout->addWidget(muted(tr("Patch fixtures with beam or focus attributes to pick them here."), m_details));
        m_detailsLayout->addLayout(grid);
    }
    m_detailsLayout->addStretch(1);
}

} // namespace lumora
