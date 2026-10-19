// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QTreeWidget;
class QLabel;

namespace lumora {

class AppContext;

// Everything parked: fixture, attribute and the value it's held at. Unpark from here, or
// with the Park and Unpark commands.
class ParkView : public QWidget {
    Q_OBJECT
public:
    explicit ParkView(AppContext *ctx, QWidget *parent = nullptr);

private:
    void reload();
    void unparkSelected();

    AppContext *m_ctx = nullptr;
    QTreeWidget *m_list = nullptr;
    QLabel *m_empty = nullptr;
};

} // namespace lumora
