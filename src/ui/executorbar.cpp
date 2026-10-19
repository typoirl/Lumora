// SPDX-License-Identifier: GPL-3.0-or-later
#include "executorbar.h"
#include "cueview.h"

#include "appcontext.h"
#include "quickedit.h"
#include "theme.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDateTime>
#include <QInputDialog>
#include <QLineEdit>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QResizeEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QLabel>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace lumora {

namespace {
constexpr int kPad = 6;
constexpr int kHeaderH = 50;
constexpr int kButtonH = 26;
constexpr int kButtonGap = 4;

QString faderLabel(FaderFn f)
{
    switch (f) {
    case FaderFn::Master: return QObject::tr("Master");
    case FaderFn::XFade: return QObject::tr("X-fade");
    case FaderFn::Temp: return QObject::tr("Temp");
    }
    return QString();
}

} // namespace

ExecutorStrip::ExecutorStrip(AppContext *ctx, int number, QWidget *parent)
    : QWidget(parent), m_ctx(ctx), m_number(number)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
}

ExecKind ExecutorStrip::kind() const
{
    Show *show = m_ctx->show;
    return executorKind(m_number, show->executor(show->currentPage(), m_number));
}

QSize ExecutorStrip::sizeHint() const
{
    switch (kind()) {
    case ExecKind::Button: return {112, 110};
    case ExecKind::Encoder: return {112, 170};
    case ExecKind::Fader: break;
    }
    return {112, 248};
}

QSize ExecutorStrip::minimumSizeHint() const
{
    switch (kind()) {
    case ExecKind::Button: return {80, kPad + kHeaderH + 4 + kButtonH + kPad};   // the key must fit
    case ExecKind::Encoder: return {80, 118};
    case ExecKind::Fader: break;
    }
    return {92, 220};
}

int ExecutorStrip::buttonCount() const { return 1; }   // every executor has one key (Go by default)

QRect ExecutorStrip::headerRect() const { return {kPad, kPad, width() - 2 * kPad, kHeaderH}; }

QRect ExecutorStrip::buttonRect(int i) const
{
    switch (kind()) {
    case ExecKind::Button: {
        // One key fills everything under the header.
        const int top = kPad + kHeaderH + 4;
        return i == 0 ? QRect(kPad, top, width() - 2 * kPad, std::max(kButtonH, height() - top - kPad)) : QRect();
    }
    case ExecKind::Encoder: {
        if (i != 0)
            return {};
        // Short and wide (a row of encoders over the faders): the push key sits beside the knob.
        const QRect area(kPad, kPad + kHeaderH + 2, width() - 2 * kPad, height() - 2 * kPad - kHeaderH - 2);
        if (area.width() >= area.height() * 1.3) {
            const int knob = std::min(area.height(), int(area.width() * 0.55));
            const int w = area.width() - knob - 6;
            return {area.right() + 1 - w, area.top() + area.height() / 2 + 2, w, kButtonH};
        }
        return QRect(kPad, height() - kPad - kButtonH, width() - 2 * kPad, kButtonH);
    }
    case ExecKind::Fader: break;
    }
    const int top = kPad + kHeaderH + 8;
    return {kPad, top + i * (kButtonH + kButtonGap), width() - 2 * kPad, kButtonH};
}

QRect ExecutorStrip::faderRect() const
{
    switch (kind()) {
    case ExecKind::Button: return {};
    case ExecKind::Encoder: {
        const int top = kPad + kHeaderH + 2;
        const QRect b = buttonRect(0);
        if (b.left() <= kPad)   // stacked: knob above the key
            return {kPad, top, width() - 2 * kPad, std::max(10, b.top() - 4 - top)};
        return {kPad, top, std::max(10, b.left() - 6 - kPad), height() - kPad - top};
    }
    case ExecKind::Fader: break;
    }
    const int top = buttonRect(buttonCount() - 1).bottom() + 10;
    return {kPad, top, width() - 2 * kPad, height() - top - kPad};
}

QRect ExecutorStrip::trackRect() const
{
    QRect f = faderRect();
    return {f.left() + 10, f.top() + 4, f.width() - 20, f.height() - 26};
}

QRectF ExecutorStrip::knobRect() const
{
    const QRect f = faderRect();
    const double d = std::max(10, std::min(f.width() - 8, f.height() - 4));
    return {f.center().x() - d / 2 + 0.5, f.top() + (f.height() - d) / 2, d, d};
}

int ExecutorStrip::regionAt(const QPoint &p) const
{
    if (headerRect().contains(p))
        return Header;
    for (int i = 0; i < buttonCount(); ++i)
        if (buttonRect(i).contains(p))
            return i;
    if (faderRect().contains(p))
        return Fader;
    return None;
}

MidiTarget ExecutorStrip::targetFor(int region) const
{
    MidiTarget t;
    t.page = 0;   // controllers follow the current page
    t.executor = m_number;
    if (region == Fader) {
        t.kind = MidiTarget::ExecutorFader;
    } else {
        t.kind = MidiTarget::ExecutorButton;
        t.button = region;
    }
    return t;
}

bool ExecutorStrip::isBound(int region) const
{
    return !m_ctx->midi->bindingsFor(targetFor(region)).isEmpty();
}

void ExecutorStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    Show *show = m_ctx->show;
    const Executor *e = show->executor(show->currentPage(), m_number);
    const Sequence *seq = e && e->sequenceId ? show->sequence(e->sequenceId) : nullptr;
    const bool on = e && e->pb().isOn();
    const bool learnMode = m_ctx->midi->learnMode();
    const bool storeMode = m_ctx->storeArmed();
    const MidiTarget armed = m_ctx->midi->armedTarget();
    const bool anyArmed = m_ctx->midi->isArmed();
    const double pulse = 0.55 + 0.45 * std::sin(QDateTime::currentMSecsSinceEpoch() / 160.0);

    // Card
    QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(on ? theme::mix(theme::line, theme::live, 0.35) : theme::line, 1));
    p.setBrush(theme::panel);
    p.drawRoundedRect(card, 8, 8);

    // The sequence's appearance: a stripe along the top of the card.
    if (seq && !seq->color.isEmpty()) {
        const QColor sc(seq->color);
        p.setPen(Qt::NoPen);
        p.setBrush(sc);
        QPainterPath top;
        top.addRoundedRect(card.adjusted(1, 1, -1, 0), 7, 7);
        QPainterPath clip;
        clip.addRect(QRectF(card.left(), card.top(), card.width(), 4));
        p.drawPath(top.intersected(clip));
    }

    // Header
    QRect h = headerRect();
    if (storeMode) {
        p.setPen(QPen(theme::programmer, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(h).adjusted(-2, -2, 2, 2), 5, 5);
    }
    p.setFont(theme::uiFont(8));
    p.setPen(theme::muted);
    p.drawText(h.adjusted(2, 0, -2, 0), Qt::AlignLeft | Qt::AlignTop, QString::number(m_number));
    const ExecKind k = executorKind(m_number, e);
    if (e && !e->isEmpty() && k != ExecKind::Button)
        p.drawText(h.adjusted(2, 0, -2, 0), Qt::AlignRight | Qt::AlignTop, faderLabel(e->faderFn));
    const RateMaster *rate = e && e->rateMaster ? show->rateMaster(e->rateMaster) : nullptr;
    p.setFont(theme::uiFont(10, QFont::DemiBold));
    p.setPen(seq || rate ? theme::text : theme::faint);
    const QString title = seq ? seq->name : rate ? rate->displayName() : tr("Empty");
    QRect titleRect(h.left() + 2, h.top() + 14, h.width() - 4, 18);
    if (seq) {
        // The sequence number, small and grey like the executor number, right after the name.
        const QString seqNo = QString::number(seq->id);
        const QFont small = theme::uiFont(8);
        const int noW = QFontMetrics(small).horizontalAdvance(seqNo) + 4;
        const QString shown = p.fontMetrics().elidedText(title, Qt::ElideRight, std::max(0, titleRect.width() - noW));
        const int nameW = p.fontMetrics().horizontalAdvance(shown);
        p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, shown);
        const int baseline = titleRect.top() + (titleRect.height() + p.fontMetrics().ascent() - p.fontMetrics().descent()) / 2;
        p.setFont(small);
        p.setPen(theme::muted);
        p.drawText(QPoint(titleRect.left() + nameW + 4, baseline), seqNo);
    } else {
        p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(title, Qt::ElideRight, titleRect.width()));
    }
    if (rate) {
        // A rate master: its mode where the cue would be, its value in the live color.
        QRect line(h.left() + 2, h.top() + 33, h.width() - 4, 16);
        p.setFont(theme::uiFont(9));
        p.setPen(rate->frozen ? theme::muted : theme::live);
        p.drawText(line, Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(rate->valueText(), Qt::ElideRight, line.width()));
    }

    QRect cueRect(h.left() + 2, h.top() + 33, h.width() - 4, 16);
    p.setFont(theme::uiFont(9));
    if (seq && on && e->pb().cueIndex < seq->cues.size()) {
        const Cue &c = seq->cues[e->pb().cueIndex];
        p.setPen(theme::live);
        QString numText = c.displayNumber();
        int nw = p.fontMetrics().horizontalAdvance(numText) + 6;
        p.drawText(cueRect, Qt::AlignLeft | Qt::AlignVCenter, numText);
        p.setPen(theme::text);
        QRect nameRect = cueRect.adjusted(nw, 0, 0, 0);
        p.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(c.name, Qt::ElideRight, nameRect.width()));
    } else if (seq) {
        p.setPen(theme::faint);
        p.drawText(cueRect, Qt::AlignLeft | Qt::AlignVCenter, seq->cues.isEmpty() ? tr("No cues") : tr("Off"));
    }
    if (on && e->pb().fading) {
        const double prog = e->pb().xfadeTarget >= 0 ? e->pb().elapsed : e->pb().progress();
        QRectF bar(h.left() + 2, h.bottom() - 1, (h.width() - 4) * prog, 2);
        p.setPen(Qt::NoPen);
        p.setBrush(e->pb().paused ? theme::muted : theme::live);
        p.drawRoundedRect(bar, 1, 1);
    }

    auto drawLearnOutline = [&](const QRect &r, int region) {
        if (!learnMode && !(anyArmed && armed == targetFor(region)))
            return;
        const bool isArmed = anyArmed && armed == targetFor(region);
        QColor c = theme::learn;
        if (isArmed)
            c.setAlphaF(float(pulse));
        else
            c.setAlphaF(0.45f);
        p.setPen(QPen(c, isArmed ? 2 : 1, isArmed ? Qt::SolidLine : Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(r).adjusted(-1.5, -1.5, 1.5, 1.5), 5, 5);
    };
    auto drawMidiDot = [&](const QRect &r, int region) {
        if (!isBound(region))
            return;
        p.setPen(Qt::NoPen);
        p.setBrush(theme::learn);
        p.drawEllipse(QPointF(r.right() - 5, r.top() + 5), 2.5, 2.5);
    };

    // Buttons
    const int nButtons = buttonCount();
    for (int i = 0; i < nButtons; ++i) {
        QRect b = buttonRect(i);
        const ButtonFn fn = e ? e->buttons[i] : k == ExecKind::Encoder ? ButtonFn::Toggle : ButtonFn::Go;
        const bool lit = rate ? (i == 2 ? rate->frozen : e->buttonDown[i]) : e && executorButtonLit(*e, i);
        const bool down = m_pressed == i;
        p.setPen(QPen(lit ? theme::live : theme::line, 1));
        p.setBrush(down ? QColor(0x36, 0x3d, 0x49) : lit ? QColor(0x3a, 0x32, 0x20) : theme::raised);
        p.drawRoundedRect(QRectF(b).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        p.setFont(theme::uiFont(k == ExecKind::Button && b.height() > 44 ? 11 : 9, QFont::Medium));
        if (rate) {
            static const QStringList rateButtons = {tr("Learn"), tr("Reset"), tr("Freeze")};
            p.setPen(lit ? theme::live : theme::text);
            p.drawText(b, Qt::AlignCenter, rateButtons.value(i));
        } else {
            p.setPen(fn == ButtonFn::None || !seq ? theme::faint : lit ? theme::live : theme::text);
            p.drawText(b, Qt::AlignCenter, fn == ButtonFn::None ? QStringLiteral("–") : buttonFnName(fn));
        }
        drawMidiDot(b, i);
        drawLearnOutline(b, i);
    }
    if (k == ExecKind::Button)
        return;   // a key has no master

    // Fader
    QRect tr = trackRect();
    const double level = rate ? rate->faderPosition() : e ? e->fader : (1.0);
    double glow = 0.0;
    if (e && on) {
        double maxDim = 0.0;
        for (const RawAttr &a : e->pb().current)
            maxDim = std::max(maxDim, a.value("Dimmer", 0.0));
        glow = Engine::sequenceMaster(*show, *e, show->anySwopHeld()) * maxDim;
    }
    if (k == ExecKind::Encoder) {
        // A knob: the arc is the master, lit with what reaches the stage.
        const QRectF kr = knobRect();
        const double thick = std::max(4.0, kr.width() * 0.09);
        const QRectF ring = kr.adjusted(thick / 2 + 1, thick / 2 + 1, -thick / 2 - 1, -thick / 2 - 1);
        constexpr int kStart = 225 * 16, kSpan = -270 * 16;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0x10, 0x13, 0x18), thick, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(ring, kStart, kSpan);
        QColor arc = glow > 0.001 ? theme::mix(theme::muted, theme::live, std::min(1.0, 0.35 + glow)) : (seq ? theme::muted : theme::line);
        p.setPen(QPen(arc, thick, Qt::SolidLine, Qt::RoundCap));
        if (level > 0.001)
            p.drawArc(ring, kStart, int(kSpan * level));
        // Cap with a pointer
        const QRectF cap = ring.adjusted(thick * 1.2, thick * 1.2, -thick * 1.2, -thick * 1.2);
        p.setPen(QPen(theme::line, 1));
        p.setBrush(m_dragging ? theme::mix(theme::raised, theme::text, 0.12) : theme::raised);
        p.drawEllipse(cap);
        // The pointer rides the ring, so the level can sit in the middle.
        const double ang = (225.0 - 270.0 * level) * 3.14159265358979 / 180.0;
        const double r = ring.width() / 2;
        p.setPen(Qt::NoPen);
        p.setBrush(seq || rate ? theme::text : theme::faint);
        p.drawEllipse(ring.center() + QPointF(std::cos(ang), -std::sin(ang)) * r, thick * 0.7, thick * 0.7);
        p.setFont(theme::uiFont(9));
        p.setPen(glow > 0.001 ? theme::live : theme::muted);
        const QString pct = rate ? rate->valueText() : QStringLiteral("%1%").arg(std::lround(level * 100));
        const QRect b0 = buttonRect(0);
        if (b0.left() > kr.right())   // beside the knob: the level sits over the key
            p.drawText(QRect(b0.left(), b0.top() - 22, b0.width(), 18), Qt::AlignCenter, pct);
        else if (cap.width() >= p.fontMetrics().horizontalAdvance(pct) + 10)
            p.drawText(cap, Qt::AlignCenter, pct);
        drawMidiDot(kr.toAlignedRect(), Fader);
        drawLearnOutline(kr.toAlignedRect(), Fader);
        return;
    }
    p.setPen(QPen(theme::line, 1));
    p.setBrush(QColor(0x10, 0x13, 0x18));
    p.drawRoundedRect(QRectF(tr).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);

    const double fillH = (tr.height() - 4) * level;
    QRectF fill(tr.left() + 2, tr.bottom() - 2 - fillH, tr.width() - 4, fillH);
    p.setPen(Qt::NoPen);
    p.setBrush(rate ? QColor(0x2c, 0x33, 0x3e) : !seq ? QColor(0x1d, 0x21, 0x28)
                    : seq->color.isEmpty() ? QColor(0x2c, 0x33, 0x3e) : theme::mix(QColor(0x2c, 0x33, 0x3e), QColor(seq->color), 0.28));
    p.drawRoundedRect(fill, 4, 4);
    if (glow > 0.001) {
        QLinearGradient g(fill.bottomLeft(), fill.topLeft());
        QColor lo = theme::live, hi = theme::live;
        lo.setAlphaF(float(0.25 + 0.65 * glow));
        hi.setAlphaF(float(0.10 + 0.35 * glow));
        g.setColorAt(0, lo);
        g.setColorAt(1, hi);
        p.setBrush(g);
        p.drawRoundedRect(fill, 4, 4);
    }
    // Handle
    const double hy = tr.bottom() - 2 - fillH;
    p.setBrush(seq || rate ? theme::text : theme::faint);
    p.drawRoundedRect(QRectF(tr.left() - 3, hy - 3, tr.width() + 6, 6), 3, 3);

    QRect f = faderRect();
    p.setFont(theme::uiFont(9));
    p.setPen(glow > 0.001 ? theme::live : theme::muted);
    p.drawText(QRect(f.left(), tr.bottom() + 4, f.width(), 18), Qt::AlignCenter,
               rate ? p.fontMetrics().elidedText(rate->valueText(), Qt::ElideRight, f.width())
                    : QStringLiteral("%1%").arg(std::lround(level * 100)));
    drawMidiDot(tr, Fader);
    drawLearnOutline(tr, Fader);
}

void ExecutorStrip::setFaderFromY(int y)
{
    QRect tr = trackRect();
    double v = 1.0 - double(y - tr.top() - 2) / double(tr.height() - 4);
    Executor &e = m_ctx->show->ensureExecutor(m_ctx->show->currentPage(), m_number);
    m_ctx->show->setFader(e, std::clamp(v, 0.0, 1.0));
    update();
}

void storeOntoExecutor(AppContext *ctx, int page, int number)
{
    Show *show = ctx->show;
    Executor &e = show->ensureExecutor(page, number);
    int seqId = e.sequenceId;
    if (seqId == 0)
        seqId = show->sequences().isEmpty() ? 1 : show->sequences().lastKey() + 1;
    const bool assign = e.sequenceId == 0;
    double stored = 0;
    if (show->storeCue(seqId, -1, Show::StoreMode::Merge, &stored)) {
        if (assign)
            show->assignSequence(seqId, page, number);
        Cue c;
        c.number = stored;
        emit ctx->status(QObject::tr("Cue %1 stored in Sequence %2 on Executor %3").arg(c.displayNumber()).arg(seqId).arg(number));
    }
    ctx->setStoreArmed(false);
}

void ExecutorStrip::storeHere()
{
    storeOntoExecutor(m_ctx, m_ctx->show->currentPage(), m_number);
}

// ===========================================================================
// Store picker
// ===========================================================================

StoreExecutorPicker::StoreExecutorPicker(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_StyledBackground);
    setObjectName("storePicker");
    setStyleSheet(QStringLiteral("QWidget#storePicker { background: %1; border-top: 2px solid %2; }")
                      .arg(theme::panel.name(), theme::programmer.name()));
    setFixedHeight(minimumSizeHint().height());
    connect(ctx->show, &Show::changed, this, [this](Show::Parts p) {
        if (p & (Show::ExecutorsPart | Show::SequencesPart))
            update();
    });
    connect(ctx, &AppContext::interfaceChanged, this, qOverload<>(&QWidget::update));
}

int StoreExecutorPicker::columns() const { return std::max(1, m_ctx->executorCount()); }

QRect StoreExecutorPicker::tileRect(int row, int col) const
{
    const int left = 10 + kLabelW;
    const double w = double(width() - left - 10 + kGap) / columns();
    return QRect(int(left + col * w), kHead + row * (kTileH + kGap), int(w) - kGap, kTileH);
}

int StoreExecutorPicker::numberAt(const QPoint &p) const
{
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < columns(); ++col)
            if (tileRect(row, col).contains(p))
                return (row + 1) * 100 + col + 1;
    return 0;
}

void StoreExecutorPicker::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    Show *show = m_ctx->show;
    const int page = show->currentPage();
    p.setFont(theme::uiFont(10, QFont::DemiBold));
    p.setPen(theme::programmer);
    p.drawText(QRect(12, 0, width() - 24, kHead), Qt::AlignLeft | Qt::AlignVCenter,
               tr("Tap an executor to store onto it  ·  %1").arg(show->pageLabel(page)));
    for (int row = 0; row < 4; ++row) {
        const QRect first = tileRect(row, 0);
        p.setFont(theme::uiFont(8));
        p.setPen(theme::muted);
        p.drawText(QRect(10, first.top(), kLabelW - 4, first.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number((row + 1) * 100));
        for (int col = 0; col < columns(); ++col) {
            const int number = (row + 1) * 100 + col + 1;
            const QRect r = tileRect(row, col);
            const Executor *e = show->executor(page, number);
            const Sequence *seq = e && e->sequenceId ? show->sequence(e->sequenceId) : nullptr;
            const bool on = e && e->pb().isOn();
            const bool hover = m_hover == number;
            p.setPen(QPen(hover ? theme::programmer : on ? theme::live : theme::line, hover ? 2 : 1));
            p.setBrush(hover ? theme::mix(theme::raised, theme::programmer, 0.12) : seq ? theme::raised : theme::bg);
            p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
            p.setFont(theme::uiFont(8));
            p.setPen(theme::muted);
            p.drawText(r.adjusted(6, 3, -4, 0), Qt::AlignLeft | Qt::AlignTop, QString::number(number));
            p.setFont(theme::uiFont(9, seq ? QFont::DemiBold : QFont::Normal));
            p.setPen(seq ? (on ? theme::live : theme::text) : theme::faint);
            const QRect nameRect = r.adjusted(6, 16, -4, -2);
            p.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(seq ? seq->name : tr("Empty"), Qt::ElideRight, nameRect.width()));
            if (seq) {
                p.setFont(theme::uiFont(8));
                p.setPen(theme::muted);
                p.drawText(r.adjusted(6, 3, -5, 0), Qt::AlignRight | Qt::AlignTop, tr("Seq %1").arg(seq->id));
            }
        }
    }
}

void StoreExecutorPicker::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() != Qt::LeftButton)
        return;
    if (const int number = numberAt(ev->position().toPoint()))
        storeOntoExecutor(m_ctx, m_ctx->show->currentPage(), number);
}

void StoreExecutorPicker::mouseMoveEvent(QMouseEvent *ev)
{
    const int n = numberAt(ev->position().toPoint());
    if (n != m_hover) {
        m_hover = n;
        setCursor(n ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void StoreExecutorPicker::leaveEvent(QEvent *)
{
    m_hover = 0;
    update();
}

void ExecutorStrip::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() != Qt::LeftButton)
        return;
    const int region = regionAt(ev->pos());
    if (region == None)
        return;

    if (m_ctx->midi->learnMode() && region != Header) {
        m_ctx->midi->arm(targetFor(region));
        emit m_ctx->status(tr("Move a control on your MIDI device to map %1").arg(targetFor(region).describe()));
        update();
        return;
    }
    if (m_ctx->storeArmed() && region != Fader) {
        storeHere();
        return;
    }
    Show *show = m_ctx->show;
    {
        // A command waiting for a target takes the tap. "Assign Flash At" + a tap on a button
        // gives that button the function; on the fader or knob, the fader function.
        const QStringList words = m_ctx->commandLine().simplified().split(' ', Qt::SkipEmptyParts);
        const bool assigningFn = words.size() >= 2 && words[0].size() >= 2
                                 && QStringLiteral("assign").startsWith(words[0].toLower())
                                 && !QStringLiteral("sequence").startsWith(words[1].toLower())
                                 && !QStringLiteral("ratemaster").startsWith(words[1].toLower())
                                 && words[1].toLower() != QLatin1String("rm")
                                 && !QStringLiteral("effect").startsWith(words[1].toLower());
        QString object = QStringLiteral("Executor %1.%2").arg(show->currentPage()).arg(m_number);
        if (assigningFn && region >= 0)
            object += QStringLiteral(" Button %1").arg(region + 1);
        else if (assigningFn && region == Fader)
            object += QStringLiteral(" Fader");
        // Any other waiting command (Assign Sequence 1 At, Move, Copy, Delete…) takes a tap anywhere
        // on the executor, not only on its header.
        static const QStringList verbs = {"move", "copy", "delete", "label", "update", "remove", "assign"};
        bool waiting = false;
        if (!words.isEmpty() && words[0].size() >= 2)
            for (const QString &v : verbs)
                waiting = waiting || v.startsWith(words[0].toLower());
        if (region == Header || assigningFn || waiting) {
            const Executor *ex = show->executor(show->currentPage(), m_number);
            const Sequence *sq = ex ? show->sequence(ex->sequenceId) : nullptr;
            if (m_ctx->tapTarget(object, sq ? sq->name : QString()))
                return;
        }
    }
    Executor &e = show->ensureExecutor(show->currentPage(), m_number);
    if (region == Header) {
        if (e.sequenceId)
            show->setSelectedSequence(e.sequenceId);
        return;
    }
    if (region == Fader) {
        m_dragging = true;
        if (kind() == ExecKind::Encoder) {
            m_dragY = ev->pos().y();
            m_dragValue = e.fader;
            update();
        } else {
            setFaderFromY(ev->pos().y());
        }
        return;
    }
    m_pressed = region;
    show->pressButton(e, region, true);
    update();
}

// ---- Page buttons -------------------------------------------------------------

namespace {
class PageButton : public QToolButton {
public:
    PageButton(AppContext *ctx, int page, QWidget *parent) : QToolButton(parent), m_ctx(ctx), m_page(page)
    {
        setCheckable(true);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(tr("Page %1 · double-click or right-click to name it").arg(page));
    }

protected:
    void mouseDoubleClickEvent(QMouseEvent *) override { rename(); }
    void contextMenuEvent(QContextMenuEvent *ev) override
    {
        QMenu menu;
        menu.addAction(tr("Name page %1…").arg(m_page), this, [this]() { rename(); });
        if (!m_ctx->show->pageName(m_page).isEmpty())
            menu.addAction(tr("Clear name"), this, [this]() { m_ctx->show->setPageName(m_page, QString()); });
        menu.exec(ev->globalPos());
    }

private:
    void rename()
    {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Name page"), tr("Name for page %1").arg(m_page), QLineEdit::Normal,
                                                   m_ctx->show->pageName(m_page), &ok);
        if (ok)
            m_ctx->show->setPageName(m_page, name);
    }
    AppContext *m_ctx;
    int m_page;
};
} // namespace

void ExecutorBar::rebuildPages()
{
    Show *show = m_ctx->show;
    const int page = show->currentPage();
    const QString signature = QStringLiteral("%1:%2").arg(page).arg(show->pageName(page));
    if (!m_pageEdit->hasFocus())
        m_pageEdit->setText(QString::number(page));
    if (signature == m_pageSignature)
        return;
    m_pageSignature = signature;
    while (QLayoutItem *it = m_pages->takeAt(0)) {
        if (it->widget()) {
            it->widget()->hide();
            it->widget()->deleteLater();
        }
        delete it;
    }
    // The page's name: double-click or right-click to name it.
    auto *b = new PageButton(m_ctx, page, m_head);
    const QString name = show->pageName(page);
    b->setText(name.isEmpty() ? tr("Page %1").arg(page) : name);
    b->setCheckable(false);
    b->setStyleSheet(QStringLiteral("QToolButton { padding: 3px 10px; border-radius: 5px; color: %1; font-weight: 600; }")
                         .arg(theme::live.name()));
    m_pages->addWidget(b);
}

void ExecutorStrip::mouseMoveEvent(QMouseEvent *ev)
{
    if (!m_dragging)
        return;
    if (kind() == ExecKind::Encoder) {
        // About 160 px of travel from 0 to full; hold Shift for fine.
        const double perPx = (ev->modifiers() & Qt::ShiftModifier) ? 1.0 / 800 : 1.0 / 160;
        Show *show = m_ctx->show;
        Executor &e = show->ensureExecutor(show->currentPage(), m_number);
        show->setFader(e, std::clamp(m_dragValue + (m_dragY - ev->pos().y()) * perPx, 0.0, 1.0));
        update();
        return;
    }
    setFaderFromY(ev->pos().y());
}

void ExecutorStrip::mouseReleaseEvent(QMouseEvent *)
{
    if (m_pressed >= 0) {
        Show *show = m_ctx->show;
        Executor &e = show->ensureExecutor(show->currentPage(), m_number);
        show->pressButton(e, m_pressed, false);
    }
    m_pressed = None;
    m_dragging = false;
    update();
}

void ExecutorStrip::wheelEvent(QWheelEvent *ev)
{
    if (!faderRect().contains(ev->position().toPoint()))
        return;
    Show *show = m_ctx->show;
    Executor &e = show->ensureExecutor(show->currentPage(), m_number);
    const double step = ev->angleDelta().y() > 0 ? 0.02 : -0.02;
    show->setFader(e, e.fader + step);
    update();
}

void ExecutorStrip::contextMenuEvent(QContextMenuEvent *ev)
{
    Show *show = m_ctx->show;
    const int page = show->currentPage();
    QMenu menu(this);
    if (const Executor *ex = show->executor(page, m_number); ex && show->sequence(ex->sequenceId)) {
        const int seqId = ex->sequenceId;
        menu.addAction(tr("Name…"), this, [this, show, seqId]() {
            bool ok = false;
            const QString n = QInputDialog::getText(this, tr("Name"), tr("Name for sequence %1").arg(seqId), QLineEdit::Normal,
                                                    show->sequence(seqId)->name, &ok);
            if (ok && !n.trimmed().isEmpty()) {
                Sequence s = *show->sequence(seqId);
                s.name = n.trimmed();
                show->setSequence(s);
            }
        });
        menu.addAction(tr("Appearance…"), this, [this, show, seqId]() { editAppearance(this, show, seqId); });
        menu.addSeparator();
    }

    QMenu *assign = menu.addMenu(tr("Assign sequence"));
    for (const Sequence &s : show->sequences()) {
        QAction *a = assign->addAction(QStringLiteral("%1  %2").arg(s.id).arg(s.name));
        connect(a, &QAction::triggered, this, [=]() { show->assignSequence(s.id, page, m_number); });
    }
    if (!show->sequences().isEmpty())
        assign->addSeparator();
    assign->addAction(tr("New sequence"), this, [=]() {
        int id = show->sequences().isEmpty() ? 1 : show->sequences().lastKey() + 1;
        show->assignSequence(id, page, m_number);
    });

    QMenu *rates = menu.addMenu(tr("Assign rate master"));
    for (const RateMaster &rm : show->rateMasters()) {
        QAction *a = rates->addAction(QStringLiteral("%1  %2  ·  %3").arg(rm.id).arg(rm.displayName(), rateModeName(rm.mode)));
        connect(a, &QAction::triggered, this, [=]() { show->assignRateMaster(rm.id, page, m_number); });
    }

    Executor &e = show->ensureExecutor(page, m_number);
    const ExecKind k = executorKind(m_number, &e);
    if (executorKindAssignable(m_number)) {
        QMenu *type = menu.addMenu(tr("Executor type"));
        for (ExecKind kk : {ExecKind::Fader, ExecKind::Button, ExecKind::Encoder}) {
            QAction *a = type->addAction(kk == ExecKind::Fader ? tr("Fader") : kk == ExecKind::Button ? tr("Button") : tr("Encoder"));
            a->setCheckable(true);
            a->setChecked(k == kk);
            connect(a, &QAction::triggered, this, [this, show, page, kk]() {
                show->setExecutorKind(page, m_number, kk);
                updateGeometry();
                update();
            });
        }
    }
    if (k != ExecKind::Button) {
        QMenu *fader = menu.addMenu(k == ExecKind::Encoder ? tr("Encoder function") : tr("Fader function"));
        for (FaderFn f : {FaderFn::Master, FaderFn::XFade, FaderFn::Temp}) {
            QAction *a = fader->addAction(faderLabel(f));
            a->setCheckable(true);
            a->setChecked(e.faderFn == f);
            connect(a, &QAction::triggered, this, [this, show, page, f]() {
                show->ensureExecutor(page, m_number).faderFn = f;
                show->notifyExecutors();
            });
        }
    }
    const int nButtons = buttonCount();
    auto buttonTitle = [k](int) { return k == ExecKind::Encoder ? tr("Push function") : tr("Button function"); };
    for (int i = 0; i < nButtons; ++i) {
        QMenu *bm = menu.addMenu(buttonTitle(i));
        for (ButtonFn fn : allButtonFns()) {
            QAction *a = bm->addAction(buttonFnName(fn));
            a->setCheckable(true);
            a->setChecked(e.buttons[i] == fn);
            connect(a, &QAction::triggered, this, [this, show, page, i, fn]() {
                show->ensureExecutor(page, m_number).buttons[i] = fn;
                show->notifyExecutors();
            });
        }
    }

    menu.addSeparator();
    auto learnAction = [&](const QString &label, int region) {
        menu.addAction(label, this, [this, region]() {
            m_ctx->midi->arm(targetFor(region));
            emit m_ctx->status(tr("Move a control on your MIDI device to map %1").arg(targetFor(region).describe()));
        });
    };
    if (k == ExecKind::Fader) {
        learnAction(tr("MIDI learn: fader"), Fader);
        learnAction(tr("MIDI learn: button"), 0);
    } else if (k == ExecKind::Encoder) {
        learnAction(tr("MIDI learn: encoder"), Fader);
        learnAction(tr("MIDI learn: push"), 0);
    } else {
        learnAction(tr("MIDI learn: button"), 0);
    }
    bool anyBound = isBound(Fader) || isBound(0) || isBound(1) || isBound(2);
    QAction *forget = menu.addAction(tr("Forget MIDI mappings"), this, [this]() {
        m_ctx->midi->removeBindingsFor(targetFor(Fader));
        for (int i = 0; i < 3; ++i)
            m_ctx->midi->removeBindingsFor(targetFor(i));
    });
    forget->setEnabled(anyBound);

    menu.addSeparator();
    QAction *unassign = menu.addAction(tr("Unassign"), this, [=]() { show->unassign(page, m_number); });
    unassign->setEnabled(!e.isEmpty());
    menu.exec(ev->globalPos());
}

// ===========================================================================

ExecutorBar::ExecutorBar(AppContext *ctx, QWidget *parent, Mode mode) : QWidget(parent), m_ctx(ctx), m_mode(mode)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(mode == Mode::Learn ? 10 : 0, mode == Mode::Learn ? 8 : 0, mode == Mode::Learn ? 10 : 0,
                          mode == Mode::Learn ? 10 : 0);
    v->setSpacing(8);

    m_head = new QWidget(this);
    auto *head = new QHBoxLayout(m_head);
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(6);
    auto *title = new QLabel(tr("Executors"), m_head);
    if (mode == Mode::Panel)
        title->setProperty("role", "title");
    head->addWidget(title);
    if (mode == Mode::Learn) {
        // The strip appears for Store and for Learn MIDI; its title and accent say which.
        // The strip that slides in for Learn MIDI when the view has no executor window.
        title->setText(tr("Click a fader or button, then move a MIDI control"));
        title->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg(theme::learn.name()));
        QTimer::singleShot(0, this, [this]() {   // once it has its object name
            setStyleSheet(QStringLiteral("QWidget#%1 { background: %2; border-top: 2px solid %3; }")
                              .arg(objectName(), theme::panel.name(), theme::learn.name()));
        });
    }
    head->addSpacing(14);
    // Pages, the way pools page: ▲, the page number (type one to jump there), ▼, and its name.
    auto *up = new QToolButton(m_head);
    up->setText(QStringLiteral("▲"));
    up->setToolTip(tr("Previous page"));
    up->setFocusPolicy(Qt::NoFocus);
    m_pageEdit = new QLineEdit(m_head);
    m_pageEdit->setFixedWidth(48);
    m_pageEdit->setAlignment(Qt::AlignCenter);
    m_pageEdit->setToolTip(tr("The executor page. Type a number to go to that page."));
    m_pageEdit->setValidator(new QIntValidator(1, 999, m_pageEdit));
    auto *down = new QToolButton(m_head);
    down->setText(QStringLiteral("▼"));
    down->setToolTip(tr("Next page"));
    down->setFocusPolicy(Qt::NoFocus);
    m_pages = new QHBoxLayout;   // the page's name
    m_pages->setSpacing(4);
    head->addWidget(up);
    head->addWidget(m_pageEdit);
    head->addWidget(down);
    head->addLayout(m_pages);
    head->addStretch(1);
    v->addWidget(m_head);
    connect(up, &QToolButton::clicked, this, [ctx]() { ctx->show->setPage(std::max(1, ctx->show->currentPage() - 1)); });
    connect(down, &QToolButton::clicked, this, [ctx]() { ctx->show->setPage(ctx->show->currentPage() + 1); });
    connect(m_pageEdit, &QLineEdit::editingFinished, this, [this, ctx]() {
        const int p = m_pageEdit->text().toInt();
        if (p >= 1 && p != ctx->show->currentPage())
            ctx->show->setPage(p);
        m_pageEdit->clearFocus();
    });

    m_grid = new QGridLayout;
    m_grid->setSpacing(8);
    v->addLayout(m_grid, 1);
    rebuild();

    connect(ctx, &AppContext::interfaceChanged, this, [this]() {
        if (m_wide <= 0 && m_tall == 1 && !m_fitWide)
            rebuild();
    });
    connect(ctx->show, &Show::changed, this, [this](Show::Parts p) {
        if (p & Show::ExecutorsPart) {
            rebuildPages();
            syncSizes();   // 401–499 change shape with their type, and with the page
        }
    });
    rebuildPages();

    // Animate only while something moves: a running playback (fades, the output glow) or MIDI
    // learn's pulsing outline. Anything else repaints when the show changes.
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {
        if (!isVisible())
            return;
        Show *show = m_ctx->show;
        bool active = m_ctx->midi->isArmed() || m_ctx->midi->learnMode();
        for (ExecutorStrip *s : m_strips)
            if (const Executor *e = show->executor(show->currentPage(), s->number()); e && e->pb().isOn())
                active = true;
        if (!active && !m_wasActive)
            return;
        m_wasActive = active;   // one more frame after it stops, to settle
        for (ExecutorStrip *s : m_strips)
            s->update();
    });
    timer->start(33);
    connect(ctx->show, &Show::changed, this, [this](Show::Parts p) {
        if (p & (Show::ExecutorsPart | Show::SequencesPart | Show::PatchPart))
            for (ExecutorStrip *s : m_strips)
                s->update();
    });
    connect(ctx, &AppContext::storeArmedChanged, this, [this]() {
        for (ExecutorStrip *s : m_strips)
            s->update();
    });
    syncSizes();
}

void ExecutorBar::syncSizes()
{
    QList<ExecKind> kinds;
    // Each grid row is as tall as its tallest executor; a grid that fits its height needs one.
    const int wide = std::max(1, m_shownGrid.width());
    QVector<int> rowHeights;
    for (int i = 0; i < m_strips.size(); ++i) {
        ExecutorStrip *s = m_strips[i];
        kinds << s->kind();
        const int r = i / wide;
        if (rowHeights.size() <= r)
            rowHeights << 0;
        rowHeights[r] = std::max(rowHeights[r], s->minimumSizeHint().height());
    }
    int tallest = 0;
    for (int r = 0; r < rowHeights.size() && (r == 0 || m_tall > 0); ++r)
        tallest += rowHeights[r] + (r ? 8 : 0);
    if (kinds != m_kinds) {
        m_kinds = kinds;
        for (ExecutorStrip *s : m_strips)
            s->updateGeometry();
    }
    const int head = m_head->isHidden() ? 0 : 38;
    const int margins = m_mode == Mode::Learn ? 18 : 0;
    setMinimumHeight(tallest + head + margins + (m_mode == Mode::Learn ? 12 : 0));
}

int ExecutorBar::rowEnd() const
{
    return (m_first / 100) * 100 + kExecutorsPerRow;   // 101–160, 201–260…
}

QSize ExecutorBar::gridSize() const
{
    // Wide × tall; 0 fits as many as there's room for. One row by default, as wide as the
    // Interface settings say.
    int wide = m_wide, tall = m_tall;
    if (wide <= 0 && tall == 1 && !m_fitWide)
        wide = m_ctx->executorCount();
    const QSize hint = ExecutorStrip(m_ctx, m_first).minimumSizeHint();
    const int head = m_head && !m_head->isHidden() ? 38 : 0;
    if (wide <= 0)
        wide = std::max(1, (width() + 8) / (std::max(60, hint.width()) + 8));
    if (tall <= 0)
        tall = std::max(1, (height() - head + 8) / (hint.height() + 8));
    return {wide, tall};
}

void ExecutorBar::rebuild()
{
    qDeleteAll(m_strips);
    m_strips.clear();
    const QSize g = gridSize();
    m_shownGrid = g;
    int n = 0;
    for (int r = 0; r < g.height(); ++r)
        for (int c = 0; c < g.width(); ++c) {
            const int number = m_first + r * g.width() + c;
            if (number > rowEnd())
                break;
            auto *strip = new ExecutorStrip(m_ctx, number, this);
            m_grid->addWidget(strip, r, c);
            m_strips << strip;
            ++n;
        }
    for (int c = 0; c < g.width(); ++c)
        m_grid->setColumnStretch(c, 1);
    m_kinds.clear();
    if (m_head)
        syncSizes();
    // The tab says which row: "Faders 201–210", "Buttons 101–110"…
    const int row = m_first / 100;
    const QString kind = row == 1 ? tr("Buttons") : row == 2 ? tr("Faders") : row == 3 ? tr("Encoders") : tr("Executors");
    setWindowTitle(n > 1 ? QStringLiteral("%1 %2–%3").arg(kind).arg(m_first).arg(m_first + n - 1)
                         : QStringLiteral("%1 %2").arg(kind).arg(m_first));
}

void ExecutorBar::resizeEvent(QResizeEvent *ev)
{
    QWidget::resizeEvent(ev);
    if ((m_wide <= 0 && (m_fitWide || m_tall != 1)) || m_tall <= 0)
        if (gridSize() != m_shownGrid)
            QTimer::singleShot(0, this, [this]() { rebuild(); });
}

QJsonObject ExecutorBar::saveOptions() const
{
    QJsonObject o;
    if (m_wide > 0)
        o["count"] = m_wide;
    if (m_fitWide)
        o["fit"] = true;
    if (m_tall != 1)
        o["tall"] = m_tall;
    if (m_first != 201)
        o["first"] = m_first;
    return o;
}

void ExecutorBar::loadOptions(const QJsonObject &o)
{
    m_wide = std::clamp(o["count"].toInt(0), 0, kExecutorsPerRow);
    m_fitWide = o["fit"].toBool(false);
    m_tall = std::clamp(o["tall"].toInt(1), 0, kExecutorsPerRow);
    m_first = std::clamp(o["first"].toInt(201), 101, 499);
    m_showPages = true;   // every executor window has the page control
    rebuild();
}

void ExecutorBar::setGridDefaults(int first, int wide, int tall)
{
    m_first = first;
    m_wide = wide;
    m_tall = tall;
    m_fitWide = wide <= 0;
    rebuild();
}

void ExecutorBar::buildOptions(QuickEditor *ed)
{
    // Like a pool: wide × tall, 0 fits as many as there's room for.
    const QSize g = gridSize();
    ed->addNumber(tr("Executors wide (0 fits)"), 0, kExecutorsPerRow, m_wide > 0 ? m_wide : (m_fitWide || m_tall != 1 ? 0 : g.width()),
                  [this](int n) {
                      m_wide = n;
                      m_fitWide = n == 0;
                      rebuild();
                  });
    ed->addNumber(tr("Executors tall (0 fits)"), 0, kExecutorsPerRow, m_tall, [this](int n) {
        m_tall = n;
        rebuild();
    });
    // Executors are numbered by row: 101… buttons, 201… faders, 301… encoders, 401… any of them,
    // up to 60 in a row (101–160).
    ed->addSegmented(tr("Row"), {QStringLiteral("101"), QStringLiteral("201"), QStringLiteral("301"), QStringLiteral("401")},
                     m_first % 100 == 1 ? m_first / 100 - 1 : -1, [this](int i) {
                         m_first = (i + 1) * 100 + 1;
                         rebuild();
                     });
    ed->addNumber(tr("First executor"), 101, 460, m_first, [this](int n) {
        if (n % 100 == 0 || n % 100 > kExecutorsPerRow)
            n = (n / 100) * 100 + 1;
        m_first = n;
        rebuild();
    });
}

} // namespace lumora
