// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "types.h"

#include <QColor>
#include <QImage>
#include <QVector>
#include <QWidget>

#include <optional>

#include "paneloptions.h"

class QLabel;
class QLineEdit;
class QStackedWidget;
class QToolButton;

namespace lumora {

class AppContext;
class Show;

// ---- Color math -----------------------------------------------------------

struct LinearRgb {
    double r = 0, g = 0, b = 0;
};

// srgbToLinear / linearToSrgb live in types.h.
LinearRgb toLinear(const QColor &c);
QColor toDisplay(const LinearRgb &c);
LinearRgb kelvinToLinear(double kelvin);

// Applies a color to every selected fixture, using whatever it has:
// RGB(W/A) mixing, CMY, or the nearest color-wheel slot. Returns how many fixtures changed.
int applyColorToSelection(Show *show, const LinearRgb &color, const QColor &display, const QString &name);
// Color temperature: warm/cool white fixtures blend their whites, others mix RGB.
int applyTemperatureToSelection(Show *show, double kelvin);
// The color the first selected fixture is set to, if it has one.
bool selectionColor(Show *show, LinearRgb *out);

// ---- Building blocks ------------------------------------------------------

struct Swatch {
    QColor color;
    QString code;    // "L201"
    QString name;    // "Full CT Blue"
    QString maker;   // "Lee", "Rosco", or empty
};

// A small picker for an object's appearance (a sequence's color): the standard colors and the
// gamut. Returns the color as "#rrggbb", an empty string for no color, or nothing if cancelled.
std::optional<QString> pickAppearanceColor(QWidget *parent, const QString &title, const QString &current);

const QVector<Swatch> &gelLibrary();
const QVector<Swatch> &standardColors();

// A grid of color chips with their code and name.
class SwatchGrid : public QWidget {
    Q_OBJECT
public:
    explicit SwatchGrid(QWidget *parent = nullptr);
    void setSwatches(const QVector<Swatch> &s);
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;
    QSize sizeHint() const override { return {300, heightForWidth(300)}; }

signals:
    void picked(const lumora::Swatch &s);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    int columns(int w) const;
    QRect cell(int i) const;
    QVector<Swatch> m_swatches;
    int m_hover = -1;
};

// Hue across, saturation down.
class HueSatField : public QWidget {
    Q_OBJECT
public:
    explicit HueSatField(QWidget *parent = nullptr);
    void setMarker(const QColor &c);
    QSize sizeHint() const override { return {300, 140}; }

signals:
    void picked(const QColor &c);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;

private:
    void pickAt(const QPoint &p);
    QImage m_cache;
    QColor m_marker;
};

// 1800 K to 10000 K.
class KelvinBar : public QWidget {
    Q_OBJECT
public:
    explicit KelvinBar(QWidget *parent = nullptr);
    void setKelvin(double k);
    double kelvin() const { return m_kelvin; }
    QSize sizeHint() const override { return {300, 54}; }
    static constexpr double kMin = 1800.0;
    static constexpr double kMax = 10000.0;

signals:
    void picked(double kelvin);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;

private:
    void pickAt(int x);
    double m_kelvin = -1;
};

// CIE 1931 chromaticity diagram with the RGB gamut triangle. Click inside to pick a color.
class CieDiagram : public QWidget {
    Q_OBJECT
public:
    explicit CieDiagram(QWidget *parent = nullptr);
    void setMarker(const LinearRgb &c, bool valid);
    QSize sizeHint() const override { return {300, 300}; }

signals:
    void picked(const lumora::LinearRgb &c);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    QRectF plotRect() const;
    QPointF toWidget(double x, double y) const;
    void pickAt(const QPointF &p);
    QImage m_cache;
    LinearRgb m_marker;
    bool m_hasMarker = false;
};

// The color picker panel: Gels, Standard, Temperature and Gamut tabs, like a theatre console's.
class ColorPickerPanel : public QWidget, public PanelOptions {
    Q_OBJECT
public:
    explicit ColorPickerPanel(AppContext *ctx, QWidget *parent = nullptr);
    // The tab shown (Gels, Standard, Temperature, Gamut) is part of the view.
    QJsonObject saveOptions() const override;
    void loadOptions(const QJsonObject &o) override;
    void buildOptions(QuickEditor *) override {}

private:
    void showTab(int i);
    void filterGels();
    void apply(const LinearRgb &c, const QColor &display, const QString &name, bool namesPreset = true);
    void refreshMarkers();
    QString m_kelvinName;   // the named temperature button last pressed, for preset names

    AppContext *m_ctx = nullptr;
    QList<QToolButton *> m_tabs;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_chip = nullptr;
    QLabel *m_lastName = nullptr;
    SwatchGrid *m_gels = nullptr;
    QLineEdit *m_gelSearch = nullptr;
    QString m_maker;
    HueSatField *m_hueSat = nullptr;
    KelvinBar *m_kelvin = nullptr;
    QLabel *m_kelvinLabel = nullptr;
    CieDiagram *m_cie = nullptr;
};

} // namespace lumora

Q_DECLARE_METATYPE(lumora::Swatch)
Q_DECLARE_METATYPE(lumora::LinearRgb)
