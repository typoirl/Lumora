// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QLineEdit;
class QTableWidget;
class QLabel;

namespace lumora {

class AppContext;

// Goto, from an executor's Goto button or "Goto Executor 201": the sequence's cues with their
// labels, big enough to tap. Tap a cue, or type its number and press Enter, and the executor goes
// to it in the cue's own fade. Opens in the middle of the screen it was asked from.
class GotoDialog : public QDialog {
    Q_OBJECT
public:
    GotoDialog(AppContext *ctx, int page, int executor, QWidget *parent = nullptr);
    static void open(AppContext *ctx, int page, int executor, QWidget *parent);

protected:
    void keyPressEvent(QKeyEvent *ev) override;

private:
    void goTo(int cueIndex);
    void goTyped();
    AppContext *m_ctx = nullptr;
    int m_page = 1, m_executor = 201;
    QLineEdit *m_number = nullptr;
    QTableWidget *m_list = nullptr;
    QLabel *m_note = nullptr;
};

} // namespace lumora
