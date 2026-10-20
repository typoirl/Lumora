// SPDX-License-Identifier: GPL-3.0-or-later
#include "effects.h"
#include "colorpicker.h"

#include "appcontext.h"
#include "fixturesheet.h"
#include "theme.h"

#include <QButtonGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QRegularExpressionValidator>
#include <QScreen>
#include <QSpinBox>
#include <QPushButton>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QSet>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace lumora {

// ===========================================================================
// Color math
// ===========================================================================



LinearRgb toLinear(const QColor &c)
{
    return {srgbToLinear(c.redF()), srgbToLinear(c.greenF()), srgbToLinear(c.blueF())};
}

QColor toDisplay(const LinearRgb &c)
{
    const double m = std::max({c.r, c.g, c.b, 1e-6});
    return QColor::fromRgbF(float(linearToSrgb(c.r / m)), float(linearToSrgb(c.g / m)), float(linearToSrgb(c.b / m)));
}

LinearRgb kelvinToLinear(double kelvin)
{
    // Tanner Helland's blackbody fit, in sRGB, then linearized.
    const double t = std::clamp(kelvin, 1000.0, 40000.0) / 100.0;
    double r, g, b;
    if (t <= 66) {
        r = 255;
        g = 99.4708025861 * std::log(t) - 161.1195681661;
    } else {
        r = 329.698727446 * std::pow(t - 60, -0.1332047592);
        g = 288.1221695283 * std::pow(t - 60, -0.0755148492);
    }
    if (t >= 66)
        b = 255;
    else if (t <= 19)
        b = 0;
    else
        b = 138.5177312231 * std::log(t - 10) - 305.0447927307;
    auto c = [](double v) { return srgbToLinear(std::clamp(v, 0.0, 255.0) / 255.0); };
    return {c(r), c(g), c(b)};
}

namespace {
LinearRgb normalized(LinearRgb c)
{
    c.r = std::max(0.0, c.r);
    c.g = std::max(0.0, c.g);
    c.b = std::max(0.0, c.b);
    const double m = std::max({c.r, c.g, c.b});
    if (m < 1e-9)
        return {1, 1, 1};
    return {c.r / m, c.g / m, c.b / m};
}
} // namespace

RawAttr colorValuesFor(const FixtureMode *m, const LinearRgb &color, const QColor &display, const QString &name)
{
    RawAttr out;
    if (!m)
        return out;
    const LinearRgb c = normalized(color);
    const RawAttr mix = emitterMix(m, c.r, c.g, c.b);
    if (!mix.isEmpty())
        return mix;   // RGB(W), amber, lime and the rest of the emitters, or a virtual color's hue and saturation
    if (m->channel("ColorSub_C")) {
        out.insert("ColorSub_C", 1 - c.r);
        out.insert("ColorSub_M", 1 - c.g);
        out.insert("ColorSub_Y", 1 - c.b);
        return out;
    }
    if (const ChannelDef *wheel = m->channel("Color1")) {
        double best = 1e9, value = -1;
        for (const ChannelSet &s : wheel->sets) {
            if (!name.isEmpty() && s.name.compare(name, Qt::CaseInsensitive) == 0) {
                value = (s.from + s.to) / 2.0;
                break;
            }
            const QColor sc = colorForName(s.name);
            if (!sc.isValid())
                continue;
            const double d = std::pow(sc.redF() - display.redF(), 2) + std::pow(sc.greenF() - display.greenF(), 2)
                             + std::pow(sc.blueF() - display.blueF(), 2);
            if (d < best) {
                best = d;
                value = (s.from + s.to) / 2.0;
            }
        }
        if (value >= 0)
            out.insert("Color1", value);
    }
    return out;
}

RawAttr temperatureValuesFor(const FixtureMode *m, double kelvin)
{
    // Whites-only fixtures blend warm and cool; everything else mixes the blackbody color.
    if (m && m->channel("ColorAdd_WW") && m->channel("ColorAdd_CW") && !m->channel("ColorAdd_R")) {
        const double t = std::clamp((kelvin - 2700.0) / (6500.0 - 2700.0), 0.0, 1.0);
        return {{"ColorAdd_WW", 1.0 - t}, {"ColorAdd_CW", t}};
    }
    const LinearRgb c = kelvinToLinear(kelvin);
    return colorValuesFor(m, c, toDisplay(c), QString());
}

int applyColorToSelection(Show *show, const LinearRgb &color, const QColor &display, const QString &name)
{
    int changed = 0;
    for (int fid : show->selection()) {
        const RawAttr values = colorValuesFor(show->fixtureMode(fid), color, display, name);
        for (auto it = values.begin(); it != values.end(); ++it)
            show->setAttribute(fid, it.key(), it.value());
        changed += !values.isEmpty();
    }
    return changed;
}

int applyTemperatureToSelection(Show *show, double kelvin)
{
    // Whites-only fixtures blend warm and cool; everything else mixes the blackbody color.
    QVector<int> mixers;
    int changed = 0;
    for (int fid : show->selection()) {
        const FixtureMode *m = show->fixtureMode(fid);
        if (m && m->channel("ColorAdd_WW") && m->channel("ColorAdd_CW") && !m->channel("ColorAdd_R")) {
            const double t = std::clamp((kelvin - 2700.0) / (6500.0 - 2700.0), 0.0, 1.0);
            show->setAttribute(fid, "ColorAdd_WW", 1.0 - t);
            show->setAttribute(fid, "ColorAdd_CW", t);
            ++changed;
        } else {
            mixers << fid;
        }
    }
    if (!mixers.isEmpty()) {
        const QVector<int> sel = show->selection();
        show->select(mixers);
        const LinearRgb c = kelvinToLinear(kelvin);
        changed += applyColorToSelection(show, c, toDisplay(c), QString());
        show->select(sel);
    }
    return changed;
}

bool selectionColor(Show *show, LinearRgb *out)
{
    for (int fid : show->selection()) {
        const FixtureMode *m = show->fixtureMode(fid);
        if (!m)
            continue;
        RawAttr v = show->lastOutput().value(fid);
        const AttrValues prog = show->programmerValues().value(fid);
        for (auto it = prog.begin(); it != prog.end(); ++it)
            v[it.key()] = show->resolveValue(fid, it.key(), it.value());
        if (m->channel("ColorAdd_R")) {
            const double w = v.value("ColorAdd_W", 0);
            *out = {v.value("ColorAdd_R", 0) + w, v.value("ColorAdd_G", 0) + w, v.value("ColorAdd_B", 0) + w};
            return out->r + out->g + out->b > 1e-4;
        }
        if (m->channel("ColorSub_C")) {
            *out = {1 - v.value("ColorSub_C", 0), 1 - v.value("ColorSub_M", 0), 1 - v.value("ColorSub_Y", 0)};
            return true;
        }
        if (const ChannelDef *wheel = m->channel("Color1")) {
            QColor c = colorForName(wheel->setNameFor(v.value("Color1", wheel->defaultValue)));
            if (c.isValid()) {
                *out = toLinear(c);
                return true;
            }
        }
    }
    return false;
}

// ===========================================================================
// Libraries
// ===========================================================================

const QVector<Swatch> &gelLibrary()
{
    // Gel numbers and names as printed in the manufacturers' swatch books.
    // Colors are approximate on-screen previews, not spectral data.
    static const QVector<Swatch> gels = [] {
        struct G {
            const char *maker, *code, *name;
            quint32 rgb;
        };
        static const G list[] = {
            {"Rosco", "R02", "Bastard Amber", 0xffd0a6}, {"Rosco", "R04", "Medium Bastard Amber", 0xffbf99},
            {"Rosco", "R08", "Pale Gold", 0xffd9a6}, {"Rosco", "R09", "Pale Amber Gold", 0xffc98a},
            {"Rosco", "R10", "Medium Yellow", 0xfff000}, {"Rosco", "R12", "Straw", 0xffe47a},
            {"Rosco", "R13", "Straw Tint", 0xffe2a8}, {"Rosco", "R15", "Deep Straw", 0xffbd00},
            {"Rosco", "R17", "Light Flame", 0xff9c6b}, {"Rosco", "R19", "Fire", 0xff4a10},
            {"Rosco", "R20", "Medium Amber", 0xffa244}, {"Rosco", "R21", "Golden Amber", 0xff8a1f},
            {"Rosco", "R23", "Orange", 0xff7a00}, {"Rosco", "R26", "Light Red", 0xff1028},
            {"Rosco", "R27", "Medium Red", 0xc5001a}, {"Rosco", "R33", "No Color Pink", 0xffd2dc},
            {"Rosco", "R39", "Skelton Exotic Sangria", 0xd1006e}, {"Rosco", "R44", "Middle Rose", 0xff7ab8},
            {"Rosco", "R46", "Magenta", 0xe0007a}, {"Rosco", "R51", "Surprise Pink", 0xe5c8ff},
            {"Rosco", "R54", "Special Lavender", 0xead9ff}, {"Rosco", "R57", "Lavender", 0xb18cff},
            {"Rosco", "R59", "Indigo", 0x3a00a0}, {"Rosco", "R60", "No Color Blue", 0xdfeeff},
            {"Rosco", "R62", "Booster Blue", 0xbfe0ff}, {"Rosco", "R64", "Light Steel Blue", 0x9bd3ff},
            {"Rosco", "R68", "Sky Blue", 0x3b8cff}, {"Rosco", "R69", "Brilliant Blue", 0x1f9bff},
            {"Rosco", "R74", "Night Blue", 0x1a39d0}, {"Rosco", "R80", "Primary Blue", 0x1d3fff},
            {"Rosco", "R83", "Medium Blue", 0x1033c8}, {"Rosco", "R85", "Deep Blue", 0x0d27b0},
            {"Rosco", "R89", "Moss Green", 0x47c95c}, {"Rosco", "R90", "Dark Yellow Green", 0x159a3a},
            {"Rosco", "R91", "Primary Green", 0x00a046}, {"Rosco", "R95", "Medium Blue Green", 0x00a7a0},
            {"Rosco", "R3202", "Full Blue (CTB)", 0xc0d6ff}, {"Rosco", "R3204", "Half Blue (CTB)", 0xdbe8ff},
            {"Rosco", "R3407", "Roscosun CTO", 0xffb26b}, {"Rosco", "R3408", "Roscosun 1/2 CTO", 0xffcf9b},

            {"Lee", "L002", "Rose Pink", 0xff3d8b}, {"Lee", "L003", "Lavender Tint", 0xf2dcef},
            {"Lee", "L007", "Pale Yellow", 0xfff4c2}, {"Lee", "L008", "Dark Salmon", 0xff8a5c},
            {"Lee", "L009", "Pale Amber Gold", 0xffd7a0}, {"Lee", "L010", "Medium Yellow", 0xffe800},
            {"Lee", "L013", "Straw Tint", 0xfde7b4}, {"Lee", "L015", "Deep Straw", 0xffb000},
            {"Lee", "L019", "Fire", 0xff3b0a}, {"Lee", "L020", "Medium Amber", 0xff9f3a},
            {"Lee", "L021", "Gold Amber", 0xff8a00}, {"Lee", "L024", "Scarlet", 0xff2a3d},
            {"Lee", "L026", "Bright Red", 0xe0001a}, {"Lee", "L027", "Medium Red", 0xc40018},
            {"Lee", "L035", "Light Pink", 0xffc3cf}, {"Lee", "L036", "Medium Pink", 0xff8fb1},
            {"Lee", "L046", "Dark Magenta", 0xd1007a}, {"Lee", "L048", "Rose Purple", 0xc45bd1},
            {"Lee", "L053", "Paler Lavender", 0xe7e3f7}, {"Lee", "L058", "Lavender", 0x9a6ad6},
            {"Lee", "L061", "Mist Blue", 0xd9ecfb}, {"Lee", "L068", "Sky Blue", 0x2f8fff},
            {"Lee", "L079", "Just Blue", 0x1d4fe0}, {"Lee", "L085", "Deeper Blue", 0x1029a8},
            {"Lee", "L089", "Moss Green", 0x3fbf6a}, {"Lee", "L090", "Dark Yellow Green", 0x2a9a2a},
            {"Lee", "L106", "Primary Red", 0xe00010}, {"Lee", "L117", "Steel Blue", 0x9fe3ea},
            {"Lee", "L118", "Light Blue", 0x00b5ff}, {"Lee", "L119", "Dark Blue", 0x0a2bd0},
            {"Lee", "L120", "Deep Blue", 0x0018b5}, {"Lee", "L124", "Dark Green", 0x00a65a},
            {"Lee", "L126", "Mauve", 0xa0007a}, {"Lee", "L128", "Bright Pink", 0xff1f9f},
            {"Lee", "L132", "Medium Blue", 0x0068ff}, {"Lee", "L139", "Primary Green", 0x009a1a},
            {"Lee", "L147", "Apricot", 0xff9c66}, {"Lee", "L152", "Pale Gold", 0xffd5b0},
            {"Lee", "L162", "Bastard Amber", 0xffd2b0}, {"Lee", "L164", "Flame Red", 0xff2a00},
            {"Lee", "L179", "Chrome Orange", 0xffa600}, {"Lee", "L181", "Congo Blue", 0x2a0090},
            {"Lee", "L195", "Zenith Blue", 0x2349ff}, {"Lee", "L200", "Double CT Blue", 0x9cbcff},
            {"Lee", "L201", "Full CT Blue", 0xc3d8ff}, {"Lee", "L202", "Half CT Blue", 0xdfe9ff},
            {"Lee", "L203", "Quarter CT Blue", 0xeef3ff}, {"Lee", "L204", "Full CT Orange", 0xffb26b},
            {"Lee", "L205", "Half CT Orange", 0xffcf9b}, {"Lee", "L206", "Quarter CT Orange", 0xffe2c2},
            {"Lee", "L709", "Electric Lilac", 0x8f7bff}, {"Lee", "L778", "Millennium Gold", 0xff9a1a},
        };
        QVector<Swatch> out;
        for (const G &g : list)
            out.append({QColor::fromRgb(g.rgb), QString::fromLatin1(g.code), QString::fromLatin1(g.name), QString::fromLatin1(g.maker)});
        return out;
    }();
    return gels;
}

const QVector<Swatch> &standardColors()
{
    static const QVector<Swatch> list = {
        {QColor(255, 0, 0), "", "Red", ""},         {QColor(255, 120, 0), "", "Orange", ""},
        {QColor(255, 230, 0), "", "Yellow", ""},    {QColor(160, 255, 0), "", "Lime", ""},
        {QColor(0, 255, 0), "", "Green", ""},       {QColor(0, 255, 160), "", "Sea green", ""},
        {QColor(0, 255, 255), "", "Cyan", ""},      {QColor(0, 140, 255), "", "Sky blue", ""},
        {QColor(0, 0, 255), "", "Blue", ""},        {QColor(120, 0, 255), "", "Purple", ""},
        {QColor(255, 0, 255), "", "Magenta", ""},   {QColor(255, 0, 120), "", "Pink", ""},
        {QColor(255, 255, 255), "", "White", ""},
    };
    return list;
}

// ===========================================================================
// SwatchGrid
// ===========================================================================

namespace {
constexpr int kCellW = 74;
constexpr int kCellH = 54;
constexpr int kCellGap = 5;

QColor readableOn(const QColor &c)
{
    const double lum = 0.2126 * srgbToLinear(c.redF()) + 0.7152 * srgbToLinear(c.greenF()) + 0.0722 * srgbToLinear(c.blueF());
    return lum > 0.35 ? QColor(20, 22, 26) : QColor(250, 250, 250);
}
} // namespace

SwatchGrid::SwatchGrid(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
}

void SwatchGrid::setSwatches(const QVector<Swatch> &s)
{
    m_swatches = s;
    setMinimumHeight(heightForWidth(width()));
    update();
}

int SwatchGrid::columns(int w) const
{
    return std::max(1, (w + kCellGap) / (kCellW + kCellGap));
}

int SwatchGrid::heightForWidth(int w) const
{
    const int rows = (int(m_swatches.size()) + columns(w) - 1) / columns(w);
    return std::max(kCellH, rows * kCellH + (rows - 1) * kCellGap);
}

QRect SwatchGrid::cell(int i) const
{
    const int cols = columns(width());
    const int extra = (width() - (cols * kCellW + (cols - 1) * kCellGap)) / cols;
    const int w = kCellW + extra;
    return {(i % cols) * (w + kCellGap), (i / cols) * (kCellH + kCellGap), w, kCellH};
}

void SwatchGrid::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < m_swatches.size(); ++i) {
        const Swatch &s = m_swatches[i];
        const QRectF r = QRectF(cell(i)).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(i == m_hover ? theme::text : theme::line, i == m_hover ? 1.5 : 1));
        p.setBrush(s.color);
        p.drawRoundedRect(r, 5, 5);
        const QColor ink = readableOn(s.color);
        p.setPen(ink);
        if (!s.code.isEmpty()) {
            p.setFont(theme::uiFont(8, QFont::DemiBold));
            p.drawText(r.adjusted(6, 4, -5, 0), Qt::AlignLeft | Qt::AlignTop, s.code);
        }
        p.setFont(theme::uiFont(8));
        QRectF nameRect = r.adjusted(6, s.code.isEmpty() ? 4 : 18, -5, -4);
        p.drawText(nameRect, Qt::AlignLeft | (s.code.isEmpty() ? Qt::AlignBottom : Qt::AlignTop) | Qt::TextWordWrap,
                   s.name);
    }
}

void SwatchGrid::mousePressEvent(QMouseEvent *ev)
{
    for (int i = 0; i < m_swatches.size(); ++i)
        if (cell(i).contains(ev->pos())) {
            emit picked(m_swatches[i]);
            return;
        }
}

void SwatchGrid::mouseMoveEvent(QMouseEvent *ev)
{
    int hover = -1;
    for (int i = 0; i < m_swatches.size(); ++i)
        if (cell(i).contains(ev->pos()))
            hover = i;
    if (hover != m_hover) {
        m_hover = hover;
        setToolTip(hover >= 0 ? (m_swatches[hover].code + ' ' + m_swatches[hover].name).trimmed() : QString());
        update();
    }
}

void SwatchGrid::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

void SwatchGrid::resizeEvent(QResizeEvent *)
{
    setMinimumHeight(heightForWidth(width()));
}

// ===========================================================================
// HueSatField
// ===========================================================================

HueSatField::HueSatField(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(110);
    setCursor(Qt::CrossCursor);
}

void HueSatField::setMarker(const QColor &c)
{
    m_marker = c;
    update();
}

void HueSatField::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (m_cache.size() != size()) {
        m_cache = QImage(size(), QImage::Format_RGB32);
        for (int y = 0; y < height(); ++y)
            for (int x = 0; x < width(); ++x)
                m_cache.setPixel(x, y, QColor::fromHsvF(float(x) / std::max(1, width() - 1) * 0.999f,
                                                         1.0f - float(y) / std::max(1, height() - 1), 1.0f).rgb());
    }
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()), 6, 6);
    p.setClipPath(clip);
    p.drawImage(0, 0, m_cache);
    p.setClipping(false);
    if (m_marker.isValid()) {
        float h, s, v;
        m_marker.getHsvF(&h, &s, &v);
        if (h < 0)
            h = 0;
        const QPointF pt(h * (width() - 1), (1.0 - s) * (height() - 1));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::black, 3));
        p.drawEllipse(pt, 6, 6);
        p.setPen(QPen(Qt::white, 1.5));
        p.drawEllipse(pt, 6, 6);
    }
}

void HueSatField::pickAt(const QPoint &pt)
{
    const double h = std::clamp(double(pt.x()) / std::max(1, width() - 1), 0.0, 0.999);
    const double s = std::clamp(1.0 - double(pt.y()) / std::max(1, height() - 1), 0.0, 1.0);
    const QColor c = QColor::fromHsvF(float(h), float(s), 1.0f);
    m_marker = c;
    update();
    emit picked(c);
}

void HueSatField::mousePressEvent(QMouseEvent *ev) { pickAt(ev->pos()); }
void HueSatField::mouseMoveEvent(QMouseEvent *ev)
{
    if (ev->buttons() & Qt::LeftButton)
        pickAt(ev->pos());
}

// ===========================================================================
// KelvinBar
// ===========================================================================

KelvinBar::KelvinBar(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(48);
    setCursor(Qt::PointingHandCursor);
}

void KelvinBar::setKelvin(double k)
{
    m_kelvin = k;
    update();
}

void KelvinBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF bar(0, 0, width(), height() - 18);
    QLinearGradient g(bar.topLeft(), bar.topRight());
    for (int i = 0; i <= 16; ++i) {
        const double t = i / 16.0;
        g.setColorAt(t, toDisplay(kelvinToLinear(kMin + (kMax - kMin) * t)));
    }
    p.setPen(QPen(theme::line, 1));
    p.setBrush(g);
    p.drawRoundedRect(bar.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    p.setFont(theme::uiFont(8));
    p.setPen(theme::muted);
    for (int k : {2000, 3200, 4300, 5600, 7000, 9000}) {
        const double x = (k - kMin) / (kMax - kMin) * width();
        p.drawLine(QPointF(x, bar.bottom() + 2), QPointF(x, bar.bottom() + 5));
        const double left = std::clamp(x - 24, 0.0, width() - 48.0);
        p.drawText(QRectF(left, bar.bottom() + 4, 48, 14), Qt::AlignCenter, QString::number(k));
    }
    if (m_kelvin >= kMin) {
        const double x = (m_kelvin - kMin) / (kMax - kMin) * width();
        p.setPen(QPen(Qt::black, 4));
        p.drawLine(QPointF(x, bar.top() + 2), QPointF(x, bar.bottom() - 2));
        p.setPen(QPen(Qt::white, 2));
        p.drawLine(QPointF(x, bar.top() + 2), QPointF(x, bar.bottom() - 2));
    }
}

void KelvinBar::pickAt(int x)
{
    const double t = std::clamp(double(x) / std::max(1, width()), 0.0, 1.0);
    const double k = std::round((kMin + (kMax - kMin) * t) / 50.0) * 50.0;
    setKelvin(k);
    emit picked(k);
}

void KelvinBar::mousePressEvent(QMouseEvent *ev) { pickAt(ev->pos().x()); }
void KelvinBar::mouseMoveEvent(QMouseEvent *ev)
{
    if (ev->buttons() & Qt::LeftButton)
        pickAt(ev->pos().x());
}

// ===========================================================================
// CIE diagram
// ===========================================================================

namespace {
// CIE 1931 2° spectral locus (x, y), 380–700 nm.
const double kLocus[][2] = {
    {0.1741, 0.0050}, {0.1738, 0.0049}, {0.1733, 0.0048}, {0.1726, 0.0048}, {0.1714, 0.0051}, {0.1689, 0.0069},
    {0.1644, 0.0109}, {0.1566, 0.0177}, {0.1440, 0.0297}, {0.1241, 0.0578}, {0.1096, 0.0868}, {0.0913, 0.1327},
    {0.0687, 0.2007}, {0.0454, 0.2950}, {0.0235, 0.4127}, {0.0082, 0.5384}, {0.0039, 0.6548}, {0.0139, 0.7502},
    {0.0389, 0.8120}, {0.0743, 0.8338}, {0.1142, 0.8262}, {0.1547, 0.8059}, {0.1929, 0.7816}, {0.2296, 0.7543},
    {0.2658, 0.7243}, {0.3016, 0.6923}, {0.3373, 0.6589}, {0.3731, 0.6245}, {0.4087, 0.5896}, {0.4441, 0.5547},
    {0.4788, 0.5202}, {0.5125, 0.4866}, {0.5448, 0.4544}, {0.5752, 0.4242}, {0.6029, 0.3965}, {0.6270, 0.3725},
    {0.6658, 0.3340}, {0.6915, 0.3083}, {0.7079, 0.2920}, {0.7190, 0.2809}, {0.7260, 0.2740}, {0.7300, 0.2700},
    {0.7334, 0.2666}, {0.7347, 0.2653},
};
// sRGB / Rec.709 primaries: a fair stand-in for a typical RGB LED fixture's gamut.
const double kPrimaries[3][2] = {{0.64, 0.33}, {0.30, 0.60}, {0.15, 0.06}};
constexpr double kXMax = 0.8, kYMax = 0.9;

LinearRgb xyToLinear(double x, double y)
{
    if (y < 1e-6)
        return {};
    const double X = x / y, Y = 1.0, Z = (1 - x - y) / y;
    return {3.2406 * X - 1.5372 * Y - 0.4986 * Z, -0.9689 * X + 1.8758 * Y + 0.0415 * Z,
            0.0557 * X - 0.2040 * Y + 1.0570 * Z};
}

void linearToXy(const LinearRgb &c, double *x, double *y)
{
    const double X = 0.4124 * c.r + 0.3576 * c.g + 0.1805 * c.b;
    const double Y = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
    const double Z = 0.0193 * c.r + 0.1192 * c.g + 0.9505 * c.b;
    const double sum = X + Y + Z;
    *x = sum > 1e-9 ? X / sum : 0.3127;
    *y = sum > 1e-9 ? Y / sum : 0.3290;
}

// The convex hull of points (Andrew's monotone chain), counter-clockwise; duplicates removed.
QVector<QPointF> convexHull(QVector<QPointF> pts)
{
    std::sort(pts.begin(), pts.end(), [](const QPointF &a, const QPointF &b) {
        return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y());
    });
    pts.erase(std::unique(pts.begin(), pts.end(), [](const QPointF &a, const QPointF &b) {
        return std::abs(a.x() - b.x()) < 1e-4 && std::abs(a.y() - b.y()) < 1e-4;
    }), pts.end());
    if (pts.size() < 3)
        return pts;
    auto cross = [](const QPointF &o, const QPointF &a, const QPointF &b) {
        return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
    };
    QVector<QPointF> h(2 * pts.size());
    int k = 0;
    for (int i = 0; i < pts.size(); ++i) {
        while (k >= 2 && cross(h[k - 2], h[k - 1], pts[i]) <= 1e-9) --k;
        h[k++] = pts[i];
    }
    for (int i = int(pts.size()) - 2, t = k + 1; i >= 0; --i) {
        while (k >= t && cross(h[k - 2], h[k - 1], pts[i]) <= 1e-9) --k;
        h[k++] = pts[i];
    }
    h.resize(k - 1);
    return h;
}

QPointF nearestOnSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF d = b - a;
    const double len2 = d.x() * d.x() + d.y() * d.y();
    if (len2 < 1e-12)
        return a;
    const double t = std::clamp(((p.x() - a.x()) * d.x() + (p.y() - a.y()) * d.y()) / len2, 0.0, 1.0);
    return a + d * t;
}

QPolygonF locusPolygon()
{
    QPolygonF poly;
    for (const auto &pt : kLocus)
        poly << QPointF(pt[0], pt[1]);
    return poly;
}
} // namespace

CieDiagram::CieDiagram(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(200, 200);
    setCursor(Qt::CrossCursor);
}

void CieDiagram::setGamut(const QVector<QPointF> &xy, const QString &label)
{
    const QVector<QPointF> hull = convexHull(xy);
    if (hull == m_gamut && label == m_gamutLabel)
        return;
    m_gamut = hull;
    m_gamutLabel = label;
    update();
}

QPointF CieDiagram::clampToGamut(const QPointF &xy) const
{
    QVector<QPointF> g = m_gamut;
    if (g.isEmpty())
        for (const auto &pt : kPrimaries)
            g << QPointF(pt[0], pt[1]);
    if (g.size() == 1)
        return g.first();
    if (g.size() >= 3 && QPolygonF(g).containsPoint(xy, Qt::OddEvenFill))
        return xy;
    // Outside: the nearest point on its outline (or on the line, for two emitters).
    QPointF best = g.first();
    double bestD = 1e9;
    for (int i = 0; i < g.size(); ++i) {
        if (g.size() == 2 && i == 1)
            break;
        const QPointF q = nearestOnSegment(xy, g[i], g[(i + 1) % g.size()]);
        const double d = std::hypot(q.x() - xy.x(), q.y() - xy.y());
        if (d < bestD) {
            bestD = d;
            best = q;
        }
    }
    return best;
}

void CieDiagram::setMarker(const LinearRgb &c, bool valid)
{
    m_marker = c;
    m_hasMarker = valid;
    update();
}

QRectF CieDiagram::plotRect() const
{
    // Keep the diagram's aspect ratio, centered.
    const double aspect = kXMax / kYMax;
    double w = width() - 16, h = height() - 16;
    if (w / h > aspect)
        w = h * aspect;
    else
        h = w / aspect;
    return {(width() - w) / 2.0, (height() - h) / 2.0, w, h};
}

QPointF CieDiagram::toWidget(double x, double y) const
{
    const QRectF r = plotRect();
    return {r.left() + x / kXMax * r.width(), r.bottom() - y / kYMax * r.height()};
}

void CieDiagram::resizeEvent(QResizeEvent *)
{
    m_cache = QImage();
}

void CieDiagram::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = plotRect();
    if (m_cache.isNull()) {
        m_cache = QImage(size(), QImage::Format_ARGB32_Premultiplied);
        m_cache.fill(Qt::transparent);
        const QPolygonF locus = locusPolygon();
        for (int py = int(r.top()); py <= int(r.bottom()); ++py)
            for (int px = int(r.left()); px <= int(r.right()); ++px) {
                const double x = (px - r.left()) / r.width() * kXMax;
                const double y = (r.bottom() - py) / r.height() * kYMax;
                if (!locus.containsPoint(QPointF(x, y), Qt::OddEvenFill))
                    continue;
                LinearRgb c = xyToLinear(x, y);
                c = normalized(c);
                QColor col = QColor::fromRgbF(float(linearToSrgb(c.r)), float(linearToSrgb(c.g)), float(linearToSrgb(c.b)));
                if (px >= 0 && py >= 0 && px < m_cache.width() && py < m_cache.height())
                    m_cache.setPixel(px, py, col.rgba());
            }
    }
    p.drawImage(0, 0, m_cache);

    // Locus outline
    QPolygonF outline;
    for (const auto &pt : kLocus)
        outline << toWidget(pt[0], pt[1]);
    outline << outline.first();
    p.setPen(QPen(theme::muted, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(outline);

    // What the fixtures can make: their emitters' outline (a typical RGB LED's with nothing selected).
    QPolygonF gamut;
    if (m_gamut.isEmpty())
        for (const auto &pt : kPrimaries)
            gamut << toWidget(pt[0], pt[1]);
    else
        for (const QPointF &pt : m_gamut)
            gamut << toWidget(pt.x(), pt.y());
    if (gamut.size() >= 3) {
        p.setPen(QPen(QColor(0, 0, 0, 160), 3));
        p.drawPolygon(gamut);
        p.setPen(QPen(Qt::white, 1.5));
        p.drawPolygon(gamut);
    } else if (gamut.size() == 2) {   // two emitters (warm and cool white, say): the line between them
        p.setPen(QPen(QColor(0, 0, 0, 160), 6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(gamut[0], gamut[1]);
        p.setPen(QPen(Qt::white, 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(gamut[0], gamut[1]);
    } else if (gamut.size() == 1) {   // one color only
        p.setPen(QPen(Qt::black, 2));
        p.setBrush(Qt::white);
        p.drawEllipse(gamut[0], 5, 5);
        p.setBrush(Qt::NoBrush);
    }

    // White point
    const QPointF d65 = toWidget(0.3127, 0.3290);
    p.setPen(QPen(Qt::black, 1));
    p.setBrush(Qt::white);
    p.drawEllipse(d65, 2.5, 2.5);

    if (m_hasMarker) {
        double x, y;
        linearToXy(m_marker, &x, &y);
        const QPointF pt = toWidget(x, y);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::black, 3));
        p.drawEllipse(pt, 7, 7);
        p.setPen(QPen(Qt::white, 1.5));
        p.drawEllipse(pt, 7, 7);
    }

    p.setFont(theme::uiFont(8));
    p.setPen(theme::muted);
    {
        // Right-aligned when it fits; otherwise from its start, cut off at the end.
        const QString label = m_gamutLabel.isEmpty() ? tr("Typical RGB LED gamut on CIE 1931") : tr("%1 on CIE 1931").arg(m_gamutLabel);
        const QRectF lr(4, r.top(), width() - 8, 16);
        if (p.fontMetrics().horizontalAdvance(label) <= lr.width())
            p.drawText(lr, Qt::AlignRight | Qt::AlignTop, label);
        else
            p.drawText(lr, Qt::AlignLeft | Qt::AlignTop, p.fontMetrics().elidedText(label, Qt::ElideRight, int(lr.width())));
    }
}

void CieDiagram::pickAt(const QPointF &pt)
{
    const QRectF r = plotRect();
    double x = std::clamp((pt.x() - r.left()) / r.width() * kXMax, 0.0, kXMax);
    double y = std::clamp((r.bottom() - pt.y()) / r.height() * kYMax, 0.001, kYMax);
    // Outside the outline the fixtures can't make the color: pulled in to the nearest they can.
    const QPointF in = clampToGamut(QPointF(x, y));
    LinearRgb c = normalized(xyToLinear(in.x(), in.y()));
    c = {std::max(0.0, c.r), std::max(0.0, c.g), std::max(0.0, c.b)};
    m_marker = c;
    m_hasMarker = true;
    update();
    emit picked(c);
}

void CieDiagram::mousePressEvent(QMouseEvent *ev) { pickAt(ev->position()); }
void CieDiagram::mouseMoveEvent(QMouseEvent *ev)
{
    if (ev->buttons() & Qt::LeftButton)
        pickAt(ev->position());
}

// ===========================================================================
// Panel
// ===========================================================================

namespace {
QWidget *scrollWrap(QWidget *content, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *holder = new QWidget(scroll);
    auto *v = new QVBoxLayout(holder);
    v->setContentsMargins(0, 0, 6, 0);
    v->addWidget(content);
    v->addStretch(1);
    scroll->setWidget(holder);
    return scroll;
}

QToolButton *chip(const QString &text, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setText(text);
    b->setCheckable(true);
    b->setStyleSheet(QStringLiteral("QToolButton { padding: 3px 10px; border-radius: 12px; }"));
    return b;
}
} // namespace

ColorPickerPanel::ColorPickerPanel(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    qRegisterMetaType<lumora::Swatch>();
    qRegisterMetaType<lumora::LinearRgb>();
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);

    // The current color (chip and name) sits at the end of the tab row: no separate title row.
    m_chip = new QLabel(this);
    m_chip->setFixedSize(18, 18);
    m_lastName = new QLabel(this);
    m_lastName->setProperty("role", "muted");

    auto *tabs = new QHBoxLayout;
    tabs->setSpacing(4);
    const QStringList names = {tr("Gels"), tr("Standard"), tr("Temperature"), tr("Gamut")};
    auto *group = new QButtonGroup(this);
    for (int i = 0; i < names.size(); ++i) {
        auto *b = new QToolButton(this);
        b->setText(names[i]);
        b->setCheckable(true);
        b->setStyleSheet(QStringLiteral(
            "QToolButton { background: transparent; border: none; border-bottom: 2px solid transparent; border-radius: 0;"
            " padding: 3px 8px; color: %1; }"
            "QToolButton:hover { color: %2; }"
            "QToolButton:checked { color: %3; border-bottom-color: %3; background: transparent; }")
                             .arg(theme::muted.name(), theme::text.name(), theme::featureColor(Feature::Color).name()));
        group->addButton(b, i);
        tabs->addWidget(b);
        m_tabs << b;
    }
    tabs->addStretch(1);
    tabs->addWidget(m_lastName);
    tabs->addWidget(m_chip);
    v->addLayout(tabs);
    connect(group, &QButtonGroup::idClicked, this, [this](int i) {
        showTab(i);
        panelStateChanged(this);
    });

    m_stack = new QStackedWidget(this);
    v->addWidget(m_stack, 1);

    // ---- Gels
    auto *gelPage = new QWidget(m_stack);
    auto *gv = new QVBoxLayout(gelPage);
    gv->setContentsMargins(0, 0, 0, 0);
    gv->setSpacing(6);
    auto *gelBar = new QHBoxLayout;
    gelBar->setSpacing(4);
    auto *makers = new QButtonGroup(this);
    for (const QString &m : {tr("All"), QStringLiteral("Rosco"), QStringLiteral("Lee")}) {
        auto *b = chip(m, gelPage);
        b->setChecked(m == tr("All"));
        makers->addButton(b);
        gelBar->addWidget(b);
        connect(b, &QToolButton::clicked, this, [this, m]() {
            m_maker = m == tr("All") ? QString() : m;
            filterGels();
        });
    }
    m_gelSearch = new QLineEdit(gelPage);
    m_gelSearch->setPlaceholderText(tr("Number or name"));
    m_gelSearch->setClearButtonEnabled(true);
    gelBar->addWidget(m_gelSearch, 1);
    gv->addLayout(gelBar);
    m_gels = new SwatchGrid(gelPage);
    gv->addWidget(scrollWrap(m_gels, gelPage), 1);
    auto *gelNote = new QLabel(tr("On-screen colors are approximate."), gelPage);
    gelNote->setProperty("role", "muted");
    gv->addWidget(gelNote);
    m_stack->addWidget(gelPage);
    connect(m_gelSearch, &QLineEdit::textChanged, this, &ColorPickerPanel::filterGels);
    connect(m_gels, &SwatchGrid::picked, this, [this](const Swatch &s) {
        apply(toLinear(s.color), s.color, (s.code + ' ' + s.name).trimmed());
    });

    // ---- Standard
    auto *stdPage = new QWidget(m_stack);
    auto *sv = new QVBoxLayout(stdPage);
    sv->setContentsMargins(0, 0, 0, 0);
    sv->setSpacing(8);
    auto *stdGrid = new SwatchGrid(stdPage);
    stdGrid->setSwatches(standardColors());
    sv->addWidget(stdGrid);
    m_hueSat = new HueSatField(stdPage);
    sv->addWidget(m_hueSat, 1);
    m_stack->addWidget(scrollWrap(stdPage, m_stack));
    connect(stdGrid, &SwatchGrid::picked, this, [this](const Swatch &s) { apply(toLinear(s.color), s.color, s.name); });
    connect(m_hueSat, &HueSatField::picked, this, [this](const QColor &c) {
        apply(toLinear(c), c, tr("Hue %1°, saturation %2%").arg(std::max(0, c.hsvHue())).arg(std::lround(c.hsvSaturationF() * 100)), false);
    });

    // ---- Temperature
    auto *tempPage = new QWidget(m_stack);
    auto *tv = new QVBoxLayout(tempPage);
    tv->setContentsMargins(0, 0, 0, 0);
    tv->setSpacing(10);
    m_kelvinLabel = new QLabel(tr("Pick a color temperature"), tempPage);
    m_kelvinLabel->setFont(theme::uiFont(14, QFont::DemiBold));
    tv->addWidget(m_kelvinLabel);
    m_kelvin = new KelvinBar(tempPage);
    tv->addWidget(m_kelvin);
    auto *presets = new QGridLayout;
    presets->setSpacing(6);
    const QList<QPair<int, QString>> temps = {
        {1900, tr("Candle")}, {2700, tr("Warm white")}, {3200, tr("Tungsten")}, {4300, tr("Moonlight")},
        {5600, tr("Daylight")}, {6500, tr("Overcast")}, {8000, tr("Blue sky")}, {10000, tr("Deep sky")},
    };
    for (int i = 0; i < temps.size(); ++i) {
        auto *b = new QToolButton(tempPage);
        b->setText(QStringLiteral("%1  %2 K").arg(temps[i].second).arg(temps[i].first));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        const QColor c = toDisplay(kelvinToLinear(temps[i].first));
        b->setStyleSheet(QStringLiteral("QToolButton { border-left: 6px solid %1; text-align: left; padding: 5px 8px; }").arg(c.name()));
        const int k = temps[i].first;
        const QString label = temps[i].second;
        connect(b, &QToolButton::clicked, this, [this, k, label]() {
            m_kelvin->setKelvin(k);
            m_kelvinName = label;
            emit m_kelvin->picked(k);
            m_kelvinName.clear();
        });
        presets->addWidget(b, i / 2, i % 2);
    }
    tv->addLayout(presets);
    tv->addStretch(1);
    m_stack->addWidget(scrollWrap(tempPage, m_stack));
    connect(m_kelvin, &KelvinBar::picked, this, [this](double k) {
        m_kelvinLabel->setText(QStringLiteral("%1 K").arg(int(k)));
        if (m_ctx->show->selection().isEmpty()) {
            emit m_ctx->status(tr("Select fixtures first"));
            return;
        }
        const int n = applyTemperatureToSelection(m_ctx->show, k);
        const QColor c = toDisplay(kelvinToLinear(k));
        m_chip->setStyleSheet(QStringLiteral("background: %1; border: 1px solid %2; border-radius: 4px;").arg(c.name(), theme::line.name()));
        const QString name = m_kelvinName.isEmpty() ? QStringLiteral("%1 K").arg(int(k))
                                                     : QStringLiteral("%1 %2 K").arg(m_kelvinName).arg(int(k));
        m_lastName->setText(name);
        m_ctx->show->setColorName(n > 0 ? name : QString());
        emit m_ctx->status(tr("%1 K applied to %2").arg(int(k)).arg(countOf(n, tr("fixture"), tr("fixtures"))));
    });

    // ---- Gamut
    m_cie = new CieDiagram(m_stack);
    m_stack->addWidget(m_cie);
    connect(m_cie, &CieDiagram::picked, this, [this](const LinearRgb &c) { apply(c, toDisplay(c), tr("Gamut pick"), false); });

    connect(ctx->show, &Show::changed, this, [this](Show::Parts p) {
        if (p & (Show::ProgrammerPart | Show::PatchPart))
            refreshMarkers();
    });
    filterGels();
    showTab(0);
    refreshMarkers();
}

void ColorPickerPanel::showTab(int i)
{
    i = std::clamp(i, 0, int(m_tabs.size()) - 1);
    m_stack->setCurrentIndex(i);
    m_tabs[i]->setChecked(true);
    setWindowTitle(tr("Color · %1").arg(m_tabs[i]->text()));
}

QJsonObject ColorPickerPanel::saveOptions() const
{
    return m_stack->currentIndex() > 0 ? QJsonObject{{"tab", m_stack->currentIndex()}} : QJsonObject();
}

void ColorPickerPanel::loadOptions(const QJsonObject &o)
{
    showTab(o["tab"].toInt(0));
}

void ColorPickerPanel::filterGels()
{
    const QString q = m_gelSearch->text().trimmed();
    QVector<Swatch> out;
    for (const Swatch &s : gelLibrary()) {
        if (!m_maker.isEmpty() && s.maker != m_maker)
            continue;
        // "201" should find L201; "blue" finds every blue
        if (!q.isEmpty() && !s.code.contains(q, Qt::CaseInsensitive) && !s.name.contains(q, Qt::CaseInsensitive))
            continue;
        out << s;
    }
    m_gels->setSwatches(out);
}

void ColorPickerPanel::apply(const LinearRgb &c, const QColor &display, const QString &name, bool namesPreset)
{
    if (m_ctx->show->selection().isEmpty()) {
        emit m_ctx->status(tr("Select fixtures first, then pick a color"));
        return;
    }
    const int n = applyColorToSelection(m_ctx->show, c, display, name);
    m_ctx->show->setColorName(namesPreset && n > 0 ? name : QString());
    m_chip->setStyleSheet(QStringLiteral("background: %1; border: 1px solid %2; border-radius: 4px;").arg(display.name(), theme::line.name()));
    m_lastName->setText(name);
    if (n == 0)
        emit m_ctx->status(tr("The selected fixtures have no color mixing or color wheel"));
    else
        emit m_ctx->status(tr("%1 applied to %2").arg(name, countOf(n, tr("fixture"), tr("fixtures"))));
}

void ColorPickerPanel::refreshMarkers()
{
    LinearRgb c;
    const bool ok = selectionColor(m_ctx->show, &c);
    m_cie->setMarker(c, ok);
    m_hueSat->setMarker(ok ? toDisplay(c) : QColor());
    // The gamut: every color the selected fixtures' emitters can make between them.
    Show *show = m_ctx->show;
    QVector<QPointF> xy;
    QStringList types;
    QSet<const FixtureMode *> seen;
    for (int fid : show->selection()) {
        const FixtureMode *m = show->fixtureMode(fid);
        if (!m || seen.contains(m))
            continue;
        seen.insert(m);
        if (const FixtureType *t = show->fixtureType(fid); t && !types.contains(t->name))
            types << t->name;
        for (const auto &e : emitterColors(m)) {
            double x, y;
            linearToXy({e[0], e[1], e[2]}, &x, &y);
            xy << QPointF(x, y);
        }
    }
    m_cie->setGamut(xy, xy.isEmpty() ? QString()
                        : types.size() == 1 ? tr("Gamut of %1").arg(types.first())
                                            : tr("Gamut of %1 fixture types").arg(types.size()));
}

std::optional<QString> pickAppearanceColor(QWidget *parent, const QString &title, const QString &current)
{
    QDialog d(parent);
    d.setWindowTitle(title);
    auto *v = new QVBoxLayout(&d);
    v->setSpacing(10);

    // The color as hex and as red, green and blue (0–255): type either, or pick below.
    auto *row = new QHBoxLayout;
    row->setSpacing(6);
    auto *chip = new QLabel(&d);
    chip->setFixedSize(36, 26);
    auto *hex = new QLineEdit(&d);
    hex->setPlaceholderText(QObject::tr("No color"));
    hex->setMaxLength(7);
    hex->setFixedWidth(92);
    hex->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("#?[0-9A-Fa-f]{0,6}")), hex));
    hex->setToolTip(QObject::tr("Hex: #RRGGBB"));
    row->addWidget(chip);
    row->addWidget(new QLabel(QObject::tr("Hex"), &d));
    row->addWidget(hex);
    row->addSpacing(8);
    QSpinBox *rgb[3];
    const char *names[3] = {"R", "G", "B"};
    for (int i = 0; i < 3; ++i) {
        rgb[i] = new QSpinBox(&d);
        rgb[i]->setRange(0, 255);
        rgb[i]->setFixedWidth(64);
        row->addWidget(new QLabel(QString::fromLatin1(names[i]), &d));
        row->addWidget(rgb[i]);
    }
    row->addStretch(1);
    v->addLayout(row);

    QString chosen = current;
    bool updating = false;
    auto *hueSat = new HueSatField(&d);
    auto show = [&](const QString &c, QWidget *from) {
        chosen = c;
        updating = true;
        chip->setStyleSheet(QStringLiteral("background: %1; border: 1px solid %2; border-radius: 4px;")
                                .arg(c.isEmpty() ? QStringLiteral("transparent") : c, theme::line.name()));
        const QColor col(c);
        if (from != hex)
            hex->setText(c.isEmpty() ? QString() : c.toUpper());
        if (from != rgb[0] && from != rgb[1] && from != rgb[2]) {
            rgb[0]->setValue(c.isEmpty() ? 0 : col.red());
            rgb[1]->setValue(c.isEmpty() ? 0 : col.green());
            rgb[2]->setValue(c.isEmpty() ? 0 : col.blue());
        }
        if (from != hueSat)
            hueSat->setMarker(c.isEmpty() ? QColor() : col);
        updating = false;
    };
    show(current, nullptr);
    QObject::connect(hex, &QLineEdit::textEdited, &d, [&](const QString &t) {
        QString h = t.trimmed();
        if (!h.startsWith('#'))
            h.prepend('#');
        if (h.size() == 7 && QColor::isValidColorName(h))
            show(h.toLower(), hex);
        else if (h == QLatin1String("#"))
            show(QString(), hex);
    });
    for (int i = 0; i < 3; ++i)
        QObject::connect(rgb[i], &QSpinBox::valueChanged, &d, [&, i](int) {
            if (updating)
                return;
            show(QColor(rgb[0]->value(), rgb[1]->value(), rgb[2]->value()).name(), rgb[i]);
        });

    // Ready-made colors, then any hue and saturation. Both a fixed height, so the dialog stays
    // short enough for its buttons to be on screen.
    auto *grid = new SwatchGrid(&d);
    grid->setSwatches(standardColors());
    grid->setFixedHeight(grid->heightForWidth(420));
    v->addWidget(grid);
    hueSat->setFixedHeight(150);
    v->addWidget(hueSat);
    QObject::connect(grid, &SwatchGrid::picked, &d, [&](const Swatch &sw) { show(sw.color.name(), grid); });
    QObject::connect(hueSat, &HueSatField::picked, &d, [&](const QColor &c) { show(c.name(), hueSat); });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    QPushButton *none = buttons->addButton(QObject::tr("No color"), QDialogButtonBox::ResetRole);
    QObject::connect(none, &QPushButton::clicked, &d, [&]() {
        show(QString(), nullptr);
        d.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    v->addWidget(buttons);
    d.setMinimumWidth(460);
    d.adjustSize();
    // Never taller than the screen it opens on.
    if (const QScreen *screen = parent ? parent->screen() : QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        d.resize(std::min(d.width(), avail.width() - 40), std::min(d.height(), avail.height() - 60));
    }
    if (d.exec() != QDialog::Accepted)
        return std::nullopt;
    return chosen;
}

} // namespace lumora
