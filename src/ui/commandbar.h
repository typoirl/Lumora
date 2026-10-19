// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;

namespace lumora {

class AppContext;

// Command line with history (Up/Down), keyword completion and a feedback line.
class CommandBar : public QWidget {
    Q_OBJECT
public:
    explicit CommandBar(AppContext *ctx, QWidget *parent = nullptr);
    void showMessage(const QString &text, bool error = false);
    bool labelDialog(const QString &line);   // "Label" + Enter with no name: ask for one
    void focusInput();
    // A key from the virtual keypad: keyword, digit, ".", "+", "-", "Back", "Enter" or "Clear".
    void pressKey(const QString &key);
    bool isActiveBar() const;   // the screen in front owns the keys

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    void execute();
    AppContext *m_ctx = nullptr;
    QLineEdit *m_input = nullptr;
    QLabel *m_feedback = nullptr;
    int m_historyPos = -1;
};

} // namespace lumora
