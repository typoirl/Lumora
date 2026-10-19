// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;

namespace lumora {

class AppContext;

class MidiView : public QWidget {
    Q_OBJECT
public:
    explicit MidiView(AppContext *ctx, QWidget *parent = nullptr);

private:
    void refreshPorts();
    void refreshBindings();
    void refreshLearnButton();

    AppContext *m_ctx = nullptr;
    QListWidget *m_inputs = nullptr;
    QCheckBox *m_autoOpen = nullptr;
    QComboBox *m_feedback = nullptr;
    QLabel *m_backend = nullptr;
    QTableWidget *m_bindings = nullptr;
    QComboBox *m_target = nullptr;
    QPushButton *m_learn = nullptr;
    QListWidget *m_monitor = nullptr;
    bool m_updating = false;
};

} // namespace lumora
