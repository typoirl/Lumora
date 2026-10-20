// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QTabBar;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace lumora {

class AppContext;
class FixtureSources;

// Two tabs. In Show: the fixture types this show already has. All fixtures: one search over
// every fixture Lumora can get: the library it ships with (files on this computer), GDTF Share,
// the Open Fixture Library and QLC+, none of them in the show until picked. Picking one that
// isn't downloaded yet gets it on the spot; typeChosen fires once it's ready to patch.
class FixtureSearch : public QWidget {
    Q_OBJECT
public:
    explicit FixtureSearch(AppContext *ctx, QWidget *parent = nullptr);
    QString typeId() const { return m_typeId; }   // the chosen, ready-to-use type (or empty)
    void selectType(const QString &typeId);
    void setSearchText(const QString &text);
    void focusSearch();
    void debugPick(int row);   // developer: pick a result row

signals:
    void typeChosen(const QString &typeId);   // empty while nothing usable is selected

private:
    void populate();
    void currentChanged();
    void setStatus(const QString &text, bool error = false);

    AppContext *m_ctx = nullptr;
    FixtureSources *m_src = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_filter = nullptr;
    QTabBar *m_tabs = nullptr;   // In Show, or every fixture Lumora can get
    bool m_tabPicked = false;    // once you pick a tab, it stays
    QTreeWidget *m_list = nullptr;
    QLabel *m_status = nullptr;
    QTimer *m_debounce = nullptr;
    QString m_typeId;
    int m_pendingSource = -1, m_pendingEntry = -1;
};

} // namespace lumora
