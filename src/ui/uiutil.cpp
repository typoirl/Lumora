// SPDX-License-Identifier: GPL-3.0-or-later
#include "uiutil.h"

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

} // namespace lumora
