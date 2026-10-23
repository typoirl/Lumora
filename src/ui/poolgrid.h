// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "paneloptions.h"

#include <QColor>
#include <QImage>
#include <QVector>
#include <QWidget>

class QLabel;
class QScrollArea;
class QHBoxLayout;
class QToolButton;
class QLineEdit;

namespace lumora {

class AppContext;

struct PoolItem {
    int id = 0;
    QString label;
    QColor tag;          // feature color strip
    QColor swatch;       // optional color chip (color presets)
    QImage image;        // optional picture (gobo presets)
    bool picture = false;   // the picture fills the tile, with the name under it (the image pool)
    QString badge;       // optional short marker in the corner (preset scope)
    QString tooltip;
    bool active = false; // e.g. the selected sequence
};

// A grid of numbered pool slots, the way consoles show groups, presets and sequences. The
// tiles fill the whole window: set how many wide and tall (0 fits as many of the chosen tile size
// as there's room for) and the number the first tile has. Scroll to see the slots after them.
// Empty slots stay visible so you can store straight into them.
class PoolTiles : public QWidget {
    Q_OBJECT
public:
    explicit PoolTiles(QWidget *parent = nullptr);
    void setItems(const QVector<PoolItem> &items);
    void setStoreMode(bool on);
    void setTileSize(int size);   // 0 small, 1 medium, 2 large: the size "fit" aims for
    int tileSize() const { return m_size; }
    void setGrid(int columns, int rows);   // 0 = fit
    int gridColumns() const { return m_cols; }
    int gridRows() const { return m_rows; }
    // Where the pool is: the slot in the top-left corner. Any number; scrolling moves a row at a
    // time and snaps to rows that start at 1 (with 5 wide: 1, 6, 11…).
    void setPosition(int slot);
    int position() const { return m_first; }
    void scrollRows(int rows);   // positive: further on
    bool atTop() const { return m_first <= 1; }
    void setLocked(bool on) { m_locked = on; }   // the position stays put: no scrolling
    bool locked() const { return m_locked; }
    QSize sizeHint() const override { return {400, 200}; }
    QSize minimumSizeHint() const override { return {60, 40}; }

signals:
    void tileClicked(int id, bool occupied, Qt::KeyboardModifiers mods);
    void tileMenu(int id, bool occupied, const QPoint &globalPos);
    void scrolled();   // the first slot on screen changed

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void leaveEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    int columns() const;
    int rows() const;
    int visibleCount() const { return columns() * rows(); }
    int idAt(int index) const { return m_first + index; }
    QRect tileRect(int index) const;
    int indexAt(const QPoint &p) const;

    QVector<PoolItem> m_items;
    bool m_store = false;
    int m_size = 1, m_tileW = 86, m_tileH = 58;
    int m_cols = 0, m_rows = 0;   // 0 = fit
    int m_first = 1;              // the slot in the top-left corner
    bool m_locked = false;
    int m_hover = -1;
};

// Titled panel holding a PoolTiles grid, with room for header controls. The header starts with
// ▲ / the slot in the top-left corner (type one to go there) / ▼. Right-click the tab to set the
// grid (tiles wide × tall), the tile size, and to lock the position (which hides the header bar).
class PoolPanel : public QWidget, public PanelOptions {
    Q_OBJECT
public:
    PoolPanel(const QString &title, QWidget *parent = nullptr);
    QJsonObject saveOptions() const override;
    void loadOptions(const QJsonObject &o) override;
    void buildOptions(QuickEditor *editor) override;
    PoolTiles *tiles() const { return m_tiles; }
    QHBoxLayout *headerLayout() const { return m_header; }
    // Empty slots light up as Store targets while Store is on.
    void followStore(AppContext *ctx);

private:
    void syncNav();
    PoolTiles *m_tiles = nullptr;
    QHBoxLayout *m_header = nullptr;
    QWidget *m_headerBox = nullptr;
    QToolButton *m_up = nullptr;
    QToolButton *m_down = nullptr;
    QLineEdit *m_at = nullptr;
};

} // namespace lumora
