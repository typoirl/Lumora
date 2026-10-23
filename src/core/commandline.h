// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace lumora {

class Show;

struct CommandResult {
    CommandResult() = default;
    CommandResult(bool ok_, const QString &message_ = QString()) : ok(ok_), message(message_) {}
    bool ok = true;
    QString message;
    // A Store onto something that exists, with no option saying what to do: the question and
    // its answers (option, label), in the same order everywhere. Running the command again with
    // "/option" added does it. Without anyone to ask (a macro, say), it fails with the message.
    QString question;
    QVector<QPair<QString, QString>> choices;
};

// A command line that reads commands like these:
//
//   Fixture 1 Thru 10 - 5 At 50          Group 2 At Full          At Color.3
//   Store Group 3 "Front"                Store Color.1          Store Cue 5 /overwrite
//   Store Sequence 2 Cue 1.5             Assign Sequence 2 At Executor 202
//   Assign Flash At Executor 201 Button 3   Assign XFade At Executor 205   Top Executor 201
//   Go Executor 201   GoBack Executor 201   Off Executor 201   Goto Cue 3 Executor 201
//   Executor 401 Button (or Encoder, Fader)   Executor 201 At 75                   Cue 3 Fade 4             Cue 3 Delay 1
//   Blueprint Cue 2 Group 1 Color.2 At 80     Build Cue 2           Label Color.1 "Red"
//   Attribute Pan At 30   Page 2   Page +   Clear   ClearAll   Highlight   Blackout
//   Update (the live cue)   Update Cue 3   Update Color.1   Remove   Remove Fixture 3 Cue 2
//   Copy Group 1 At 5   Move Color.1 At Color.9   Copy Cue 2 At 5   Move Sequence 1 At 4
//   View 2   Store View 3 "Busking"   Label View 3 "Name"   Move View 3 At 5   Delete View 3
//   Store Color.1 /universal   /type (or /global)   /fixture (or /selective)
//
// Keywords are case-insensitive and accept common abbreviations (F, G, T, P, Seq, Exec, @).
class CommandLine {
public:
    explicit CommandLine(Show *show) : m_show(show) {}

    CommandResult execute(const QString &line);
    const QStringList &history() const { return m_history; }
    static QStringList keywords();

private:
    Show *m_show;
    QStringList m_history;
};

} // namespace lumora
