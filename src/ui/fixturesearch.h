// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace lumora {

class AppContext;
class FixtureSources;

// One search over every fixture Lumora can get: the types already in the library first, then
// GDTF Share, the Open Fixture Library and QLC+. Picking one that isn't in the library yet
// downloads it on the spot; typeChosen fires once it's ready to patch.
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
    QTreeWidget *m_list = nullptr;
    QLabel *m_status = nullptr;
    QTimer *m_debounce = nullptr;
    QString m_typeId;
    int m_pendingSource = -1, m_pendingEntry = -1;
};

} // namespace lumora
