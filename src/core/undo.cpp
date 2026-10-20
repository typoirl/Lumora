// SPDX-License-Identifier: GPL-3.0-or-later
#include "undo.h"

#include <QTimer>

namespace lumora {

namespace {
// Changes this close together, of the same kind and not from a command, are one step.
constexpr qint64 kMergeMs = 1000;
} // namespace

UndoManager::UndoManager(Show *show, QObject *parent) : QObject(parent), m_show(show)
{
    m_current = show->snapshot();
    m_plotsCurrent = show->plotsSnapshot();
    connect(show, &Show::changed, this, &UndoManager::onChanged);
}

void UndoManager::beginAction(const QString &label)
{
    m_label = label.trimmed();
    m_batch = false;
    m_plotBatch = false;
    // The command's changes are one step until control gets back to the event loop.
    QTimer::singleShot(0, this, [this]() {
        m_label.clear();
        m_batch = false;
        m_plotBatch = false;
    });
}

void UndoManager::clear()
{
    m_undo.clear();
    m_redo.clear();
    m_plotUndo.clear();
    m_plotRedo.clear();
    m_current = m_show->snapshot();
    m_plotsCurrent = m_show->plotsSnapshot();
    emit stepsChanged();
}

QString UndoManager::describe(Show::Parts parts)
{
    if (parts & Show::PatchPart) return tr("the patch");
    if (parts & Show::SequencesPart) return tr("cues");
    if (parts & Show::PresetsPart) return tr("presets");
    if (parts & Show::GroupsPart) return tr("groups");
    if (parts & Show::EffectsPart) return tr("effects");
    if (parts & Show::MacrosPart) return tr("macros");
    if (parts & Show::ViewsPart) return tr("views");
    if (parts & Show::SpreadsPart) return tr("spreads");
    if (parts & Show::ParkPart) return tr("parking");
    if (parts & Show::ProgrammerPart) return tr("the programmer");
    if (parts & Show::ExecutorsPart) return tr("executors");
    return tr("settings");
}

void UndoManager::onChanged(Show::Parts parts)
{
    if (m_restoring)
        return;
    if (parts == Show::Parts(Show::AllParts)) {   // a new or opened show
        clear();
        return;
    }
    if (parts & Show::PlotsPart) {
        const bool merge = m_plotBatch
                           || (m_label.isEmpty() && m_lastPlotChange.isValid() && m_lastPlotChange.elapsed() < kMergeMs);
        if (!merge) {
            m_plotUndo.append({m_plotsCurrent, m_label.isEmpty() ? tr("a plot edit") : m_label});
            if (m_plotUndo.size() > kMaxSteps)
                m_plotUndo.removeFirst();
            m_plotRedo.clear();
            emit stepsChanged();
        }
        m_plotsCurrent = m_show->plotsSnapshot();
        m_lastPlotChange.start();
        if (!m_plotBatch) {
            m_plotBatch = true;
            if (m_label.isEmpty())
                QTimer::singleShot(0, this, [this]() { m_plotBatch = false; });
        }
    }
    const Show::Parts rest = parts & ~Show::Parts(Show::PlotsPart);
    if (!rest)
        return;
    // What's running (Go, faders, the page, the grand master) isn't undone; it rides along.
    const bool runtimeOnly = !(rest & ~Show::Parts(Show::ExecutorsPart | Show::SettingsPart));
    if (runtimeOnly && m_label.isEmpty() && !m_batch) {
        m_current = m_show->snapshot();
        return;
    }
    const Show::Snapshot now = m_show->snapshot();
    if (!m_batch && now.sameData(m_current))
        return;   // a running macro ticking, a playback moving on: nothing to take back
    const bool merge = m_batch
                       || (m_label.isEmpty() && rest == m_lastParts && m_lastChange.isValid() && m_lastChange.elapsed() < kMergeMs);
    if (!merge) {
        m_undo.append({m_current, m_label.isEmpty() ? tr("a change to %1").arg(describe(rest)) : m_label});
        if (m_undo.size() > kMaxSteps)
            m_undo.removeFirst();
        m_redo.clear();
        emit stepsChanged();
    }
    m_current = now;
    m_lastParts = rest;
    m_lastChange.start();
    if (!m_batch) {
        m_batch = true;
        if (m_label.isEmpty())   // a command's batch ends with beginAction's timer
            QTimer::singleShot(0, this, [this]() { m_batch = false; });
    }
}

QString UndoManager::undo(bool plots)
{
    m_restoring = true;
    QString label;
    if (plots && !m_plotUndo.isEmpty()) {
        PlotStep st = m_plotUndo.takeLast();
        m_plotRedo.append({m_show->plotsSnapshot(), st.label});
        m_show->restorePlots(st.plots);
        m_plotsCurrent = st.plots;
        label = st.label;
    } else if (!plots && !m_undo.isEmpty()) {
        Step st = m_undo.takeLast();
        m_redo.append({m_show->snapshot(), st.label});
        m_show->restore(st.state);
        m_current = m_show->snapshot();
        label = st.label;
    }
    m_restoring = false;
    m_lastChange.invalidate();
    m_lastPlotChange.invalidate();
    if (!label.isEmpty())
        emit stepsChanged();
    return label;
}

QString UndoManager::redo(bool plots)
{
    m_restoring = true;
    QString label;
    if (plots && !m_plotRedo.isEmpty()) {
        PlotStep st = m_plotRedo.takeLast();
        m_plotUndo.append({m_show->plotsSnapshot(), st.label});
        m_show->restorePlots(st.plots);
        m_plotsCurrent = st.plots;
        label = st.label;
    } else if (!plots && !m_redo.isEmpty()) {
        Step st = m_redo.takeLast();
        m_undo.append({m_show->snapshot(), st.label});
        m_show->restore(st.state);
        m_current = m_show->snapshot();
        label = st.label;
    }
    m_restoring = false;
    m_lastChange.invalidate();
    m_lastPlotChange.invalidate();
    if (!label.isEmpty())
        emit stepsChanged();
    return label;
}

} // namespace lumora
