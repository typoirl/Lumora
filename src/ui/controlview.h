// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QCheckBox;
class QListWidget;
class QTableWidget;
class QLabel;

namespace lumora {

class AppContext;

// OSC: the lines Lumora talks on (each UDP or TCP, its ports, the other device's address, a
// prefix, and what it may do), what's listening, and a monitor of what comes in and goes out.
class OscView : public QWidget {
    Q_OBJECT
public:
    explicit OscView(AppContext *ctx, QWidget *parent = nullptr);

private:
    void refreshLines();
    void refreshStatus();
    void commitRow(int row);
    AppContext *m_ctx = nullptr;
    QCheckBox *m_input = nullptr;
    QCheckBox *m_output = nullptr;
    QTableWidget *m_lines = nullptr;
    QListWidget *m_status = nullptr;
    QListWidget *m_monitor = nullptr;
    QCheckBox *m_showSent = nullptr;
    bool m_updating = false;
};

// Setup → Control: MIDI and OSC, on tabs.
class ControlView : public QWidget {
    Q_OBJECT
public:
    explicit ControlView(AppContext *ctx, QWidget *parent = nullptr);
};

} // namespace lumora
