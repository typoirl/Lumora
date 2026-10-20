// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "show.h"

#include <QElapsedTimer>
#include <QObject>

namespace lumora {

// Undo and redo. Every change to the show becomes a step: a command (with everything it changed),
// a store, a delete, an edit in a window. Changes in quick succession of the same kind (turning
// an encoder, dragging a fader of values) make one step. Plots keep their own steps, so undo in
// a plot takes back the last plot edit and nothing else.
class UndoManager : public QObject {
    Q_OBJECT
public:
    explicit UndoManager(Show *show, QObject *parent = nullptr);

    // A command is about to run: what it changes is one step, named after it.
    void beginAction(const QString &label);
    bool canUndo(bool plots = false) const { return (plots ? !m_plotUndo.isEmpty() : !m_undo.isEmpty()); }
    bool canRedo(bool plots = false) const { return (plots ? !m_plotRedo.isEmpty() : !m_redo.isEmpty()); }
    // Returns what was undone ("Store Cue 3"), or empty when there was nothing.
    QString undo(bool plots = false);
    QString redo(bool plots = false);
    void clear();   // a new or opened show starts with no history
    int depth(bool plots = false) const { return int(plots ? m_plotUndo.size() : m_undo.size()); }

    static constexpr int kMaxSteps = 100;

signals:
    void stepsChanged();

private:
    struct Step {
        Show::Snapshot state;
        QString label;
    };
    struct PlotStep {
        QMap<int, Plot> plots;
        QString label;
    };
    void onChanged(Show::Parts parts);
    static QString describe(Show::Parts parts);

    Show *m_show = nullptr;
    QVector<Step> m_undo, m_redo;
    QVector<PlotStep> m_plotUndo, m_plotRedo;
    Show::Snapshot m_current;          // the show as of the last step
    QMap<int, Plot> m_plotsCurrent;
    QString m_label;                    // the running command's
    bool m_batch = false;               // inside one command or one event: one step
    bool m_plotBatch = false;
    bool m_restoring = false;
    Show::Parts m_lastParts;
    QElapsedTimer m_lastChange, m_lastPlotChange;
};

} // namespace lumora
