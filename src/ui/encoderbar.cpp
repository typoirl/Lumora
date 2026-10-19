// SPDX-License-Identifier: GPL-3.0-or-later
#include "encoderbar.h"

#include "appcontext.h"
#include "media.h"
#include "midi.h"
#include "quickedit.h"
#include "theme.h"

#include <QComboBox>
#include <QContextMenuEvent>
#include <QPainterPath>
#include <QTransform>
#include <QDateTime>
#include <QGuiApplication>
#include <QJsonObject>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace lumora {

namespace {
constexpr double kStartAngle = 225.0;   // degrees, 0 = 3 o'clock, counter-clockwise
constexpr double kSweep = 270.0;
constexpr int kDialW = 136;
constexpr double kPi = 3.14159265358979323846;
} // namespace

// ===========================================================================
// EncoderDial
// ===========================================================================

EncoderDial::EncoderDial(AppContext *ctx, const QString &attribute, std::function<bool()> fine, QWidget *parent)
    : QWidget(parent), m_ctx(ctx), m_attr(attribute), m_fine(std::move(fine))
{
    setFixedSize(kDialW, 132);
    setCursor(Qt::SizeVerCursor);
    setToolTip(tr("%1: drag up or down, or scroll. Double-click to type a value. Right-click for slots and Default. "
                  "Click the name to put it on the command line (then Park, or At).")
                   .arg(attributeLabel(attribute)));
}

double EncoderDial::currentValue(bool *programmed, QString *setName, int *fidOut) const
{
    Show *show = m_ctx->show;
    *programmed = false;
    for (int fid : show->selection()) {
        if (!show->fixtureHas(fid, m_attr))
            continue;
        double v;
        const AttrValues prog = show->programmerValues().value(fid);
        if (prog.contains(m_attr)) {
            *programmed = true;
            v = show->resolveValue(fid, m_attr, prog.value(m_attr));
        } else {
            v = show->lastOutput().value(fid).value(m_attr, show->defaultValue(fid, m_attr));
        }
        if (setName) {
            const FixtureMode *m = show->fixtureMode(fid);
            const ChannelDef *c = m ? m->channel(m_attr) : nullptr;
            *setName = c ? c->setNameFor(v) : QString();
        }
        if (fidOut)
            *fidOut = fid;
        return v;
    }
    return 0.0;
}

const ChannelDef *EncoderDial::channel() const
{
    for (int fid : m_ctx->show->selection())
        if (const ChannelDef *c = m_ctx->show->channelOf(fid, m_attr))
            return c;
    return nullptr;
}

void EncoderDial::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    bool programmed = false;
    QString setName;
    int fid = 0;
    const double raw = currentValue(&programmed, &setName, &fid);
    m_shown = QStringLiteral("%1|%2|%3|%4").arg(raw, 0, 'f', 4).arg(programmed).arg(setName).arg(fid);
    // The ring shows where the value sits in its range; pan and tilt read in degrees.
    const ChannelDef *ch = fid ? m_ctx->show->channelOf(fid, m_attr) : nullptr;
    const bool angle = isAngle(m_attr);
    const double v = ch ? ch->toDmx(raw) : raw;
    const QColor fc = programmed ? theme::programmer : theme::featureColor(featureOf(m_attr));

    // Label
    p.setFont(theme::uiFont(9, QFont::Medium));
    p.setPen(programmed ? theme::programmer : theme::muted);
    p.drawText(QRect(0, 0, width(), 16), Qt::AlignHCenter | Qt::AlignTop,
               p.fontMetrics().elidedText(attributeLabel(m_attr), Qt::ElideRight, width() - 4));

    // Ring: the value arc sweeps 270° like an encoder's LED ring
    const double r = 36;
    const QPointF c(width() / 2.0, 20 + r + 4);
    const QRectF ring(c.x() - r, c.y() - r, 2 * r, 2 * r);
    // Knob and ring are one flat color.
    const QColor body = theme::line;
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(body, 6, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(ring, int(kStartAngle * 16), int(-kSweep * 16));
    if (v > 0.0005) {
        p.setPen(QPen(fc, 6, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(ring, int(kStartAngle * 16), int(-kSweep * v * 16));
    }
    // Knob face with a pointer
    const double face = r - 9;
    p.setPen(Qt::NoPen);
    p.setBrush(body);
    p.drawEllipse(c, face, face);

    // Center: gobo image when the fixture file has one, otherwise pointer + value
    const QImage img = fid ? slotImage(m_ctx->show, fid, m_attr, v) : QImage();
    if (!img.isNull()) {
        const int size = int(face * 2 - 6);
        p.drawPixmap(QPointF(c.x() - size / 2.0, c.y() - size / 2.0), goboPixmap(img, size));
    } else {
        const double a = (kStartAngle - kSweep * v) * kPi / 180.0;
        p.setPen(QPen(fc, 2.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(c.x() + std::cos(a) * (face - 9), c.y() - std::sin(a) * (face - 9)),
                   QPointF(c.x() + std::cos(a) * (face - 3), c.y() - std::sin(a) * (face - 3)));
        p.setFont(theme::uiFont(12, QFont::DemiBold));
        p.setPen(programmed ? theme::programmer : theme::text);
        // In the channel's own values: degrees, %, or whatever range the fixture type gives it.
        const double shown = ch ? ch->toPhysical(raw) : raw * 100.0;
        const QString num = angle ? QString::number(raw, 'f', m_fine() ? 1 : 0) + QChar(0x00b0)
                                  : QString::number(shown, 'f', (m_fine() || (v > 0 && v < 0.1)) ? 1 : 0);
        p.drawText(QRectF(c.x() - face, c.y() - 12, face * 2, 24), Qt::AlignCenter, num);
    }

    // Slot name (or the number, when the center shows a gobo)
    p.setFont(theme::uiFont(9));
    p.setPen(setName.isEmpty() ? theme::faint : theme::text);
    const QString below = setName.isEmpty() ? (img.isNull() ? QString() : QString::number(std::lround(v * 100))) : setName;
    p.drawText(QRect(2, int(c.y() + r + 4), width() - 4, 18), Qt::AlignHCenter | Qt::AlignTop,
               p.fontMetrics().elidedText(below, Qt::ElideRight, width() - 6));

    // Learn MIDI: the bar's encoders can be mapped (outlined; filled while waiting for the control).
    if (m_slot > 0 && m_ctx->midi->learnMode()) {
        MidiTarget t;
        t.kind = MidiTarget::Encoder;
        t.encoder = m_slot;
        const bool armed = m_ctx->midi->isArmed() && m_ctx->midi->armedTarget() == t;
        const bool bound = !m_ctx->midi->bindingsFor(t).isEmpty();
        QColor fill = theme::learn;
        fill.setAlpha(armed ? 70 : 0);
        p.setBrush(fill);
        p.setPen(QPen(theme::learn, armed ? 2.5 : 1.5, bound || armed ? Qt::SolidLine : Qt::DashLine));
        p.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 6, 6);
        p.setFont(theme::uiFont(8, QFont::DemiBold));
        p.setPen(theme::learn);
        p.drawText(QRect(4, 4, width() - 10, 14), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("E%1").arg(m_slot));
    }
}

void EncoderDial::refresh()
{
    bool programmed = false;
    QString setName;
    int fid = 0;
    const double v = currentValue(&programmed, &setName, &fid);
    if (QStringLiteral("%1|%2|%3|%4").arg(v, 0, 'f', 4).arg(programmed).arg(setName).arg(fid) != m_shown)
        update();
}

void EncoderDial::turn(double delta)
{
    m_ctx->show->nudgeAttribute(m_attr, delta);
    update();
}

void EncoderDial::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::LeftButton && m_slot > 0 && m_ctx->midi->learnMode()) {
        MidiTarget t;
        t.kind = MidiTarget::Encoder;
        t.encoder = m_slot;
        m_ctx->midi->arm(t);
        emit m_ctx->status(tr("Turn an encoder or knob on your MIDI device to map %1 (%2 here now)").arg(t.describe(), attributeLabel(m_attr)));
        update();
        return;
    }
    // The name puts "Attribute Pan" on the command line: then Park, At 30…
    if (ev->button() == Qt::LeftButton && ev->pos().y() < 18) {
        m_ctx->pressKey(QStringLiteral("Attribute ") + m_attr);
        return;
    }
    if (ev->button() == Qt::LeftButton) {
        m_drag = true;
        m_last = ev->pos();
    }
}

void EncoderDial::mouseMoveEvent(QMouseEvent *ev)
{
    if (!m_drag)
        return;
    const QPoint d = ev->pos() - m_last;
    m_last = ev->pos();
    const bool fine = m_fine() || (ev->modifiers() & Qt::ShiftModifier);
    turn((d.x() - d.y()) * (fine ? 0.0005 : 0.004));
}

void EncoderDial::mouseReleaseEvent(QMouseEvent *)
{
    m_drag = false;
}

void EncoderDial::mouseDoubleClickEvent(QMouseEvent *)
{
    bool programmed;
    const double v = currentValue(&programmed, nullptr, nullptr);
    bool ok = false;
    if (const ChannelDef *c = channel(); c && c->isAngle()) {
        const double typed = QInputDialog::getDouble(this, attributeLabel(m_attr),
                                                     tr("Degrees (%1 to %2)").arg(c->minValue()).arg(c->maxValue()), v,
                                                     c->minValue(), c->maxValue(), 1, &ok);
        if (ok)
            m_ctx->show->setAttribute(m_attr, typed);
        return;
    }
    // In the channel's own values (0–100 for most, 5–50 for a zoom that says so).
    const ChannelDef *c = channel();
    const double lo = c ? std::min(c->rangeFrom(), c->rangeTo()) : 0.0, hi = c ? std::max(c->rangeFrom(), c->rangeTo()) : 100.0;
    const double typed = QInputDialog::getDouble(this, attributeLabel(m_attr), tr("Value (%1 to %2)").arg(lo).arg(hi),
                                                 c ? c->toPhysical(v) : v * 100.0, lo, hi, 2, &ok);
    if (ok)
        m_ctx->run(QStringLiteral("Attribute %1 At %2").arg(m_attr).arg(typed));
}

void EncoderDial::wheelEvent(QWheelEvent *ev)
{
    const bool fine = m_fine() || (ev->modifiers() & Qt::ShiftModifier);
    turn((ev->angleDelta().y() > 0 ? 1 : -1) * (fine ? 0.001 : 0.01));
}

void EncoderDial::contextMenuEvent(QContextMenuEvent *ev)
{
    showRanges(ev->globalPos());
}

void EncoderDial::showRanges(const QPoint &global)
{
    Show *show = m_ctx->show;
    const ChannelDef *channel = nullptr;
    const FixtureType *type = nullptr;
    for (int fid : show->selection())
        if (const FixtureMode *m = show->fixtureMode(fid))
            if ((channel = m->channel(m_attr))) {
                type = show->fixtureType(fid);
                break;
            }
    bool programmed = false;
    const double v = currentValue(&programmed, nullptr, nullptr);
    const QString attr = m_attr;
    auto *ed = new QuickEditor(attributeLabel(attr), this);
    if (channel && channel->isAngle())
        ed->addValueField(tr("Degrees"), v, [show, attr](double x) { show->setAttribute(attr, x); }, channel->minValue(),
                          channel->maxValue(), QStringLiteral("°"), 1.0);
    else
        ed->addValueField(tr("Value"), v, [show, attr](double x) { show->setAttribute(attr, x); });
    if (channel && !channel->sets.isEmpty()) {
        ed->addSection(tr("Ranges"));
        QVector<QuickEditor::Choice> choices;
        for (const ChannelSet &cs : channel->sets) {
            const double mid = channel->fromDmx((cs.from + cs.to) / 2.0);
            const QImage img = mediaImage(type, cs.media);
            choices.append({cs.name, img.isNull() ? QIcon() : QIcon(goboPixmap(img, 32)),
                            [show, attr, mid]() { show->setAttribute(attr, mid); }});
        }
        ed->addChoices(choices);
    }
    ed->addSection(QString());
    ed->addAction(tr("Default"), [show, attr]() {
        for (int fid : show->selection())
            show->setAttribute(fid, attr, show->defaultValue(fid, attr));
    });
    // Full and Zero: the ends of each fixture's range (degrees for pan and tilt).
    auto toEnd = [show, attr](double level) {
        for (int fid : show->selection())
            if (const ChannelDef *c = show->channelOf(fid, attr))
                show->setAttribute(fid, attr, c->fromDmx(level));
    };
    ed->addAction(tr("Full"), [toEnd]() { toEnd(1.0); });
    ed->addAction(tr("Zero"), [toEnd]() { toEnd(0.0); });
    if (m_slot > 0) {
        // MIDI by place on the bar: the same encoder turns whatever the bar shows there.
        ed->addSection(tr("MIDI (encoder %1 on the bar)").arg(m_slot));
        MidiManager *midi = m_ctx->midi;
        MidiTarget turnT, pushT;
        turnT.kind = MidiTarget::Encoder;
        pushT.kind = MidiTarget::EncoderPush;
        turnT.encoder = pushT.encoder = m_slot;
        AppContext *ctx = m_ctx;
        auto learn = [midi, ctx](const MidiTarget &t) {
            midi->arm(t);
            emit ctx->status(tr("Move a control on your MIDI device to map %1").arg(t.describe()));
        };
        ed->addAction(tr("MIDI learn: encoder"), [learn, turnT]() { learn(turnT); });
        ed->addAction(tr("MIDI learn: push"), [learn, pushT]() { learn(pushT); });
        if (!midi->bindingsFor(turnT).isEmpty() || !midi->bindingsFor(pushT).isEmpty())
            ed->addAction(tr("Forget MIDI mappings"), [midi, turnT, pushT]() {
                midi->removeBindingsFor(turnT);
                midi->removeBindingsFor(pushT);
            });
    }
    ed->popup(global);
}

// ===========================================================================
// XyPad
// ===========================================================================

XyPad::XyPad(AppContext *ctx, std::function<bool()> fine, QWidget *parent) : QWidget(parent), m_ctx(ctx), m_fine(std::move(fine))
{
    setFixedSize(sizeHint());
    setCursor(Qt::CrossCursor);
    setToolTip(tr("Pan across, tilt up and down. Drag to move the selection (Shift or Fine for small moves)."));
}

QVector<QPointF> XyPad::points(QString *readout) const
{
    Show *show = m_ctx->show;
    QVector<QPointF> out;
    auto value = [show](int fid, const QString &a) {
        const AttrValues prog = show->programmerValues().value(fid);
        if (prog.contains(a))
            return show->resolveValue(fid, a, prog.value(a));
        return show->lastOutput().value(fid).value(a, show->defaultValue(fid, a));
    };
    for (int fid : show->selection()) {
        const ChannelDef *pan = show->channelOf(fid, QStringLiteral("Pan"));
        const ChannelDef *tilt = show->channelOf(fid, QStringLiteral("Tilt"));
        if (!pan && !tilt)
            continue;
        const double p = pan ? value(fid, QStringLiteral("Pan")) : 0.0;
        const double t = tilt ? value(fid, QStringLiteral("Tilt")) : 0.0;
        if (readout && out.isEmpty())
            *readout = QStringLiteral("%1   %2")
                           .arg(pan ? tr("P %1°").arg(p, 0, 'f', 1) : QString(), tilt ? tr("T %1°").arg(t, 0, 'f', 1) : QString())
                           .trimmed();
        // Tilt reads up the pad: its high end at the top.
        out << QPointF(pan ? pan->toDmx(p) : 0.5, tilt ? 1.0 - tilt->toDmx(t) : 0.5);
        if (out.size() >= 64)
            break;
    }
    return out;
}

void XyPad::refresh()
{
    QString readout;
    const QVector<QPointF> pts = points(&readout);
    QString sig = readout;
    for (const QPointF &p : pts)
        sig += QStringLiteral("|%1,%2").arg(p.x(), 0, 'f', 4).arg(p.y(), 0, 'f', 4);
    if (sig != m_shown)
        update();
}

void XyPad::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QString readout;
    const QVector<QPointF> pts = points(&readout);
    m_shown = readout;
    for (const QPointF &pt : pts)
        m_shown += QStringLiteral("|%1,%2").arg(pt.x(), 0, 'f', 4).arg(pt.y(), 0, 'f', 4);

    const QRectF area = QRectF(rect()).adjusted(1, 1, -1, -15);
    p.setPen(QPen(theme::line, 1));
    p.setBrush(QColor(0x14, 0x17, 0x1c));
    p.drawRoundedRect(area, 4, 4);
    // Quarter lines, and the middle (0°, 0°) a little brighter.
    for (int i = 1; i < 4; ++i) {
        p.setPen(QPen(i == 2 ? theme::muted : theme::line, 1, i == 2 ? Qt::SolidLine : Qt::DotLine));
        const double x = area.left() + area.width() * i / 4.0, y = area.top() + area.height() * i / 4.0;
        p.drawLine(QPointF(x, area.top() + 2), QPointF(x, area.bottom() - 2));
        p.drawLine(QPointF(area.left() + 2, y), QPointF(area.right() - 2, y));
    }
    // Every fixture a dot; the first one ringed.
    const QColor c = theme::featureColor(Feature::Position);
    for (int i = int(pts.size()) - 1; i >= 0; --i) {
        const QPointF at(area.left() + pts[i].x() * area.width(), area.top() + pts[i].y() * area.height());
        p.setPen(Qt::NoPen);
        p.setBrush(i == 0 ? c : theme::mix(QColor(0x14, 0x17, 0x1c), c, 0.6));
        p.drawEllipse(at, i == 0 ? 4.5 : 3.0, i == 0 ? 4.5 : 3.0);
        if (i == 0) {
            p.setPen(QPen(c, 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(at, 8.0, 8.0);
        }
    }
    p.setFont(theme::uiFont(8));
    p.setPen(theme::muted);
    p.drawText(QRectF(0, height() - 14, width(), 14), Qt::AlignLeft | Qt::AlignVCenter,
               pts.isEmpty() ? tr("No pan or tilt") : readout);
}

void XyPad::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::LeftButton) {
        m_drag = true;
        m_last = ev->pos();
    }
}

void XyPad::mouseMoveEvent(QMouseEvent *ev)
{
    if (!m_drag)
        return;
    const QPoint d = ev->pos() - m_last;
    m_last = ev->pos();
    const double scale = (m_fine() || (ev->modifiers() & Qt::ShiftModifier)) ? 0.2 : 1.0;
    // Across the pad is the whole pan range, up it the whole tilt range.
    Show *show = m_ctx->show;
    if (d.x())
        show->nudgeAttribute(QStringLiteral("Pan"), scale * d.x() / std::max(1, width()));
    if (d.y())
        show->nudgeAttribute(QStringLiteral("Tilt"), -scale * d.y() / std::max(1, height() - 15));
    update();
}

void XyPad::mouseReleaseEvent(QMouseEvent *)
{
    m_drag = false;
}

// ===========================================================================
// ShaperView
// ===========================================================================

namespace {
// Blade n (0..3): its corners A and B on the square (half size 1), and the inward normal.
void bladeFrame(int n, QPointF *a, QPointF *b, QPointF *inward)
{
    static const double corners[4][4] = {{-1, -1, 1, -1}, {1, -1, 1, 1}, {1, 1, -1, 1}, {-1, 1, -1, -1}};
    static const double normals[4][2] = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}};
    *a = QPointF(corners[n][0], corners[n][1]);
    *b = QPointF(corners[n][2], corners[n][3]);
    *inward = QPointF(normals[n][0], normals[n][1]);
}
double dot(const QPointF &p, const QPointF &q) { return p.x() * q.x() + p.y() * q.y(); }
QString bladeAttr(int n, const char *part) { return QStringLiteral("Blade%1%2").arg(n + 1).arg(QLatin1String(part)); }
} // namespace

ShaperView::ShaperView(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    setFixedSize(sizeHint());
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Framing shutters: drag a blade's number in or out; drag its ends to swivel it (Ins+Rot) or move its corners (A+B)."));
}

double ShaperView::value(const QString &a, double fallback, bool *has) const
{
    Show *show = m_ctx->show;
    for (int fid : show->selection()) {
        if (!show->channelOf(fid, a))
            continue;
        if (has)
            *has = true;
        const AttrValues prog = show->programmerValues().value(fid);
        if (prog.contains(a))
            return show->resolveValue(fid, a, prog.value(a));
        return show->lastOutput().value(fid).value(a, show->defaultValue(fid, a));
    }
    if (has)
        *has = false;
    return fallback;
}

bool ShaperView::hasAB() const
{
    for (int n = 0; n < 4; ++n) {
        bool has = false;
        value(bladeAttr(n, "B"), 0, &has);
        if (has)
            return true;
    }
    return false;
}

void ShaperView::setMode(Mode m)
{
    m_mode = m == AB && hasAB() ? AB : InsRot;
    update();
}

void ShaperView::readBlades(Blade (&bl)[4], double *systemRot) const
{
    for (int n = 0; n < 4; ++n) {
        Blade &b = bl[n];
        bool hasA = false, hasB = false, hasRot = false;
        b.a = value(bladeAttr(n, "A"), 0.0, &hasA);
        const double bv = value(bladeAttr(n, "B"), b.a, &hasB);
        b.rot = value(bladeAttr(n, "Rot"), 0.5, &hasRot);
        b.present = hasA || hasB;
        QPointF ca, cb, in;
        bladeFrame(n, &ca, &cb, &in);
        if (m_mode == AB && hasB) {
            b.b = bv;
            b.pa = ca + in * b.a;
            b.pb = cb + in * b.b;
        } else {
            // Insertion, swivelled round the middle of its edge: ±45° across the range.
            b.b = b.a;
            const QPointF mid = (ca + cb) / 2 + in * b.a;
            const double ang = (b.rot - 0.5) * M_PI / 2;
            const QPointF dir = (cb - ca) / 2;
            const QPointF r(dir.x() * std::cos(ang) - dir.y() * std::sin(ang), dir.x() * std::sin(ang) + dir.y() * std::cos(ang));
            b.pa = mid - r;
            b.pb = mid + r;
        }
    }
    *systemRot = (value(QStringLiteral("BladeSystemRot"), 0.5) - 0.5) * 90.0;
}

void ShaperView::refresh()
{
    Blade bl[4];
    double rot;
    readBlades(bl, &rot);
    QString sig = QString::number(rot, 'f', 3);
    for (const Blade &b : bl)
        sig += QStringLiteral("|%1,%2,%3").arg(b.a, 0, 'f', 3).arg(b.b, 0, 'f', 3).arg(b.rot, 0, 'f', 3);
    if (sig != m_shown)
        update();
}

void ShaperView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    Blade bl[4];
    double rot;
    readBlades(bl, &rot);
    m_shown = QString::number(rot, 'f', 3);
    for (const Blade &b : bl)
        m_shown += QStringLiteral("|%1,%2,%3").arg(b.a, 0, 'f', 3).arg(b.b, 0, 'f', 3).arg(b.rot, 0, 'f', 3);

    const double h = half();
    const QPointF c(width() / 2.0, height() / 2.0);
    p.setPen(QPen(theme::line, 1));
    p.setBrush(QColor(0x14, 0x17, 0x1c));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    p.translate(c);
    p.rotate(rot);
    p.scale(h, h);
    const QRectF square(-1, -1, 2, 2);
    // The beam, light grey; the blades darker over it, clipped to the gate.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xc9, 0xcc, 0xd2));
    p.drawEllipse(square);
    QPainterPath gate;
    gate.addRect(square);
    p.setClipPath(gate);
    const QColor bladeFill(0x3a, 0x3f, 0x48);
    for (int n = 0; n < 4; ++n) {
        if (!bl[n].present)
            continue;
        QPointF ca, cb, in;
        bladeFrame(n, &ca, &cb, &in);
        // The blade covers from its edge line outwards, extended well past the gate.
        const QPointF d = bl[n].pb - bl[n].pa;
        const QPointF ext = d * 3.0;
        QPolygonF poly;
        poly << bl[n].pa - ext << bl[n].pb + ext << bl[n].pb + ext - in * 4.0 << bl[n].pa - ext - in * 4.0;
        p.setBrush(bladeFill);
        p.drawPolygon(poly);
    }
    p.setClipping(false);
    QPen edge(Qt::white, 1.6 / h);
    for (int n = 0; n < 4; ++n) {
        if (!bl[n].present || (bl[n].a < 0.004 && bl[n].b < 0.004 && std::fabs(bl[n].rot - 0.5) < 0.004))
            continue;
        p.setPen(edge);
        p.setClipPath(gate);
        const QPointF d = bl[n].pb - bl[n].pa;
        p.drawLine(bl[n].pa - d, bl[n].pb + d);
        p.setClipping(false);
    }
    p.setPen(QPen(theme::line, 1.0 / h));
    p.setBrush(Qt::NoBrush);
    p.drawRect(square);
    // Handles: the blade's number in the middle of its edge, A and B at its ends.
    p.resetTransform();
    const QColor accent = theme::featureColor(Feature::Beam);
    auto toWidget = [&](const QPointF &l) {
        QTransform t;
        t.translate(c.x(), c.y());
        t.rotate(rot);
        t.scale(h, h);
        return t.map(l);
    };
    p.setFont(theme::uiFont(7));
    for (int n = 0; n < 4; ++n) {
        if (!bl[n].present)
            continue;
        const QPointF mid = toWidget((bl[n].pa + bl[n].pb) / 2);
        const bool hot = m_dragBlade == n;
        p.setPen(Qt::NoPen);
        p.setBrush(hot && m_dragEnd == 0 ? theme::programmer : accent);
        p.drawEllipse(mid, 7, 7);
        p.setPen(Qt::black);
        p.drawText(QRectF(mid.x() - 7, mid.y() - 7, 14, 14), Qt::AlignCenter, QString::number(n + 1));
        bool hasRot = false;
        value(bladeAttr(n, "Rot"), 0.5, &hasRot);
        if (m_mode == AB || hasRot) {
            for (int end = 1; end <= 2; ++end) {
                const QPointF at = toWidget(end == 1 ? bl[n].pa * 0.78 + bl[n].pb * 0.22 : bl[n].pb * 0.78 + bl[n].pa * 0.22);
                p.setPen(QPen(hot && m_dragEnd == end ? theme::programmer : accent, 1.2));
                p.setBrush(QColor(0x14, 0x17, 0x1c));
                p.drawEllipse(at, 5, 5);
                p.setPen(theme::text);
                p.drawText(QRectF(at.x() - 5, at.y() - 5, 10, 10), Qt::AlignCenter, end == 1 ? QStringLiteral("A") : QStringLiteral("B"));
            }
        }
    }
}

QPointF ShaperView::toLocal(const QPointF &pos) const
{
    double rot;
    Blade bl[4];
    readBlades(bl, &rot);
    QTransform t;
    t.translate(width() / 2.0, height() / 2.0);
    t.rotate(rot);
    t.scale(half(), half());
    return t.inverted().map(pos);
}

void ShaperView::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() != Qt::LeftButton)
        return;
    // The nearest handle, wherever you press.
    Blade bl[4];
    double rot;
    readBlades(bl, &rot);
    const QPointF at = toLocal(ev->position());
    double best = 1e9;
    m_dragBlade = -1;
    for (int n = 0; n < 4; ++n) {
        if (!bl[n].present)
            continue;
        bool hasRot = false;
        value(bladeAttr(n, "Rot"), 0.5, &hasRot);
        const QPointF handles[3] = {(bl[n].pa + bl[n].pb) / 2, bl[n].pa * 0.78 + bl[n].pb * 0.22, bl[n].pb * 0.78 + bl[n].pa * 0.22};
        for (int end = 0; end < 3; ++end) {
            if (end && m_mode == InsRot && !hasRot)
                continue;
            const QPointF d = handles[end] - at;
            const double dist = std::hypot(d.x(), d.y()) * (end ? 1.15 : 1.0);   // the blade wins a tie
            if (dist < best) {
                best = dist;
                m_dragBlade = n;
                m_dragEnd = end;
            }
        }
    }
    update();
}

void ShaperView::setBladeInsertion(int n, double ins)
{
    Show *show = m_ctx->show;
    ins = std::clamp(ins, 0.0, 1.0);
    const double old = value(bladeAttr(n, "A"), 0.0);
    const double delta = ins - old;
    auto shift = [&](int k, double dv) {
        const QString a = bladeAttr(k, "A");
        show->setAttribute(a, std::clamp(value(a, 0.0) + dv, 0.0, 1.0));
        if (m_mode == AB) {
            const QString b = bladeAttr(k, "B");
            bool has = false;
            const double bv = value(b, 0.0, &has);
            if (has)
                show->setAttribute(b, std::clamp(bv + dv, 0.0, 1.0));
        }
    };
    shift(n, delta);
    const int opposite = (n + 2) % 4;
    switch (m_link) {
    case None: break;
    case All:
        for (int k = 0; k < 4; ++k)
            if (k != n)
                shift(k, delta);
        break;
    case Parallel: shift(opposite, -delta); break;   // the opening moves across
    case Mirrored: shift(opposite, delta); break;    // both close in together
    }
}

void ShaperView::mouseMoveEvent(QMouseEvent *ev)
{
    if (m_dragBlade < 0)
        return;
    const int n = m_dragBlade;
    QPointF ca, cb, in;
    bladeFrame(n, &ca, &cb, &in);
    const QPointF at = toLocal(ev->position());
    Show *show = m_ctx->show;
    if (m_dragEnd == 0) {
        setBladeInsertion(n, dot(at - (ca + cb) / 2, in));
    } else if (m_mode == AB) {
        const QPointF corner = m_dragEnd == 1 ? ca : cb;
        const QString attr = bladeAttr(n, m_dragEnd == 1 ? "A" : "B");
        show->setAttribute(attr, std::clamp(dot(at - corner, in), 0.0, 1.0));
    } else {
        // Swivel round the middle of the blade: the angle from there to the pointer.
        const double ins = value(bladeAttr(n, "A"), 0.0);
        const QPointF mid = (ca + cb) / 2 + in * ins;
        QPointF dir = (m_dragEnd == 2 ? cb - ca : ca - cb) / 2;
        const QPointF v = at - mid;
        double ang = std::atan2(v.y(), v.x()) - std::atan2(dir.y(), dir.x());
        while (ang > M_PI) ang -= 2 * M_PI;
        while (ang < -M_PI) ang += 2 * M_PI;
        show->setAttribute(bladeAttr(n, "Rot"), std::clamp(0.5 + ang / (M_PI / 2), 0.0, 1.0));
    }
    update();
}

void ShaperView::mouseReleaseEvent(QMouseEvent *)
{
    m_dragBlade = -1;
    update();
}

// ===========================================================================
// EncoderBar
// ===========================================================================

EncoderBar::EncoderBar(AppContext *ctx, QWidget *parent, bool mainBar) : QWidget(parent), m_ctx(ctx), m_main(mainBar)
{
    setObjectName("encoderBar");
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QStringLiteral("QWidget#encoderBar { background: %1; %2 }")
                      .arg(theme::panel.name(), m_main ? QStringLiteral("border-top: 1px solid %1;").arg(theme::line.name()) : QString()));
    if (m_main)
        setFixedHeight(156);
    else
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_feature = static_cast<Feature>(std::clamp(m_ctx->uiValue("encoderCategory", 1).toInt(1), 1, 7));
    m_preferred = m_feature;
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    m_body = new QWidget(this);
    outer->addWidget(m_body);
    if (!m_main)
        outer->addStretch(1);
    m_tucked = new QLabel(tr("Encoders"), this);
    m_tucked->setProperty("role", "muted");
    m_tucked->setContentsMargins(12, 0, 0, 0);
    m_tucked->hide();
    outer->addWidget(m_tucked);
    auto *h = new QHBoxLayout(m_body);
    h->setContentsMargins(12, 10, 12, 10);
    h->setSpacing(14);

    // Categories
    auto *left = new QVBoxLayout;
    left->setSpacing(6);
    m_selection = new QLabel(m_body);
    m_selection->setProperty("role", "muted");
    left->addWidget(m_selection);
    auto *grid = new QGridLayout;
    grid->setSpacing(4);
    const QList<Feature> features = allFeatures();
    for (int i = 0; i < features.size(); ++i) {
        const Feature f = features[i];
        auto *b = new QToolButton(m_body);
        b->setText(featureName(f));
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setChecked(f == m_feature);
        b->setFixedSize(92, 26);
        const QColor c = theme::featureColor(f);
        b->setStyleSheet(QStringLiteral(
            "QToolButton { background: %1; border: 1px solid %2; border-left: 3px solid %3; border-radius: 4px;"
            " color: %4; padding: 0 6px; text-align: left; }"
            "QToolButton:checked { background: #2c313b; color: %5; border-color: %3; }"
            "QToolButton:disabled { color: %6; border-left-color: %2; }")
                             .arg(theme::raised.name(), theme::line.name(), c.name(), theme::muted.name(),
                                  theme::text.name(), theme::faint.name()));
        connect(b, &QToolButton::clicked, this, [this, f]() {
            m_feature = f;
            m_preferred = f;
            if (m_main)
                m_ctx->setUiValue("encoderCategory", int(f));
            else
                panelStateChanged(this);
            m_page = 0;
            rebuild(true);
        });
        grid->addWidget(b, i / 2, i % 2);
        m_categories << b;
    }
    left->addLayout(grid);
    left->addStretch(1);
    h->addLayout(left);

    auto *divider = new QFrame(m_body);
    divider->setFixedWidth(1);
    divider->setStyleSheet(QStringLiteral("background: %1;").arg(theme::line.name()));
    h->addWidget(divider);

    // Encoders
    m_dialArea = new QWidget(m_body);
    m_dialLayout = new QHBoxLayout(m_dialArea);
    m_dialLayout->setContentsMargins(0, 0, 0, 0);
    m_dialLayout->setSpacing(4);
    h->addWidget(m_dialArea, 1);

    // Paging, Fine, Default
    auto *right = new QVBoxLayout;
    right->setSpacing(6);
    m_pageLabel = new QLabel(m_body);
    m_pageLabel->setProperty("role", "muted");
    m_pageLabel->setAlignment(Qt::AlignCenter);
    auto *pager = new QHBoxLayout;
    pager->setSpacing(4);
    m_prev = new QToolButton(m_body);
    m_prev->setText(QStringLiteral("‹"));
    m_prev->setToolTip(tr("Previous encoder page"));
    m_prev->setFixedWidth(36);
    m_next = new QToolButton(m_body);
    m_next->setText(QStringLiteral("›"));
    m_next->setToolTip(tr("Next encoder page"));
    m_next->setFixedWidth(36);
    pager->addWidget(m_prev);
    pager->addWidget(m_next);
    auto *fine = new QToolButton(m_body);
    fine->setText(tr("Fine"));
    fine->setCheckable(true);
    fine->setToolTip(tr("Encoders move in small steps (or hold Shift)"));
    fine->setFixedWidth(76);
    auto *home = new QToolButton(m_body);
    home->setText(tr("Default"));
    home->setToolTip(tr("This category's default values for the selection, into the programmer. Press twice: every parameter"));
    home->setFixedWidth(76);
    auto *top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->addWidget(m_pageLabel, 1);
    if (m_main)
        top->addSpacing(18);   // the hide arrow sits here
    right->addLayout(top);
    right->addLayout(pager);
    right->addWidget(fine);
    right->addWidget(home);
    right->addStretch(1);
    h->addLayout(right);

    // Follow playbacks: a running cue or fader moves the dials just like the programmer does.
    auto *live = new QTimer(this);
    connect(live, &QTimer::timeout, this, [this]() {
        if (!isVisible())
            return;
        for (EncoderDial *d : m_dialArea->findChildren<EncoderDial *>())
            d->refresh();
        for (XyPad *pad : m_dialArea->findChildren<XyPad *>())
            pad->refresh();
        for (ShaperView *v : m_dialArea->findChildren<ShaperView *>())
            v->refresh();
    });
    live->start(100);

    connect(m_prev, &QToolButton::clicked, this, [this]() {
        m_page = std::max(0, m_page - 1);
        rebuild(true);
        if (!m_main)
            panelStateChanged(this);
    });
    connect(m_next, &QToolButton::clicked, this, [this]() {
        ++m_page;
        rebuild(true);
        if (!m_main)
            panelStateChanged(this);
    });
    connect(fine, &QToolButton::toggled, this, [this](bool on) {
        m_fine = on;
        for (QWidget *w : m_dialArea->findChildren<EncoderDial *>())
            w->update();
    });
    connect(home, &QToolButton::clicked, this, [this]() {
        // Twice within a second and a half: every parameter (Default Default).
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const bool twice = now - m_lastDefault < 1500;
        m_lastDefault = twice ? 0 : now;
        if (twice) {
            m_ctx->run(QStringLiteral("Default Default"));
            return;
        }
        Show *show = m_ctx->show;
        show->home(show->selection(), attributesFor(m_feature));
        emit m_ctx->status(tr("%1 to defaults. Press Default again for every parameter").arg(featureName(m_feature)));
    });
    connect(ctx->show, &Show::changed, this, [this](Show::Parts parts) {
        if (parts & (Show::ProgrammerPart | Show::PatchPart))
            rebuild();
    });
    connect(ctx, &AppContext::uiValueChanged, this, [this](const QString &key) {
        if (key == QLatin1String("encodersPerPage"))
            rebuild(true);
    });

    if (m_main) {
        // Tuck the bar away (and bring it back) with the arrow in its corner.
        m_arrow = new QToolButton(this);
        m_arrow->setAutoRaise(true);
        m_arrow->setFixedSize(26, 18);
        m_arrow->setStyleSheet(QStringLiteral("QToolButton { border: none; color: %1; font-size: 12px; padding: 0; }"
                                              "QToolButton:hover { color: %2; }").arg(theme::muted.name(), theme::text.name()));
        connect(m_arrow, &QToolButton::clicked, this, [this]() { setHidden(!m_hidden); });
        // MIDI: encoders by their place on the bar, whatever it shows.
        connect(ctx->midi, &MidiManager::encoderTurned, this, &EncoderBar::midiTurn);
        connect(ctx->midi, &MidiManager::encoderPushed, this, &EncoderBar::midiPush);
        connect(ctx->midi, &MidiManager::learnStateChanged, this, [this]() {
            for (EncoderDial *d : std::as_const(m_dials))
                d->update();
        });
        connect(ctx->midi, &MidiManager::bindingsChanged, this, [this]() {
            for (EncoderDial *d : std::as_const(m_dials))
                d->update();
        });
        setHidden(m_ctx->uiValue("encoderBarHidden", false).toBool());
    }
    rebuild(true);
}

void EncoderBar::setHidden(bool hidden)
{
    m_hidden = hidden;
    m_body->setVisible(!hidden);
    m_tucked->setVisible(hidden);
    setFixedHeight(hidden ? 20 : 156);
    m_arrow->setText(hidden ? QStringLiteral("▴") : QStringLiteral("▾"));
    m_arrow->setToolTip(hidden ? tr("Show the encoders") : tr("Hide the encoders"));
    if (m_ctx->uiValue("encoderBarHidden", false).toBool() != hidden)
        m_ctx->setUiValue("encoderBarHidden", hidden);
    placeArrow();
}

void EncoderBar::placeArrow()
{
    if (!m_arrow)
        return;
    m_arrow->move(width() - m_arrow->width() - 2, 1);
    m_arrow->raise();
}

void EncoderBar::midiTurn(int encoder, double delta)
{
    if (encoder < 1 || encoder > m_dials.size())
        return;
    // Fine with the Fine button, or while Shift is held on the keyboard.
    const bool fine = m_fine || (QGuiApplication::queryKeyboardModifiers() & Qt::ShiftModifier);
    m_dials[encoder - 1]->turn(delta * (fine ? 0.1 : 1.0));
}

void EncoderBar::midiPush(int encoder)
{
    if (encoder < 1 || encoder > m_dials.size())
        return;
    EncoderDial *d = m_dials[encoder - 1];
    const QPoint at = m_hidden || !d->isVisible() ? mapToGlobal(QPoint(width() / 2, 0))
                                                  : d->mapToGlobal(QPoint(d->width() / 2, d->height() / 2));
    d->showRanges(at);
}

QJsonObject EncoderBar::saveOptions() const
{
    return QJsonObject{{"category", int(m_preferred)}, {"page", m_page}, {"perPage", m_perPage}};
}

void EncoderBar::loadOptions(const QJsonObject &o)
{
    if (m_main)
        return;
    m_feature = m_preferred = static_cast<Feature>(std::clamp(o["category"].toInt(int(m_preferred)), 1, 7));
    m_page = std::max(0, o["page"].toInt());
    m_perPage = std::clamp(o["perPage"].toInt(0), 0, 20);
    rebuild(true);
}

void EncoderBar::buildOptions(QuickEditor *editor)
{
    editor->addNumber(tr("Encoders per page (0: Interface settings)"), 0, 20, m_perPage, [this](int n) {
        m_perPage = n;
        rebuild(true);
        panelStateChanged(this);
    });
}

bool EncoderBar::hasBlades() const
{
    for (const QString &a : attributesFor(Feature::Beam))
        if (a.startsWith(QLatin1String("Blade")))
            return true;
    return false;
}

QStringList EncoderBar::attributesFor(Feature f) const
{
    Show *show = m_ctx->show;
    QStringList attrs;
    for (int fid : show->selection())
        for (const QString &a : show->fixtureAttributes(fid))
            if (featureOf(a) == f && !attrs.contains(a))
                attrs << a;
    std::sort(attrs.begin(), attrs.end(), [](const QString &a, const QString &b) {
        return attributeSortKey(a) < attributeSortKey(b);
    });
    return attrs;
}

int EncoderBar::dialsPerPage() const
{
    int pad = m_feature == Feature::Position ? 232 + 80 : 0;   // the XY pad and Flip take room
    if (m_feature == Feature::Beam && hasBlades())
        pad = 142 + 112;   // the shaper and its buttons
    const int fit = std::max(2, (m_dialArea->width() - 8 - pad) / (kDialW + 4));
    const int most = m_perPage > 0 ? m_perPage : m_ctx->encodersPerPage();
    return std::max(1, std::min(fit, most));
}

void EncoderBar::resizeEvent(QResizeEvent *)
{
    placeArrow();
    rebuild();   // only rebuilds when the number of encoders that fit changes
}

void EncoderBar::rebuild(bool force)
{
    Show *show = m_ctx->show;
    const int count = int(show->selection().size());
    QString selText = count ? countOf(count, tr("fixture selected"), tr("fixtures selected")) : tr("Nothing selected");
    if (count && !show->activeSpread().isDefault())
        selText += tr(" · Spread: %1").arg(show->activeSpread().summary());
    m_selection->setText(selText);
    m_selection->setToolTip(selText);

    QSet<Feature> present;
    for (int fid : show->selection())
        for (const QString &a : show->fixtureAttributes(fid))
            present.insert(featureOf(a));
    const QList<Feature> features = allFeatures();
    for (int i = 0; i < m_categories.size(); ++i)
        m_categories[i]->setEnabled(count == 0 || present.contains(features[i]));
    // Show a category the selection actually has (the remembered choice comes back when it applies).
    const Feature preferred = m_main ? static_cast<Feature>(std::clamp(m_ctx->uiValue("encoderCategory", 1).toInt(1), 1, 7))
                                     : m_preferred;
    Feature shownFeature = present.contains(preferred) || count == 0 ? preferred : m_feature;
    if (count > 0 && !present.contains(shownFeature))
        for (Feature f : features)
            if (present.contains(f)) {
                shownFeature = f;
                break;
            }
    if (shownFeature != m_feature) {
        m_feature = shownFeature;
        m_page = 0;
    }
    for (int i = 0; i < m_categories.size(); ++i)
        if (features[i] == m_feature) {
            QSignalBlocker b(m_categories[i]);
            m_categories[i]->setChecked(true);
        }

    const QStringList attrs = attributesFor(m_feature);
    const int per = dialsPerPage();
    const int pages = std::max(1, int((attrs.size() + per - 1) / per));
    m_page = std::clamp(m_page, 0, pages - 1);
    m_pageLabel->setText(tr("Page %1 of %2").arg(m_page + 1).arg(pages));
    m_prev->setEnabled(m_page > 0);
    m_next->setEnabled(m_page < pages - 1);

    const QStringList shown = attrs.mid(m_page * per, per);
    const QString signature = QStringLiteral("%1|%2|%3|%4").arg(int(m_feature)).arg(per).arg(count > 0).arg(shown.join(','));
    if (!force && signature == m_signature) {
        for (QWidget *w : m_dialArea->findChildren<EncoderDial *>())
            w->update();
        return;
    }
    m_signature = signature;
    m_dials.clear();
    while (QLayoutItem *item = m_dialLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();          // gone now; deleted once any event it's handling finishes
            w->deleteLater();
        }
        delete item;
    }
    if (count == 0) {
        auto *l = new QLabel(tr("Select fixtures to use the encoders: tap a group, click rows in a fixture sheet, or type Fixture 1 Thru 4."), m_dialArea);
        l->setProperty("role", "muted");
        l->setWordWrap(true);
        m_dialLayout->addWidget(l);
        return;
    }
    if (shown.isEmpty()) {
        auto *l = new QLabel(tr("The selected fixtures have no %1 attributes.").arg(featureName(m_feature).toLower()), m_dialArea);
        l->setProperty("role", "muted");
        m_dialLayout->addWidget(l);
        m_dialLayout->addStretch(1);
        return;
    }
    if (m_feature == Feature::Position && (attrs.contains("Pan") || attrs.contains("Tilt"))) {
        // An XY pad for pan and tilt, and Flip.
        m_dialLayout->addWidget(new XyPad(m_ctx, [this]() { return m_fine; }, m_dialArea));
        auto *side = new QWidget(m_dialArea);
        auto *sv = new QVBoxLayout(side);
        sv->setContentsMargins(0, 0, 6, 0);
        sv->setSpacing(6);
        auto *flip = new QToolButton(side);
        flip->setText(tr("Flip"));
        flip->setToolTip(tr("Same direction, other way round: pan half a turn, tilt mirrored (or type Flip)"));
        flip->setFixedWidth(64);
        connect(flip, &QToolButton::clicked, this, [this]() { m_ctx->run(QStringLiteral("Flip")); });
        sv->addWidget(flip);
        sv->addStretch(1);
        m_dialLayout->addWidget(side);
    }
    if (m_feature == Feature::Beam && hasBlades()) {
        // The blades drawn over the beam, to drag.
        auto *view = new ShaperView(m_ctx, m_dialArea);
        view->setMode(ShaperView::Mode(m_ctx->uiValue("shaperMode", 0).toInt()));
        view->setLink(ShaperView::Link(m_ctx->uiValue("shaperLink", 0).toInt()));
        m_dialLayout->addWidget(view);
        auto *side = new QWidget(m_dialArea);
        auto *sv = new QVBoxLayout(side);
        sv->setContentsMargins(0, 0, 6, 0);
        sv->setSpacing(4);
        auto *mode = new QToolButton(side);
        mode->setFixedWidth(104);
        mode->setEnabled(view->hasAB());
        mode->setText(view->mode() == ShaperView::AB ? tr("A + B") : tr("Ins + Rot"));
        mode->setToolTip(tr("Ins + Rot: each blade's insertion and swivel. A + B: its two corners (fixtures that have them)."));
        connect(mode, &QToolButton::clicked, this, [this, view, mode]() {
            view->setMode(view->mode() == ShaperView::AB ? ShaperView::InsRot : ShaperView::AB);
            mode->setText(view->mode() == ShaperView::AB ? tr("A + B") : tr("Ins + Rot"));
            m_ctx->setUiValue("shaperMode", int(view->mode()));
        });
        auto *link = new QComboBox(side);
        link->setFixedWidth(104);
        link->addItems({tr("Link: none"), tr("Link: all"), tr("Link: parallel"), tr("Link: mirrored")});
        link->setCurrentIndex(std::clamp(m_ctx->uiValue("shaperLink", 0).toInt(), 0, 3));
        link->setToolTip(tr("None: one blade at a time. All: every blade moves with it. Parallel: the opposite blade moves "
                            "the same way, shifting the opening. Mirrored: the opposite blade closes in too."));
        connect(link, &QComboBox::currentIndexChanged, this, [this, view](int i) {
            view->setLink(ShaperView::Link(i));
            m_ctx->setUiValue("shaperLink", i);
        });
        auto *resetBlades = new QToolButton(side);
        resetBlades->setText(tr("Reset blades"));
        resetBlades->setFixedWidth(104);
        auto *resetAll = new QToolButton(side);
        resetAll->setText(tr("Reset shaper"));
        resetAll->setFixedWidth(104);
        resetAll->setToolTip(tr("Blades out, swivels and the module's rotation straight"));
        auto bladeAttrs = [this](bool withRotation) {
            QStringList out;
            for (const QString &a : attributesFor(Feature::Beam))
                if (a.startsWith(QLatin1String("Blade")) && (withRotation || !a.endsWith(QLatin1String("Rot"))))
                    out << a;
            return out;
        };
        connect(resetBlades, &QToolButton::clicked, this, [this, bladeAttrs]() {
            m_ctx->show->home(m_ctx->show->selection(), bladeAttrs(false));
        });
        connect(resetAll, &QToolButton::clicked, this, [this, bladeAttrs]() {
            m_ctx->show->home(m_ctx->show->selection(), bladeAttrs(true));
        });
        sv->addWidget(mode);
        sv->addWidget(link);
        sv->addWidget(resetBlades);
        sv->addWidget(resetAll);
        sv->addStretch(1);
        m_dialLayout->addWidget(side);
    }
    for (const QString &a : shown) {
        auto *d = new EncoderDial(m_ctx, a, [this]() { return m_fine; }, m_dialArea);
        if (m_main)
            d->setSlot(int(m_dials.size()) + 1);
        m_dials << d;
        m_dialLayout->addWidget(d);
    }
    m_dialLayout->addStretch(1);
}

} // namespace lumora
