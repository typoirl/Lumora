// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "macros.h"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace lumora {

// A macro from Lumora's macro library (resources/macros/library.json), ready to import.
struct LibraryMacro {
    QString category;
    QString description;
    Macro macro;   // id 0 until imported

    // A copy numbered for slot `id`: "{self}" in its lines becomes that number.
    Macro numbered(int id) const;
};

QVector<LibraryMacro> parseMacroLibrary(const QByteArray &json);

} // namespace lumora
