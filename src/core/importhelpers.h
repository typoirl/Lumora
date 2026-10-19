// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QRegularExpression>
#include <QString>

// Small helpers the fixture file readers (OFL, GDTF, QLC+) share.
namespace lumora::import {

// A name made safe for an attribute: letters and digits only ("Strobe Speed" → "StrobeSpeed").
inline QString attributeSafeName(const QString &s)
{
    static const QRegularExpression notAlnum(QStringLiteral("[^A-Za-z0-9]"));
    QString out = s;
    out.remove(notAlnum);
    return out.isEmpty() ? QStringLiteral("Channel") : out;
}

// The color attribute for an emitter or flag name ("Red", "Warm White", "cyan"…), or empty.
inline QString colorNameAttribute(const QString &name)
{
    static const QHash<QString, QString> map = {
        {"red", "ColorAdd_R"},        {"green", "ColorAdd_G"},     {"blue", "ColorAdd_B"},
        {"white", "ColorAdd_W"},      {"amber", "ColorAdd_A"},     {"uv", "ColorAdd_UV"},
        {"lime", "ColorAdd_L"},       {"indigo", "ColorAdd_I"},    {"warmwhite", "ColorAdd_WW"},
        {"coldwhite", "ColorAdd_CW"}, {"coolwhite", "ColorAdd_CW"}, {"cyan", "ColorSub_C"},
        {"magenta", "ColorSub_M"},    {"yellow", "ColorSub_Y"},     {"orange", "ColorAdd_O"},
        {"mint", "ColorAdd_Mint"},    {"purple", "ColorAdd_P"},     {"violet", "ColorAdd_P"},
        {"royalblue", "ColorAdd_I"},  {"ultraviolet", "ColorAdd_UV"},
    };
    return map.value(name.toLower().remove(' '));
}

} // namespace lumora::import
