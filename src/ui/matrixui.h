// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSet>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;
class QPushButton;

namespace lumora {

class AppContext;

// The selection matrix: the selected fixtures where they sit, X across and Y down, on square
// cells. Selecting adds along X; Next Y starts another line. Drag a box around fixtures (or
// click, Ctrl-click) to pick them, then drag them to move them together: fixtures in the way
// take the cells they left. Effects and spreads run across it and down it.
class SelectionMatrixView : public QWidget {
    Q_OBJECT
public:
    explicit SelectionMatrixView(AppContext *ctx, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

private:
    QRect cellRect(const QPoint &cell) const;
    QPoint cellAt(const QPoint &pos) const;
    int fixtureAt(const QPoint &cell) const;
    int cellSize() const;
    mutable int m_cols = 1, m_rows = 1;   // cells in use, plus one to drop onto
    AppContext *m_ctx = nullptr;
    QSet<int> m_picked;       // fixtures picked here to move together
    bool m_dragging = false;  // moving the picked fixtures
    bool m_banding = false;   // drawing a box to pick fixtures
    QPoint m_pressPos, m_dragPos;
    QPoint m_pressCell;
    QPoint m_origin{8, 8};
};

class SelectionMatrixPanel : public QWidget {
    Q_OBJECT
public:
    explicit SelectionMatrixPanel(AppContext *ctx, QWidget *parent = nullptr);

private:
    void refresh();
    AppContext *m_ctx = nullptr;
    QLabel *m_info = nullptr;
};

// The spreads on the selection, X and Y side by side: each axis switched on or off and edited
// live. An axis whose spread was tapped from the pool edits that pool spread; otherwise it's the
// programmer's own until stored into the pool. Clearing the selection resets them.
class SpreadModifierPanel : public QWidget {
    Q_OBJECT
public:
    explicit SpreadModifierPanel(AppContext *ctx, QWidget *parent = nullptr);

private:
    struct Side {
        QCheckBox *on = nullptr;
        QSpinBox *every = nullptr, *offset = nullptr, *blocks = nullptr, *groups = nullptr, *wings = nullptr;
        QSpinBox *phaseFrom = nullptr, *phaseTo = nullptr;
        QComboBox *direction = nullptr;
        QLabel *summary = nullptr;
        QLabel *target = nullptr;   // what it edits: Spread 3 "Odd", or the programmer's own
    };
    void refresh();
    void apply(int axis);
    AppContext *m_ctx = nullptr;
    Side m_side[2];
    bool m_updating = false;
};

} // namespace lumora
