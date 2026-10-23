// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "effects.h"
#include "types.h"
#include "paneloptions.h"

#include <QAbstractTableModel>
#include <QIcon>
#include <QColor>
#include <QSet>
#include <QWidget>

class QScrollArea;
class QTableView;

namespace lumora {

class AppContext;
class Show;
struct FixtureMode;

// Best guess at the color a fixture emits, from mixing channels or a wheel slot name.
// Returns an invalid color when the values carry no color information.
QColor approximateColor(const RawAttr &values, const FixtureMode *mode);
QColor colorForName(const QString &name);
// A sequence's appearance as a color to draw: its own, or with "Follow fixtures" the color its
// fixtures show (on stage while it plays, else its first cue's). Invalid when it has none.
QColor sequenceAppearance(const Show *show, int sequenceId);

class FixtureSheetModel : public QAbstractTableModel {
    Q_OBJECT
public:
    // Value columns follow the feature order so column - Dimmer + 1 is the Feature.
    enum Column { Fid, Name, Patch, Dimmer, Position, Gobo, Color, Beam, Focus, Control, ColumnCount };
    explicit FixtureSheetModel(AppContext *ctx, QObject *parent = nullptr);

    int rowCount(const QModelIndex & = {}) const override { return m_fids.size(); }
    int columnCount(const QModelIndex & = {}) const override { return ColumnCount; }
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

    int fidAt(int row) const { return m_fids.value(row); }
    int rowOf(int fid) const { return m_fids.indexOf(fid); }
    const QVector<int> &fids() const { return m_fids; }
    void reloadPatch(bool force = false);   // force: reset even when the rows are the same
    void refreshValues();

    // Which fixtures are listed.
    enum class Filter { All, Patched, Selected, Programmed };
    Filter filter() const { return m_filter; }
    void setFilter(Filter f);

    // Pixel fixtures nest: the fixture's row, then its cells, indented, when expanded.
    bool hasCells(int fid) const;   // has subfixtures nested under it
    int depthOf(int fid) const;     // 0 a fixture, 1 a head or pixel, 2 a part of a head
    bool isExpanded(int master) const { return m_expanded.contains(master); }
    void setExpanded(int master, bool on);
    QList<int> expanded() const { return m_expanded.values(); }
    void setExpandedAll(const QList<int> &masters);
    // A fixture whose channels are all in its cells: its row is just a label over them.
    bool isLabelRow(int fid) const;
    // Collapsed, with some of its cells selected (shown with a tint).
    bool hidesSelection(int fid) const;

    QString cellText(int fid, Feature f) const;
    // Whether a feature's values are in the programmer, and away from where they rest.
    void cellState(int fid, Feature f, bool *programmed, bool *changed) const;

private:
    // What a row shows, worked out once per refresh (not once per paint and role): a refresh
    // repaints only the rows that changed, and nothing when the stage stands still.
    struct Row {
        QString text[7];
        quint8 state[7] = {};   // 1: programmed, 2: away from its default
        QColor color;           // the Color cell's swatch
        double gobo = -1;       // Gobo1's value, -1 without one
        QIcon goboIcon;
        bool operator==(const Row &o) const;
    };
    Row computeRow(int fid) const;
    const Row &row(int fid) const;
    mutable QHash<int, Row> m_rows;
    QString computeCellText(int fid, Feature f) const;
    void computeCellState(int fid, Feature f, bool *programmed, bool *changed) const;
    // The preset a running playback gives this attribute, when its value is still that preset's.
    QString playbackPresetName(int fid, Feature f) const;
    QString effectNames(int fid, Feature f) const;   // effects running on this fixture's feature
    void collectPlaybacks();

    AppContext *m_ctx = nullptr;
    QVector<int> m_fids;
    Filter m_filter = Filter::All;
    QSet<int> m_expanded;
    struct Source {
        int sequenceId = 0;
        int cueIndex = -1;
        RawTable to, current;
    };
    QVector<Source> m_sources;   // running playbacks, newest first (refreshed with the values)
    mutable QHash<qint64, QVector<EffectInstance>> m_effectCache;   // (sequence, cue) → its effects
    mutable QHash<qint64, QMap<int, QHash<QString, QPair<int, int>>>> m_presetCache;   // (sequence, cue) →
};

// The fixtures as tiles, one per fixture: ID, name, level, color,
// gobo and position at a glance. Click to add to the selection (again to drop it); Shift adds a range.
class FixtureTiles : public QWidget {
    Q_OBJECT
public:
    FixtureTiles(AppContext *ctx, FixtureSheetModel *model, QWidget *parent = nullptr);
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;

signals:
    void expandedChanged();   // a fixture's subfixtures opened or closed (saved with the view)

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    int columns(int w) const;
    QRect tileRect(int index) const;
    static QRectF arrowRect(const QRect &tile);
    AppContext *m_ctx = nullptr;
    FixtureSheetModel *m_model = nullptr;
    int m_anchor = -1;
};

class FixtureSheet : public QWidget, public PanelOptions {
    Q_OBJECT
public:
    explicit FixtureSheet(AppContext *ctx, QWidget *parent = nullptr);

    // Right-click the tab: which fixtures (all, patched, selected, in the programmer) and
    // how (Table or Tiles).
    QJsonObject saveOptions() const override;
    void loadOptions(const QJsonObject &o) override;
    void buildOptions(QuickEditor *editor) override;

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    void syncSelectionFromShow();
    void updateTitle();
    void setTiles(bool on);
    AppContext *m_ctx = nullptr;
    FixtureSheetModel *m_model = nullptr;
    QTableView *m_view = nullptr;
    QScrollArea *m_tileScroll = nullptr;
    FixtureTiles *m_tiles = nullptr;
    bool m_syncing = false;
};

} // namespace lumora
