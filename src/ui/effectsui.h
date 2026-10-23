// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "effects.h"
#include "poolgrid.h"

#include <QDialog>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QScrollArea;
class QSpinBox;
class QVBoxLayout;

namespace lumora {

class AppContext;

// A small picture of a shape, for menus and lists.
QPixmap shapeIcon(const Shape &shape, int w = 40, int h = 22);

// ---- Effects pool ------------------------------------------------------------
// Tap an effect to run it on the selection. Below the pool, every effect running in the
// programmer gets live Rate and Size controls and a Stop button.
class EffectsPanel : public PoolPanel {
    Q_OBJECT
public:
    explicit EffectsPanel(AppContext *ctx, QWidget *parent = nullptr);

private:
    void refresh();
    void refreshRunning();
    void createAt(int id, const QPoint &globalPos);

    AppContext *m_ctx = nullptr;
    QWidget *m_running = nullptr;
    QVBoxLayout *m_runningLayout = nullptr;
    QString m_runningSignature;
};

// ---- Live preview ------------------------------------------------------------
// One row per fixture across one cycle, painted with what the effect does to it, and a
// playhead. Uses the selection (or the first fixtures in the patch) so you see the real thing.
class EffectPreview : public QWidget {
    Q_OBJECT
public:
    EffectPreview(AppContext *ctx, QWidget *parent = nullptr);
    void setEffect(const Effect &e);
    QSize sizeHint() const override { return {420, 170}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void renderCache();
    AppContext *m_ctx = nullptr;
    Effect m_effect;
    double m_playhead = 0;
    QImage m_cache;           // the rows, re-rendered when something changes (not every frame)
    qint64 m_cacheTime = 0;
    bool m_dirty = true;
};

// ---- Shape editor ---------------------------------------------------------------
class ShapeCanvas : public QWidget {
    Q_OBJECT
public:
    explicit ShapeCanvas(QWidget *parent = nullptr);
    Shape shape;
    QSize sizeHint() const override { return {420, 220}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;

private:
    QRectF plot() const;
    QPointF toWidget(const ShapePoint &p) const;
    ShapePoint fromWidget(const QPointF &p) const;
    int hit(const QPointF &p) const;
    int m_drag = -1;
};

class ShapeEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShapeEditorDialog(const Shape &shape, QWidget *parent = nullptr);
    Shape result() const;

private:
    ShapeCanvas *m_canvas = nullptr;
    QLineEdit *m_name = nullptr;
    QCheckBox *m_smooth = nullptr;
};

// ---- Library -----------------------------------------------------------------------
class EffectLibraryDialog : public QDialog {
    Q_OBJECT
public:
    explicit EffectLibraryDialog(QWidget *parent = nullptr);
    Effect chosen() const;

private:
    QListWidget *m_list = nullptr;
    QVector<Effect> m_templates;
};

// ---- Editor --------------------------------------------------------------------------
class EffectEditor : public QWidget {
    Q_OBJECT
public:
    explicit EffectEditor(AppContext *ctx, QWidget *parent = nullptr);
    void edit(int effectId);

private:
    void refreshPicker();
    void loadEffect();
    void refreshLayers();
    void buildLayerDetails();
    void commit();   // writes the working copy back to the show (effects update live)
    QWidget *pointEditor(int index);
    QStringList candidateAttributes() const;

    AppContext *m_ctx = nullptr;
    int m_effectId = 0;
    Effect m_effect;
    int m_layer = 0;
    int m_pendingScroll = -1; // where the rebuilt details page goes back to, once it's laid out
    int m_builtLayer = -1;    // the layer and effect the details page was last built for
    int m_builtEffect = 0;
    bool m_loading = false;

    QLabel *m_picker;   // which effect is open
    QLineEdit *m_name = nullptr;
    QDoubleSpinBox *m_cycle = nullptr;
    QLabel *m_bpm = nullptr;
    QComboBox *m_rateMaster = nullptr;
    QComboBox *m_spread = nullptr;
    QCheckBox *m_axisX = nullptr;   // runs across the selection matrix
    QCheckBox *m_axisY = nullptr;   // runs down it
    QListWidget *m_layers = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_details = nullptr;
    QVBoxLayout *m_detailsLayout = nullptr;
    EffectPreview *m_preview = nullptr;
    QWidget *m_body = nullptr;
    QLabel *m_empty = nullptr;
};

} // namespace lumora
