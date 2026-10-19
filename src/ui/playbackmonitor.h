// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "paneloptions.h"

#include <QStringList>
#include <QWidget>

class QLabel;
class QProgressBar;
class QTableWidget;

namespace lumora {

class AppContext;
struct Executor;
struct Sequence;

// The playback monitor: a cue list as it runs. The live cue is lit, the next one is marked, and
// a progress bar runs while a cue fades in. Follows the sequence the last Go went to, or locks
// to one. Right-click the tab to lock it and to pick and order the columns (cue, label, fade,
// delay, notes, contents); right-click a cell to edit it.
class PlaybackMonitor : public QWidget, public PanelOptions {
    Q_OBJECT
public:
    explicit PlaybackMonitor(AppContext *ctx, QWidget *parent = nullptr);

    QJsonObject saveOptions() const override;
    void loadOptions(const QJsonObject &o) override;
    void buildOptions(QuickEditor *editor) override;

    // Column ids, in their default order.
    static QStringList allColumns();
    static QString columnTitle(const QString &id);

private:
    int sequenceId() const;
    const Executor *executor() const;
    void rebuild();
    void tick();
    void editCell(int row, int column);

    AppContext *m_ctx = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_now = nullptr;
    QLabel *m_next = nullptr;
    QProgressBar *m_progress = nullptr;
    QWidget *m_banner = nullptr;
    QTableWidget *m_table = nullptr;

    int m_locked = 0;            // 0: follow the last sequence started with Go
    QStringList m_columns;       // shown, in order
    bool m_showBanner = true;
    int m_liveRow = -2;   // -2: rebuilt, restyle everything
    int m_nextRow = -2;
    QString m_signature;
};

} // namespace lumora
