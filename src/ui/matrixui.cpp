// SPDX-License-Identifier: GPL-3.0-or-later
#include "matrixui.h"

#include "appcontext.h"
#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QHelpEvent>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>

namespace lumora {

namespace {
constexpr int kCellMax = 56, kCellMin = 18, kGap = 4;   // square cells, smaller to fit
}

// ===========================================================================
// Selection matrix
// ===========================================================================

SelectionMatrixView::SelectionMatrixView(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    setMouseTracking(false);
    setMinimumSize(200, 120);
    connect(ctx->show, &Show::changed, this, [this](Show::Parts p) {
        if (!(p & (Show::ProgrammerPart | Show::PatchPart)))
            return;
        // Picks that left the selection go.
        const QVector<int> &sel = m_ctx->show->selection();
        for (auto it = m_picked.begin(); it != m_picked.end();)
            it = sel.contains(*it) ? std::next(it) : m_picked.erase(it);
        // As big as what's used (plus a row and column to drop onto), so it scrolls.
        int cols = 1, rows = 1;
        for (int fid : sel) {
            cols = std::max(cols, m_ctx->show->gridPosition(fid).x() + 2);
            rows = std::max(rows, m_ctx->show->gridPosition(fid).y() + 2);
        }
        m_cols = cols;
        m_rows = rows;
        setMinimumSize(std::max(200, m_origin.x() * 2 + cols * (kCellMin + kGap)), std::max(120, m_origin.y() * 2 + rows * (kCellMin + kGap)));
        update();
    });
}

QRect SelectionMatrixView::cellRect(const QPoint &cell) const
{
    const int c = cellSize();
    return {m_origin.x() + cell.x() * (c + kGap), m_origin.y() + cell.y() * (c + kGap), c, c};
}

QPoint SelectionMatrixView::cellAt(const QPoint &pos) const
{
    const int c = cellSize();
    return {std::max(0, (pos.x() - m_origin.x()) / (c + kGap)), std::max(0, (pos.y() - m_origin.y()) / (c + kGap))};
}

int SelectionMatrixView::cellSize() const
{
    // As big as fits the window (up to 56 px), so a large matrix shows whole where it can.
    const int w = (width() - 2 * m_origin.x()) / std::max(1, m_cols) - kGap;
    const int h = (height() - 2 * m_origin.y()) / std::max(1, m_rows) - kGap;
    return std::clamp(std::min(w, h), kCellMin, kCellMax);
}

int SelectionMatrixView::fixtureAt(const QPoint &cell) const
{
    Show *show = m_ctx->show;
    for (int fid : show->selection())
        if (show->gridPosition(fid) == cell)
            return fid;
    return 0;
}

void SelectionMatrixView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0x12, 0x15, 0x1a));
    Show *show = m_ctx->show;
    const QVector<int> &sel = show->selection();
    if (sel.isEmpty()) {
        p.setPen(theme::muted);
        p.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap,
                   tr("Select fixtures: they line up here along X. Next Y (or Grid 0/1) starts the next ones one step down in Y; a group keeps its layout. "
                      "Drag a box around fixtures to pick them, then drag them to move them."));
        return;
    }
    int cols = 1, rows = 1;
    for (int fid : sel) {
        const QPoint g = show->gridPosition(fid);
        cols = std::max(cols, g.x() + 2);
        rows = std::max(rows, g.y() + 2);
    }
    m_cols = cols;
    m_rows = rows;
    const int cs = cellSize();
    const QPoint offset = m_dragging ? cellAt(m_dragPos) - m_pressCell : QPoint();
    // Empty cells, faintly, one past what's used.
    p.setPen(Qt::NoPen);
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < cols; ++x) {
            p.setBrush(QColor(0x1b, 0x1f, 0x26));
            p.drawRoundedRect(cellRect({x, y}), 5, 5);
        }
    const QVector<int> working = show->workingSelection();
    auto drawFixture = [&](int fid, int order, const QRect &r, bool ghost) {
        const bool kept = working.contains(fid);
        const bool picked = m_picked.contains(fid);
        p.setPen(QPen(picked ? theme::programmer : kept ? theme::featureColor(Feature::Position) : theme::line, picked ? 2.0 : 1.2));
        p.setBrush(ghost ? QColor(0x3a, 0x32, 0x20) : picked ? QColor(0x2e, 0x2a, 0x22) : kept ? QColor(0x26, 0x2c, 0x36) : QColor(0x1b, 0x1f, 0x26));
        p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
        // Small cells (a big matrix in a small window): smaller text, and no order number.
        p.setFont(theme::uiFont(cs >= 44 ? 10 : cs >= 34 ? 8 : 7, QFont::DemiBold));
        p.setPen(kept || ghost ? theme::text : theme::faint);
        if (cs >= 24)   // tiny cells are just squares (hover for the fixture)
            p.drawText(r.adjusted(cs >= 34 ? 5 : 2, cs >= 34 ? 4 : 2, -2, cs >= 44 ? -14 : -2),
                       Qt::AlignLeft | Qt::AlignTop | (cs >= 34 ? Qt::AlignTop : Qt::AlignVCenter), show->idText(fid));
        if (cs >= 44) {
            p.setFont(theme::uiFont(7));
            p.setPen(theme::muted);
            p.drawText(r.adjusted(5, 0, -4, -4), Qt::AlignLeft | Qt::AlignBottom, QString::number(order + 1));
        }
    };
    for (int i = 0; i < sel.size(); ++i) {
        const int fid = sel[i];
        if (m_dragging && m_picked.contains(fid)) {
            QRect r = cellRect(show->gridPosition(fid));
            p.setPen(QPen(theme::line, 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
            continue;
        }
        drawFixture(fid, i, cellRect(show->gridPosition(fid)), false);
    }
    if (m_dragging) {   // the picked fixtures where they'd land
        for (int i = 0; i < sel.size(); ++i)
            if (m_picked.contains(sel[i])) {
                const QPoint g = show->gridPosition(sel[i]) + offset;
                drawFixture(sel[i], i, cellRect(QPoint(std::max(0, g.x()), std::max(0, g.y()))), true);
            }
    }
    if (m_banding) {
        p.setPen(QPen(theme::programmer, 1, Qt::DashLine));
        p.setBrush(QColor(0xe8, 0xa8, 0x3a, 28));
        p.drawRect(QRect(m_pressPos, m_dragPos).normalized());
    }
}

bool SelectionMatrixView::event(QEvent *ev)
{
    if (ev->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(ev);
        const QPoint c = cellAt(he->pos());
        const int fid = cellRect(c).contains(he->pos()) ? fixtureAt(c) : 0;
        if (fid) {
            const Fixture *f = m_ctx->show->fixture(fid);
            QToolTip::showText(he->globalPos(), QStringLiteral("%1  %2  ·  X %3, Y %4")
                                                    .arg(m_ctx->show->idText(fid), f ? f->name : QString())
                                                    .arg(c.x()).arg(c.y()), this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(ev);
}

void SelectionMatrixView::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() != Qt::LeftButton)
        return;
    m_pressPos = m_dragPos = ev->pos();
    m_pressCell = cellAt(ev->pos());
    const int fid = cellRect(m_pressCell).contains(ev->pos()) ? fixtureAt(m_pressCell) : 0;
    const bool ctrl = ev->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
    if (!fid) {
        // A box to pick fixtures (Ctrl or Shift: add to the picks).
        if (!ctrl)
            m_picked.clear();
        m_banding = true;
    } else if (ctrl) {
        if (m_picked.contains(fid))
            m_picked.remove(fid);
        else
            m_picked.insert(fid);
    } else {
        if (!m_picked.contains(fid))
            m_picked = {fid};
        m_dragging = true;
    }
    update();
}

void SelectionMatrixView::mouseMoveEvent(QMouseEvent *ev)
{
    if (!m_dragging && !m_banding)
        return;
    m_dragPos = ev->pos();
    if (m_banding) {
        const QRect band = QRect(m_pressPos, m_dragPos).normalized();
        const bool add = ev->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
        QSet<int> inBand;
        for (int fid : m_ctx->show->selection())
            if (band.intersects(cellRect(m_ctx->show->gridPosition(fid))))
                inBand.insert(fid);
        if (!add)
            m_picked = inBand;
        else
            m_picked.unite(inBand);
    }
    update();
}

void SelectionMatrixView::mouseReleaseEvent(QMouseEvent *ev)
{
    if (m_dragging) {
        m_dragging = false;
        const QPoint offset = cellAt(ev->pos()) - m_pressCell;
        QVector<int> fids;
        for (int fid : m_ctx->show->selection())
            if (m_picked.contains(fid))
                fids << fid;
        m_ctx->show->moveInGrid(fids, offset);
    }
    m_banding = false;
    update();
}

void SelectionMatrixView::contextMenuEvent(QContextMenuEvent *ev)
{
    const int fid = fixtureAt(cellAt(ev->pos()));
    QMenu menu(this);
    if (!m_picked.isEmpty()) {
        const QVector<int> picked(m_picked.begin(), m_picked.end());
        menu.addAction(tr("Take the %n picked fixture(s) out of the selection", nullptr, int(picked.size())), this,
                       [this, picked]() { m_ctx->show->select(picked, Show::SelectMode::Remove); });
        menu.addAction(tr("Unpick"), this, [this]() {
            m_picked.clear();
            update();
        });
    } else if (fid) {
        menu.addAction(tr("Take fixture %1 out of the selection").arg(m_ctx->show->idText(fid)), this,
                       [this, fid]() { m_ctx->show->select({fid}, Show::SelectMode::Remove); });
    }
    menu.addAction(tr("Pick all"), this, [this]() {
        const QVector<int> &sel = m_ctx->show->selection();
        m_picked = QSet<int>(sel.begin(), sel.end());
        update();
    });
    menu.addSeparator();
    menu.addAction(tr("Next Y"), this, [this]() { m_ctx->run(QStringLiteral("NextY")); });
    menu.addAction(tr("All along X, in selection order"), this, [this]() { m_ctx->show->resetGrid(); });
    menu.exec(ev->globalPos());
}

SelectionMatrixPanel::SelectionMatrixPanel(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    setWindowTitle(tr("Selection matrix"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(6, 6, 6, 6);
    v->setSpacing(6);
    auto *head = new QHBoxLayout;
    m_info = new QLabel(this);
    m_info->setProperty("role", "muted");
    auto *next = new QToolButton(this);
    next->setText(tr("Next Y"));
    next->setToolTip(tr("Fixtures selected next go one step down in Y, from X 0 (NextY, or Grid 0/1)"));
    auto *oneRow = new QToolButton(this);
    oneRow->setText(tr("All along X"));
    oneRow->setToolTip(tr("Put the selection back along X at Y 0, in the order it was selected"));
    head->addWidget(m_info, 1);
    head->addWidget(next);
    head->addWidget(oneRow);
    v->addLayout(head);
    auto *scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(new SelectionMatrixView(ctx, scroll));
    v->addWidget(scroll, 1);
    connect(next, &QToolButton::clicked, this, [this]() { m_ctx->run(QStringLiteral("NextY")); });
    connect(oneRow, &QToolButton::clicked, this, [this]() { m_ctx->show->resetGrid(); });
    connect(ctx->show, &Show::changed, this, [this](Show::Parts p) {
        if (p & (Show::ProgrammerPart | Show::SpreadsPart))
            refresh();
    });
    refresh();
}

void SelectionMatrixPanel::refresh()
{
    Show *show = m_ctx->show;
    int cols = 0, rows = 0;
    for (int fid : show->selection()) {
        cols = std::max(cols, show->gridPosition(fid).x() + 1);
        rows = std::max(rows, show->gridPosition(fid).y() + 1);
    }
    QString t = show->selection().isEmpty() ? tr("Nothing selected")
                                            : tr("%1 × %2 · %3 fixtures").arg(cols).arg(rows).arg(show->selection().size());
    const QString sp = show->activeSpreadSummary();
    if (!sp.isEmpty())
        t += tr(" · Spread %1").arg(sp);
    m_info->setText(t);
}

// ===========================================================================
// Spreads modifier
// ===========================================================================

SpreadModifierPanel::SpreadModifierPanel(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    setWindowTitle(tr("Spreads modifier"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    auto *body = new QWidget(scroll);
    scroll->setWidget(body);
    outer->addWidget(scroll);
    auto *v = new QVBoxLayout(body);
    v->setContentsMargins(8, 6, 8, 6);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);
    const QStringList rows = {tr("Every Nth"), tr("Starting at"), tr("Blocks"), tr("Groups (0 off)"), tr("Wings"), tr("Direction"),
                              tr("Phase from"), tr("Phase to")};
    for (int r = 0; r < rows.size(); ++r) {
        auto *l = new QLabel(rows[r], this);
        l->setProperty("role", "muted");
        grid->addWidget(l, r + 1, 0);
    }
    for (int axis = 0; axis < 2; ++axis) {
        Side &s = m_side[axis];
        s.on = new QCheckBox(axis == 0 ? QStringLiteral("X") : QStringLiteral("Y"), this);
        s.on->setStyleSheet(QStringLiteral("font-weight: 600;"));
        s.target = new QLabel(this);
        s.target->setProperty("role", "muted");
        auto *headCell = new QVBoxLayout;
        headCell->setSpacing(2);
        headCell->addWidget(s.on);
        headCell->addWidget(s.target);
        grid->addLayout(headCell, 0, axis + 1);
        auto spin = [this](int lo, int hi, int step = 1) {
            auto *b = new QSpinBox(this);
            b->setRange(lo, hi);
            b->setSingleStep(step);
            return b;
        };
        s.every = spin(1, 64);
        s.offset = spin(1, 64);
        s.blocks = spin(1, 64);
        s.groups = spin(0, 64);
        s.wings = spin(1, 16);
        s.direction = new QComboBox(this);
        for (int i = 0; i <= int(SpreadDirection::Random); ++i)
            s.direction->addItem(spreadDirectionName(SpreadDirection(i)));
        s.phaseFrom = spin(-720, 720, 15);
        s.phaseTo = spin(-720, 720, 15);
        s.phaseFrom->setSuffix(QStringLiteral("°"));
        s.phaseTo->setSuffix(QStringLiteral("°"));
        QWidget *fields[] = {s.every, s.offset, s.blocks, s.groups, s.wings, s.direction, s.phaseFrom, s.phaseTo};
        for (int r = 0; r < 8; ++r)
            grid->addWidget(fields[r], r + 1, axis + 1);
        s.summary = new QLabel(this);
        s.summary->setProperty("role", "muted");
        s.summary->setWordWrap(true);
        grid->addWidget(s.summary, 9, axis + 1);
        auto *buttons = new QHBoxLayout;
        auto *reset = new QPushButton(tr("Reset"), this);
        buttons->addWidget(reset);
        grid->addLayout(buttons, 10, axis + 1);
        for (QSpinBox *b : {s.every, s.offset, s.blocks, s.groups, s.wings, s.phaseFrom, s.phaseTo})
            connect(b, &QSpinBox::valueChanged, this, [this, axis]() { apply(axis); });
        connect(s.direction, &QComboBox::currentIndexChanged, this, [this, axis]() { apply(axis); });
        connect(s.on, &QCheckBox::toggled, this, [this, axis](bool on) {
            if (!m_updating)
                m_ctx->show->setSpreadAxisOn(axis ? Spread::Axis::Y : Spread::Axis::X, on);
        });
        connect(reset, &QPushButton::clicked, this, [this, axis]() {
            const Spread::Axis a = axis ? Spread::Axis::Y : Spread::Axis::X;
            m_ctx->show->setActiveSpread(a, Spread::none(a));
        });
    }
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    v->addLayout(grid);
    auto *note = new QLabel(tr("They stack: X works along the selection matrix's X, Y along its Y. Clearing the selection "
                               "resets them. To keep them, press Store and tap a slot in the Spreads pool (or Store Spread 5): "
                               "both directions go in, the Y one in the next free slot if they differ; /x or /y stores just one."),
                            this);
    note->setProperty("role", "muted");
    note->setWordWrap(true);
    v->addWidget(note);
    v->addStretch(1);
    connect(ctx->show, &Show::changed, this, [this](Show::Parts p) {
        if (p & (Show::ProgrammerPart | Show::SpreadsPart))
            refresh();
    });
    refresh();
}

void SpreadModifierPanel::apply(int axis)
{
    if (m_updating)
        return;
    const Side &s = m_side[axis];
    const Spread::Axis a = axis ? Spread::Axis::Y : Spread::Axis::X;
    Spread m = m_ctx->show->activeSpread(a);
    m.every = s.every->value();
    m.offset = s.offset->value() - 1;
    m.blocks = s.blocks->value();
    m.groups = s.groups->value();
    m.wings = s.wings->value();
    m.direction = SpreadDirection(s.direction->currentIndex());
    m.phaseFrom = s.phaseFrom->value();
    m.phaseTo = s.phaseTo->value();
    m_updating = true;
    Show *show = m_ctx->show;
    show->setActiveSpread(a, m);
    // Tapped from the pool: the pool spread changes with it (keeping its name and axes).
    if (m.id > 0)
        if (const Spread *pool = show->spread(m.id)) {
            Spread kept = m;
            kept.name = pool->name;
            kept.onX = pool->onX;
            kept.onY = pool->onY;
            show->setSpread(kept);
        }
    m_updating = false;
    refresh();
}

void SpreadModifierPanel::refresh()
{
    if (m_updating)
        return;
    m_updating = true;
    Show *show = m_ctx->show;
    for (int axis = 0; axis < 2; ++axis) {
        const Spread::Axis a = axis ? Spread::Axis::Y : Spread::Axis::X;
        const Spread &m = show->activeSpread(a);
        Side &s = m_side[axis];
        s.on->setChecked(show->spreadAxisOn(a));
        s.every->setValue(m.every);
        s.offset->setValue(m.offset + 1);
        s.blocks->setValue(m.blocks);
        s.groups->setValue(m.groups);
        s.wings->setValue(m.wings);
        s.direction->setCurrentIndex(int(m.direction));
        s.phaseFrom->setValue(int(m.phaseFrom));
        s.phaseTo->setValue(int(m.phaseTo));
        s.summary->setText(m.isDefault() ? tr("None") : m.summary());
        const Spread *pool = m.id > 0 ? show->spread(m.id) : nullptr;
        s.target->setText(pool ? tr("Editing spread %1 \"%2\"").arg(m.id).arg(pool->name) : tr("The programmer's (not stored)"));
        for (QWidget *w : std::initializer_list<QWidget *>{s.every, s.offset, s.blocks, s.groups, s.wings, s.direction, s.phaseFrom, s.phaseTo})
            w->setEnabled(show->spreadAxisOn(a));
    }
    m_updating = false;
}

} // namespace lumora
