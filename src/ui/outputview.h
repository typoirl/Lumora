// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dmxoutput.h"

#include <QDialog>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;

namespace lumora {

class AppContext;

class RouteDialog : public QDialog {
    Q_OBJECT
public:
    RouteDialog(const OutputConfig &c, QWidget *parent = nullptr);
    OutputConfig config() const;

private:
    void kindChanged();
    QComboBox *m_kind = nullptr;
    QCheckBox *m_enabled = nullptr;
    QSpinBox *m_first = nullptr;
    QSpinBox *m_protocol = nullptr;
    QLineEdit *m_host = nullptr;
    QComboBox *m_nic = nullptr;
    QComboBox *m_port = nullptr;
    QSpinBox *m_priority = nullptr;
    QFormLayout *m_form = nullptr;
};

// 512 channels of one universe, painted as a grid. Brighter cells mean higher values.
class DmxMonitor : public QWidget {
    Q_OBJECT
public:
    explicit DmxMonitor(AppContext *ctx, QWidget *parent = nullptr);
    void setUniverse(int u) { m_universe = u; update(); }
    QSize minimumSizeHint() const override { return {520, 300}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    AppContext *m_ctx = nullptr;
    int m_universe = 1;
};

// Outputs, one row per output: which console universe, the protocol, the universe it goes out as
// on the network (Art-Net 8 for console universe 1, say), the network card or USB port, and where
// to. A universe can have several rows (Art-Net and sACN at once). Universes get a row with the
// defaults when they're first patched. Below, a DMX monitor.
class OutputView : public QWidget {
    Q_OBJECT
public:
    explicit OutputView(AppContext *ctx, QWidget *parent = nullptr);

private:
    void refresh();
    void refreshDefaults();
    void edit(int row);
    void apply(int row, const std::function<void(OutputConfig &)> &change, bool rebuildRow = false);
    void addOutput(OutputKind kind);
    void refreshUsb();

    AppContext *m_ctx = nullptr;
    QTableWidget *m_routes = nullptr;
    QComboBox *m_defKind = nullptr;
    QComboBox *m_defNic = nullptr;
    QComboBox *m_defBase = nullptr;
    QLabel *m_usb = nullptr;
    class DmxMonitor *m_monitor = nullptr;
    QSpinBox *m_monitorUniverse = nullptr;
    bool m_updating = false;
    bool m_selfEdit = false;
};

} // namespace lumora
