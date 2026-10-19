// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFrame>

class QPushButton;

namespace lumora {

class AppContext;

// Virtual console keypad. Keys write into the command line the same way a console's hard keys
// do; Enter runs the command. Each key shows its keyboard shortcut underneath.
// Floating: an overlay with a header you drag and a close button.
// Embedded: an ordinary window inside a view (Add window → Command keys).
class CommandKeypad : public QFrame {
    Q_OBJECT
public:
    CommandKeypad(AppContext *ctx, bool embedded, QWidget *parent = nullptr);

signals:
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;

private:
    AppContext *m_ctx = nullptr;
    bool m_embedded;
    QPoint m_dragOffset;
    bool m_dragging = false;
};

} // namespace lumora
