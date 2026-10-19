// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "macros.h"
#include "poolgrid.h"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace lumora {

class AppContext;

// The macro pool: tap a macro to run it (again to go on from a Wait), Store and tap an empty slot
// to make one, right-click to edit, recolor, switch foreground/background or stop it.
class MacrosPanel : public PoolPanel {
    Q_OBJECT
public:
    explicit MacrosPanel(AppContext *ctx, QWidget *parent = nullptr);

private:
    void refresh();
    AppContext *m_ctx = nullptr;
};

// Edits one macro's lines: the command, Follow (with a time) or Wait, enabled, execute, a note.
class MacroEditorDialog : public QDialog {
    Q_OBJECT
public:
    MacroEditorDialog(AppContext *ctx, int id, QWidget *parent = nullptr);

private:
    void load();
    void addRow(int at, const MacroLine &line);
    Macro collect() const;
    void move(int delta);

    AppContext *m_ctx = nullptr;
    Macro m_macro;
    QLineEdit *m_name = nullptr;
    QComboBox *m_mode = nullptr;
    QPushButton *m_color = nullptr;
    QTableWidget *m_lines = nullptr;
};

void openMacroEditor(AppContext *ctx, int id, QWidget *parent);

} // namespace lumora
