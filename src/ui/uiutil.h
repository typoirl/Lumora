// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "show.h"

#include <QAbstractItemView>
#include <QStyledItemDelegate>
#include <QVector>
#include <QColor>
#include <QString>
#include <QStringList>
#include <QElapsedTimer>
#include <QLabel>
#include <QTimer>
#include <QToolButton>

#include <functional>

class QPainter;
class QRect;
class QTableWidget;
class QWidget;

namespace lumora {

struct Preset;

// Small things several windows share.

// Lines of text stacked in the middle of a rect. A line that fits is centered; one that doesn't
// starts at the left and is cut off with "…", so its first part can still be read.
void drawFittedLines(QPainter &p, const QRect &r, const QStringList &lines);

// A tool button that, squeezed, shows the start of its text and "…" rather than cutting it off
// at both ends. It asks the layout for room for its whole text, and gives it up when it must.
class FittingToolButton : public QToolButton {
public:
    explicit FittingToolButton(const QString &text, QWidget *parent = nullptr);
    void setFullText(const QString &text);
    void setShortText(const QString &text);   // tried before cutting letters off ("Ctl")
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *ev) override;
    void changeEvent(QEvent *ev) override;

private:
    void fit();
    int padding() const;   // the button's width around its text
    QString m_full, m_short;
    mutable int m_pad = -1;
};

// A label that fits its text to its width with "…" at the end, whenever either changes; the
// whole text is its tooltip.
class ElidedLabel : public QLabel {
public:
    using QLabel::QLabel;
    void setFullText(const QString &text);

protected:
    void resizeEvent(QResizeEvent *ev) override;

private:
    void elide();
    QString m_full;
};

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

// Show changes for a window: `handler` runs at once while the window is visible; a hidden one
// (another view, a tab behind) collects the parts that changed and runs it once when shown, so
// windows nobody is looking at don't rebuild after every command.
void onShowChanged(QWidget *window, Show *show, std::function<void(Show::Parts)> handler);

// Runs `update` for a live view at most every `ms` milliseconds (30 times a second by
// default), however often it's asked: the output changes 44 times a second, and a view that
// redraws with every frame costs more than it shows. The last request is never dropped.
class LiveRefresh : public QObject {
public:
    LiveRefresh(QWidget *view, std::function<void()> update, int ms = 33);
    void request();

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;   // shown again: catch up

private:
    bool m_missed = false;   // asked while hidden
    QWidget *m_view;
    std::function<void()> m_update;
    QTimer m_timer;
    QElapsedTimer m_last;
    int m_ms;
};

} // namespace lumora
