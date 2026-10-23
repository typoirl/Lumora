// SPDX-License-Identifier: GPL-3.0-or-later
#include "workspace.h"

#include "encoderbar.h"
#include "parkview.h"

#include "appcontext.h"
#include "colorpicker.h"
#include "cueview.h"
#include "effectsui.h"
#include "executorbar.h"
#include "controlview.h"
#include "imagesui.h"
#include "helpview.h"
#include "matrixui.h"
#include "media.h"
#include "midiview.h"
#include "patchview.h"
#include "fixturesheet.h"
#include "historyview.h"
#include "macrosui.h"
#include "plotsui.h"
#include "playbackmonitor.h"
#include "keypad.h"
#include "outputview.h"
#include "quickedit.h"
#include "theme.h"
#include "uiutil.h"

#include <algorithm>
#include <QStyle>
#include <QApplication>
#include <QDialog>
#include <QPainterPath>
#include <QContextMenuEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QStackedWidget>
#include <QTabBar>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QSet>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <memory>

namespace lumora {

namespace {

// Pads a panel so content doesn't touch the dock edges.
QWidget *padded(QWidget *content, QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(4, 3, 4, 3);
    l->addWidget(content);
    content->setParent(w);
    w->setProperty("panelContent", QVariant::fromValue<QObject *>(content));
    return w;
}

class DmxPanel : public QWidget {
public:
    DmxPanel(AppContext *ctx, QWidget *parent) : QWidget(parent)
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        auto *head = new QHBoxLayout;
        auto *title = new QLabel(tr("DMX monitor"), this);
        title->setProperty("role", "title");
        auto *uniLabel = new QLabel(tr("Universe"), this);
        uniLabel->setProperty("role", "muted");
        auto *uni = new QSpinBox(this);
        uni->setRange(1, kMaxUniverses);
        uni->setMinimumWidth(80);
        head->addWidget(title);
        head->addSpacing(14);
        head->addWidget(uniLabel);
        head->addWidget(uni);
        head->addStretch(1);
        v->addLayout(head);
        auto *mon = new DmxMonitor(ctx, this);
        v->addWidget(mon, 1);
        connect(uni, &QSpinBox::valueChanged, mon, &DmxMonitor::setUniverse);
    }
};
} // namespace

// ===========================================================================
// Groups
// ===========================================================================

GroupsPanel::GroupsPanel(AppContext *ctx, QWidget *parent) : PoolPanel(tr("Groups"), parent), m_ctx(ctx)
{
    Show *show = ctx->show;
    connect(tiles(), &PoolTiles::tileClicked, this, [this, show](int id, bool occupied, Qt::KeyboardModifiers mods) {
        if (m_ctx->storeArmed()) {
            m_ctx->setStoreArmed(false);
            m_ctx->run(QStringLiteral("Store Group %1").arg(id));   // asks when it exists
            return;
        }
        if (m_ctx->tapTarget(QStringLiteral("Group %1").arg(id), show->group(id) ? show->group(id)->name : QString()))
            return;
        if (occupied)
            // Groups add to the selection; Ctrl takes a group's fixtures back out.
            show->selectGroup(id, (mods & Qt::ControlModifier) ? Show::SelectMode::Remove : Show::SelectMode::Add);
    });
    connect(tiles(), &PoolTiles::tileMenu, this, [this, show](int id, bool occupied, const QPoint &pos) {
        if (!occupied)
            return;
        auto *ed = new QuickEditor(tr("Group %1").arg(id), this);
        ed->addNameField(tr("Name"), show->group(id)->name, [show, id](const QString &n) {
            if (const Group *g0 = show->group(id)) {
                Group g = *g0;
                g.name = n;
                show->setGroup(g);
            }
        });
        ed->addSection(QString());
        ed->addAction(tr("Update with selection"), [show, id]() { show->storeGroup(id); });
        ed->addAction(tr("Delete"), [show, id]() { show->deleteGroup(id); }, QIcon(), true);
        ed->popup(pos);
    });
    onShowChanged(this, show, [this](Show::Parts p) {
        if (p & (Show::GroupsPart | Show::ProgrammerPart | Show::PatchPart))
            refresh();
    });
    followStore(ctx);
    refresh();
}

void GroupsPanel::refresh()
{
    QVector<PoolItem> items;
    const QVector<int> sel = m_ctx->show->selection();
    for (const Group &g : m_ctx->show->groups()) {
        PoolItem it;
        it.id = g.id;
        it.label = g.name;
        it.active = !sel.isEmpty() && sel == g.fixtures;
        items << it;
    }
    tiles()->setItems(items);
}

// ===========================================================================
// Spreads (spread-style)
// ===========================================================================

namespace {
// The fields of a spread; every change applies at once.
void spreadFields(QuickEditor *ed, const Spread &start, std::function<void(const Spread &)> apply)
{
    auto m = std::make_shared<Spread>(start);
    auto set = [m, apply](std::function<void(Spread &)> change) {
        change(*m);
        apply(*m);
    };
    // Which way it works on the selection matrix: X, Y, or both.
    ed->addSegmented(QObject::tr("Works along"), {QStringLiteral("X"), QStringLiteral("Y"), QObject::tr("X and Y")},
                     m->onX && m->onY ? 2 : m->onY ? 1 : 0, [set](int i) {
                         set([i](Spread &x) {
                             x.onX = i != 1;
                             x.onY = i != 0;
                         });
                     });
    ed->addSection(QObject::tr("Which fixtures"));
    ed->addNumber(QObject::tr("Every Nth fixture (interleave)"), 1, 64, m->every, [set](int v) { set([v](Spread &x) { x.every = v; }); });
    ed->addNumber(QObject::tr("Starting at"), 1, 64, m->offset + 1, [set](int v) { set([v](Spread &x) { x.offset = v - 1; }); });
    ed->addSection(QObject::tr("How effects spread across them"));
    // -720 to 720 either way: -360 thru 0 runs the chase the other way, 0 thru 720 goes round twice.
    ed->addNumber(QObject::tr("Phase from (degrees)"), -720, 720, int(m->phaseFrom),
                  [set](int v) { set([v](Spread &x) { x.phaseFrom = v; }); }, 15);
    ed->addNumber(QObject::tr("Phase to (0 together, 360 a full chase, -360 the other way)"), -720, 720, int(m->phaseTo),
                  [set](int v) { set([v](Spread &x) { x.phaseTo = v; }); }, 15);
    ed->addNumber(QObject::tr("Blocks (fixtures sharing a phase)"), 1, 64, m->blocks, [set](int v) { set([v](Spread &x) { x.blocks = v; }); });
    ed->addNumber(QObject::tr("Groups (0 off: the phase repeats every N)"), 0, 64, m->groups,
                  [set](int v) { set([v](Spread &x) { x.groups = v; }); });
    ed->addNumber(QObject::tr("Wings (mirrored)"), 1, 16, m->wings, [set](int v) { set([v](Spread &x) { x.wings = v; }); });
    QStringList dirs;
    for (int i = 0; i <= int(SpreadDirection::Random); ++i)
        dirs << spreadDirectionName(SpreadDirection(i));
    ed->addSegmented(QObject::tr("Direction"), dirs, int(m->direction),
                     [set](int i) { set([i](Spread &x) { x.direction = SpreadDirection(i); }); });
}

bool sameLayout(const Spread &a, const Spread &b)
{
    Spread x = a, y = b;
    x.id = y.id = 0;
    x.name = y.name = QString();
    x.onX = y.onX = true;   // the selection keeps a copy per axis
    x.onY = y.onY = false;
    return x.toJson() == y.toJson();
}
} // namespace

SpreadsPanel::SpreadsPanel(AppContext *ctx, QWidget *parent) : PoolPanel(tr("Spreads"), parent), m_ctx(ctx)
{
    Show *show = ctx->show;
    // The spreads on the selection, in the header: tap to open the Spreads modifier. Clearing
    // the selection resets them.
    m_active = new QToolButton(this);
    m_active->setToolTip(tr("The spreads on the selection, across (X) and down (Y). Tap to change them in the Spreads modifier."));
    headerLayout()->addWidget(m_active);
    connect(m_active, &QToolButton::clicked, this, [this]() {
        auto *d = new QDialog(this, Qt::Tool);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->setWindowTitle(tr("Spreads modifier"));
        auto *v = new QVBoxLayout(d);
        v->setContentsMargins(0, 0, 0, 0);
        v->addWidget(new SpreadModifierPanel(m_ctx, d));
        d->resize(460, 420);
        d->show();
    });

    connect(tiles(), &PoolTiles::tileClicked, this, [this, show](int id, bool occupied, Qt::KeyboardModifiers) {
        if (m_ctx->storeArmed()) {
            m_ctx->setStoreArmed(false);
            m_ctx->run(QStringLiteral("Store Spread %1").arg(id));
            return;
        }
        if (m_ctx->tapTarget(QStringLiteral("Spread %1").arg(id), show->spread(id) ? show->spread(id)->name : QString()))
            return;
        if (!occupied) {
            emit m_ctx->status(tr("Empty spread slot: set the spread (tap the header), press Store, then tap it"));
            return;
        }
        // Tap again to take it off (on its axes only: an X and a Y spread stack).
        const Spread &sp = *show->spread(id);
        auto onAxis = [&](Spread::Axis a) {
            const Spread &active = show->activeSpread(a);
            return sp.on(a) && sameLayout(active, sp) && !active.isDefault() && show->spreadAxisOn(a);
        };
        const bool on = (sp.onX && onAxis(Spread::Axis::X)) || (sp.onY && onAxis(Spread::Axis::Y));
        if (!on) {
            m_ctx->run(QStringLiteral("Spread %1").arg(id));
        } else {
            if (sp.onX)
                m_ctx->run(QStringLiteral("Spread X Off"));
            if (sp.onY)
                m_ctx->run(QStringLiteral("Spread Y Off"));
        }
    });
    connect(tiles(), &PoolTiles::tileMenu, this, [this, show](int id, bool occupied, const QPoint &pos) {
        auto *ed = new QuickEditor(tr("Spread %1").arg(id), this);
        if (!occupied) {
            ed->addAction(tr("Store the active spread here"), [this, id]() { m_ctx->run(QStringLiteral("Store Spread %1").arg(id)); });
            ed->popup(pos);
            return;
        }
        ed->addNameField(tr("Name"), show->spread(id)->name, [show, id](const QString &n) {
            if (const Spread *m = show->spread(id)) {
                Spread c = *m;
                c.name = n;
                show->setSpread(c);
            }
        });
        spreadFields(ed, *show->spread(id), [show, id](const Spread &edited) {
            if (const Spread *m = show->spread(id)) {
                Spread c = edited;
                c.id = id;
                c.name = m->name;
                show->setSpread(c);
            }
        });
        ed->addSection(QString());
        ed->addAction(tr("Put on the selection"), [this, id]() { m_ctx->run(QStringLiteral("Spread %1").arg(id)); });
        ed->addAction(tr("Update with the active spread"), [this, id]() { m_ctx->run(QStringLiteral("Store Spread %1").arg(id)); });
        ed->addAction(tr("Delete"), [this, id]() { m_ctx->run(QStringLiteral("Delete Spread %1").arg(id)); }, QIcon(), true);
        ed->popup(pos);
    });
    onShowChanged(this, show, [this](Show::Parts p) {
        if (p & (Show::SpreadsPart | Show::ProgrammerPart))
            refresh();
    });
    followStore(ctx);
    refresh();
}

void SpreadsPanel::refresh()
{
    Show *show = m_ctx->show;
    const QString summary = show->activeSpreadSummary();
    m_active->setText(summary.isEmpty() ? tr("On the selection: none") : tr("On the selection: %1").arg(summary));
    QVector<PoolItem> items;
    for (const Spread &m : show->spreads()) {
        PoolItem it;
        it.id = m.id;
        it.label = m.name + (m.onX && m.onY ? QStringLiteral("  XY") : m.onY ? QStringLiteral("  Y") : QString());
        it.tag = theme::featureColor(m.onY && !m.onX ? Feature::Beam : Feature::Position);
        auto onAxis = [&](Spread::Axis a) {
            const Spread &active = show->activeSpread(a);
            return m.on(a) && !active.isDefault() && show->spreadAxisOn(a) && sameLayout(active, m);
        };
        it.active = onAxis(Spread::Axis::X) || onAxis(Spread::Axis::Y);
        it.tooltip = tr("Spread %1 \"%2\": %3").arg(m.id).arg(m.name, m.summary());
        items << it;
    }
    tiles()->setItems(items);
}

// ===========================================================================
// Executor pages
// ===========================================================================

ExecutorPagesPanel::ExecutorPagesPanel(AppContext *ctx, QWidget *parent) : PoolPanel(tr("Executor pages"), parent), m_ctx(ctx)
{
    setWindowTitle(tr("Executor pages"));
    Show *show = ctx->show;
    connect(tiles(), &PoolTiles::tileClicked, this, [this, show](int id, bool, Qt::KeyboardModifiers) {
        if (m_ctx->tapTarget(QStringLiteral("Page %1").arg(id), show->pageName(id)))
            return;
        m_ctx->run(QStringLiteral("Page %1").arg(id));
    });
    connect(tiles(), &PoolTiles::tileMenu, this, [this, show](int id, bool, const QPoint &pos) {
        auto *ed = new QuickEditor(tr("Page %1").arg(id), this);
        ed->addNameField(tr("Name"), show->pageName(id), [show, id](const QString &n) { show->setPageName(id, n.trimmed()); });
        ed->addAction(tr("Go to page %1").arg(id), [show, id]() { show->setPage(id); });
        ed->popup(pos);
    });
    onShowChanged(this, show, [this](Show::Parts p) {
        if (p & (Show::ExecutorsPart | Show::SequencesPart))
            refresh();
    });
    refresh();
}

void ExecutorPagesPanel::refresh()
{
    Show *show = m_ctx->show;
    QList<int> pages = show->pages();
    if (!pages.contains(show->currentPage()))
        pages << show->currentPage();
    QVector<PoolItem> items;
    for (int p : pages) {
        PoolItem it;
        it.id = p;
        const QString name = show->pageName(p);
        int used = 0;
        for (Executor *e : show->allExecutors())
            if (e->page == p && !e->isEmpty())
                ++used;
        it.label = name.isEmpty() ? tr("Page %1").arg(p) : name;
        it.tooltip = countOf(used, tr("executor"), tr("executors"));
        it.active = p == show->currentPage();
        it.tag = it.active ? theme::live : QColor();
        items << it;
    }
    tiles()->setItems(items);
}

// ===========================================================================
// Rate masters
// ===========================================================================

namespace {
void rateMasterMenu(AppContext *ctx, QWidget *parent, int id, const QPoint &pos)
{
    Show *show = ctx->show;
    const RateMaster *cur = show->rateMaster(id);
    if (!cur)
        return;
    auto change = [show, id](std::function<void(RateMaster &)> fn) {
        RateMaster m = *show->rateMaster(id);
        fn(m);
        show->setRateMaster(m);
    };
    auto *ed = new QuickEditor(QObject::tr("Master %1").arg(id), parent);
    ed->addNameField(QObject::tr("Name"), cur->name, [change](const QString &n) {
        change([n](RateMaster &m) { m.name = n.trimmed(); });
    }, true, QObject::tr("Master %1").arg(id));
    ed->addSegmented(QObject::tr("Show it as"), {QObject::tr("Fader"), QObject::tr("Button")}, cur->button ? 1 : 0,
                     [change](int i) { change([i](RateMaster &m) { m.button = i == 1; }); });
    ed->addSegmented(QObject::tr("Its key"), {QObject::tr("Learn (tap tempo)"), QObject::tr("Reset")}, cur->keyLearns() ? 0 : 1,
                     [change](int i) { change([i](RateMaster &m) { m.key = i == 0 ? RateMaster::Key::Learn : RateMaster::Key::Reset; }); });
    ed->addSegmented(QObject::tr("Mode"), {QObject::tr("Rate"), QObject::tr("BPM"), QObject::tr("Ratio")}, int(cur->mode),
                     [change](int i) {
                         change([i](RateMaster &m) {
                             if (int(m.mode) == i)
                                 return;
                             m.mode = RateMode(i);
                             m.value = m.defaultValue();
                         });
                     });
    switch (cur->mode) {
    case RateMode::Rate:
        ed->addValueField(QObject::tr("Rate"), cur->value, [change](double v) { change([v](RateMaster &m) { m.value = v; }); }, 0.0, 1600.0);
        break;
    case RateMode::BPM:
        ed->addValueField(QObject::tr("Tempo"), cur->value, [change](double v) { change([v](RateMaster &m) { m.value = v; }); }, 0.0,
                          999.0, QStringLiteral(" BPM"), 1.0);
        break;
    case RateMode::Ratio:
        ed->addValueField(QObject::tr("Ratio"), cur->value, [change](double v) { change([v](RateMaster &m) { m.setTyped(v); }); },
                          0.0625, 16.0, QStringLiteral(" ×"), 1.0);
        break;
    }
    ed->addAction(QObject::tr("Reset to %1").arg(cur->mode == RateMode::BPM ? QObject::tr("120 BPM")
                                                  : cur->mode == RateMode::Ratio ? QObject::tr("×1") : QObject::tr("100%")),
                  [show, id]() { show->resetRateMaster(id); });
    ed->addAction(cur->frozen ? QObject::tr("Unfreeze") : QObject::tr("Freeze"), [show, id]() { show->toggleRateMasterFreeze(id); });
    {
        // MIDI: the fader and the Learn key, like an executor's fader and button.
        MidiManager *midi = ctx->midi;
        MidiTarget fader, key;
        fader.kind = MidiTarget::RateMasterFader;
        key.kind = MidiTarget::RateMasterKey;
        fader.master = key.master = id;
        auto learn = [ctx, midi](const MidiTarget &t) {
            midi->arm(t);
            emit ctx->status(QObject::tr("Move a control on your MIDI device to map %1").arg(t.describe()));
        };
        ed->addSection(QObject::tr("MIDI"));
        ed->addAction(QObject::tr("MIDI learn: fader"), [learn, fader]() { learn(fader); });
        ed->addAction(QObject::tr("MIDI learn: button"), [learn, key]() { learn(key); });
        if (!midi->bindingsFor(fader).isEmpty() || !midi->bindingsFor(key).isEmpty())
            ed->addAction(QObject::tr("Forget MIDI mappings"), [midi, fader, key]() {
                midi->removeBindingsFor(fader);
                midi->removeBindingsFor(key);
            });
        ed->addSection(QString());
    }
    ed->addAction(QObject::tr("Appearance…"), [show, id, parent]() {
        const RateMaster *m = show->rateMaster(id);
        if (!m)
            return;
        if (const auto c = pickAppearanceColor(parent, QObject::tr("Appearance · %1").arg(m->displayName()), m->color))
            show->setAppearance(QStringLiteral("ratemaster"), id, *c);
    });
    ed->popup(pos);
}
constexpr int kRmPad = 6;
constexpr int kRmHeaderH = 50;
constexpr int kRmKeyH = 26;
} // namespace

RateMasterStrip::RateMasterStrip(AppContext *ctx, int id, QWidget *parent) : QWidget(parent), m_ctx(ctx), m_id(id)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumWidth(60);
}

QRect RateMasterStrip::keyRect() const
{
    const RateMaster *m = m_ctx->show->rateMaster(m_id);
    const int top = kRmPad + kRmHeaderH + (m && m->button ? 4 : 8);
    if (m && m->button)   // one big key under the header
        return {kRmPad, top, width() - 2 * kRmPad, std::max(kRmKeyH, height() - top - kRmPad)};
    return {kRmPad, top, width() - 2 * kRmPad, kRmKeyH};
}

QRect RateMasterStrip::trackRect() const
{
    const int top = keyRect().bottom() + 10;
    const QRect f(kRmPad, top, width() - 2 * kRmPad, height() - top - kRmPad);
    return {f.left() + 10, f.top() + 4, f.width() - 20, f.height() - 26};
}

void RateMasterStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const RateMaster *m = m_ctx->show->rateMaster(m_id);
    if (!m)
        return;
    int following = 0;
    for (const Effect &e : m_ctx->show->effects())
        following += e.rateMaster == m_id;
    const bool live = following > 0 && !m->frozen;

    const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(live ? theme::mix(theme::line, theme::live, 0.35) : theme::line, 1));
    p.setBrush(theme::panel);
    p.drawRoundedRect(card, 8, 8);
    if (!m->color.isEmpty()) {
        // Its appearance: a stripe along the top, as on an executor.
        QPainterPath top;
        top.addRoundedRect(card.adjusted(1, 1, -1, 0), 7, 7);
        QPainterPath clip;
        clip.addRect(QRectF(card.left(), card.top(), card.width(), 4));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(m->color));
        p.drawPath(top.intersected(clip));
    }

    const QRect h(kRmPad, kRmPad, width() - 2 * kRmPad, kRmHeaderH);
    p.setFont(theme::uiFont(8));
    p.setPen(theme::muted);
    p.drawText(h.adjusted(2, 0, -2, 0), Qt::AlignLeft | Qt::AlignTop, QString::number(m_id));
    p.drawText(h.adjusted(2, 0, -2, 0), Qt::AlignRight | Qt::AlignTop, rateModeName(m->mode));
    p.setFont(theme::uiFont(10, QFont::DemiBold));
    p.setPen(theme::text);
    const QRect title(h.left() + 2, h.top() + 14, h.width() - 4, 18);
    p.drawText(title, Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(m->displayName(), Qt::ElideRight, title.width()));
    const QRect line(h.left() + 2, h.top() + 33, h.width() - 4, 16);
    p.setFont(theme::uiFont(9));
    p.setPen(m->frozen ? theme::muted : live ? theme::live : theme::text);
    const QString sub = following ? tr("%1 · %n fx", nullptr, following).arg(m->valueText()) : m->valueText();
    p.drawText(line, Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(sub, Qt::ElideRight, line.width()));

    // The key: Learn (tap it in time) on a BPM master, Reset on the others, unless set otherwise.
    const QRect k = keyRect();
    p.setPen(QPen(m_keyDown ? theme::live : theme::line, 1));
    p.setBrush(m_keyDown ? QColor(0x3a, 0x32, 0x20) : theme::raised);
    p.drawRoundedRect(QRectF(k).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    p.setFont(theme::uiFont(m->button && k.height() > 44 ? 11 : 9, QFont::Medium));
    p.setPen(m_keyDown ? theme::live : theme::text);
    drawFittedLines(p, k.adjusted(4, 0, -4, 0),
                    m->button && k.height() > 60 ? QStringList{m->keyLearns() ? tr("Learn") : tr("Reset"), m->valueText()}
                                                 : QStringList{m->keyLearns() ? tr("Learn") : tr("Reset")});
    // Learn MIDI: what can be mapped is outlined; what's waiting for a control pulses. A dot marks
    // what's mapped already.
    MidiManager *midi = m_ctx->midi;
    auto outline = [&](const QRect &r, bool key) {
        const bool armed = midi->isArmed() && midi->armedTarget() == target(key);
        if (!midi->bindingsFor(target(key)).isEmpty()) {
            p.setPen(Qt::NoPen);
            p.setBrush(theme::learn);
            p.drawEllipse(QPointF(r.right() - 5, r.top() + 5), 2.5, 2.5);
        }
        if (!midi->learnMode() && !armed)
            return;
        QColor c = theme::learn;
        c.setAlphaF(armed ? float(0.55 + 0.45 * std::sin(QDateTime::currentMSecsSinceEpoch() / 160.0)) : 0.45f);
        p.setPen(QPen(c, armed ? 2 : 1, armed ? Qt::SolidLine : Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(r).adjusted(-1.5, -1.5, 1.5, 1.5), 5, 5);
    };
    outline(k, true);
    if (m->button)
        return;
    outline(trackRect().adjusted(-8, -2, 8, 2), false);

    const QRect track = trackRect();
    const double level = m->faderPosition();
    p.setPen(QPen(theme::line, 1));
    p.setBrush(QColor(0x10, 0x13, 0x18));
    p.drawRoundedRect(QRectF(track).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    const double fillH = (track.height() - 4) * level;
    const QRectF fill(track.left() + 2, track.bottom() - 2 - fillH, track.width() - 4, fillH);
    p.setPen(Qt::NoPen);
    p.setBrush(m->color.isEmpty() ? QColor(0x2c, 0x33, 0x3e) : theme::mix(QColor(0x2c, 0x33, 0x3e), QColor(m->color), 0.28));
    p.drawRoundedRect(fill, 4, 4);
    if (live) {
        QLinearGradient g(fill.bottomLeft(), fill.topLeft());
        QColor lo = theme::live, hi = theme::live;
        lo.setAlphaF(0.55f);
        hi.setAlphaF(0.2f);
        g.setColorAt(0, lo);
        g.setColorAt(1, hi);
        p.setBrush(g);
        p.drawRoundedRect(fill, 4, 4);
    }
    // The middle (100%, ×1) marked on the track.
    p.setPen(QPen(theme::muted, 1, Qt::DotLine));
    if (m->mode != RateMode::BPM) {
        const double my = track.bottom() - 2 - (track.height() - 4) * 0.5;
        p.drawLine(QPointF(track.left() + 3, my), QPointF(track.right() - 3, my));
    }
    const double hy = track.bottom() - 2 - fillH;
    p.setPen(Qt::NoPen);
    p.setBrush(m->frozen ? theme::muted : theme::text);
    p.drawRoundedRect(QRectF(track.left() - 3, hy - 3, track.width() + 6, 6), 3, 3);
    p.setFont(theme::uiFont(9));
    p.setPen(live ? theme::live : theme::muted);
    p.drawText(QRect(kRmPad, track.bottom() + 4, width() - 2 * kRmPad, 18), Qt::AlignCenter,
               p.fontMetrics().elidedText(m->frozen ? tr("Frozen") : m->valueText(), Qt::ElideRight, width() - 2 * kRmPad));
}

MidiTarget RateMasterStrip::target(bool key) const
{
    MidiTarget t;
    t.kind = key ? MidiTarget::RateMasterKey : MidiTarget::RateMasterFader;
    t.master = m_id;
    return t;
}

void RateMasterStrip::setFromY(int y)
{
    const QRect tr = trackRect();
    const double v = std::clamp(1.0 - double(y - tr.top() - 2) / double(std::max(1, tr.height() - 4)), 0.0, 1.0);
    RateMaster m = *m_ctx->show->rateMaster(m_id);
    m.setFromFader(v);
    m_ctx->show->setRateMaster(m);
}

void RateMasterStrip::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() != Qt::LeftButton)
        return;
    const RateMaster *m = m_ctx->show->rateMaster(m_id);
    // Learn MIDI: the key or the fader waits for a control.
    if (m_ctx->midi->learnMode() && m) {
        const bool key = m->button || ev->pos().y() <= keyRect().bottom();
        m_ctx->midi->arm(target(key));
        emit m_ctx->status(tr("Move a control on your MIDI device to map %1").arg(target(key).describe()));
        update();
        return;
    }
    // Assign / Label / … waiting for a target take the tap.
    if (m_ctx->tapTarget(QStringLiteral("RateMaster %1").arg(m_id), m ? m->name : QString()))
        return;
    if (keyRect().contains(ev->pos())) {
        m_keyDown = true;
        m_ctx->show->pressRateMasterKey(m_id);
        update();
        return;
    }
    if (!m->button && ev->pos().y() > keyRect().bottom()) {
        m_dragging = true;
        setFromY(ev->pos().y());
    }
}

void RateMasterStrip::mouseMoveEvent(QMouseEvent *ev)
{
    if (m_dragging)
        setFromY(ev->pos().y());
}

void RateMasterStrip::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
    m_keyDown = false;
    update();
}

void RateMasterStrip::contextMenuEvent(QContextMenuEvent *ev)
{
    rateMasterMenu(m_ctx, this, m_id, ev->globalPos());
}

RateMastersPanel::RateMastersPanel(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    setWindowTitle(tr("Masters"));
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(4, 4, 4, 4);
    h->setSpacing(4);
    for (int id = 1; id <= RateMaster::kCount; ++id) {   // always ten across, one row
        auto *strip = new RateMasterStrip(ctx, id, this);
        m_strips << strip;
        h->addWidget(strip, 1);
    }
    connect(ctx->show, &Show::changed, this, [this](Show::Parts p) {
        if (p & (Show::EffectsPart | Show::ExecutorsPart))
            for (RateMasterStrip *s : m_strips)
                s->update();
    });
    auto repaint = [this]() {
        for (RateMasterStrip *s : m_strips)
            s->update();
    };
    connect(ctx->midi, &MidiManager::learnStateChanged, this, repaint);
    connect(ctx->midi, &MidiManager::bindingsChanged, this, repaint);
    // The armed control pulses.
    auto *pulse = new QTimer(this);
    connect(pulse, &QTimer::timeout, this, [this, repaint]() {
        if (isVisible() && m_ctx->midi->isArmed())
            repaint();
    });
    pulse->start(80);
}

// ===========================================================================
// Views
// ===========================================================================

ViewsPanel::ViewsPanel(AppContext *ctx, QWidget *parent) : PoolPanel(tr("Views"), parent), m_ctx(ctx)
{
    Show *show = ctx->show;
    connect(tiles(), &PoolTiles::tileClicked, this, [this, show](int id, bool occupied, Qt::KeyboardModifiers) {
        // Commands go to the screen in front: the one this pool was tapped on.
        if (m_ctx->storeArmed()) {
            m_ctx->setStoreArmed(false);
            m_ctx->run(occupied ? QStringLiteral("Store View %1").arg(id)
                                : QStringLiteral("Store View %1 \"%2\"").arg(id).arg(tr("View %1").arg(id)));
            return;
        }
        if (m_ctx->tapTarget(QStringLiteral("View %1").arg(id), show->view(id) ? show->view(id)->name : QString()))
            return;
        if (occupied)
            m_ctx->run(QStringLiteral("View %1").arg(id));
        else
            emit m_ctx->status(tr("Empty view slot: press Store, then tap it to keep the windows on screen here"));
    });
    connect(tiles(), &PoolTiles::tileMenu, this, [this, show](int id, bool occupied, const QPoint &pos) {
        auto *ed = new QuickEditor(tr("View %1").arg(id), this);
        if (occupied) {
            ed->addNameField(tr("Name"), show->view(id)->name, [this, id](const QString &n) {
                QString q = n.trimmed();
                q.replace('"', '\'');
                if (!q.isEmpty())
                    m_ctx->run(QStringLiteral("Label View %1 \"%2\"").arg(id).arg(q));
            });
            ed->addSection(QString());
        }
        ed->addAction(occupied ? tr("Store the windows on screen here") : tr("Store the windows on screen here (new view)"),
                      [this, id, occupied]() {
                          m_ctx->run(occupied ? QStringLiteral("Store View %1").arg(id)
                                              : QStringLiteral("Store View %1 \"%2\"").arg(id).arg(tr("View %1").arg(id)));
                      });
        if (occupied)
            ed->addAction(tr("Delete view"), [this, id]() { m_ctx->run(QStringLiteral("Delete View %1").arg(id)); }, QIcon(), true);
        ed->popup(pos);
    });
    onShowChanged(this, show, [this](Show::Parts p) {
        if (p & Show::ViewsPart)
            refresh();
    });
    connect(ctx, &AppContext::uiValueChanged, this, [this](const QString &key) {
        if (key.startsWith(QLatin1String("view")))
            refresh();
    });
    followStore(ctx);
    refresh();
}

int ViewsPanel::screen() const
{
    const QVariant s = window() ? window()->property("lumoraScreen") : QVariant();
    return s.isValid() ? s.toInt() : 1;
}

void ViewsPanel::refresh()
{
    const int current = m_ctx->uiValue(QStringLiteral("view%1").arg(screen())).toInt();
    QVector<PoolItem> items;
    for (const StoredView &v : m_ctx->show->views()) {
        PoolItem it;
        it.id = v.id;
        it.label = v.name;
        it.tag = theme::muted;
        it.active = v.id == current;   // shown on this screen
        it.tooltip = tr("View %1 \"%2\": tap to show it here").arg(v.id).arg(v.name);
        items << it;
    }
    tiles()->setItems(items);
}

void ViewsPanel::showEvent(QShowEvent *ev)
{
    PoolPanel::showEvent(ev);
    refresh();   // which screen it's on is known once it's shown
}

// ===========================================================================
// Presets
// ===========================================================================

PresetsPanel::PresetsPanel(AppContext *ctx, QWidget *parent) : PoolPanel(tr("Presets"), parent), m_ctx(ctx)
{
    m_type = 4;   // Color until the view says otherwise
    for (int type = 1; type <= kPresetTypeAll; ++type) {
        auto *b = new FittingToolButton(presetTypeName(type), this);
        static const char *shortNames[] = {"", "Dim", "Pos", "Gobo", "Col", "Beam", "Foc", "Ctl", "All"};
        if (type < int(std::size(shortNames)))
            b->setShortText(tr(shortNames[type]));
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setChecked(type == m_type);
        const QColor c = theme::presetTypeColor(type);
        b->setStyleSheet(QStringLiteral(
            "QToolButton { background: transparent; border: none; border-bottom: 2px solid transparent; border-radius: 0;"
            " padding: 3px 5px; color: %1; }"
            "QToolButton:hover { color: %2; }"
            "QToolButton:checked { color: %3; border-bottom-color: %3; background: transparent; }")
                             .arg(theme::muted.name(), theme::text.name(), c.name()));
        connect(b, &QToolButton::clicked, this, [this, type]() {
            setType(type);
            panelStateChanged(this);
        });
        headerLayout()->addWidget(b);
        m_tabs << b;
    }
    Show *show = ctx->show;

    connect(tiles(), &PoolTiles::tileClicked, this, [this, show](int id, bool occupied, Qt::KeyboardModifiers) {
        if (m_ctx->storeArmed()) {
            m_ctx->setStoreArmed(false);
            m_ctx->run(QStringLiteral("Store Preset %1.%2").arg(m_type).arg(id));   // asks when it exists
            return;
        }
        if (m_ctx->tapTarget(presetRef(m_type, id),
                             show->preset(m_type, id) ? show->preset(m_type, id)->name : QString()))
            return;
        if (occupied && show->applyPreset(m_type, id))
            emit m_ctx->status(tr("%1 applied").arg(show->preset(m_type, id)->name));
    });
    connect(tiles(), &PoolTiles::tileMenu, this, [this, show](int id, bool occupied, const QPoint &pos) {
        if (!occupied)
            return;
        const int type = m_type;
        auto *ed = new QuickEditor(tr("Preset %1").arg(presetRef(type, id)), this);
        ed->addNameField(tr("Name"), show->preset(type, id)->name, [show, type, id](const QString &n) {
            if (const Preset *p0 = show->preset(type, id)) {
                Preset p = *p0;
                p.name = n;
                show->setPreset(p);
            }
        });
        // Layers: Selective overrides Global overrides Universal.
        ed->addSection(tr("Update with the programmer"));
        const Preset *cur = show->preset(type, id);
        for (PresetScope layer : {PresetScope::Universal, PresetScope::Type, PresetScope::Fixture}) {
            const QString tip = layer == PresetScope::Universal ? tr("any fixture")
                                : layer == PresetScope::Type    ? tr("per fixture type")
                                                                : tr("per fixture");
            ed->addAction(QStringLiteral("%1%2  (%3)").arg(cur->hasLayer(layer) ? QStringLiteral("●  ") : QStringLiteral("○  "),
                                                          presetScopeName(layer), tip),
                          [show, type, id, layer]() { show->storePreset(type, id, Show::StoreMode::Merge, QString(), layer); });
        }
        ed->addSection(QString());
        ed->addAction(tr("Delete"), [show, type, id]() { show->deletePreset(type, id); }, QIcon(), true);
        ed->popup(pos);
    });
    onShowChanged(this, show, [this](Show::Parts p) {
        if (p & (Show::PresetsPart | Show::ProgrammerPart | Show::PatchPart))
            refresh();
    });
    followStore(ctx);
    setType(m_type);
}

void PresetsPanel::setType(int type)
{
    m_type = std::clamp(type, 1, kPresetTypeAll);
    for (int i = 0; i < m_tabs.size(); ++i)
        m_tabs[i]->setChecked(i + 1 == m_type);
    // The tab says which pool: "Dimmer presets", "All presets".
    setWindowTitle(tr("%1 presets").arg(presetTypeName(m_type)));
    refresh();
}

QJsonObject PresetsPanel::saveOptions() const
{
    QJsonObject o = PoolPanel::saveOptions();
    o["type"] = m_type;
    return o;
}

void PresetsPanel::loadOptions(const QJsonObject &o)
{
    PoolPanel::loadOptions(o);
    if (o.contains("type"))
        setType(o["type"].toInt(4));
}

void PresetsPanel::refresh()
{
    Show *show = m_ctx->show;
    QSet<int> inUse;
    for (const AttrValues &a : show->programmerValues())
        for (const ValueRef &r : a)
            if (r.presetType == m_type)
                inUse.insert(r.presetId);
    QVector<PoolItem> items;
    for (const Preset &p : show->presets(m_type)) {
        PoolItem it;
        it.id = p.id;
        it.label = p.name;
        it.tag = theme::presetTypeColor(p.type);
        it.swatch = presetSwatch(p);
        it.image = presetImage(show, p);
        it.badge = p.layerBadge();
        QStringList layers;
        if (p.hasLayer(PresetScope::Universal))
            layers << tr("Universal: any fixture with these attributes");
        if (p.hasLayer(PresetScope::Type))
            layers << tr("Global: values per fixture type (%1)").arg(p.perType.size());
        if (p.hasLayer(PresetScope::Fixture))
            layers << tr("Selective: values per fixture (%1)").arg(p.perFixture.size());
        it.tooltip = layers.join('\n');
        it.active = inUse.contains(p.id);
        items << it;
    }
    tiles()->setItems(items);
}

// ===========================================================================
// Registry
// ===========================================================================

const QList<PanelType> &panelTypes()
{
    // Listed alphabetically wherever windows are offered.
    static const QList<PanelType> types = [] {
    QList<PanelType> list = {
        {"groups", QObject::tr("Groups"), [](AppContext *c, QWidget *p) -> QWidget * { return new GroupsPanel(c, p); }},
        {"views", QObject::tr("Views"), [](AppContext *c, QWidget *p) -> QWidget * { return new ViewsPanel(c, p); }},
        {"macros", QObject::tr("Macros"), [](AppContext *c, QWidget *p) -> QWidget * { return new MacrosPanel(c, p); }},
        {"plots", QObject::tr("Plots"), [](AppContext *c, QWidget *p) -> QWidget * { return new PlotsPanel(c, p); }},
        {"plot", QObject::tr("Plot"), [](AppContext *c, QWidget *p) -> QWidget * { return new PlotView(c, p); }},
        {"spreads", QObject::tr("Spreads"), [](AppContext *c, QWidget *p) -> QWidget * { return new SpreadsPanel(c, p); }},
        {"presets", QObject::tr("Presets"), [](AppContext *c, QWidget *p) -> QWidget * { return new PresetsPanel(c, p); }},
        {"fixtures", QObject::tr("Fixtures"), [](AppContext *c, QWidget *p) -> QWidget * { return new FixtureSheet(c, p); }},
        {"color", QObject::tr("Color picker"), [](AppContext *c, QWidget *p) -> QWidget * { return new ColorPickerPanel(c, p); }},
        {"cues", QObject::tr("Sequence editor"), [](AppContext *c, QWidget *p) -> QWidget * { return new CueView(c, p); }},
        {"effects", QObject::tr("Effects"), [](AppContext *c, QWidget *p) -> QWidget * { return new EffectsPanel(c, p); }},
        {"effecteditor", QObject::tr("Effect editor"), [](AppContext *c, QWidget *p) -> QWidget * { return new EffectEditor(c, p); }},
        {"executors", QObject::tr("Executors"), [](AppContext *c, QWidget *p) -> QWidget * { return new ExecutorBar(c, p); }},
        {"dmx", QObject::tr("DMX monitor"), [](AppContext *c, QWidget *p) -> QWidget * { return new DmxPanel(c, p); }},
        {"patch", QObject::tr("Patch"), [](AppContext *c, QWidget *p) -> QWidget * { return new PatchView(c, p); }},
        {"outputs", QObject::tr("DMX outputs"), [](AppContext *c, QWidget *p) -> QWidget * { return new OutputView(c, p); }},
        {"encoders", QObject::tr("Encoders"), [](AppContext *c, QWidget *p) -> QWidget * { return new EncoderBar(c, p, false); }},
        {"selectionmatrix", QObject::tr("Selection matrix"), [](AppContext *c, QWidget *p) -> QWidget * { return new SelectionMatrixPanel(c, p); }},
        {"spreadmodifier", QObject::tr("Spreads modifier"), [](AppContext *c, QWidget *p) -> QWidget * { return new SpreadModifierPanel(c, p); }},
        {"help", QObject::tr("Help"), [](AppContext *c, QWidget *p) -> QWidget * { return new HelpView(c, p); }},
        {"midi", QObject::tr("Control (MIDI and OSC)"), [](AppContext *c, QWidget *p) -> QWidget * { return new ControlView(c, p); }},
        {"playbackmonitor", QObject::tr("Playback monitor"), [](AppContext *c, QWidget *p) -> QWidget * { return new PlaybackMonitor(c, p); }},
        {"history", QObject::tr("Command history"), [](AppContext *c, QWidget *p) -> QWidget * { return new HistoryView(c, p); }},
        {"keypad", QObject::tr("Command keys"), [](AppContext *c, QWidget *p) -> QWidget * { return new CommandKeypad(c, true, p); }},
        {"sequences", QObject::tr("Sequences"), [](AppContext *c, QWidget *p) -> QWidget * { return new SequencePool(c, p); }},
        {"executorgrid", QObject::tr("Executor grid"), [](AppContext *c, QWidget *p) -> QWidget * {
             auto *grid = new ExecutorBar(c, p);
             grid->setGridDefaults(101, 0, 0);   // from 101, as many as fit
             return grid;
         }},
        {"images", QObject::tr("Images"), [](AppContext *c, QWidget *p) -> QWidget * { return new ImagesPanel(c, p); }},
        {"executorpages", QObject::tr("Executor pages"), [](AppContext *c, QWidget *p) -> QWidget * { return new ExecutorPagesPanel(c, p); }},
        {"park", QObject::tr("Park"), [](AppContext *c, QWidget *p) -> QWidget * { return new ParkView(c, p); }},
        {"ratemasters", QObject::tr("Masters"), [](AppContext *c, QWidget *p) -> QWidget * { return new RateMastersPanel(c, p); }},
    };
    std::sort(list.begin(), list.end(), [](const PanelType &a, const PanelType &b) {
        return QString::localeAwareCompare(a.title, b.title) < 0;
    });
    return list;
    }();
    return types;
}

// ===========================================================================
// Frames
// ===========================================================================

namespace {
constexpr int kGap = 2;        // pixels between windows
constexpr int kGrip = 8;       // edge band that resizes
constexpr int kCorner = 16;    // corner zone that resizes both ways
constexpr int kHeader = 26;
} // namespace

class PanelFrame : public QFrame {
    enum class Op { None, Move, Resize };

public:
    PanelFrame(Workspace *ws, const QRect &cells) : QFrame(ws), cells(cells), m_ws(ws)
    {
        setObjectName("panelFrame");
        setAttribute(Qt::WA_StyledBackground);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(1, 1, 1, 1);
        v->setSpacing(0);

        m_header = new QWidget(this);
        m_header->setObjectName("frameHeader");
        m_header->setAttribute(Qt::WA_StyledBackground);
        m_header->setFixedHeight(kHeader);
        m_header->setCursor(Qt::OpenHandCursor);
        m_header->installEventFilter(this);
        auto *h = new QHBoxLayout(m_header);
        h->setContentsMargins(4, 0, 4, 0);
        h->setSpacing(2);
        m_tabs = new QTabBar(m_header);
        m_tabs->setDocumentMode(true);
        m_tabs->setMovable(true);
        m_tabs->setTabsClosable(false);   // our own × buttons, see addTab
        m_tabs->setExpanding(false);
        m_tabs->setDrawBase(false);
        m_tabs->setElideMode(Qt::ElideRight);
        // Never scroll: a narrow window squeezes its tabs (eliding their titles) instead of
        // sliding them out of sight behind scroll arrows, where they looked gone.
        m_tabs->setUsesScrollButtons(false);
        m_tabs->setCursor(Qt::ArrowCursor);
        m_tabs->installEventFilter(this);
        m_tabs->setToolTip(QObject::tr("Drag a tab onto another window to stack it, or onto empty space to give it its own window"));
        h->addWidget(m_tabs, 0, Qt::AlignBottom);
        h->addStretch(1);
        m_add = new QToolButton(m_header);
        m_add->setText(QStringLiteral("+"));
        m_add->setObjectName("frameAdd");
        m_add->setCursor(Qt::ArrowCursor);
        m_add->setToolTip(QObject::tr("Add a tab to this window"));
        h->addWidget(m_add);
        v->addWidget(m_header);

        m_stack = new QStackedWidget(this);
        m_stack->setMinimumSize(0, 0);
        v->addWidget(m_stack, 1);
        createGrips();

        QObject::connect(m_tabs, &QTabBar::currentChanged, m_stack, [this](int i) {
            if (i >= 0 && i < m_stack->count())
                m_stack->setCurrentIndex(i);
            m_ws->frameEdited(this);
        });
        QObject::connect(m_tabs, &QTabBar::tabMoved, m_stack, [this](int from, int to) {
            QWidget *w = m_stack->widget(from);
            m_stack->removeWidget(w);
            m_stack->insertWidget(to, w);
            m_stack->setCurrentIndex(m_tabs->currentIndex());
            m_ws->frameEdited(this);
        });
        QObject::connect(m_tabs, &QTabBar::tabCloseRequested, m_stack, [this](int i) {
            QWidget *w = takeTab(i);
            if (w)
                w->deleteLater();
            if (count() == 0)
                m_ws->frameEmptied(this);
            else
                m_ws->frameEdited(this);
        });
        QObject::connect(m_add, &QToolButton::clicked, m_stack, [this]() {
            m_ws->showAddMenu(m_add->mapToGlobal(QPoint(0, m_add->height())), this);
        });
    }

    // A tab always says what it is: the panel's title, or its kind's name while it has none.
    static QString tabText(QWidget *panel, const QString &title)
    {
        if (!title.trimmed().isEmpty())
            return title;
        const QString type = panel->property("panelType").toString();
        for (const PanelType &t : panelTypes())
            if (t.id == type)
                return t.title;
        return QObject::tr("Window");
    }
    void addTab(QWidget *panel, const QString &title, bool makeCurrent = true)
    {
        m_stack->addWidget(panel);
        const int i = m_tabs->addTab(tabText(panel, title));
        if (panel->testAttribute(Qt::WA_SetWindowIcon))   // its own icon, not the app's it inherits
            m_tabs->setTabIcon(i, panel->windowIcon());
        auto *close = new QToolButton(m_tabs);
        close->setObjectName("tabClose");
        close->setText(QStringLiteral("\u00d7"));
        close->setCursor(Qt::ArrowCursor);
        close->setToolTip(QObject::tr("Close this window"));
        close->setFixedSize(16, 16);
        m_tabs->setTabButton(i, QTabBar::RightSide, close);
        QObject::connect(close, &QToolButton::clicked, m_tabs, [this, close]() {
            for (int t = 0; t < m_tabs->count(); ++t)
                if (m_tabs->tabButton(t, QTabBar::RightSide) == close)
                    emit m_tabs->tabCloseRequested(t);
        });
        if (makeCurrent)
            m_tabs->setCurrentIndex(i);
        m_stack->setCurrentIndex(m_tabs->currentIndex());
        QObject::connect(panel, &QWidget::windowIconChanged, m_tabs, [this, panel](const QIcon &icon) {
            const int at = m_stack->indexOf(panel);
            if (at >= 0)
                m_tabs->setTabIcon(at, panel->testAttribute(Qt::WA_SetWindowIcon) ? icon : QIcon());
        });
        QObject::connect(panel, &QWidget::windowTitleChanged, m_tabs, [this, panel](const QString &title) {
            const int at = m_stack->indexOf(panel);
            if (at >= 0)
                m_tabs->setTabText(at, tabText(panel, title));
        });
    }
    QWidget *takeTab(int i)
    {
        if (i < 0 || i >= m_stack->count())
            return nullptr;
        QWidget *w = m_stack->widget(i);
        m_stack->removeWidget(w);
        m_tabs->removeTab(i);
        if (m_tabs->currentIndex() >= 0)
            m_stack->setCurrentIndex(m_tabs->currentIndex());
        return w;
    }
    int count() const { return m_stack->count(); }
    QWidget *panel(int i) const { return m_stack->widget(i); }
    QString tabTitle(int i) const { return m_tabs->tabText(i); }
    int current() const { return m_tabs->currentIndex(); }
    void setCurrent(int i) { m_tabs->setCurrentIndex(i); }
    bool overHeader(const QPoint &global) const { return m_header->rect().contains(m_header->mapFromGlobal(global)); }
    void setDropTarget(bool on)
    {
        if (m_dropTarget == on)
            return;
        m_dropTarget = on;
        setProperty("drop", on);
        style()->unpolish(this);
        style()->polish(this);
    }

    QRect cells;

    // Resize handles: invisible strips on the edges and squares on the corners, above the content.
    // Edges: bit 1 left, 2 right, 4 top, 8 bottom.
    class Grip : public QWidget {
    public:
        Grip(PanelFrame *frame, int edges) : QWidget(frame), edges(edges), m_frame(frame)
        {
            setAttribute(Qt::WA_NoSystemBackground);
            if ((edges & 5) == 5 || (edges & 10) == 10)
                setCursor(Qt::SizeFDiagCursor);
            else if ((edges & 6) == 6 || (edges & 9) == 9)
                setCursor(Qt::SizeBDiagCursor);
            else if (edges & 3)
                setCursor(Qt::SizeHorCursor);
            else
                setCursor(Qt::SizeVerCursor);
        }
        const int edges;

    protected:
        void mousePressEvent(QMouseEvent *ev) override
        {
            if (ev->button() == Qt::LeftButton)
                m_frame->beginResize(edges);
        }
        void mouseMoveEvent(QMouseEvent *ev) override
        {
            if (ev->buttons() & Qt::LeftButton)
                m_frame->resizeTo(ev->globalPosition().toPoint());
        }
        void mouseReleaseEvent(QMouseEvent *) override { m_frame->endOp(); }
        void paintEvent(QPaintEvent *) override
        {
            if (edges != 10)
                return;
            // A small grip in the bottom-right corner says "drag me".
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(theme::faint, 1.2));
            const QPointF c(width() - 4.5, height() - 4.5);
            for (int i = 1; i <= 2; ++i)
                p.drawLine(c - QPointF(4 * i, 0), c - QPointF(0, 4 * i));
        }

    private:
        PanelFrame *m_frame = nullptr;
    };

    void createGrips()
    {
        for (int e : {1, 2, 4, 8, 5, 6, 9, 10})
            m_grips << new Grip(this, e), m_grips.last()->setObjectName(QStringLiteral("grip%1").arg(e));
    }
    void beginResize(int edges)
    {
        m_op = Op::Resize;
        m_edges = edges;
        m_startCells = cells;
        raise();
        m_ws->setGuides(true);
    }

protected:
    void resizeEvent(QResizeEvent *ev) override
    {
        QFrame::resizeEvent(ev);
        const int w = width(), h = height();
        for (Grip *g : m_grips) {
            QRect r;
            switch (g->edges) {
            case 1: r = QRect(0, kCorner, kGrip, h - 2 * kCorner); break;
            case 2: r = QRect(w - kGrip, kCorner, kGrip, h - 2 * kCorner); break;
            case 4: r = QRect(kCorner, 0, w - 2 * kCorner, 4); break;
            case 8: r = QRect(kCorner, h - kGrip, w - 2 * kCorner, kGrip); break;
            case 5: r = QRect(0, 0, kCorner, 8); break;             // top corners stay thin: the header is there
            case 6: r = QRect(w - kCorner, 0, kCorner, 8); break;
            case 9: r = QRect(0, h - kCorner, kCorner, kCorner); break;
            case 10: r = QRect(w - kCorner, h - kCorner, kCorner, kCorner); break;
            }
            g->setGeometry(r);
            g->raise();
        }
    }
    void paintEvent(QPaintEvent *ev) override
    {
        QFrame::paintEvent(ev);
    }

    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        if (obj == m_header) {
            auto *me = dynamic_cast<QMouseEvent *>(ev);
            switch (ev->type()) {
            case QEvent::MouseButtonPress:
                if (me->button() == Qt::LeftButton) {
                    m_op = Op::Move;
                    m_startCells = cells;
                    m_grabCell = m_ws->cellAt(m_ws->mapFromGlobal(me->globalPosition().toPoint())) - cells.topLeft();
                    m_header->setCursor(Qt::ClosedHandCursor);
                    raise();
                    m_ws->setGuides(true);
                    return true;
                }
                break;
            case QEvent::MouseMove:
                if (m_op == Op::Move) {
                    moveTo(me->globalPosition().toPoint());
                    return true;
                }
                break;
            case QEvent::MouseButtonRelease:
                if (m_op == Op::Move) {
                    m_header->setCursor(Qt::OpenHandCursor);
                    endOp();
                    return true;
                }
                break;
            case QEvent::MouseButtonDblClick:
                m_ws->showAddMenu(me->globalPosition().toPoint(), this);
                return true;
            default:
                break;
            }
        }
        if ((obj == m_tabs || obj == m_header) && ev->type() == QEvent::ContextMenu) {
            // Right-click a tab (or the header): that window's options.
            auto *ce = static_cast<QContextMenuEvent *>(ev);
            int i = obj == m_tabs ? m_tabs->tabAt(ce->pos()) : m_tabs->currentIndex();
            if (i < 0)
                i = m_tabs->currentIndex();
            showOptions(i, ce->globalPos());
            return true;
        }
        if (obj == m_tabs) {
            auto *me = dynamic_cast<QMouseEvent *>(ev);
            switch (ev->type()) {
            case QEvent::MouseButtonPress:
                if (me->button() == Qt::LeftButton) {
                    m_tabPress = me->position().toPoint();
                    m_tabIndex = m_tabs->tabAt(m_tabPress);
                    m_tabDragging = false;
                }
                break;
            case QEvent::MouseMove:
                if (m_tabIndex >= 0 && (me->buttons() & Qt::LeftButton)) {
                    const QPoint p = me->position().toPoint();
                    if (!m_tabDragging && (p.y() < -14 || p.y() > m_tabs->height() + 14
                                           || p.x() < -30 || p.x() > m_tabs->width() + 60)) {
                        m_tabDragging = true;
                        m_tabs->setCursor(Qt::DragMoveCursor);
                    }
                    if (m_tabDragging) {
                        m_ws->tabDragMove(me->globalPosition().toPoint());
                        return true;
                    }
                }
                break;
            case QEvent::MouseButtonRelease:
                if (m_tabDragging) {
                    m_tabDragging = false;
                    m_tabs->setCursor(Qt::ArrowCursor);
                    const int index = m_tabs->currentIndex();
                    const QPoint at = me->globalPosition().toPoint();
                    m_tabIndex = -1;
                    // Let the tab bar finish its own drag first, then move the tab.
                    QTimer::singleShot(0, m_ws, [ws = m_ws, self = QPointer<PanelFrame>(this), index, at]() {
                        if (self)
                            ws->tabDrop(self, index, at);
                        else
                            ws->tabDragCancel();
                    });
                }
                m_tabIndex = -1;
                break;
            default:
                break;
            }
        }
        return QFrame::eventFilter(obj, ev);
    }

public:
    void showOptionsAt(int index, const QPoint &global) { showOptions(index, global); }

private:
    void showOptions(int index, const QPoint &global)
    {
        QWidget *panel = this->panel(index);
        if (!panel)
            return;
        m_tabs->setCurrentIndex(index);
        auto *ed = new QuickEditor(QObject::tr("%1 window").arg(tabTitle(index)), this);
        if (PanelOptions *opts = Workspace::optionsOf(panel))
            opts->buildOptions(ed);
        ed->addSection(QString());
        ed->addAction(QObject::tr("Close window"), [this, panel]() {
            for (int t = 0; t < count(); ++t)
                if (this->panel(t) == panel)
                    emit m_tabs->tabCloseRequested(t);
        }, QIcon(), true);
        // Save the options with the layout once the editor closes.
        QObject::connect(ed, &QObject::destroyed, m_ws, [ws = m_ws, self = QPointer<PanelFrame>(this)]() {
            if (self)
                ws->frameEdited(self);
        });
        ed->popup(global);
    }
    void moveTo(const QPoint &global)
    {
        const QPoint cell = m_ws->cellAt(m_ws->mapFromGlobal(global));
        QRect r(cell - m_grabCell, cells.size());
        r.moveLeft(std::clamp(r.left(), 0, Workspace::kCols - r.width()));
        r.moveTop(std::clamp(r.top(), 0, m_ws->rows() - r.height()));
        tryCells(r);
    }
    void resizeTo(const QPoint &global)
    {
        const QPoint pos = m_ws->mapFromGlobal(global);
        // Snap to the nearest grid line rather than the cell under the pointer.
        const double cw = double(m_ws->width()) / Workspace::kCols, ch = double(m_ws->height()) / m_ws->rows();
        const int gx = std::clamp(int(std::lround(pos.x() / cw)), 0, Workspace::kCols);
        const int gy = std::clamp(int(std::lround(pos.y() / ch)), 0, m_ws->rows());
        int l = m_startCells.left(), t = m_startCells.top();
        int r = m_startCells.right() + 1, b = m_startCells.bottom() + 1;
        if (m_edges & 1) l = std::min(gx, r - 1);
        if (m_edges & 2) r = std::max(gx, l + 1);
        if (m_edges & 4) t = std::min(gy, b - 1);
        if (m_edges & 8) b = std::max(gy, t + 1);
        const QRect want(l, t, r - l, b - t);
        if (tryCells(want))
            return;
        // Blocked: grow as far as possible toward the pointer, one axis at a time.
        QRect partial = cells;
        if (m_edges & 3) {
            QRect h(want.left(), cells.top(), want.width(), cells.height());
            if (m_ws->isFree(h, this)) partial = h;
        }
        if (m_edges & 12) {
            QRect v(partial.left(), want.top(), partial.width(), want.height());
            if (m_ws->isFree(v, this)) partial = v;
        }
        tryCells(partial);
    }
    bool tryCells(const QRect &r)
    {
        if (r == cells)
            return true;
        if (!m_ws->isFree(r, this))
            return false;
        cells = r;
        setGeometry(m_ws->pixelRect(cells));
        return true;
    }
    void endOp()
    {
        if (m_op == Op::None)
            return;
        m_op = Op::None;
        m_ws->setGuides(false);
        if (cells != m_startCells)
            m_ws->frameEdited(this);
    }

    Workspace *m_ws = nullptr;
    QList<Grip *> m_grips;
    QWidget *m_header = nullptr;
    QTabBar *m_tabs = nullptr;
    QToolButton *m_add = nullptr;
    QStackedWidget *m_stack = nullptr;
    Op m_op = Op::None;
    int m_edges = 0;
    QRect m_startCells;
    QPoint m_grabCell;
    QPoint m_tabPress;
    int m_tabIndex = -1;
    bool m_tabDragging = false;
    bool m_dropTarget = false;
};

// ===========================================================================
// Workspace
// ===========================================================================

Workspace::Workspace(AppContext *ctx, QWidget *parent)
    : QWidget(parent), m_ctx(ctx)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    m_empty = new QLabel(tr("This view is empty.\nDouble-click anywhere, or use + in the top bar, to put a window here."), this);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setProperty("role", "muted");
    m_empty->setWordWrap(true);
    m_empty->setAttribute(Qt::WA_TransparentForMouseEvents);
    setStyleSheet(QStringLiteral(
        "QFrame#panelFrame { background: %5; border: 1px solid %2; border-radius: 8px; }"
        "QFrame#panelFrame[drop=\"true\"] { border: 2px solid %7; }"
        "QWidget#frameHeader { background: %4; border-top-left-radius: 8px; border-top-right-radius: 8px;"
        " border-bottom: 1px solid %2; }"
        "QWidget#frameHeader QTabBar::tab { background: transparent; color: %6; padding: 5px 10px 5px 12px; border: none;"
        " border-top-left-radius: 6px; border-top-right-radius: 6px; margin-right: 2px; min-width: 40px; }"
        "QWidget#frameHeader QTabBar::tab:selected { background: %5; color: %3; }"
        "QWidget#frameHeader QTabBar::tab:hover:!selected { color: %3; }"
        "QToolButton#frameAdd { background: transparent; border: none; color: %6; font-size: 16px; padding: 0 6px; }"
        "QToolButton#frameAdd:hover { color: %3; }"
        "QToolButton#tabClose { background: transparent; border: none; color: %6; font-size: 14px; padding: 0; }"
        "QToolButton#tabClose:hover { background: %1; color: %3; border-radius: 3px; }")
                      .arg(theme::bg.name(), theme::line.name(), theme::text.name(), theme::raised.name(),
                           theme::panel.name(), theme::muted.name(), theme::live.name()));
    updateEmptyState();
}

QRect Workspace::pixelRect(const QRect &c) const
{
    const double cw = double(width()) / kCols, ch = double(height()) / m_rows;
    const int l = int(std::lround(c.left() * cw)), t = int(std::lround(c.top() * ch));
    const int r = int(std::lround((c.right() + 1) * cw)), b = int(std::lround((c.bottom() + 1) * ch));
    return QRect(QPoint(l + kGap / 2, t + kGap / 2), QPoint(r - kGap / 2 - 1, b - kGap / 2 - 1));
}

QPoint Workspace::cellAt(const QPoint &pos) const
{
    const double cw = double(std::max(1, width())) / kCols, ch = double(std::max(1, height())) / m_rows;
    return {std::clamp(int(pos.x() / cw), 0, kCols - 1), std::clamp(int(pos.y() / ch), 0, m_rows - 1)};
}

bool Workspace::isFree(const QRect &c, const PanelFrame *ignore) const
{
    if (c.left() < 0 || c.top() < 0 || c.right() >= kCols || c.bottom() >= m_rows || c.isEmpty())
        return false;
    for (const PanelFrame *f : m_frames)
        if (f != ignore && f->cells.intersects(c))
            return false;
    return true;
}

QRect Workspace::growFrom(const QPoint &cell, int maxW, int maxH, const PanelFrame *ignore) const
{
    QRect r(cell, QSize(1, 1));
    if (!isFree(r, ignore))
        return {};
    bool grew = true;
    while (grew) {
        grew = false;
        const QRect tries[] = {r.adjusted(0, 0, 1, 0), r.adjusted(0, 0, 0, 1), r.adjusted(-1, 0, 0, 0), r.adjusted(0, -1, 0, 0)};
        for (const QRect &t : tries)
            if (t.width() <= maxW && t.height() <= maxH && isFree(t, ignore)) {
                r = t;
                grew = true;
            }
    }
    return r;
}

QRect Workspace::largestFree() const
{
    QRect best;
    for (int y = 0; y < m_rows; ++y)
        for (int x = 0; x < kCols; ++x) {
            const QRect r = growFrom({x, y}, kCols, m_rows);
            if (r.width() * r.height() > best.width() * best.height())
                best = r;
        }
    return best;
}

PanelFrame *Workspace::frameAt(const QPoint &pos) const
{
    for (PanelFrame *f : m_frames)
        if (f->geometry().contains(pos))
            return f;
    return nullptr;
}

PanelFrame *Workspace::createFrame(const QRect &cells)
{
    auto *f = new PanelFrame(this, cells);
    m_frames << f;
    f->setGeometry(pixelRect(cells));
    f->show();
    return f;
}

void Workspace::debugOptions(const QString &typeId, const QJsonObject &options)
{
    for (PanelFrame *f : m_frames)
        for (int i = 0; i < f->count(); ++i)
            if (f->panel(i)->property("panelType").toString() == typeId) {
                if (PanelOptions *o = optionsOf(f->panel(i)); o && !options.isEmpty())
                    o->loadOptions(options);
                return;
            }
}

void panelStateChanged(QWidget *panel)
{
    for (QWidget *w = panel; w; w = w->parentWidget())
        if (auto *ws = qobject_cast<Workspace *>(w)) {
            ws->contentChanged();
            return;
        }
}

PanelOptions *Workspace::optionsOf(QWidget *panel)
{
    if (!panel)
        return nullptr;
    QObject *content = panel->property("panelContent").value<QObject *>();
    return dynamic_cast<PanelOptions *>(content);
}

QWidget *Workspace::createPanel(const QString &requestedId, const QJsonObject &options)
{
    // Shows saved before a.7.2.1 keep the Playback monitor under its old id.
    const QString typeId = requestedId == QLatin1String("psd") ? QStringLiteral("playbackmonitor") : requestedId;
    for (const PanelType &t : panelTypes())
        if (t.id == typeId) {
            QWidget *content = t.make(m_ctx, nullptr);
            // The tab already names the window: drop a panel's own matching title.
            for (QLabel *l : content->findChildren<QLabel *>())
                if (l->property("role").toString() == QLatin1String("title")
                    && l->text().compare(t.title, Qt::CaseInsensitive) == 0)
                    l->hide();
            if (auto *opts = dynamic_cast<PanelOptions *>(content); opts && !options.isEmpty())
                opts->loadOptions(options);
            QWidget *w = padded(content, nullptr);
            w->setProperty("panelType", typeId);
            // A window can name its tab after what it shows ("Dimmer presets", "Faders 201–210").
            w->setWindowTitle(content->windowTitle().isEmpty() ? t.title : content->windowTitle());
            const QString fallback = t.title;
            QObject::connect(content, &QWidget::windowTitleChanged, w, [w, fallback](const QString &title) {
                w->setWindowTitle(title.isEmpty() ? fallback : title);
            });
            // A mark on the tab (a locked executor page): the window's own icon, when it sets one.
            auto ownIcon = [content]() { return content->testAttribute(Qt::WA_SetWindowIcon) ? content->windowIcon() : QIcon(); };
            w->setWindowIcon(ownIcon());
            QObject::connect(content, &QWidget::windowIconChanged, w, [w, ownIcon]() { w->setWindowIcon(ownIcon()); });
            return w;
        }
    return nullptr;
}

QWidget *Workspace::addPanel(const QString &typeId, const QRect &want)
{
    QWidget *panel = createPanel(typeId);
    if (!panel)
        return nullptr;
    QRect cells = want;
    if (cells.isValid() && !isFree(cells)) {
        // Asked for a spot that's taken: use the free space around its top-left instead.
        cells = growFrom(cells.topLeft(), cells.width(), cells.height());
    }
    if (!cells.isValid()) {
        const QRect free = largestFree();
        if (free.width() >= 4 && free.height() >= 3)
            cells = free;
    }
    if (cells.isValid()) {
        createFrame(cells)->addTab(panel, panel->windowTitle());
    } else {
        // The view is full: stack it on the biggest window.
        PanelFrame *biggest = nullptr;
        for (PanelFrame *f : m_frames)
            if (!biggest || f->cells.width() * f->cells.height() > biggest->cells.width() * biggest->cells.height())
                biggest = f;
        if (!biggest)
            biggest = createFrame(QRect(0, 0, kCols, m_rows));
        biggest->addTab(panel, panel->windowTitle());
    }
    updateEmptyState();
    notifyEdited();
    emit panelsChanged();
    return panel;
}

bool Workspace::hasPanel(const QString &typeId) const
{
    for (const PanelFrame *f : m_frames)
        for (int i = 0; i < f->count(); ++i)
            if (f->panel(i)->property("panelType").toString() == typeId)
                return true;
    return false;
}

bool Workspace::raisePanel(const QString &typeId)
{
    for (PanelFrame *f : m_frames)
        for (int i = 0; i < f->count(); ++i)
            if (f->panel(i)->property("panelType").toString() == typeId) {
                f->setCurrent(i);
                return true;
            }
    return false;
}

void Workspace::frameEdited(PanelFrame *)
{
    notifyEdited();
}

void Workspace::frameEmptied(PanelFrame *frame)
{
    m_frames.removeAll(frame);
    frame->hide();
    frame->deleteLater();
    updateEmptyState();
    notifyEdited();
    emit panelsChanged();
}

void Workspace::setGuides(bool on)
{
    m_guides = on;
    update();
}

void Workspace::tabDragMove(const QPoint &globalPos)
{
    const QPoint pos = mapFromGlobal(globalPos);
    PanelFrame *over = rect().contains(pos) ? frameAt(pos) : nullptr;
    for (PanelFrame *f : m_frames)
        f->setDropTarget(f == over);
    m_dropFrame = over;
    m_dropPreview = (!over && rect().contains(pos)) ? growFrom(cellAt(pos), 8, 6) : QRect();
    m_guides = true;
    update();
}

void Workspace::tabDragCancel()
{
    for (PanelFrame *f : m_frames)
        f->setDropTarget(false);
    m_dropFrame = nullptr;
    m_dropPreview = QRect();
    m_guides = false;
    update();
}

void Workspace::tabDrop(PanelFrame *from, int index, const QPoint &globalPos)
{
    tabDragMove(globalPos);
    PanelFrame *target = m_dropFrame;
    const QRect preview = m_dropPreview;
    tabDragCancel();
    if (target == from || (!target && !preview.isValid()))
        return;
    if (!target && from->count() == 1) {
        // Dragging the only tab to empty space just moves the window there.
        if (isFree(preview, from)) {
            from->cells = preview;
            from->setGeometry(pixelRect(preview));
            notifyEdited();
        }
        return;
    }
    const QString title = from->tabTitle(index);
    QWidget *w = from->takeTab(index);
    if (!w)
        return;
    if (target)
        target->addTab(w, title);
    else
        createFrame(preview)->addTab(w, title);
    if (from->count() == 0)
        frameEmptied(from);
    notifyEdited();
    emit panelsChanged();
}

void Workspace::showAddMenu(const QPoint &globalPos, PanelFrame *into, const QPoint &cell)
{
    QMenu menu(this);
    menu.addSection(into ? tr("Add a tab") : tr("Add a window here"));
    for (const PanelType &t : panelTypes()) {
        const QString id = t.id;
        menu.addAction(t.title, this, [=]() {
            if (into) {
                QWidget *w = createPanel(id);
                into->addTab(w, w->windowTitle());
                notifyEdited();
                emit panelsChanged();
            } else {
                addPanel(id, cell.x() >= 0 ? growFrom(cell, 8, 6) : QRect());
            }
        });
    }
    menu.exec(globalPos);
}

void Workspace::mouseDoubleClickEvent(QMouseEvent *ev)
{
    if (!frameAt(ev->position().toPoint()))
        showAddMenu(ev->globalPosition().toPoint(), nullptr, cellAt(ev->position().toPoint()));
}

void Workspace::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::RightButton && !frameAt(ev->position().toPoint()))
        showAddMenu(ev->globalPosition().toPoint(), nullptr, cellAt(ev->position().toPoint()));
}

void Workspace::paintEvent(QPaintEvent *ev)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // Grid dots where windows can snap: only those in the part being repainted (a window
    // repainting repaints the workspace behind it).
    const double cw = double(width()) / kCols, ch = double(height()) / m_rows;
    const QRect area = ev->rect().adjusted(-2, -2, 2, 2);
    const int x0 = std::max(0, int(std::floor(area.left() / cw))), x1 = std::min(kCols, int(std::ceil(area.right() / cw)));
    const int y0 = std::max(0, int(std::floor(area.top() / ch))), y1 = std::min(m_rows, int(std::ceil(area.bottom() / ch)));
    p.setPen(Qt::NoPen);
    p.setBrush(m_guides ? theme::faint : theme::line);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (area.contains(QPoint(int(x * cw), int(y * ch))))
                p.drawEllipse(QPointF(x * cw, y * ch), 1.4, 1.4);
    if (m_dropPreview.isValid()) {
        QColor fill = theme::live;
        fill.setAlpha(40);
        p.setBrush(fill);
        p.setPen(QPen(theme::live, 2, Qt::DashLine));
        p.drawRoundedRect(QRectF(pixelRect(m_dropPreview)), 8, 8);
    }
}

void Workspace::resizeEvent(QResizeEvent *)
{
    layoutFrames();
}

void Workspace::layoutFrames()
{
    for (PanelFrame *f : m_frames)
        f->setGeometry(pixelRect(f->cells));
    m_empty->setGeometry(rect().adjusted(40, 40, -40, -40));
}

void Workspace::updateEmptyState()
{
    m_empty->setVisible(m_frames.isEmpty());
    m_empty->setGeometry(rect().adjusted(40, 40, -40, -40));
    update();
}

void Workspace::clearFrames()
{
    for (PanelFrame *f : m_frames) {
        f->hide();
        f->deleteLater();
    }
    m_frames.clear();
}

void Workspace::notifyEdited()
{
    if (!m_restoring)
        emit layoutEdited();
}

QJsonArray Workspace::layout() const
{
    QJsonArray frames;
    for (const PanelFrame *f : m_frames) {
        QJsonArray tabs;
        for (int i = 0; i < f->count(); ++i) {
            const QString type = f->panel(i)->property("panelType").toString();
            const PanelOptions *opts = optionsOf(f->panel(i));
            const QJsonObject o = opts ? opts->saveOptions() : QJsonObject();
            if (o.isEmpty())
                tabs.append(type);
            else
                tabs.append(QJsonObject{{"type", type}, {"options", o}});
        }
        frames.append(QJsonObject{{"x", f->cells.x()}, {"y", f->cells.y()}, {"w", f->cells.width()},
                                  {"h", f->cells.height()}, {"tabs", tabs}, {"current", f->current()}});
    }
    return frames;
}

bool Workspace::sameLayout(const QJsonArray &a, const QJsonArray &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i) {
        QJsonObject x = a[i].toObject(), y = b[i].toObject();
        x.remove("current");
        y.remove("current");
        if (x != y)
            return false;
    }
    return true;
}

void Workspace::setRows(int rows)
{
    rows = std::max(1, rows);
    if (rows == m_rows)
        return;
    m_rows = rows;
    layoutFrames();
    update();
}

void Workspace::loadLayout(const QJsonArray &frames)
{
    const bool was = m_restoring;
    m_restoring = true;
    clearFrames();
    for (const QJsonValue &v : frames) {
        const QJsonObject o = v.toObject();
        QRect cells(o["x"].toInt(), o["y"].toInt(), o["w"].toInt(1), o["h"].toInt(1));
        // A view stored on a taller screen (no encoder bar) is pulled up to fit this one.
        if (cells.bottom() >= m_rows) {
            cells.setHeight(std::min(cells.height(), m_rows));
            cells.moveTop(std::max(0, std::min(cells.top(), m_rows - cells.height())));
        }
        if (!isFree(cells))
            continue;
        PanelFrame *f = nullptr;
        for (const QJsonValue &t : o["tabs"].toArray())
            if (QWidget *w = t.isObject() ? createPanel(t["type"].toString(), t["options"].toObject())
                                          : createPanel(t.toString())) {
                if (!f)
                    f = createFrame(cells);
                f->addTab(w, w->windowTitle(), false);
            }
        if (f)
            f->setCurrent(std::clamp(o["current"].toInt(), 0, f->count() - 1));
    }
    m_restoring = was;
    updateEmptyState();
    emit panelsChanged();
}

} // namespace lumora
