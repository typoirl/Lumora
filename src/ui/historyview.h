// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "paneloptions.h"

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

namespace lumora {

class AppContext;
struct HistoryEntry;

// Every command run this session (typed, keyed or tapped) and every edit made in a window,
// newest at the bottom. Double-click a command to put it back on the command line;
// right-click to run it again or copy it.
class HistoryView : public QWidget, public PanelOptions {
    Q_OBJECT
public:
    explicit HistoryView(AppContext *ctx, QWidget *parent = nullptr);

    QJsonObject saveOptions() const override;
    void loadOptions(const QJsonObject &o) override;
    void buildOptions(QuickEditor *editor) override;

private:
    void append(const HistoryEntry &e);
    void reload();
    bool shows(const HistoryEntry &e) const;

    AppContext *m_ctx = nullptr;
    QTreeWidget *m_list = nullptr;
    int m_filter = 0;   // 0 everything, 1 commands, 2 edits
};

} // namespace lumora
