// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcontext.h"

#include <QApplication>
#include <QInputDialog>
#include <QRegularExpression>
#include <QTimer>

namespace lumora {

bool AppContext::tapTarget(const QString &object, const QString &currentName)
{
    const QString line = m_commandLine.simplified();
    if (line.isEmpty())
        return false;
    const QStringList words = line.split(' ');
    // The verb, allowing abbreviations ("mo", "del", "lab").
    static const QStringList verbs = {"Move", "Copy", "Delete", "Label", "Update", "Remove", "Assign"};
    QString verb;
    for (const QString &v : verbs)
        if (words[0].size() >= 2 && v.startsWith(words[0], Qt::CaseInsensitive))
            verb = v;
    if (verb.isEmpty())
        return false;

    const bool twoPart = verb == "Move" || verb == "Copy" || verb == "Assign";
    if (words.size() == 1) {
        if (twoPart) {
            setCommandLine(verb + ' ' + object + QStringLiteral(" At "));
            emit status(verb == "Assign" ? tr("Now tap the executor") : tr("Now tap where it goes"));
            return true;
        }
        if (verb == "Label") {
            bool ok = false;
            const QString name = QInputDialog::getText(QApplication::activeWindow(), tr("Label"), tr("Name for %1").arg(object),
                                                       QLineEdit::Normal, currentName, &ok);
            setCommandLine(QString());
            if (ok && !name.trimmed().isEmpty()) {
                QString quoted = name.trimmed();
                quoted.replace('"', '\'');
                run(verb + ' ' + object + QStringLiteral(" \"") + quoted + '"');
            }
            return true;
        }
        setCommandLine(QString());
        run(verb + ' ' + object);
        return true;
    }
    if (twoPart) {
        QString l = line;
        if (!l.endsWith(QLatin1String(" At"), Qt::CaseInsensitive))
            l += QStringLiteral(" At");
        setCommandLine(QString());
        run(l + ' ' + object);
        return true;
    }
    return false;
}

bool AppContext::runMacroLine(const Macro &m, const MacroLine &line, const QString &command)
{
    QString cmd = command.trimmed();
    // Popups: each "(Prompt)" asks for the text that goes in its place.
    static const QRegularExpression popup(QStringLiteral("\\(([^()]*)\\)"));
    for (QRegularExpressionMatch mt = popup.match(cmd); mt.hasMatch(); mt = popup.match(cmd)) {
        bool ok = false;
        const QString answer = QInputDialog::getText(QApplication::activeWindow(), m.displayName(),
                                                     mt.captured(1).trimmed().isEmpty() ? tr("Input") : mt.captured(1).trimmed(),
                                                     QLineEdit::Normal, QString(), &ok);
        if (!ok) {
            emit status(tr("%1 stopped").arg(m.displayName()));
            return false;
        }
        cmd.replace(mt.capturedStart(), mt.capturedLength(), answer);
    }
    bool finishByHand = !line.execute;
    if (cmd.endsWith('@')) {
        cmd.chop(1);
        finishByHand = true;
    }
    if (cmd.startsWith('@'))
        cmd = (m_commandLine.trimmed() + ' ' + cmd.mid(1).trimmed()).trimmed();
    cmd = cmd.trimmed();
    if (finishByHand && !m.background) {
        setCommandLine(cmd + ' ');   // for you to finish and press Enter
        return true;
    }
    const QString source = QStringLiteral("Macro %1%2").arg(m.id).arg(m.name.isEmpty() ? QString() : QStringLiteral(" ") + m.name);
    const QString was = m_macroSource;
    m_macroSource = source;
    if (!m.background) {
        // Foreground: the line shows on the command line as it runs, then clears.
        setCommandLine(cmd);
        run(cmd);
        QTimer::singleShot(450, this, [this, cmd]() {
            if (m_commandLine.trimmed() == cmd)
                setCommandLine(QString());
        });
    } else {
        run(cmd);
    }
    m_macroSource = was;
    return true;
}

} // namespace lumora
