// SPDX-License-Identifier: GPL-3.0-or-later
#include "poolgrid.h"

#include "appcontext.h"

#include "media.h"
#include "quickedit.h"
#include "theme.h"

#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

#include <algorithm>

namespace lumora {

namespace {
constexpr int kGap = 3;
} // namespace

PoolTiles::PoolTiles(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void PoolTiles::setItems(const QVector<PoolItem> &items)
{
    m_items = items;
    update();
}

void PoolTiles::setTileSize(int size)
{
    static const int w[] = {66, 86, 118}, h[] = {44, 58, 80};
    m_size = std::clamp(size, 0, 2);
    m_tileW = w[m_size];
    m_tileH = h[m_size];
    update();
}

void PoolTiles::setGrid(int columns, int rows)
{
    m_cols = std::clamp(columns, 0, 100);
    m_rows = std::clamp(rows, 0, 100);
    update();
    emit scrolled();
}

void PoolTiles::setPosition(int slot)
{
    m_first = std::clamp(slot, 1, 9999);
    m_hover = -1;
    update();
    emit scrolled();
}

void PoolTiles::setStoreMode(bool on)
{
    m_store = on;
    update();
}

int PoolTiles::columns() const
{
    return m_cols > 0 ? m_cols : std::max(1, (width() + kGap) / (m_tileW + kGap));
}

int PoolTiles::rows() const
{
    return m_rows > 0 ? m_rows : std::max(1, (height() + kGap) / (m_tileH + kGap));
}

QRect PoolTiles::tileRect(int index) const
{
    // Tiles share the whole window evenly, edge to edge.
    const int cols = columns(), rws = rows();
    const double w = double(width() - (cols - 1) * kGap) / cols;
    const double h = double(height() - (rws - 1) * kGap) / rws;
    const int c = index % cols, r = index / cols;
    const int x0 = int(std::lround(c * (w + kGap))), x1 = int(std::lround(c * (w + kGap) + w));
    const int y0 = int(std::lround(r * (h + kGap))), y1 = int(std::lround(r * (h + kGap) + h));
    return {x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
}

int PoolTiles::indexAt(const QPoint &p) const
{
    // Straight from the grid: which column and row, then check it's on the tile, not the gap.
    const int cols = columns(), rws = rows();
    if (p.x() < 0 || p.y() < 0 || width() <= 0 || height() <= 0)
        return -1;
    const int c = std::min(cols - 1, p.x() * cols / width());
    const int r = std::min(rws - 1, p.y() * rws / height());
    for (int dc = -1; dc <= 1; ++dc)   // rounding can put the point in a neighbour
        for (int dr = -1; dr <= 1; ++dr) {
            const int cc = c + dc, rr = r + dr;
            if (cc < 0 || rr < 0 || cc >= cols || rr >= rws)
                continue;
            if (tileRect(rr * cols + cc).contains(p))
                return rr * cols + cc;
        }
    return -1;
}

void PoolTiles::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int n = visibleCount();
    QHash<int, const PoolItem *> byId;
    for (const PoolItem &i : m_items)
        byId.insert(i.id, &i);

    for (int idx = 0; idx < n; ++idx) {
        const int id = idAt(idx);
        const QRectF r = QRectF(tileRect(idx)).adjusted(0.5, 0.5, -0.5, -0.5);
        const PoolItem *item = byId.value(id, nullptr);
        const bool hover = idx == m_hover;
        if (!item) {
            QColor outline = m_store ? theme::programmer : theme::line;
            if (!m_store)
                outline.setAlpha(hover ? 255 : 150);
            p.setPen(QPen(outline, 1, m_store ? Qt::DashLine : Qt::SolidLine));
            p.setBrush(hover ? QColor(0x20, 0x24, 0x2c) : Qt::transparent);
            p.drawRoundedRect(r, 6, 6);
            p.setFont(theme::uiFont(8));
            p.setPen(theme::faint);
            p.drawText(r.adjusted(7, 5, -6, -4), Qt::AlignLeft | Qt::AlignTop, QString::number(id));
            continue;
        }
        p.setPen(QPen(item->active ? theme::live : (m_store ? theme::programmer : theme::line), item->active ? 1.5 : 1));
        p.setBrush(hover ? QColor(0x2d, 0x33, 0x3d) : theme::raised);
        p.drawRoundedRect(r, 6, 6);
        if (item->tag.isValid()) {
            p.setPen(Qt::NoPen);
            p.setBrush(item->tag);
            QPainterPath clip;
            clip.addRoundedRect(r, 6, 6);
            p.save();
            p.setClipPath(clip);
            p.drawRect(QRectF(r.left(), r.top(), r.width(), 3));
            p.restore();
        }
        p.setFont(theme::uiFont(8));
        p.setPen(theme::muted);
        p.drawText(r.adjusted(7, 6, -6, -4), Qt::AlignLeft | Qt::AlignTop, QString::number(id));
        QRectF labelRect = r.adjusted(7, 20, -7, -5);
        if (item->picture && !item->image.isNull()) {
            // The picture over the tile, the name in a line along the bottom.
            const QRectF area = r.adjusted(22, 6, -6, -18);
            const QImage scaled = item->image.scaled(area.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.drawImage(QPointF(area.center().x() - scaled.width() / 2.0, area.center().y() - scaled.height() / 2.0), scaled);
            p.setFont(theme::uiFont(8, QFont::Medium));
            p.setPen(theme::text);
            const QRectF nameRect = r.adjusted(6, r.height() - 17, -6, -3);
            p.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(item->label, Qt::ElideRight, int(nameRect.width())));
            continue;
        }
        if (!item->image.isNull()) {
            const int size = int(r.height()) - 12;
            p.drawPixmap(QPointF(r.right() - size - 6, r.top() + 6), goboPixmap(item->image, size));
            labelRect.setRight(r.right() - size - 10);
        }
        if (!item->badge.isEmpty()) {
            p.setFont(theme::uiFont(7, QFont::DemiBold));
            p.setPen(theme::faint);
            const QRectF badgeRect(r.left() + 22, r.top() + 6, 40, 12);
            p.drawText(badgeRect, Qt::AlignLeft | Qt::AlignTop, item->badge);
        }
        if (item->swatch.isValid()) {
            p.setPen(QPen(theme::line, 1));
            p.setBrush(item->swatch);
            p.drawRoundedRect(QRectF(r.right() - 20, r.top() + 8, 13, 13), 3, 3);
        }
        p.setFont(theme::uiFont(9, QFont::Medium));
        p.setPen(theme::text);
        QString text = p.fontMetrics().elidedText(item->label, Qt::ElideRight, int(labelRect.width()) * 2 - 10);
        p.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, text);
    }
}

void PoolTiles::wheelEvent(QWheelEvent *ev)
{
    // A row at a time; never above the first number.
    const int steps = ev->angleDelta().y() / 120;
    if (steps != 0 && !m_locked)
        scrollRows(-steps);
}

void PoolTiles::scrollRows(int rows)
{
    if (m_locked || rows == 0)
        return;
    // A row at a time, landing on rows that start at 1: with 5 wide, from 7 up is 1, down is 11.
    const int cols = columns();
    const int target = m_first + rows * cols;
    const int snapped = 1 + std::max(0, (target - 1) / cols) * cols;
    if (snapped == m_first && rows < 0 && m_first > 1)
        setPosition(std::max(1, m_first - cols));
    else if (snapped != m_first)
        setPosition(std::max(1, snapped));
}

void PoolTiles::mousePressEvent(QMouseEvent *ev)
{
    const int idx = indexAt(ev->pos());
    if (idx < 0)
        return;
    const int id = idAt(idx);
    const bool occupied = std::any_of(m_items.begin(), m_items.end(), [id](const PoolItem &i) { return i.id == id; });
    if (ev->button() == Qt::LeftButton)
        emit tileClicked(id, occupied, ev->modifiers());
    else if (ev->button() == Qt::RightButton)
        emit tileMenu(id, occupied, ev->globalPosition().toPoint());
}

void PoolTiles::mouseMoveEvent(QMouseEvent *ev)
{
    const int idx = indexAt(ev->pos());
    if (idx != m_hover) {
        m_hover = idx;
        QString tip;
        for (const PoolItem &i : m_items)
            if (idx >= 0 && i.id == idAt(idx))
                tip = i.tooltip;
        setToolTip(tip);
        update();
    }
}

void PoolTiles::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

void PoolTiles::resizeEvent(QResizeEvent *)
{
    update();
    emit scrolled();   // fitting more columns moves the first slot of a scrolled row
}

// ===========================================================================

PoolPanel::PoolPanel(const QString &title, QWidget *parent) : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);
    // The bar above the tiles: paging, and whatever the pool adds there. A pool locked to its
    // place hides the whole bar.
    m_headerBox = new QWidget(this);
    m_header = new QHBoxLayout(m_headerBox);
    m_header->setContentsMargins(0, 0, 0, 0);
    m_header->setSpacing(6);
    auto *label = new QLabel(title, this);
    label->setProperty("role", "title");
    m_header->addWidget(label);
    // ▲ 21 ▼: page through the pool a row at a time; the number is the first slot on screen.
    const QString navStyle = QStringLiteral("QToolButton { padding: 1px 8px; }");
    m_up = new QToolButton(this);
    m_up->setText(QStringLiteral("▲"));
    m_up->setToolTip(tr("Scroll up a row"));
    m_up->setAutoRepeat(true);
    m_up->setStyleSheet(navStyle);
    m_at = new QLineEdit(this);
    m_at->setAlignment(Qt::AlignCenter);
    m_at->setFixedWidth(52);
    m_at->setValidator(new QIntValidator(1, 9999, m_at));
    m_at->setToolTip(tr("The slot in the top-left corner. Type a number to go there."));
    m_down = new QToolButton(this);
    m_down->setText(QStringLiteral("▼"));
    m_down->setToolTip(tr("Scroll down a row"));
    m_down->setAutoRepeat(true);
    m_down->setStyleSheet(navStyle);
    m_header->addWidget(m_up);
    m_header->addWidget(m_at);
    m_header->addWidget(m_down);
    m_header->addStretch(1);
    v->addWidget(m_headerBox);

    m_tiles = new PoolTiles(this);   // fills the window; the wheel scrolls by rows
    v->addWidget(m_tiles, 1);
    connect(m_up, &QToolButton::clicked, this, [this]() { m_tiles->scrollRows(-1); });
    connect(m_down, &QToolButton::clicked, this, [this]() { m_tiles->scrollRows(1); });
    connect(m_tiles, &PoolTiles::scrolled, this, [this]() {
        syncNav();
        panelStateChanged(this);   // where the pool is belongs to the view
    });
    connect(m_at, &QLineEdit::returnPressed, m_at, &QWidget::clearFocus);   // done typing: shortcuts again
    connect(m_at, &QLineEdit::editingFinished, this, [this]() {
        const int n = m_at->text().toInt();
        if (n >= 1 && n != m_tiles->position())
            m_tiles->setPosition(n);
        else
            syncNav();
    });
    syncNav();
}

void PoolPanel::followStore(AppContext *ctx)
{
    connect(ctx, &AppContext::storeArmedChanged, m_tiles, &PoolTiles::setStoreMode);
    m_tiles->setStoreMode(ctx->storeArmed());
}

void PoolPanel::syncNav()
{
    if (!m_at->hasFocus())
        m_at->setText(QString::number(m_tiles->position()));
    const bool locked = m_tiles->locked();
    m_headerBox->setVisible(!locked);
    m_up->setEnabled(!locked && !m_tiles->atTop());
    m_down->setEnabled(!locked);
    m_at->setReadOnly(locked);
    m_at->setToolTip(locked ? tr("Position locked (right-click the tab to unlock)")
                            : tr("The slot in the top-left corner. Type a number to go there."));
}

QJsonObject PoolPanel::saveOptions() const
{
    QJsonObject o;
    if (m_tiles->tileSize() != 1)
        o["tiles"] = m_tiles->tileSize();
    if (m_tiles->gridColumns() > 0)
        o["columns"] = m_tiles->gridColumns();
    if (m_tiles->gridRows() > 0)
        o["rows"] = m_tiles->gridRows();
    if (m_tiles->position() != 1)
        o["first"] = m_tiles->position();
    if (m_tiles->locked())
        o["locked"] = true;
    return o;
}

void PoolPanel::loadOptions(const QJsonObject &o)
{
    m_tiles->setTileSize(o["tiles"].toInt(1));
    m_tiles->setGrid(o["columns"].toInt(0), o["rows"].toInt(0));
    m_tiles->setPosition(o["first"].toInt(1));
    m_tiles->setLocked(o["locked"].toBool());
    syncNav();
}

void PoolPanel::buildOptions(QuickEditor *ed)
{
    // Grid: wide × tall in one line; 0 fits as many as there's room for.
    auto *grid = new QWidget;
    auto *gl = new QHBoxLayout(grid);
    gl->setContentsMargins(0, 0, 0, 0);
    gl->setSpacing(6);
    auto *label = new QLabel(tr("Grid"), grid);
    auto *wide = new QLineEdit(QString::number(m_tiles->gridColumns()), grid);
    auto *tall = new QLineEdit(QString::number(m_tiles->gridRows()), grid);
    for (QLineEdit *e : {wide, tall}) {
        e->setValidator(new QIntValidator(0, 100, e));
        e->setFixedWidth(46);
        e->setAlignment(Qt::AlignCenter);
    }
    wide->setToolTip(tr("Tiles wide (0 fits the window)"));
    tall->setToolTip(tr("Tiles tall (0 fits the window)"));
    auto *times = new QLabel(QStringLiteral("×"), grid);
    gl->addWidget(label);
    gl->addStretch(1);
    gl->addWidget(wide);
    gl->addWidget(times);
    gl->addWidget(tall);
    auto applyGrid = [this, wide, tall]() { m_tiles->setGrid(wide->text().toInt(), tall->text().toInt()); };
    connect(wide, &QLineEdit::textEdited, grid, applyGrid);
    connect(tall, &QLineEdit::textEdited, grid, applyGrid);
    ed->addWidget(grid);
    ed->addSegmented(tr("Tile size when fitting"), {tr("Small"), tr("Medium"), tr("Large")}, m_tiles->tileSize(),
                     [this](int i) { m_tiles->setTileSize(i); });
    // Keeps the pool where it is: no scrolling with the arrows, the wheel or a typed number.
    ed->addSegmented(tr("Lock position"), {tr("Off"), tr("On")}, m_tiles->locked() ? 1 : 0, [this](int i) {
        m_tiles->setLocked(i == 1);
        syncNav();
    });
}

} // namespace lumora
