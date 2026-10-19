// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model.h"

#include <QDialog>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTreeWidget;

namespace lumora {

class AppContext;
struct FixtureType;
struct FixtureMode;

class FixtureSearch;

// The "Add to patch" side panel: search any fixture (library or online), pick a mode, how many,
// IDs and addresses, and add.
class AddFixturesPanel : public QWidget {
    Q_OBJECT
public:
    explicit AddFixturesPanel(AppContext *ctx, QWidget *parent = nullptr);
    void focusSearch();
    FixtureSearch *search() const { return m_search; }

private:
    void typeChanged();
    void updateAddress();
    void updateSummary();
    void add();
    QVector<Fixture> planned(const FixtureType *t, const FixtureMode *m) const;   // named and numbered, not yet placed

    AppContext *m_ctx = nullptr;
    FixtureSearch *m_search = nullptr;
    QLabel *m_chosen = nullptr;
    QComboBox *m_mode = nullptr;
    QSpinBox *m_quantity = nullptr;
    QSpinBox *m_firstId = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_universe = nullptr;
    QLineEdit *m_address = nullptr;
    bool m_unpatched = false;   // the address was cleared on purpose
    int universe() const;
    int address() const;        // 0 when empty: unpatched
    QLabel *m_summary = nullptr;
    QPushButton *m_add = nullptr;
};

// Changes the fixture type and/or mode of one or more patched fixtures.
class ChangeTypeDialog : public QDialog {
    Q_OBJECT
public:
    ChangeTypeDialog(AppContext *ctx, const QVector<int> &fids, QWidget *parent = nullptr);

private:
    void typeChanged();
    void accept() override;

    AppContext *m_ctx = nullptr;
    QVector<int> m_fids;
    FixtureSearch *m_types = nullptr;
    QComboBox *m_mode = nullptr;
    QComboBox *m_addressing = nullptr;
    QSpinBox *m_universe = nullptr;
    QSpinBox *m_address = nullptr;
    QLabel *m_summary = nullptr;
};

class PatchView : public QWidget {
    Q_OBJECT
public:
    explicit PatchView(AppContext *ctx, QWidget *parent = nullptr);

    // Where imported fixture files are kept so they load again next time.

private:
    void refresh();
    void importFiles();
    void changeType();
    void patchSequential(const QVector<int> &fids, int universe);
    void unpatch(const QVector<int> &fids);
    void setAddress(int fid, const QString &text);   // "2.101", "101", or empty to unpatch
    int m_patchUniverse = 1;
    QVector<int> selectedFids() const;

    AppContext *m_ctx = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_libraryInfo = nullptr;
    AddFixturesPanel *m_addPanel = nullptr;
    bool m_updating = false;
};

} // namespace lumora
