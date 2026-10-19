// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractItemView>
#include <QStyledItemDelegate>
#include <QVector>
#include <QColor>
#include <QString>
#include <QStringList>

class QTableWidget;
class QWidget;

namespace lumora {

struct Preset;

// Small things several windows share.

// "2", "1.5": seconds the way cue lists show them.
QString formatSeconds(double s);

// A plain list table: row selection, no grid, headers on the left, last column stretched.
QTableWidget *makeTable(QWidget *parent, const QStringList &headers, int rowHeight = 28,
                        QAbstractItemView::EditTriggers edit = QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);

// Times as ranges: "0 thru 3 thru 0" <-> {0, 3, 0}. Parsing takes thru, t, to or - between
// numbers; empty or "off" is no times (an empty list, *ok true).
QString formatTimes(const QVector<double> &points);
QVector<double> parseTimes(const QString &text, bool *ok);

// A cell editor for times: pressing T types " thru ", so 0 T 5 reads 0 thru 5.
class TimeRangeDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    bool eventFilter(QObject *obj, QEvent *ev) override;
};

// The color a Color (or All) preset makes, for swatches; invalid for other presets.
QColor presetSwatch(const Preset &p);

} // namespace lumora
