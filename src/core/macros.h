// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace lumora {

// How a macro goes on after a line: Follow runs the next line after the line's time (0: at
// once); Wait stops until the macro is tapped (or Go Macro) again.
enum class MacroWait { Follow, Wait };

struct MacroLine {
    QString command;
    MacroWait wait = MacroWait::Follow;
    double time = 0.0;      // Follow: seconds before the next line
    bool enabled = true;    // off: skipped
    // Off: the command is put on the command line for you to finish (a foreground macro), like a
    // line ending in "@". A background macro runs it anyway.
    bool execute = true;
    QString info;           // a note for whoever edits it

    QJsonObject toJson() const;
    static MacroLine fromJson(const QJsonObject &o);
};

// A list of command lines run one after another. A foreground macro types its lines on the
// command line as it goes; a background one runs them out of sight (they still go into the
// command history).
struct Macro {
    int id = 0;
    QString name;
    QString color;            // "#rrggbb" for its pool tile, or empty
    bool background = false;
    QVector<MacroLine> lines;

    QString displayName() const;   // its name, or "Macro 3"
    QJsonObject toJson() const;
    static Macro fromJson(const QJsonObject &o);
};

// Where a running macro is.
struct MacroRun {
    int id = 0;
    int line = 0;              // the next line to run
    double timeLeft = 0.0;     // Follow time before it
    bool waiting = false;      // a Wait line ran: waiting for a tap
};

// A named color for Appearance commands and macros: "Red", "orange", "#ff8800". Empty if unknown.
QString colorFromName(const QString &name);

} // namespace lumora
