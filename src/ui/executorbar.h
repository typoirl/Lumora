// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "midi.h"
#include "model.h"

#include "paneloptions.h"

#include <QSet>
#include <QWidget>

class QGridLayout;
class QHBoxLayout;
class QLineEdit;
class QLabel;

namespace lumora {

class AppContext;
constexpr int kExecutorsPerRow = 99;   // 101–199, 201–299…

// One executor. What it looks like follows its number:
//  * 201–299 faders: header (number, sequence, current cue), three function buttons and a fader
//  * 101–199 buttons: header and one big key (Toggle unless changed)
//  * 301–399 encoders: header, a knob for the master (drag or scroll it) and a push key
//  * 401–499 any of the three, set per executor (right-click → Executor type)
// Faders and knobs glow with what the playback actually puts on stage, so they read from
// across the booth.
class ExecutorStrip : public QWidget {
    Q_OBJECT
public:
    ExecutorStrip(AppContext *ctx, int number, QWidget *parent = nullptr);
    int number() const { return m_number; }
    // The page it shows: its window's locked page, or the current page (0: follow).
    void setLockedPage(int page) { m_lockedPage = page; update(); }
    int page() const;
    ExecKind kind() const;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

private:
    enum Region { None = -1, Header = -2, Fader = -3 };   // 0..2 = buttons
    QRect headerRect() const;
    QRect buttonRect(int i) const;
    QRect faderRect() const;
    QRect trackRect() const;
    QRectF knobRect() const;
    int buttonCount() const;
    int regionAt(const QPoint &p) const;
    MidiTarget targetFor(int region) const;
    bool isBound(int region) const;
    void setFaderFromY(int y);
    void storeHere();

    AppContext *m_ctx = nullptr;
    int m_number;
    int m_lockedPage = 0;
    int m_pressed = None;
    bool m_dragging = false;
    int m_dragY = 0;           // encoders turn by dragging up and down
    double m_dragValue = 0.0;
};

// A row of executors with page controls. In Learn mode it is the strip that slides in for
// Learn MIDI (when the view has no executor window), so you can click a control to map.
class ExecutorBar : public QWidget, public PanelOptions {
    Q_OBJECT
public:
    explicit ExecutorBar(AppContext *ctx, QWidget *parent = nullptr);

    // Right-click the window's tab: how many executors, which numbers (101…, 201…), page buttons.
    QJsonObject saveOptions() const override;
    void loadOptions(const QJsonObject &o) override;
    void buildOptions(QuickEditor *editor) override;
    // The Executor grid window: from this executor, wide × tall (0 fits).
    void setGridDefaults(int first, int wide, int tall);
    void setLockedPage(int page);   // 0: follows the current page

protected:
    void resizeEvent(QResizeEvent *ev) override;

private:
    void rebuild();
    void rebuildPages();
    void syncSizes();
    QSize gridSize() const;
    int rowEnd() const;
    bool fixedGrid() const;
    AppContext *m_ctx = nullptr;
    QGridLayout *m_grid = nullptr;
    QLineEdit *m_pageEdit = nullptr;
    QSize m_shownGrid;
    QHBoxLayout *m_pages = nullptr;
    QWidget *m_head = nullptr;
    bool m_showPages = true;
    QList<ExecKind> m_kinds;
    QSet<ExecutorStrip *> m_wasMoving;   // strips the last animation frame repainted
    QString m_pageSignature;
    int m_first = 201;
    int m_wide = 0;          // executors across; 0 with one row: Interface settings, else fit
    int m_tall = 1;          // rows; 0 fits
    bool m_fitWide = false;  // wide 0 really means fit
    int m_lockedPage = 0;    // a page of its own (0: the current page)
    QString m_baseTitle;     // "Faders 201–210"
    void updateTitle();
    int shownPage() const;
    QWidget *m_pageControls = nullptr;
    QList<ExecutorStrip *> m_strips;
};

// Stores the programmer onto an executor: a new cue in its sequence, or a new sequence on it.
void storeOntoExecutor(AppContext *ctx, int page, int number);

// What slides in when Store is pressed and the view has no executor window: a tile for every
// executor of the 100, 200, 300 and 400 rows on this page. Tap one to store onto it.
class StoreExecutorPicker : public QWidget {
    Q_OBJECT
public:
    explicit StoreExecutorPicker(AppContext *ctx, QWidget *parent = nullptr);
    QSize sizeHint() const override { return {900, kHead + 4 * (kTileH + kGap) + 8}; }
    QSize minimumSizeHint() const override { return {400, kHead + 4 * (kTileH + kGap) + 8}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    static constexpr int kHead = 30;
    static constexpr int kTileH = 40;
    static constexpr int kGap = 4;
    static constexpr int kLabelW = 34;
    int columns() const;
    QRect tileRect(int row, int col) const;
    int numberAt(const QPoint &p) const;   // 0: none

    AppContext *m_ctx = nullptr;
    int m_hover = 0;
};

} // namespace lumora
