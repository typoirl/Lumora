// SPDX-License-Identifier: GPL-3.0-or-later
#include "uiutil.h"

#include <QEvent>
#include <QSignalBlocker>
#include <QPainter>
#include <QResizeEvent>

#include <algorithm>

#include "fixturesheet.h"
#include "model.h"

#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QRegularExpression>
#include <QTableWidget>

#include <cmath>

namespace lumora {

QString formatSeconds(double s)
{
    return QString::number(s, 'f', std::fabs(s - std::round(s)) < 1e-6 ? 0 : 1);
}

QTableWidget *makeTable(QWidget *parent, const QStringList &headers, int rowHeight, QAbstractItemView::EditTriggers edit)
{
    auto *t = new QTableWidget(parent);
    t->setColumnCount(int(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->hide();
    t->verticalHeader()->setDefaultSectionSize(rowHeight);
    t->horizontalHeader()->setHighlightSections(false);
    t->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setShowGrid(false);
    t->setAlternatingRowColors(true);
    t->setEditTriggers(edit);
    return t;
}

QColor presetSwatch(const Preset &p)
{
    if (p.type != int(Feature::Color) && p.type != kPresetTypeAll)
        return QColor();
    return approximateColor(p.sampleValues(), nullptr);
}

QString formatTimes(const QVector<double> &points)
{
    QStringList parts;
    for (double v : points)
        parts << formatSeconds(v);
    return parts.join(QObject::tr(" thru "));
}

QVector<double> parseTimes(const QString &text, bool *ok)
{
    QString t = text.trimmed().toLower();
    *ok = true;
    if (t.isEmpty() || t == QLatin1String("off") || t == QLatin1String("-"))
        return {};
    static const QRegularExpression sep(QStringLiteral("\\s*(?:thru|through|to|t|-|–)\\s*|\\s+"));
    t.remove(QRegularExpression(QStringLiteral("\\s*s$")));
    QVector<double> out;
    for (const QString &part : t.split(sep, Qt::SkipEmptyParts)) {
        bool good = false;
        const double v = part.toDouble(&good);
        if (!good || v < 0) {
            *ok = false;
            return {};
        }
        out << v;
    }
    *ok = !out.isEmpty();
    return out;
}

QWidget *TimeRangeDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QWidget *w = QStyledItemDelegate::createEditor(parent, option, index);
    if (auto *line = qobject_cast<QLineEdit *>(w)) {
        line->installEventFilter(const_cast<TimeRangeDelegate *>(this));
        line->setPlaceholderText(QObject::tr("2, or 0 thru 5 (T types thru)"));
    }
    return w;
}

bool TimeRangeDelegate::eventFilter(QObject *obj, QEvent *ev)
{
    if (ev->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(ev);
        if (auto *line = qobject_cast<QLineEdit *>(obj); line && ke->key() == Qt::Key_T
            && !(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            QString before = line->text().left(line->cursorPosition()).trimmed();
            const QString after = line->text().mid(line->cursorPosition());
            before += QObject::tr(" thru ");
            line->setText(before + after.trimmed());
            line->setCursorPosition(int(before.size()));
            return true;
        }
    }
    return QStyledItemDelegate::eventFilter(obj, ev);
}

void drawFittedLines(QPainter &p, const QRect &r, const QStringList &lines)
{
    const QFontMetrics fm = p.fontMetrics();
    const int lineH = fm.height();
    int y = r.top() + (r.height() - lineH * int(lines.size())) / 2;
    for (const QString &line : lines) {
        const QRect row(r.left(), y, r.width(), lineH);
        if (fm.horizontalAdvance(line) <= r.width())
            p.drawText(row, Qt::AlignCenter, line);
        else
            p.drawText(row, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(line, Qt::ElideRight, r.width()));
        y += lineH;
    }
}

FittingToolButton::FittingToolButton(const QString &text, QWidget *parent) : QToolButton(parent), m_full(text)
{
    setText(text);
    setToolTip(text);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void FittingToolButton::setFullText(const QString &text)
{
    m_full = text;
    m_pad = -1;
    setToolTip(text);
    fit();
    updateGeometry();
}

void FittingToolButton::setShortText(const QString &text)
{
    m_short = text;
    fit();
}

int FittingToolButton::padding() const
{
    // Measured with the whole text: buttons have a minimum width, which a short text would hide.
    if (m_pad < 0) {
        auto *self = const_cast<FittingToolButton *>(this);
        const QString shown = text();
        const QSignalBlocker block(self);
        self->QToolButton::setText(m_full);
        m_pad = std::max(0, QToolButton::sizeHint().width() - fontMetrics().horizontalAdvance(m_full));
        self->QToolButton::setText(shown);
    }
    return m_pad;
}

QSize FittingToolButton::sizeHint() const
{
    QSize s = QToolButton::sizeHint();
    s.setWidth(padding() + fontMetrics().horizontalAdvance(m_full) + 2);   // a little slack for rounding
    return s;
}

QSize FittingToolButton::minimumSizeHint() const
{
    QSize s = QToolButton::minimumSizeHint();
    s.setWidth(std::min(sizeHint().width(), padding() + fontMetrics().horizontalAdvance(m_full.left(2) + QChar(0x2026))));
    return s;
}

void FittingToolButton::fit()
{
    const QFontMetrics fm = fontMetrics();
    const int room = std::max(0, width() - padding());
    QString shown = fm.elidedText(m_full, Qt::ElideRight, room);
    // Very tight: as many of its first letters as fit beat one letter and "…".
    if (shown != m_full && shown.size() < 4 && !m_short.isEmpty() && fm.horizontalAdvance(m_short) <= room)
        shown = m_short;
    else if (shown != m_full && shown.size() < 4) {
        const QString &from = m_short.isEmpty() ? m_full : m_short;
        int n = 0;
        while (n < from.size() && fm.horizontalAdvance(from.left(n + 1)) <= room)
            ++n;
        shown = from.left(n);
    }
    if (shown != text())
        setText(shown);
}

void FittingToolButton::resizeEvent(QResizeEvent *ev)
{
    QToolButton::resizeEvent(ev);
    fit();
}

void FittingToolButton::changeEvent(QEvent *ev)
{
    QToolButton::changeEvent(ev);
    if (ev->type() == QEvent::FontChange || ev->type() == QEvent::StyleChange) {
        m_pad = -1;
        updateGeometry();   // its size hint changes with the style's padding
        fit();
    }
}

LiveRefresh::LiveRefresh(QWidget *view, std::function<void()> update, int ms)
    : QObject(view), m_view(view), m_update(std::move(update)), m_ms(ms)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        m_last.start();
        m_update();
    });
    view->installEventFilter(this);
}

bool LiveRefresh::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_view && ev->type() == QEvent::Show && m_missed) {
        m_missed = false;
        request();
    }
    return false;
}

void LiveRefresh::request()
{
    if (!m_view->isVisible()) {
        m_missed = true;   // nothing to show now; catch up when it's shown
        return;
    }
    if (m_timer.isActive())
        return;   // a refresh already waiting covers this one
    const qint64 since = m_last.isValid() ? m_last.elapsed() : m_ms;
    if (since >= m_ms) {
        m_last.start();
        m_update();
    } else {
        m_timer.start(int(m_ms - since));
    }
}

namespace {
class HiddenChanges : public QObject {
public:
    HiddenChanges(QWidget *window, Show *show, std::function<void(Show::Parts)> handler)
        : QObject(window), m_window(window), m_handler(std::move(handler))
    {
        connect(show, &Show::changed, this, [this](Show::Parts parts) {
            if (m_window->isVisible())
                m_handler(parts);
            else
                m_pending |= parts;
        });
        window->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        if (obj == m_window && ev->type() == QEvent::Show && m_pending) {
            const Show::Parts parts = m_pending;
            m_pending = {};
            m_handler(parts);
        }
        return false;
    }

private:
    QWidget *m_window;
    std::function<void(Show::Parts)> m_handler;
    Show::Parts m_pending;
};
} // namespace

void onShowChanged(QWidget *window, Show *show, std::function<void(Show::Parts)> handler)
{
    new HiddenChanges(window, show, std::move(handler));
}

void ElidedLabel::setFullText(const QString &text)
{
    m_full = text;
    setToolTip(text);
    elide();
}

void ElidedLabel::resizeEvent(QResizeEvent *ev)
{
    QLabel::resizeEvent(ev);
    elide();
}

void ElidedLabel::elide()
{
    setText(fontMetrics().elidedText(m_full, Qt::ElideRight, std::max(10, width())));
}

} // namespace lumora
