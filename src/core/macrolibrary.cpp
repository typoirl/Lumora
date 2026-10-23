// SPDX-License-Identifier: GPL-3.0-or-later
#include "macrolibrary.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace lumora {

Macro LibraryMacro::numbered(int id) const
{
    Macro m = macro;
    m.id = id;
    for (MacroLine &l : m.lines)
        l.command.replace(QStringLiteral("{self}"), QString::number(id));
    return m;
}

QVector<LibraryMacro> parseMacroLibrary(const QByteArray &json)
{
    QVector<LibraryMacro> out;
    const QJsonArray list = QJsonDocument::fromJson(json).object()["macros"].toArray();
    for (const QJsonValue &v : list) {
        const QJsonObject o = v.toObject();
        LibraryMacro lm;
        lm.category = o["category"].toString();
        lm.description = o["description"].toString();
        lm.macro.name = o["name"].toString();
        lm.macro.color = o["color"].toString();
        lm.macro.background = o["background"].toBool(false);
        for (const QJsonValue &lv : o["lines"].toArray()) {
            const QJsonObject lo = lv.toObject();
            MacroLine line;
            line.command = lo["command"].toString();
            line.wait = lo["wait"].toBool(false) ? MacroWait::Wait : MacroWait::Follow;
            line.time = lo["time"].toDouble(0.0);
            line.info = lo["info"].toString();
            lm.macro.lines << line;
        }
        if (!lm.macro.name.isEmpty() && !lm.macro.lines.isEmpty())
            out << lm;
    }
    return out;
}

} // namespace lumora
