// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "paneloptions.h"
#include "types.h"

#include <QWidget>

#include <functional>

class QHBoxLayout;
class QLabel;
class QToolButton;

namespace lumora {

class ElidedLabel;

class AppContext;
struct ChannelDef;

// One rotary encoder: drag (up/right turns it up), scroll, double-click to type a value,
// right-click for named slots (gobos, colors, shutter modes) and Default.
class EncoderDial : public QWidget {
    Q_OBJECT
public:
    EncoderDial(AppContext *ctx, const QString &attribute, std::function<bool()> fine, QWidget *parent = nullptr);
    QSize sizeHint() const override { return {136, 132}; }
    void refresh();   // repaint if what it shows has changed (playbacks move values too)
    void turn(double delta);
    void setSlot(int n) { m_slot = n; }   // its place on the encoder bar, for MIDI (0: not on the bar)
    QString attribute() const { return m_attr; }
    QString valueText() const;   // as it shows it: degrees, percent, or the channel's own range
    void showRanges(const QPoint &global);   // the right-click menu: value, ranges, Default…

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

private:
    double currentValue(bool *programmed, QString *setName, int *fidOut) const;
    const ChannelDef *channel() const;   // the first selected fixture's, for ranges and degrees
    AppContext *m_ctx = nullptr;
    int m_slot = 0;
    QString m_attr;
    std::function<bool()> m_fine;
    bool m_drag = false;
    QPoint m_last;
    QString m_shown;   // what was last painted, to skip repaints when nothing moved
};

// Pan and tilt on one pad: every selected fixture is a dot, pan across, tilt up and
// down, each in its own range. Drag to move them all together (Shift or Fine for small moves).
class XyPad : public QWidget {
    Q_OBJECT
public:
    XyPad(AppContext *ctx, std::function<bool()> fine, QWidget *parent = nullptr);
    QSize sizeHint() const override { return {232, 132}; }
    void refresh();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    QVector<QPointF> points(QString *readout) const;   // 0..1 across and down, per fixture
    AppContext *m_ctx = nullptr;
    std::function<bool()> m_fine;
    bool m_drag = false;
    QPoint m_last;
    QString m_shown;
};

// Framing shutters drawn over the beam: the beam, four blades over it (1 top, 2 right,
// 3 bottom, 4 left) and the whole module's rotation. Drag a blade's number to put it in or out;
// in Ins+Rot mode its A and B ends swivel it, in A+B mode they move its two corners. Link moves
// other blades with it: All, Parallel (the opposite blade follows, shifting the opening) or
// Mirrored (the opposite blade closes in too).
class ShaperView : public QWidget {
    Q_OBJECT
public:
    explicit ShaperView(AppContext *ctx, QWidget *parent = nullptr);
    QSize sizeHint() const override { return {142, 142}; }
    void refresh();
    enum Mode { InsRot, AB };
    enum Link { None, All, Parallel, Mirrored };
    void setMode(Mode m);
    void setLink(Link l) { m_link = l; }
    Mode mode() const { return m_mode; }
    bool hasAB() const;

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    struct Blade {
        bool present = false;
        double a = 0, b = 0;      // insertion at each corner, 0..1 (Ins+Rot: both the insertion)
        double rot = 0.5;         // Ins+Rot: 0.5 straight
        QPointF pa, pb;           // the blade's edge at its two corners, in local coordinates
    };
    double value(const QString &attribute, double fallback, bool *has = nullptr) const;
    void readBlades(Blade (&bl)[4], double *systemRot) const;
    QPointF toLocal(const QPointF &widgetPos) const;
    void setBladeInsertion(int blade, double ins);
    // The gate's half size: room to turn the module round inside the view.
    double half() const { return (std::min(width(), height()) / 2.0 - 4) / 1.3; }

    AppContext *m_ctx = nullptr;
    Mode m_mode = InsRot;
    Link m_link = None;
    int m_dragBlade = -1;     // 0..3
    int m_dragEnd = 0;        // 0 the blade, 1 its A end, 2 its B end
    QString m_shown;
};

// The encoder strip along the bottom of the screen, laid out like a theatre console's
// encoder display: category buttons, a page of encoders, paging, Fine and Default.
// The one on the main screen answers MIDI (encoders by their place on the bar) and can be tucked
// away with its arrow; more can be added as windows, each with its own category and page.
class EncoderBar : public QWidget, public PanelOptions {
    Q_OBJECT
public:
    explicit EncoderBar(AppContext *ctx, QWidget *parent = nullptr, bool mainBar = true);
    QJsonObject saveOptions() const override;
    void loadOptions(const QJsonObject &o) override;
    void buildOptions(QuickEditor *editor) override;

protected:
    void resizeEvent(QResizeEvent *) override;

private:
    void setHidden(bool hidden);
    void placeArrow();
    void midiTurn(int encoder, double delta);
    void midiPush(int encoder);
    bool m_main = true;
    bool m_hidden = false;
    Feature m_preferred = Feature::Dimmer;   // a window's chosen category
    int m_perPage = 0;            // a window's own encoder count (0: Config)
    QWidget *m_body = nullptr;
    QWidget *m_tucked = nullptr;           // the strip left when the bar is tucked away
    ElidedLabel *m_tuckedSelection = nullptr;   // the selection, beside "Encoders" there
    QString m_selText;
    void showSelectionText();
    QToolButton *m_arrow = nullptr;
    QList<EncoderDial *> m_dials;   // the page's encoders, in order
    QStringList attributesFor(Feature f) const;
    bool hasBlades() const;
    void rebuild(bool force = false);
    int dialsPerPage() const;

    AppContext *m_ctx = nullptr;
    qint64 m_lastDefault = 0;
    Feature m_feature = Feature::Dimmer;
    int m_page = 0;
    bool m_fine = false;
    QList<QToolButton *> m_categories;
    ElidedLabel *m_selection = nullptr;
    QWidget *m_dialArea = nullptr;
    QHBoxLayout *m_dialLayout = nullptr;
    QLabel *m_pageLabel = nullptr;
    QToolButton *m_prev = nullptr;
    QToolButton *m_next = nullptr;
    QString m_signature;
};

} // namespace lumora
