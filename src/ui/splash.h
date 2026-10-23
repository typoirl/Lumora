// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QLabel;
class QProgressBar;

namespace lumora {

// The loading screen: a borderless window with the logo, a progress bar and, under it, what's
// loading right now. Lives from startup until the main window shows.
class Splash : public QWidget
{
    Q_OBJECT
public:
    explicit Splash(bool active = true, QWidget *parent = nullptr);   // inactive: never shows
    ~Splash() override;

    // Move the bar and say what's loading. Unforced steps (one per file, say) repaint at most
    // ~60 times a second.
    void step(int percent, const QString &text, bool force = true);
    // From anywhere: says what's loading on the loading screen, when there is one.
    static void status(const QString &text);
    void finish();   // done: closes for good

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void refresh(bool force);
    QProgressBar *m_bar = nullptr;
    QLabel *m_text = nullptr;
    qint64 m_lastPaint = 0;
    bool m_active = true;
    static Splash *s_current;
};

}  // namespace lumora
