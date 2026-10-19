// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>

class QWidget;

namespace lumora {

class QuickEditor;

// A window with its own options, shown when you right-click its tab. Options are saved with the
// view's layout, per window, so two fixture windows can look different.
class PanelOptions {
public:
    virtual ~PanelOptions() = default;
    virtual QJsonObject saveOptions() const = 0;
    virtual void loadOptions(const QJsonObject &o) = 0;
    virtual void buildOptions(QuickEditor *editor) = 0;   // controls apply live
};

// Call when a window's own state changes (a pool tab picked, a row chosen): the view it's in
// notices, so storing the view keeps it and recalling the view brings it back.
void panelStateChanged(QWidget *panel);

} // namespace lumora
