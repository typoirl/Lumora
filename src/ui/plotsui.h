// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "paneloptions.h"
#include "plots.h"
#include "poolgrid.h"

#include <QHash>
#include <QIcon>
#include <QPixmap>
#include <QWidget>

class QLabel;
class QMenu;
class QToolButton;

namespace lumora {

class AppContext;

// The pool of plots: tap one to show it in the plot windows, Store and tap a slot to make one,
// right-click to rename, open or delete it.
class PlotsPanel : public PoolPanel {
    Q_OBJECT
public:
    explicit PlotsPanel(AppContext *ctx, QWidget *parent = nullptr);

private:
    void refresh();
    AppContext *m_ctx = nullptr;
};

// The sheet itself: fixtures showing their intensity and color, pool objects that act like their
// pool tiles, text and pictures. Tap fixtures to add them to the selection (again to take them
// out), drag a box round several. In Edit, drag things about, resize them and add more.
class PlotCanvas : public QWidget {
    Q_OBJECT
public:
    PlotCanvas(AppContext *ctx, QWidget *parent = nullptr);
    void setPlotId(int id);
    int plotId() const { return m_plotId; }
    void setEditing(bool on);
    bool editing() const { return m_editing; }
    void zoomBy(double factor);   // around the middle; nothing while the zoom is locked
    void fit();
    QPointF viewCenter() const;   // in plot units
    double zoom() const { return view().zoom; }
    bool zoomLocked() const { return view().locked; }
    void setZoomLocked(bool on);
    bool hasView(int plotId) const { return m_views.contains(plotId); }
    // Each window keeps its own zoom, position and lock per plot; the view stores them.
    QJsonObject saveViews() const;
    void loadViews(const QJsonObject &o);

    // Align and distribute, like a drawing program. Items in a group move as one.
    enum AlignTo { LastPicked, FirstPicked, Biggest, Smallest, Selection, WholePlot };
    enum Align { Left, CenterX, Right, Top, CenterY, Bottom,
                 SpreadLeft, SpreadCenterX, SpreadRight, SpreadGapX, SpreadTop, SpreadCenterY, SpreadBottom, SpreadGapY };
    void align(Align how, AlignTo to);
    int pickedUnits() const;   // groups count once
    static QIcon alignIcon(Align how);
    static QString alignName(Align how);

    static QPixmap renderImage(const QByteArray &bytes, const QSize &size, const QString &tint);

signals:
    void zoomChanged();
    void viewChanged();                          // zoom, position or lock moved (the view notices)
    void backgroundMenu(const QPoint &global);   // right-click on an empty part

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

private:
    const Plot *plot() const;
    QPointF toPlot(const QPointF &w) const;
    QPointF toWidget(const QPointF &p) const;
    QRectF toWidget(const QRectF &r) const;
    int itemAt(const QPointF &w, bool includeBackground) const;
    void tapObject(const QString &object);
    void tapLink(const QString &link);                  // what a linked item stands for
    QString normalizeLink(const QString &text) const;   // "Color.1" -> "Preset 4.1", "101" -> "Fixture 101"; empty if not one
    void itemMenu(int index, const QPoint &global);
    void commit(const Plot &p);
    void setView(double zoom, double panX, double panY);
    bool objectActive(const QString &object) const;
    QString objectTitle(const QString &object, QString *kind) const;

    struct ViewState {
        double zoom = 1.0, panX = 0, panY = 0;
        bool locked = false;
    };
    const ViewState &view() const;   // this plot's, made from the plot's own on first use

    AppContext *m_ctx = nullptr;
    int m_plotId = 0;
    bool m_editing = false;
    mutable QHash<int, ViewState> m_views;
    // Dragging
    enum Drag { None, Lasso, Move, Resize, Pan };
    Drag m_drag = None;
    QPointF m_pressPos, m_lastPos;
    QVector<int> m_picked;            // Edit: the items picked
    QVector<QRectF> m_startRects;     // their rects when the drag started
    Plot m_dragPlot;                  // the plot while dragging (committed on release)
    bool m_dragging = false;
    mutable QHash<QString, QPixmap> m_imageCache;
};

// A plot window: the sheet with Edit, Add, zoom buttons and the zoom lock.
class PlotView : public QWidget, public PanelOptions {
    Q_OBJECT
public:
    explicit PlotView(AppContext *ctx, QWidget *parent = nullptr);
    QJsonObject saveOptions() const override;
    void loadOptions(const QJsonObject &o) override;
    void buildOptions(QuickEditor *editor) override;

private:
    void follow();
    void refreshHeader();
    void addMenu();
    void addPicture(bool background);
    void fillAddMenu(QMenu *menu);
    void fillAlignMenu(QMenu *menu);
    void backgroundMenu(const QPoint &global);
    void setBarHidden(bool hidden);
    AppContext *m_ctx = nullptr;
    QWidget *m_head = nullptr;
    QToolButton *m_align = nullptr;
    bool m_barHidden = false;
    PlotCanvas *m_canvas = nullptr;
    QLabel *m_title = nullptr;
    QToolButton *m_edit = nullptr;
    QToolButton *m_add = nullptr;
    QToolButton *m_minus = nullptr;
    QToolButton *m_plus = nullptr;
    QToolButton *m_fit = nullptr;
    QToolButton *m_lock = nullptr;
    QLabel *m_zoomLabel = nullptr;
    int m_pinned = 0;   // 0: shows the plot chosen last (pool, Plot 3); else always this one
};

// Ready-made icons for plots (SVG): name and file bytes.
QVector<QPair<QString, QByteArray>> builtinPlotIcons();

} // namespace lumora
