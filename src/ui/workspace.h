// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "paneloptions.h"
#include "midi.h"
#include "poolgrid.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

#include <functional>

class QLabel;
class QToolButton;

namespace lumora {

class AppContext;

// ---- Pool panels ------------------------------------------------------------

class GroupsPanel : public PoolPanel {
    Q_OBJECT
public:
    explicit GroupsPanel(AppContext *ctx, QWidget *parent = nullptr);

private:
    void refresh();
    AppContext *m_ctx = nullptr;
};

class PresetsPanel : public PoolPanel {
    Q_OBJECT
public:
    explicit PresetsPanel(AppContext *ctx, QWidget *parent = nullptr);
    // The pool shown (Dimmer, Position…) is part of the view, with the tile size.
    QJsonObject saveOptions() const override;
    void loadOptions(const QJsonObject &o) override;
    void setType(int type);

private:
    void refresh();
    AppContext *m_ctx = nullptr;
    int m_type = 4;
    QList<QToolButton *> m_tabs;
};

// spread as a pool: tap one to put it on the selection (again to take it off), Store and tap
// to keep the active one, right-click to edit. The header shows (and edits) the active spread.
class SpreadsPanel : public PoolPanel {
    Q_OBJECT
public:
    explicit SpreadsPanel(AppContext *ctx, QWidget *parent = nullptr);

private:
    void refresh();
    AppContext *m_ctx = nullptr;
    QToolButton *m_active = nullptr;
};

// The show's views as a pool: tap one to show it on this screen, Store and tap to keep the
// windows on screen there (an empty slot makes a new view), right-click to rename or delete.
class ViewsPanel : public PoolPanel {
    Q_OBJECT
public:
    explicit ViewsPanel(AppContext *ctx, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *ev) override;

private:
    void refresh();
    int screen() const;
    AppContext *m_ctx = nullptr;
};

// Executor pages as a pool: tap one to go to that page (an empty slot makes it), right-click to
// name it. The current page is lit.
class ExecutorPagesPanel : public PoolPanel {
    Q_OBJECT
public:
    explicit ExecutorPagesPanel(AppContext *ctx, QWidget *parent = nullptr);

private:
    void refresh();
    AppContext *m_ctx = nullptr;
};

// One rate master drawn like a fader executor: its name and value, a Learn key and a fader (or,
// set to a button, one big Learn key). Right-click to name it, set its mode and value, make it a
// fader or a button, reset or freeze it.
class RateMasterStrip : public QWidget {
    Q_OBJECT
public:
    RateMasterStrip(AppContext *ctx, int id, QWidget *parent = nullptr);
    QSize sizeHint() const override { return {112, 248}; }
    QSize minimumSizeHint() const override { return {72, 110}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

private:
    QRect keyRect() const;
    QRect trackRect() const;
    void setFromY(int y);
    MidiTarget target(bool key) const;   // this master's fader, or its Learn key, for MIDI
    AppContext *m_ctx = nullptr;
    int m_id = 1;
    bool m_dragging = false;
    bool m_keyDown = false;
};

// The ten rate masters effects can follow, always ten across in one row, like the 200 executors.
// Assign puts one on an executor too.
class RateMastersPanel : public QWidget {
    Q_OBJECT
public:
    explicit RateMastersPanel(AppContext *ctx, QWidget *parent = nullptr);

private:
    AppContext *m_ctx = nullptr;
    QList<RateMasterStrip *> m_strips;
};

// ---- Panel registry -----------------------------------------------------------

struct PanelType {
    QString id;      // stable, used in saved layouts
    QString title;   // shown in menus and dock title bars
    std::function<QWidget *(AppContext *, QWidget *)> make;
};
const QList<PanelType> &panelTypes();

class PanelFrame;

// A view's window canvas, laid out on a grid:
//  * drag a window's header to move it, drag any corner or edge to resize it; both snap to the grid
//  * windows never overlap
//  * every window is a stack of tabs: drag a tab onto another window to stack it there,
//    or onto empty space to give it a window of its own
//  * double-click (or right-click) empty space to put a new window there
// The arrangement belongs to a stored view in the show: layout() reads it, loadLayout() recalls it.
class Workspace : public QWidget {
    Q_OBJECT
public:
    static constexpr int kCols = 24;
    static constexpr int kRows = 16;   // rows on the main screen, above the encoders
    // Screens without the encoder bar get its height as extra rows, so cells stay the same size.
    static constexpr int kExtraRows = 4;
    int rows() const { return m_rows; }
    void setRows(int rows);

    explicit Workspace(AppContext *ctx, QWidget *parent = nullptr);

    // Adds a window. With no cells it goes into the largest free space, or becomes a tab of the
    // biggest window when the view is full. Returns the panel widget.
    QWidget *addPanel(const QString &typeId, const QRect &cells = QRect());
    bool hasPanel(const QString &typeId) const;    // any tab of this type
    bool raisePanel(const QString &typeId);        // brings its tab to the front
    // The windows as JSON: [{x, y, w, h, tabs: ["type" | {type, options}], current}], in grid cells.
    QJsonArray layout() const;
    void loadLayout(const QJsonArray &frames);   // replaces every window
    // Two layouts show the same windows (which tab is in front doesn't count).
    static bool sameLayout(const QJsonArray &a, const QJsonArray &b);
    void contentChanged() { notifyEdited(); }   // a window's own state (see panelStateChanged)

    QSize minimumSizeHint() const override { return {240, 160}; }

    // ---- used by frames ----
    QRect pixelRect(const QRect &cells) const;
    QPoint cellAt(const QPoint &pos) const;   // clamped to the grid
    bool isFree(const QRect &cells, const PanelFrame *ignore = nullptr) const;
    void frameEdited(PanelFrame *frame);      // moved, resized or tabs changed
    void frameEmptied(PanelFrame *frame);
    void tabDragMove(const QPoint &globalPos);
    void tabDrop(PanelFrame *from, int index, const QPoint &globalPos);
    void tabDragCancel();
    void setGuides(bool on);
    static PanelOptions *optionsOf(QWidget *panel);
    // Developer: apply options to the first window of a type, and optionally open its options editor.
    void debugOptions(const QString &typeId, const QJsonObject &options);   // developer: a window's options
    void showAddMenu(const QPoint &globalPos, PanelFrame *into = nullptr, const QPoint &cell = QPoint(-1, -1));

signals:
    void panelsChanged();
    void layoutEdited();   // moved, resized, added, closed, retabbed or options changed

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    PanelFrame *createFrame(const QRect &cells);
    QWidget *createPanel(const QString &typeId, const QJsonObject &options = {});
    QRect growFrom(const QPoint &cell, int maxW, int maxH, const PanelFrame *ignore = nullptr) const;
    QRect largestFree() const;
    PanelFrame *frameAt(const QPoint &pos) const;
    void notifyEdited();
    void clearFrames();
    void layoutFrames();
    void updateEmptyState();

    AppContext *m_ctx = nullptr;
    QList<PanelFrame *> m_frames;
    QLabel *m_empty = nullptr;
    bool m_guides = false;
    QRect m_dropPreview;          // cells, while dragging a tab over empty space
    PanelFrame *m_dropFrame = nullptr;
    bool m_restoring = false;
    int m_rows = kRows;
};

} // namespace lumora
