// SPDX-License-Identifier: GPL-3.0-or-later
#include "parameters.h"

#include <QObject>

namespace lumora {

namespace {
ChannelSet range(int from, int to, const QString &name)
{
    return {from / 255.0, to / 255.0, name, QString()};
}

ParameterInfo param(const char *attribute, const QString &label, const QString &description, double def, double highlight,
                    bool fine = false, QVector<ChannelSet> sets = {})
{
    return {QString::fromLatin1(attribute), label, description, def, highlight, fine, std::move(sets)};
}

QVector<ChannelSet> wheel(const QString &openName, const QString &slotName, int count)
{
    // Open, then equal slots up to 127, then continuous rotation both ways.
    QVector<ChannelSet> sets{range(0, 9, openName)};
    const double step = (127 - 10 + 1) / double(count);
    for (int i = 0; i < count; ++i)
        sets << range(10 + int(i * step), 10 + int((i + 1) * step) - 1, QStringLiteral("%1 %2").arg(slotName).arg(i + 1));
    sets << range(128, 191, QObject::tr("Spin clockwise")) << range(192, 255, QObject::tr("Spin counter-clockwise"));
    return sets;
}
} // namespace

ChannelDef ParameterInfo::channel() const
{
    ChannelDef c;
    c.attribute = attribute;
    c.name = label;
    c.coarse = -1;
    c.defaultValue = c.fromDmx(defaultValue);       // pan and tilt in degrees: the middle is 0
    c.highlightValue = c.fromDmx(highlightValue);
    // Highlight brightens and opens: dimmer, color and shutter. Others can be given one.
    c.hasHighlight = feature() == Feature::Dimmer || feature() == Feature::Color || attribute == QLatin1String("Shutter");
    c.sets = sets;
    return c;
}

const QVector<ParameterInfo> &parameterLibrary()
{
    static const QVector<ParameterInfo> list = [] {
        QVector<ParameterInfo> l;
        // Dimmer
        l << param("Dimmer", QObject::tr("Dimmer"), QObject::tr("Intensity. RGB fixtures without one get a virtual dimmer."), 0, 1);
        // Position
        l << param("Pan", QObject::tr("Pan"), QObject::tr("Horizontal position, usually 16-bit"), 0.5, 0.5, true)
          << param("Tilt", QObject::tr("Tilt"), QObject::tr("Vertical position, usually 16-bit"), 0.5, 0.5, true)
          << param("PanRotate", QObject::tr("Infinite pan"), QObject::tr("Endless (continuous) pan rotation"), 0, 0, false,
                   {range(0, 9, QObject::tr("Stop")), range(10, 127, QObject::tr("Clockwise slow to fast")),
                    range(128, 255, QObject::tr("Counter-clockwise slow to fast"))})
          << param("TiltRotate", QObject::tr("Infinite tilt"), QObject::tr("Endless (continuous) tilt rotation"), 0, 0, false,
                   {range(0, 9, QObject::tr("Stop")), range(10, 127, QObject::tr("Forward slow to fast")),
                    range(128, 255, QObject::tr("Backward slow to fast"))})
          << param("PTSpeed", QObject::tr("Pan/tilt speed"), QObject::tr("Movement speed or smoothing"), 0, 0)
          << param("PanSpeed", QObject::tr("Pan speed"), QObject::tr("Pan movement speed on its own"), 0, 0)
          << param("TiltSpeed", QObject::tr("Tilt speed"), QObject::tr("Tilt movement speed on its own"), 0, 0)
          << param("PositionMacro", QObject::tr("Movement macro"), QObject::tr("Built-in pan/tilt patterns"), 0, 0, false,
                   {range(0, 7, QObject::tr("Off")), range(8, 255, QObject::tr("Macros"))})
          << param("PositionMacroRate", QObject::tr("Movement macro speed"), QObject::tr("Speed of the movement macros"), 0, 0)
          << param("PositionMacroSize", QObject::tr("Movement macro size"), QObject::tr("Size of the movement macros"), 0, 0);
        // Color mixing: emitters rest at white where the fixture can make it
        l << param("ColorAdd_R", QObject::tr("Red"), QObject::tr("Red emitter"), 1, 1)
          << param("ColorAdd_G", QObject::tr("Green"), QObject::tr("Green emitter"), 1, 1)
          << param("ColorAdd_B", QObject::tr("Blue"), QObject::tr("Blue emitter"), 1, 1)
          << param("ColorAdd_W", QObject::tr("White"), QObject::tr("White emitter"), 1, 1)
          << param("ColorAdd_WW", QObject::tr("Warm white"), QObject::tr("Warm white emitter"), 0, 0)
          << param("ColorAdd_CW", QObject::tr("Cool white"), QObject::tr("Cool white emitter"), 0, 0)
          << param("ColorAdd_A", QObject::tr("Amber"), QObject::tr("Amber emitter"), 0, 0)
          << param("ColorAdd_L", QObject::tr("Lime"), QObject::tr("Lime emitter"), 0, 0)
          << param("ColorAdd_C", QObject::tr("Cyan emitter"), QObject::tr("Cyan emitter (additive)"), 0, 0)
          << param("ColorAdd_I", QObject::tr("Indigo"), QObject::tr("Indigo or royal blue emitter"), 0, 0)
          << param("ColorAdd_O", QObject::tr("Orange"), QObject::tr("Orange (red-orange) emitter"), 0, 0)
          << param("ColorAdd_Mint", QObject::tr("Mint"), QObject::tr("Mint emitter"), 0, 0)
          << param("ColorAdd_P", QObject::tr("Purple"), QObject::tr("Purple or violet emitter"), 0, 0)
          << param("ColorAdd_M", QObject::tr("Magenta emitter"), QObject::tr("Magenta emitter (additive)"), 0, 0)
          << param("ColorAdd_Y", QObject::tr("Yellow emitter"), QObject::tr("Yellow emitter (additive)"), 0, 0)
          << param("ColorAdd_UV", QObject::tr("UV"), QObject::tr("Ultraviolet emitter"), 0, 0)
          << param("ColorSub_C", QObject::tr("Cyan"), QObject::tr("Cyan flag (CMY mixing)"), 0, 0)
          << param("ColorSub_M", QObject::tr("Magenta"), QObject::tr("Magenta flag (CMY mixing)"), 0, 0)
          << param("ColorSub_Y", QObject::tr("Yellow"), QObject::tr("Yellow flag (CMY mixing)"), 0, 0)
          << param("CTO", QObject::tr("CTO"), QObject::tr("Color temperature orange"), 0, 0)
          << param("CTB", QObject::tr("CTB"), QObject::tr("Color temperature blue"), 0, 0)
          << param("CTC", QObject::tr("Color temperature"), QObject::tr("Color temperature correction (CCT)"), 0, 0)
          << param("Tint", QObject::tr("Tint"), QObject::tr("Green/magenta shift"), 0.5, 0.5)
          << param("ColorRedShift", QObject::tr("Red shift"), QObject::tr("Warms the dimming curve toward red, like tungsten"), 0, 0)
          << param("HSB_Hue", QObject::tr("Hue"), QObject::tr("Virtual color: hue"), 0, 0)
          << param("HSB_Saturation", QObject::tr("Saturation"), QObject::tr("Virtual color: saturation"), 0, 0)
          << param("HSB_Brightness", QObject::tr("Brightness"), QObject::tr("Virtual color: value or brightness"), 1, 1)
          << param("Color1", QObject::tr("Color wheel"), QObject::tr("Color wheel: Open, slots, then spin"), 0, 0, false,
                   wheel(QObject::tr("Open"), QObject::tr("Color"), 7))
          << param("Color2", QObject::tr("Color wheel 2"), QObject::tr("Second color wheel"), 0, 0, false,
                   wheel(QObject::tr("Open"), QObject::tr("Color"), 7))
          << param("ColorMacro", QObject::tr("Color macro"), QObject::tr("Built-in color presets or chases"), 0, 0, false,
                   {range(0, 7, QObject::tr("Off")), range(8, 255, QObject::tr("Macros"))})
          << param("ColorMacroRate", QObject::tr("Color macro speed"), QObject::tr("Speed of the color macros"), 0, 0)
          << param("ColorWheelShake", QObject::tr("Color wheel shake"), QObject::tr("Shakes the color wheel slot"), 0, 0);
        // Gobo
        l << param("Gobo1", QObject::tr("Gobo wheel"), QObject::tr("Rotating or fixed gobos: Open, slots, then spin"), 0, 0, false,
                   wheel(QObject::tr("Open"), QObject::tr("Gobo"), 7))
          << param("Gobo1Pos", QObject::tr("Gobo rotation"), QObject::tr("Gobo index or rotation speed"), 0, 0)
          << param("Gobo2", QObject::tr("Gobo wheel 2"), QObject::tr("Second gobo wheel"), 0, 0, false,
                   wheel(QObject::tr("Open"), QObject::tr("Gobo"), 7))
          << param("Gobo2Pos", QObject::tr("Gobo 2 rotation"), QObject::tr("Second wheel index or rotation"), 0, 0)
          << param("Gobo1Shake", QObject::tr("Gobo shake"), QObject::tr("Shakes the gobo slot"), 0, 0)
          << param("Gobo3", QObject::tr("Gobo wheel 3"), QObject::tr("Third gobo wheel"), 0, 0, false,
                   wheel(QObject::tr("Open"), QObject::tr("Gobo"), 7))
          << param("AnimationWheel", QObject::tr("Animation wheel"), QObject::tr("Animation (effects) wheel"), 0, 0)
          << param("AnimationWheelPos", QObject::tr("Animation rotation"), QObject::tr("Animation wheel rotation or speed"), 0, 0);
        // Beam
        l << param("Shutter", QObject::tr("Shutter"), QObject::tr("Closed, open and strobe ranges; rests open"), 40 / 255.0,
                   40 / 255.0, false,
                   {range(0, 31, QObject::tr("Closed")), range(32, 63, QObject::tr("Open")), range(64, 223, QObject::tr("Strobe slow to fast")),
                    range(224, 247, QObject::tr("Random strobe")), range(248, 255, QObject::tr("Open"))})
          << param("StrobeRate", QObject::tr("Strobe rate"), QObject::tr("A strobe speed of its own"), 0, 0)
          << param("Iris", QObject::tr("Iris"), QObject::tr("Beam size: rests open"), 0, 0)
          << param("Prism1", QObject::tr("Prism"), QObject::tr("Prism in or out"), 0, 0, false,
                   {range(0, 19, QObject::tr("Out")), range(20, 255, QObject::tr("In"))})
          << param("Prism1Pos", QObject::tr("Prism rotation"), QObject::tr("Prism index or rotation speed"), 0, 0)
          << param("Prism2", QObject::tr("Prism 2"), QObject::tr("Second prism in or out"), 0, 0, false,
                   {range(0, 19, QObject::tr("Out")), range(20, 255, QObject::tr("In"))})
          << param("Prism2Pos", QObject::tr("Prism 2 rotation"), QObject::tr("Second prism index or rotation speed"), 0, 0)
          << param("Frost1", QObject::tr("Frost"), QObject::tr("Frost or diffusion"), 0, 0)
          << param("Frost2", QObject::tr("Frost 2"), QObject::tr("Second (heavier) frost"), 0, 0)
          << param("StrobeVirtual", QObject::tr("Virtual strobe"), QObject::tr("Strobe made by pulsing the emitters"), 0, 0)
          << param("StrobeDuration", QObject::tr("Strobe duration"), QObject::tr("Flash length of each strobe pulse"), 0, 0)
          << param("Effects", QObject::tr("Effects wheel"), QObject::tr("Effect or FX wheel"), 0, 0)
          << param("EffectsRate", QObject::tr("Effect speed"), QObject::tr("Speed of the effects wheel or beam FX"), 0, 0)
          << param("Blade1A", QObject::tr("Blade 1 in"), QObject::tr("Framing shutter 1 insertion"), 0, 0)
          << param("Blade1B", QObject::tr("Blade 1 B"), QObject::tr("Framing shutter 1, its second corner (A+B shutters; A is the first)"), 0, 0)
          << param("Blade1Rot", QObject::tr("Blade 1 swivel"), QObject::tr("Framing shutter 1 angle"), 0.5, 0.5)
          << param("Blade2A", QObject::tr("Blade 2 in"), QObject::tr("Framing shutter 2 insertion"), 0, 0)
          << param("Blade2B", QObject::tr("Blade 2 B"), QObject::tr("Framing shutter 2, its second corner (A+B shutters; A is the first)"), 0, 0)
          << param("Blade2Rot", QObject::tr("Blade 2 swivel"), QObject::tr("Framing shutter 2 angle"), 0.5, 0.5)
          << param("Blade3A", QObject::tr("Blade 3 in"), QObject::tr("Framing shutter 3 insertion"), 0, 0)
          << param("Blade3B", QObject::tr("Blade 3 B"), QObject::tr("Framing shutter 3, its second corner (A+B shutters; A is the first)"), 0, 0)
          << param("Blade3Rot", QObject::tr("Blade 3 swivel"), QObject::tr("Framing shutter 3 angle"), 0.5, 0.5)
          << param("Blade4A", QObject::tr("Blade 4 in"), QObject::tr("Framing shutter 4 insertion"), 0, 0)
          << param("Blade4B", QObject::tr("Blade 4 B"), QObject::tr("Framing shutter 4, its second corner (A+B shutters; A is the first)"), 0, 0)
          << param("Blade4Rot", QObject::tr("Blade 4 swivel"), QObject::tr("Framing shutter 4 angle"), 0.5, 0.5)
          << param("BladeSystemRot", QObject::tr("Blade rotation"), QObject::tr("Rotates the whole framing module"), 0.5, 0.5);
        // Focus
        l << param("Zoom", QObject::tr("Zoom"), QObject::tr("Beam angle"), 0.5, 0.5)
          << param("Focus1", QObject::tr("Focus"), QObject::tr("Sharpness"), 0.5, 0.5)
          << param("Focus2", QObject::tr("Focus 2"), QObject::tr("Second focus"), 0.5, 0.5)
          << param("FocusAuto", QObject::tr("Auto focus"), QObject::tr("Focus that follows zoom (auto focus modes)"), 0, 0);
        // Control
        l << param("Ctl_Control", QObject::tr("Control"), QObject::tr("Reset, lamp and fixture settings"), 0, 0, false,
                   {range(0, 99, QObject::tr("No function")), range(100, 149, QObject::tr("Lamp on")),
                    range(150, 199, QObject::tr("Lamp off")), range(200, 255, QObject::tr("Reset"))})
          << param("Ctl_Macro", QObject::tr("Macros"), QObject::tr("Built-in programs"), 0, 0, false,
                   {range(0, 7, QObject::tr("Off")), range(8, 255, QObject::tr("Macros"))})
          << param("Ctl_Speed", QObject::tr("Effect speed"), QObject::tr("Speed of built-in programs"), 0, 0)
          << param("Ctl_Fan", QObject::tr("Fan"), QObject::tr("Fan or blower speed"), 0, 0)
          << param("Ctl_Fog", QObject::tr("Fog output"), QObject::tr("Fog or haze output"), 0, 0)
          << param("Ctl_Haze", QObject::tr("Haze output"), QObject::tr("Haze volume"), 0, 0)
          << param("Ctl_Bubble", QObject::tr("Bubble output"), QObject::tr("Bubble machine output"), 0, 0)
          << param("Ctl_DimmerCurve", QObject::tr("Dimmer speed mode"), QObject::tr("Dimmer curve or speed mode"), 0, 0)
          << param("Ctl_Mode", QObject::tr("Mode / zone"), QObject::tr("Operating mode or zone selection"), 0, 0)
          << param("Ctl_ProgramSpeed", QObject::tr("Program speed"), QObject::tr("Auto or sound-active program speed"), 0, 0)
          << param("Ctl_Motor", QObject::tr("Motor"), QObject::tr("Mirror or motor rotation"), 0, 0)
          << param("Ctl_LaserPattern", QObject::tr("Laser pattern"), QObject::tr("Laser pattern select"), 0, 0)
          << param("Ctl_LaserSize", QObject::tr("Pattern size"), QObject::tr("Laser pattern size"), 0, 0)
          << param("Ctl_LaserX", QObject::tr("Pattern X"), QObject::tr("Laser pattern X position"), 0.5, 0.5)
          << param("Ctl_LaserY", QObject::tr("Pattern Y"), QObject::tr("Laser pattern Y position"), 0.5, 0.5)
          << param("Ctl_LaserRoll", QObject::tr("Pattern rolling"), QObject::tr("Laser X/Y/Z axis rolling"), 0, 0);
        return l;
    }();
    return list;
}

const ParameterInfo *parameterInfo(const QString &attribute)
{
    for (const ParameterInfo &p : parameterLibrary())
        if (p.attribute == attribute)
            return &p;
    return nullptr;
}

const QVector<ParameterBundle> &parameterBundles()
{
    static const QVector<ParameterBundle> list = {
        {QObject::tr("RGB"), {"ColorAdd_R", "ColorAdd_G", "ColorAdd_B"}},
        {QObject::tr("RGBW"), {"ColorAdd_R", "ColorAdd_G", "ColorAdd_B", "ColorAdd_W"}},
        {QObject::tr("RGBA"), {"ColorAdd_R", "ColorAdd_G", "ColorAdd_B", "ColorAdd_A"}},
        {QObject::tr("RGBAW"), {"ColorAdd_R", "ColorAdd_G", "ColorAdd_B", "ColorAdd_A", "ColorAdd_W"}},
        {QObject::tr("RGBWA + UV"), {"ColorAdd_R", "ColorAdd_G", "ColorAdd_B", "ColorAdd_W", "ColorAdd_A", "ColorAdd_UV"}},
        {QObject::tr("Warm / cool white"), {"ColorAdd_WW", "ColorAdd_CW"}},
        {QObject::tr("CMY"), {"ColorSub_C", "ColorSub_M", "ColorSub_Y"}},
        {QObject::tr("Pan / tilt (16-bit)"), {"Pan", "Tilt"}},
        {QObject::tr("Moving spot basics"),
         {"Pan", "Tilt", "PTSpeed", "Dimmer", "Shutter", "Color1", "Gobo1", "Gobo1Pos", "Prism1", "Focus1", "Zoom", "Ctl_Control"}},
        {QObject::tr("Moving wash basics"),
         {"Pan", "Tilt", "PTSpeed", "Dimmer", "Shutter", "ColorAdd_R", "ColorAdd_G", "ColorAdd_B", "ColorAdd_W", "Zoom", "Ctl_Control"}},
    };
    return list;
}

} // namespace lumora
