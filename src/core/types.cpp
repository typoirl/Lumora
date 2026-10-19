// SPDX-License-Identifier: GPL-3.0-or-later
#include "types.h"

#include "parameters.h"

#include <QHash>

#include <algorithm>
#include <cmath>

namespace lumora {

QString featureName(Feature f)
{
    switch (f) {
    case Feature::Dimmer: return QStringLiteral("Dimmer");
    case Feature::Position: return QStringLiteral("Position");
    case Feature::Gobo: return QStringLiteral("Gobo");
    case Feature::Color: return QStringLiteral("Color");
    case Feature::Beam: return QStringLiteral("Beam");
    case Feature::Focus: return QStringLiteral("Focus");
    case Feature::Control: return QStringLiteral("Control");
    }
    return QString();
}

QString presetTypeName(int type)
{
    if (type == kPresetTypeAll)
        return QStringLiteral("All");
    if (type >= 1 && type <= 7)
        return featureName(static_cast<Feature>(type));
    return QStringLiteral("?");
}

bool isAngle(const QString &a)
{
    for (const QLatin1String base : {QLatin1String("Pan"), QLatin1String("Tilt")})
        if (a.startsWith(base)) {
            const QStringView rest = QStringView(a).mid(base.size());
            if (rest.isEmpty())
                return true;
            bool ok = false;
            (void)rest.toInt(&ok);
            return ok;   // Pan2, not PanRotate
        }
    return false;
}

double defaultAngleSpan(const QString &a)
{
    return a.startsWith(QLatin1String("Tilt")) ? 270.0 : 540.0;
}

QString presetRef(int type, int id)
{
    return QStringLiteral("%1.%2").arg(presetTypeName(type)).arg(id);
}

int presetTypeFromName(const QString &name)
{
    static const QHash<QString, int> map = {
        {"dimmer", 1}, {"dim", 1}, {"intensity", 1}, {"position", 2}, {"pos", 2}, {"gobo", 3},
        {"color", 4}, {"colour", 4}, {"col", 4}, {"beam", 5}, {"focus", 6}, {"foc", 6},
        {"control", 7}, {"ctrl", 7}, {"all", kPresetTypeAll},
    };
    bool ok = false;
    const int n = name.toInt(&ok);
    if (ok)
        return n >= 1 && n <= kPresetTypeAll ? n : 0;
    return map.value(name.toLower(), 0);
}

QVector<Feature> allFeatures()
{
    return {Feature::Dimmer, Feature::Position, Feature::Gobo, Feature::Color,
            Feature::Beam, Feature::Focus, Feature::Control};
}

Feature featureOf(const QString &a)
{
    if (a == QLatin1String("Dimmer"))
        return Feature::Dimmer;
    if (a.startsWith(QLatin1String("Pan")) || a.startsWith(QLatin1String("Tilt")) || a == QLatin1String("XYZ")
        || a.startsWith(QLatin1String("Position")))
        return Feature::Position;
    if (a.startsWith(QLatin1String("Gobo")) || a.startsWith(QLatin1String("Animation")))
        return Feature::Gobo;
    if (a.startsWith(QLatin1String("Color")) || a == QLatin1String("CTO") || a == QLatin1String("CTB")
        || a == QLatin1String("CTC") || a == QLatin1String("Tint") || a.startsWith(QLatin1String("HSB_")))
        return Feature::Color;
    if (a.startsWith(QLatin1String("Shutter")) || a.startsWith(QLatin1String("Strobe"))
        || a.startsWith(QLatin1String("Prism")) || a.startsWith(QLatin1String("Iris"))
        || a.startsWith(QLatin1String("Frost")) || a.startsWith(QLatin1String("Effect"))
        || a.startsWith(QLatin1String("Blade")))
        return Feature::Beam;
    if (a.startsWith(QLatin1String("Zoom")) || a.startsWith(QLatin1String("Focus")))
        return Feature::Focus;
    return Feature::Control;
}

double linearToSrgb(double c)
{
    c = std::clamp(c, 0.0, 1.0);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

double srgbToLinear(double c)
{
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

QString attributeLabel(const QString &a)
{
    static const QHash<QString, QString> labels = {
        {"ColorAdd_R", "Red"}, {"ColorAdd_G", "Green"}, {"ColorAdd_B", "Blue"},
        {"ColorAdd_W", "White"}, {"ColorAdd_WW", "Warm White"}, {"ColorAdd_CW", "Cool White"},
        {"ColorAdd_A", "Amber"}, {"ColorAdd_UV", "UV"}, {"ColorAdd_L", "Lime"},
        {"ColorAdd_C", "Cyan+"}, {"ColorAdd_I", "Indigo"},
        {"ColorSub_C", "Cyan"}, {"ColorSub_M", "Magenta"}, {"ColorSub_Y", "Yellow"},
        {"Color1", "Color Wheel"}, {"Color2", "Color Wheel 2"},
        {"Gobo1", "Gobo"}, {"Gobo1Pos", "Gobo Rot"}, {"Gobo2", "Gobo 2"}, {"Gobo2Pos", "Gobo 2 Rot"},
        {"Shutter", "Shutter"}, {"Prism1", "Prism"}, {"Prism1Pos", "Prism Rot"}, {"Frost1", "Frost"},
        {"Focus1", "Focus"}, {"PTSpeed", "P/T Speed"},
    };
    if (labels.contains(a))
        return labels.value(a);
    if (const ParameterInfo *p = parameterInfo(a))   // the fixture editor's parameter library
        return p->label;
    return a;
}

int attributeSortKey(const QString &a)
{
    static const QStringList order = {
        "Dimmer", "Pan", "Tilt", "PanRotate", "TiltRotate",
        "Gobo1", "Gobo1Pos", "Gobo2", "Gobo2Pos",
        "ColorAdd_R", "ColorAdd_G", "ColorAdd_B", "ColorAdd_W", "ColorAdd_WW", "ColorAdd_CW",
        "ColorAdd_A", "ColorAdd_L", "ColorAdd_C", "ColorAdd_I", "ColorAdd_UV",
        "ColorSub_C", "ColorSub_M", "ColorSub_Y", "CTO", "CTB", "Color1", "Color2",
        "Shutter", "Iris", "Prism1", "Prism1Pos", "Frost1",
        "Blade1A", "Blade1B", "Blade1Rot", "Blade2A", "Blade2B", "Blade2Rot", "Blade3A", "Blade3B", "Blade3Rot",
        "Blade4A", "Blade4B", "Blade4Rot", "BladeSystemRot",
        "Zoom", "Focus1", "Focus2",
    };
    int i = order.indexOf(a);
    return i >= 0 ? i : 1000;
}

} // namespace lumora
