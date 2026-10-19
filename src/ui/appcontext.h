// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "commandline.h"
#include "dmxoutput.h"
#include "engine.h"
#include "midi.h"
#include "show.h"

#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QVector>

#include <algorithm>

namespace lumora {

// One line of the command history: a command typed or keyed in, or an edit made in a window.
struct HistoryEntry {
    QDateTime time;
    bool command = true;   // false: an edit made by tapping, dragging or a dialog
    QString text;          // the command line, or what the edit did
    QString result;        // the command's answer
    bool ok = true;
    QString source;        // the macro that ran it ("Macro 3 Busk"), or empty
};

// Everything the views share. One instance lives for the whole app.
class AppContext : public QObject {
    Q_OBJECT
public:
    explicit AppContext(QObject *parent = nullptr)
        : QObject(parent), show(new Show(this)), engine(new Engine(show, this)),
          outputs(new OutputManager(this)), midi(new MidiManager(show, this)), commands(show)
    {
        engine->setOutputs(outputs);
        connect(show, &Show::message, this, &AppContext::status);
        // The history keeps every command and every edit that reports itself.
        connect(this, &AppContext::commandRan, this, [this](const QString &line, bool ok, const QString &msg) {
            addHistory({QDateTime::currentDateTime(), true, line.trimmed(), msg, ok, m_macroSource});
        });
        connect(this, &AppContext::status, this, [this](const QString &msg) {
            if (msg.isEmpty() || m_inCommand)   // messages from inside a command belong to it
                return;
            // A command's own messages are already on its line.
            if (!m_history.isEmpty() && m_history.last().time.msecsTo(QDateTime::currentDateTime()) < 250
                && (m_history.last().result == msg || m_history.last().text == msg))
                return;
            addHistory({QDateTime::currentDateTime(), false, msg, QString(), true});
        });
        // "Plot 3" (or the pool): the plot windows show it.
        connect(show, &Show::plotRequested, this, [this](int id) { setUiValue(QStringLiteral("plot"), id); });
        show->setMacroExecutor([this](const Macro &m, const MacroLine &line, const QString &command) {
            return runMacroLine(m, line, command);
        });
        // A universe that gets patched gets an output, with the default protocol and network card.
        connect(show, &Show::changed, this, [this](Show::Parts p) {
            if (p & Show::PatchPart)
                patchOutputs(p == Show::AllParts);
        });
    }

    Show *show;
    Engine *engine;
    OutputManager *outputs;
    MidiManager *midi;
    CommandLine commands;

    // ---- Command history (this session) ----
    const QVector<HistoryEntry> &history() const { return m_history; }
    void clearHistory()
    {
        m_history.clear();
        emit historyCleared();
    }

    // ---- Interface state, saved in the show ----
    // Everything about how the console is set up travels with the show file. Only network
    // settings (outputs, which network interface sends) and window placement stay on this computer.
    QJsonValue uiValue(const QString &key, const QJsonValue &fallback = QJsonValue()) const
    {
        const QJsonObject o = show->extra(QStringLiteral("interface"));
        return o.contains(key) ? o.value(key) : fallback;
    }
    // markChanged: a setting worth saving (executor count), not passing state (the view on screen).
    void setUiValue(const QString &key, const QJsonValue &value, bool markChanged = false)
    {
        QJsonObject o = show->extra(QStringLiteral("interface"));
        if (o.value(key) == value)
            return;
        o[key] = value;
        show->setExtra(QStringLiteral("interface"), o, markChanged);
        emit uiValueChanged(key);
    }
    int encodersPerPage() const { return qBound(1, uiValue("encodersPerPage", 5).toInt(5), 20); }
    void setEncodersPerPage(int n) { setUiValue("encodersPerPage", qBound(1, n, 20), true); }
    int executorCount() const { return uiValue("executors", 10).toInt(10); }
    void setExecutorCount(int n)
    {
        setUiValue("executors", qBound(1, n, 60), true);
        emit interfaceChanged();
    }
    // ---- Command line and keys ----
    // The command line's text (kept in sync by the command bar) and a way to replace it.
    QString commandLine() const { return m_commandLine; }
    void noteCommandLine(const QString &text) { m_commandLine = text; }
    void setCommandLine(const QString &text) { emit commandLineEdit(text); }
    // A keypad key from anywhere: on-screen keypads, keyboard shortcuts.
    void pressKey(const QString &key) { emit keyRequested(key); }
    // Single-key keyboard shortcuts; Insert toggles them.
    bool shortcutsEnabled() const { return uiValue("shortcuts", true).toBool(true); }
    void setShortcutsEnabled(bool on)
    {
        if (on == shortcutsEnabled())
            return;
        setUiValue("shortcuts", on, true);
        emit shortcutsChanged(on);
    }
    // Tapping an object (a pool slot, an executor) while the command line holds Move, Copy, Delete,
    // Label, Update, Remove or Assign completes the command with it:
    //   Move → tap Color.1 → "Move Color.1 At" → tap Color.9 → runs.
    // Returns true when the tap was used this way. `object` is e.g. "Color.1", "Executor 1.201".
    bool tapTarget(const QString &object, const QString &currentName = QString());

    bool storeArmed() const { return m_storeArmed; }
    void setStoreArmed(bool on)
    {
        if (m_storeArmed == on)
            return;
        m_storeArmed = on;
        emit storeArmedChanged(on);
    }

    // Opens an effect in the effect editor (adding an editor window if the view has none).
    int editingEffect() const { return m_editEffect; }
    void requestEditEffect(int id)
    {
        m_editEffect = id;
        emit editEffect(id);
    }
    // Points every open effect editor at this effect, without opening or switching to one.
    void chooseEffect(int id)
    {
        m_editEffect = id;
        emit effectChosen(id);
    }

    // Runs a command line and reports the result in the status area.
    CommandResult run(const QString &line)
    {
        m_inCommand = true;
        CommandResult r = commands.execute(line);
        m_inCommand = false;
        emit commandRan(line, r.ok, r.message);
        return r;
    }

signals:
    void status(const QString &text);
    void storeArmedChanged(bool on);
    void interfaceChanged();
    void editEffect(int id);
    void effectChosen(int id);
    void historyAdded(const HistoryEntry &entry);
    void historyCleared();
    void commandRan(const QString &line, bool ok, const QString &message);
    void keyRequested(const QString &key);
    void commandLineEdit(const QString &text);
    void shortcutsChanged(bool on);
    void uiValueChanged(const QString &key);

public:
    // Every patched universe gets an output if it has none.
    // One macro line: popups "(Prompt)" ask for text, "@" at the start adds the command line in
    // front of it, "@" at the end (or Execute off) leaves it on the command line to finish. A
    // foreground macro shows each line on the command line; a background one doesn't. Both go
    // into the history, marked with the macro.
    bool runMacroLine(const Macro &m, const MacroLine &line, const QString &command);
    void refreshOutputs() { patchOutputs(true); }

private:
    void patchOutputs(bool wholeShow)
    {
        QSet<int> used;
        for (const Fixture &f : show->fixtures())
            if (f.patched())
                used.insert(f.universe);
        // Only universes newly patched (or all of them when a show opens): an output removed by
        // hand doesn't come back while its universe stays patched.
        QList<int> fresh;
        for (int u : used)
            if (wholeShow || !m_knownUniverses.contains(u))
                fresh << u;
        m_knownUniverses = used;
        std::sort(fresh.begin(), fresh.end());
        QStringList added;
        for (int u : fresh)
            if (outputs->ensureUniverses({u}))
                added << QString::number(u);
        if (!added.isEmpty())
            emit status(tr("Universe %1 now sent on %2 (Outputs)")
                            .arg(added.join(", "), outputKindName(outputs->defaults().kind)));
    }
    QSet<int> m_knownUniverses;
    void addHistory(const HistoryEntry &e)
    {
        m_history.append(e);
        if (m_history.size() > 5000)
            m_history.remove(0, m_history.size() - 5000);
        emit historyAdded(e);
    }
    QVector<HistoryEntry> m_history;
    bool m_inCommand = false;
    QString m_macroSource;
    bool m_storeArmed = false;
    QString m_commandLine;
    int m_editEffect = 0;
};

} // namespace lumora
