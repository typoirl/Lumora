// SPDX-License-Identifier: GPL-3.0-or-later
#include "macros.h"

#include <QRegularExpression>
#include <QHash>
#include <QJsonArray>
#include <QObject>

#include <algorithm>

namespace lumora {

QJsonObject MacroLine::toJson() const
{
    QJsonObject o{{"command", command}};
    if (wait == MacroWait::Wait)
        o["wait"] = "Wait";
    if (time > 0)
        o["time"] = time;
    if (!enabled)
        o["enabled"] = false;
    if (!execute)
        o["execute"] = false;
    if (!info.isEmpty())
        o["info"] = info;
    return o;
}

MacroLine MacroLine::fromJson(const QJsonObject &o)
{
    MacroLine l;
    l.command = o["command"].toString();
    l.wait = o["wait"].toString() == QLatin1String("Wait") ? MacroWait::Wait : MacroWait::Follow;
    l.time = std::clamp(o["time"].toDouble(0.0), 0.0, 3600.0);
    l.enabled = o["enabled"].toBool(true);
    l.execute = o["execute"].toBool(true);
    l.info = o["info"].toString();
    return l;
}

QString Macro::displayName() const
{
    return name.isEmpty() ? QObject::tr("Macro %1").arg(id) : name;
}

QJsonObject Macro::toJson() const
{
    QJsonArray ls;
    for (const MacroLine &l : lines)
        ls.append(l.toJson());
    QJsonObject o{{"id", id}, {"name", name}, {"lines", ls}};
    if (!color.isEmpty())
        o["color"] = color;
    if (background)
        o["background"] = true;
    return o;
}

Macro Macro::fromJson(const QJsonObject &o)
{
    Macro m;
    m.id = o["id"].toInt();
    m.name = o["name"].toString();
    m.color = o["color"].toString();
    m.background = o["background"].toBool(false);
    for (const auto &v : o["lines"].toArray())
        m.lines << MacroLine::fromJson(v.toObject());
    return m;
}

QString colorFromName(const QString &name)
{
    static const QHash<QString, QString> names = {
        {"red", "#ff0000"},     {"orange", "#ff7800"}, {"yellow", "#ffe600"}, {"lime", "#a0ff00"},
        {"green", "#00ff00"},   {"seagreen", "#00ffa0"}, {"cyan", "#00ffff"}, {"skyblue", "#008cff"},
        {"blue", "#0000ff"},    {"purple", "#7800ff"}, {"magenta", "#ff00ff"}, {"pink", "#ff0078"},
        {"white", "#ffffff"},   {"amber", "#ffa000"},  {"grey", "#808080"},   {"gray", "#808080"},
    };
    const QString key = name.trimmed().toLower().remove(' ');
    if (names.contains(key))
        return names.value(key);
    static const QRegularExpression hex(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    const QString t = name.trimmed();
    return hex.match(t).hasMatch() ? t.toLower() : QString();
}

} // namespace lumora
