// SPDX-License-Identifier: GPL-3.0-or-later
#include "outputview.h"

#include "appcontext.h"
#include "theme.h"
#include "types.h"

#include <QHeaderView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace lumora {

RouteDialog::RouteDialog(const OutputConfig &c, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("DMX output"));
    auto *v = new QVBoxLayout(this);
    m_form = new QFormLayout;
    m_form->setVerticalSpacing(10);
    m_kind = new QComboBox(this);
    for (OutputKind k : {OutputKind::ArtNet, OutputKind::Sacn, OutputKind::EnttecPro, OutputKind::OpenDmx})
        m_kind->addItem(outputKindName(k), int(k));
    m_kind->setCurrentIndex(m_kind->findData(int(c.kind)));
    m_enabled = new QCheckBox(tr("Send output"), this);
    m_enabled->setChecked(c.enabled);
    m_first = new QSpinBox(this);
    m_first->setRange(1, kMaxUniverses);
    m_first->setValue(c.firstUniverse);
    m_protocol = new QSpinBox(this);
    m_protocol->setRange(0, 63999);
    m_protocol->setValue(c.protocolUniverse);
    m_host = new QLineEdit(c.host, this);
    m_nic = new QComboBox(this);
    m_nic->addItem(tr("Any (the system picks)"), QString());
    for (const auto &n : networkInterfaces())
        m_nic->addItem(n.first, n.second);
    if (!c.interfaceAddress.isEmpty() && m_nic->findData(c.interfaceAddress) < 0)
        m_nic->addItem(tr("%1 (not connected)").arg(c.interfaceAddress), c.interfaceAddress);
    m_nic->setCurrentIndex(std::max(0, m_nic->findData(c.interfaceAddress)));
    m_nic->setToolTip(tr("The network card to send from. Art-Net broadcasts on that card's network."));
    m_port = new QComboBox(this);
    for (const auto &p : OutputManager::serialPortLabels())
        m_port->addItem(p.first, p.second);
    if (!c.serialPort.isEmpty() && m_port->findData(c.serialPort) < 0)
        m_port->addItem(tr("%1 (not connected)").arg(c.serialPort), c.serialPort);
    m_port->setCurrentIndex(std::max(0, m_port->findData(c.serialPort)));
    m_priority = new QSpinBox(this);
    m_priority->setRange(0, 200);
    m_priority->setValue(c.priority);

    m_form->addRow(tr("Protocol"), m_kind);
    m_form->addRow(QString(), m_enabled);
    m_form->addRow(tr("Console universe"), m_first);
    m_form->addRow(tr("Network universe"), m_protocol);
    m_form->addRow(tr("Network interface"), m_nic);
    m_form->addRow(tr("Destination IP"), m_host);
    m_form->addRow(tr("USB port"), m_port);
    m_form->addRow(tr("sACN priority"), m_priority);
    v->addLayout(m_form);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(bb);
    connect(m_kind, &QComboBox::currentIndexChanged, this, &RouteDialog::kindChanged);
    kindChanged();
}

void RouteDialog::kindChanged()
{
    const auto k = OutputKind(m_kind->currentData().toInt());
    const bool net = k == OutputKind::ArtNet || k == OutputKind::Sacn;
    m_form->setRowVisible(m_protocol, net);
    m_form->setRowVisible(m_host, net);
    m_form->setRowVisible(m_nic, net);
    m_form->setRowVisible(m_port, !net);
    m_form->setRowVisible(m_priority, k == OutputKind::Sacn);
    if (k == OutputKind::ArtNet) {
        m_host->setPlaceholderText(tr("Leave empty to broadcast"));
        m_protocol->setMinimum(0);
        m_protocol->setToolTip(tr("Art-Net port-address, counted from 0"));
    } else if (k == OutputKind::Sacn) {
        m_host->setPlaceholderText(tr("Leave empty for multicast"));
        m_protocol->setMinimum(1);
        m_protocol->setToolTip(tr("sACN universe, counted from 1"));
    }
    if (m_port->count() == 0 && !net)
        m_port->setPlaceholderText(tr("No USB DMX interfaces found"));
    adjustSize();
}

OutputConfig RouteDialog::config() const
{
    OutputConfig c;
    c.kind = OutputKind(m_kind->currentData().toInt());
    c.enabled = m_enabled->isChecked();
    c.firstUniverse = m_first->value();
    const bool net = c.kind == OutputKind::ArtNet || c.kind == OutputKind::Sacn;
    c.count = 1;
    c.interfaceAddress = m_nic->currentData().toString();
    c.protocolUniverse = m_protocol->value();
    c.host = m_host->text().trimmed();
    if (!net)
        c.interfaceAddress.clear();
    c.serialPort = m_port->currentData().toString();
    c.priority = m_priority->value();
    return c;
}

// ===========================================================================

DmxMonitor::DmxMonitor(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *t = new QTimer(this);
    connect(t, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
    t->start(100);
}

void DmxMonitor::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QByteArray data = m_ctx->engine->universes().value(m_universe);
    constexpr int cols = 32, rows = 16;
    const double cw = double(width()) / cols, ch = double(height()) / rows;
    p.setFont(theme::uiFont(7));
    for (int i = 0; i < 512; ++i) {
        const int v = i < data.size() ? quint8(data[i]) : 0;
        QRectF r((i % cols) * cw + 1, (i / cols) * ch + 1, cw - 2, ch - 2);
        p.setPen(Qt::NoPen);
        p.setBrush(v ? theme::mix(theme::raised, theme::live, 0.15 + 0.7 * v / 255.0) : theme::panel);
        p.drawRoundedRect(r, 2, 2);
        p.setPen(v ? QColor(0x14, 0x17, 0x1c) : theme::faint);
        if (v > 0 || cw > 26)
            p.drawText(r, Qt::AlignCenter, v ? QString::number(v) : QString::number(i + 1));
    }
}

// ===========================================================================

namespace {
enum OutputColumn { ColOn, ColUniverse, ColProtocol, ColNetUniverse, ColVia, ColTo, ColStatus, OutputColumns };

bool isNetwork(OutputKind k) { return k == OutputKind::ArtNet || k == OutputKind::Sacn; }

void fillInterfaces(QComboBox *c, const QString &current)
{
    c->addItem(QObject::tr("Any (the system picks)"), QString());
    for (const auto &n : networkInterfaces())
        c->addItem(n.first, n.second);
    if (!current.isEmpty() && c->findData(current) < 0)
        c->addItem(QObject::tr("%1 (not connected)").arg(current), current);
    c->setCurrentIndex(std::max(0, c->findData(current)));
}
} // namespace

OutputView::OutputView(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(10, 8, 10, 6);
    v->setSpacing(8);
    auto *head = new QHBoxLayout;
    auto *title = new QLabel(tr("DMX outputs"), this);
    title->setProperty("role", "title");
    head->addWidget(title);
    head->addStretch(1);
    auto *more = new QPushButton(tr("More settings…"), this);
    more->setToolTip(tr("sACN priority and the rest of the selected output's settings"));
    auto *remove = new QPushButton(tr("Remove"), this);
    auto *add = new QPushButton(tr("Add output"), this);
    add->setProperty("role", "primary");
    add->setToolTip(tr("Another output for the selected universe (send it on Art-Net and sACN at once, say)"));
    auto *addMenu = new QMenu(add);
    addMenu->addSection(tr("Network"));
    for (OutputKind k : {OutputKind::ArtNet, OutputKind::Sacn})
        addMenu->addAction(outputKindName(k), this, [this, k]() { addOutput(k); });
    addMenu->addSection(tr("USB DMX interface"));
    addMenu->addAction(tr("Enttec DMX USB Pro (and compatibles)"), this, [this]() { addOutput(OutputKind::EnttecPro); });
    addMenu->addAction(tr("Enttec Open DMX (FTDI cables)"), this, [this]() { addOutput(OutputKind::OpenDmx); });
    add->setMenu(addMenu);
    head->addWidget(more);
    head->addWidget(remove);
    head->addWidget(add);
    v->addLayout(head);

    // Defaults for universes patched from now on.
    auto *defs = new QHBoxLayout;
    defs->setSpacing(8);
    auto *defLabel = new QLabel(tr("New universes go out on"), this);
    defLabel->setProperty("role", "muted");
    m_defKind = new QComboBox(this);
    m_defKind->addItem(outputKindName(OutputKind::ArtNet), int(OutputKind::ArtNet));
    m_defKind->addItem(outputKindName(OutputKind::Sacn), int(OutputKind::Sacn));
    auto *via = new QLabel(tr("via"), this);
    via->setProperty("role", "muted");
    m_defNic = new QComboBox(this);
    m_defNic->setMinimumWidth(220);
    m_defBase = new QComboBox(this);
    m_defBase->addItem(tr("Universe 1 → Art-Net 0"), 0);
    m_defBase->addItem(tr("Universe 1 → Art-Net 1"), 1);
    m_defBase->setToolTip(tr("Art-Net counts universes from 0. Pick how console universes line up with it."));
    defs->addWidget(defLabel);
    defs->addWidget(m_defKind);
    defs->addWidget(via);
    defs->addWidget(m_defNic);
    defs->addWidget(m_defBase);
    defs->addStretch(1);
    v->addLayout(defs);
    auto *where = new QLabel(tr("Every patched universe gets an output, on, with these defaults. Outputs are this computer's settings: "
                                "they stay here whatever show you open."), this);
    where->setProperty("role", "muted");
    where->setWordWrap(true);
    v->addWidget(where);
    // USB DMX interfaces: what's plugged in, and how to use it.
    auto *usbRow = new QHBoxLayout;
    m_usb = new QLabel(this);
    m_usb->setWordWrap(true);
    auto *usbRefresh = new QPushButton(tr("Look again"), this);
    usbRefresh->setToolTip(tr("Plugged an interface in? Look for USB DMX interfaces again."));
    usbRow->addWidget(m_usb, 1);
    usbRow->addWidget(usbRefresh);
    v->addLayout(usbRow);
    connect(usbRefresh, &QPushButton::clicked, this, [this]() {
        refreshUsb();
        refresh();
    });
    auto saveDefaults = [this]() {
        if (m_updating)
            return;
        OutputDefaults d;
        d.kind = OutputKind(m_defKind->currentData().toInt());
        d.interfaceAddress = m_defNic->currentData().toString();
        d.artnetBase = m_defBase->currentData().toInt();
        m_selfEdit = true;
        m_ctx->outputs->setDefaults(d);
        m_selfEdit = false;
    };
    connect(m_defKind, &QComboBox::activated, this, saveDefaults);
    connect(m_defNic, &QComboBox::activated, this, saveDefaults);
    connect(m_defBase, &QComboBox::activated, this, saveDefaults);

    m_routes = new QTableWidget(this);
    m_routes->setColumnCount(OutputColumns);
    m_routes->setHorizontalHeaderLabels({tr("On"), tr("Universe"), tr("Protocol"), tr("Sent as"), tr("Network interface / port"),
                                         tr("Destination"), tr("Status")});
    m_routes->verticalHeader()->hide();
    m_routes->verticalHeader()->setDefaultSectionSize(32);
    m_routes->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_routes->horizontalHeader()->setStretchLastSection(true);
    m_routes->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_routes->setSelectionMode(QAbstractItemView::SingleSelection);
    m_routes->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_routes->setShowGrid(false);
    m_routes->setColumnWidth(ColOn, 40);
    m_routes->setColumnWidth(ColUniverse, 84);
    m_routes->setColumnWidth(ColProtocol, 150);
    m_routes->setColumnWidth(ColNetUniverse, 110);
    m_routes->setColumnWidth(ColVia, 250);
    m_routes->setColumnWidth(ColTo, 160);
    v->addWidget(m_routes, 2);

    auto *monHead = new QHBoxLayout;
    auto *monTitle = new QLabel(tr("DMX monitor"), this);
    monTitle->setProperty("role", "title");
    auto *uniLabel = new QLabel(tr("Universe"), this);
    uniLabel->setProperty("role", "muted");
    m_monitorUniverse = new QSpinBox(this);
    m_monitorUniverse->setRange(1, kMaxUniverses);
    m_monitorUniverse->setMinimumWidth(80);
    monHead->addWidget(monTitle);
    monHead->addSpacing(14);
    monHead->addWidget(uniLabel);
    monHead->addWidget(m_monitorUniverse);
    monHead->addStretch(1);
    v->addLayout(monHead);
    m_monitor = new DmxMonitor(ctx, this);
    v->addWidget(m_monitor, 3);
    connect(m_monitorUniverse, &QSpinBox::valueChanged, m_monitor, &DmxMonitor::setUniverse);

    connect(more, &QPushButton::clicked, this, [this]() { this->edit(m_routes->currentRow()); });
    connect(m_routes, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { this->edit(row); });
    connect(remove, &QPushButton::clicked, this, [this]() { m_ctx->outputs->removeConfig(m_routes->currentRow()); });
    connect(m_routes, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
        if (m_updating || it->column() != ColOn)
            return;
        const bool on = it->checkState() == Qt::Checked;
        apply(it->row(), [on](OutputConfig &c) { c.enabled = on; });
    });
    // The monitor follows the selected output's universe.
    connect(m_routes, &QTableWidget::currentCellChanged, this, [this](int row) {
        const auto &cfgs = m_ctx->outputs->configs();
        if (row >= 0 && row < cfgs.size())
            m_monitorUniverse->setValue(cfgs[row].firstUniverse);
    });
    connect(ctx->outputs, &OutputManager::changed, this, [this]() {
        if (!m_selfEdit)
            refresh();
    });
    auto *t = new QTimer(this);
    connect(t, &QTimer::timeout, this, [this]() {
        for (int r = 0; r < m_routes->rowCount(); ++r)
            if (QTableWidgetItem *it = m_routes->item(r, ColStatus))
                it->setText(m_ctx->outputs->status(r));
    });
    t->start(1000);
    refreshUsb();
    refresh();
}

void OutputView::apply(int row, const std::function<void(OutputConfig &)> &change, bool rebuildRow)
{
    if (row < 0 || row >= m_ctx->outputs->configs().size())
        return;
    OutputConfig c = m_ctx->outputs->configs()[row];
    change(c);
    m_selfEdit = !rebuildRow;   // edits in place keep the table (and the field being typed in)
    m_ctx->outputs->updateConfig(row, c);
    m_selfEdit = false;
    if (rebuildRow)
        QTimer::singleShot(0, this, [this, row]() {
            refresh();
            m_routes->selectRow(row);
        });
}

void OutputView::addOutput(OutputKind kind)
{
    const auto &cfgs = m_ctx->outputs->configs();
    const int row = m_routes->currentRow();
    const int universe = row >= 0 && row < cfgs.size() ? cfgs[row].firstUniverse : m_monitorUniverse->value();
    OutputConfig c = m_ctx->outputs->defaultFor(universe, kind);
    if (kind == OutputKind::EnttecPro || kind == OutputKind::OpenDmx)
        c.serialPort = m_ctx->outputs->freeSerialPort();
    m_ctx->outputs->addConfig(c);
    m_routes->selectRow(m_routes->rowCount() - 1);
}

void OutputView::edit(int row)
{
    if (row < 0 || row >= m_ctx->outputs->configs().size())
        return;
    RouteDialog d(m_ctx->outputs->configs()[row], this);
    if (d.exec() == QDialog::Accepted)
        m_ctx->outputs->updateConfig(row, d.config());
}

void OutputView::refreshUsb()
{
    const auto ports = OutputManager::serialPortLabels();
    QStringList names;
    for (const auto &p : ports)
        names << p.first;
    m_usb->setText(ports.isEmpty()
                       ? tr("USB DMX: no interface found. Plug one in, press Look again, then Add output → USB DMX interface.")
                       : tr("USB DMX: %1. Use Add output → USB DMX interface, or pick Enttec as an output's protocol.")
                             .arg(names.join(QStringLiteral(", "))));
}

void OutputView::refreshDefaults()
{
    const OutputDefaults &d = m_ctx->outputs->defaults();
    m_defKind->setCurrentIndex(std::max(0, m_defKind->findData(int(d.kind))));
    m_defNic->clear();
    fillInterfaces(m_defNic, d.interfaceAddress);
    m_defBase->setCurrentIndex(std::max(0, m_defBase->findData(d.artnetBase)));
}

void OutputView::refresh()
{
    m_updating = true;
    refreshDefaults();
    const int keep = m_routes->currentRow();
    const auto &cfgs = m_ctx->outputs->configs();
    m_routes->clearContents();
    m_routes->setRowCount(cfgs.size());
    for (int r = 0; r < cfgs.size(); ++r) {
        const OutputConfig &c = cfgs[r];
        const bool net = isNetwork(c.kind);
        auto *on = new QTableWidgetItem;
        on->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        on->setCheckState(c.enabled ? Qt::Checked : Qt::Unchecked);
        m_routes->setItem(r, ColOn, on);

        auto *uni = new QSpinBox(m_routes);
        uni->setRange(1, kMaxUniverses);
        uni->setValue(c.firstUniverse);
        uni->setToolTip(tr("The console universe this output sends"));
        connect(uni, &QSpinBox::editingFinished, this, [this, r, uni]() {
            const int u = uni->value();
            apply(r, [u](OutputConfig &x) { x.firstUniverse = u; });
            m_monitorUniverse->setValue(u);
        });
        m_routes->setCellWidget(r, ColUniverse, uni);

        auto *kind = new QComboBox(m_routes);
        for (OutputKind k : {OutputKind::ArtNet, OutputKind::Sacn, OutputKind::EnttecPro, OutputKind::OpenDmx})
            kind->addItem(outputKindName(k), int(k));
        kind->setCurrentIndex(kind->findData(int(c.kind)));
        connect(kind, &QComboBox::activated, this, [this, r, kind]() {
            const auto k = OutputKind(kind->currentData().toInt());
            const OutputConfig fresh = m_ctx->outputs->defaultFor(m_ctx->outputs->configs().value(r).firstUniverse, k);
            const QString port = m_ctx->outputs->freeSerialPort();
            apply(r, [k, fresh, port](OutputConfig &x) {
                x.kind = k;
                x.protocolUniverse = fresh.protocolUniverse;   // the usual numbering for the new protocol
                if (isNetwork(k) && x.interfaceAddress.isEmpty())
                    x.interfaceAddress = fresh.interfaceAddress;
                if (!isNetwork(k) && x.serialPort.isEmpty())
                    x.serialPort = port;
            }, true);
        });
        m_routes->setCellWidget(r, ColProtocol, kind);

        if (net) {
            auto *netUni = new QSpinBox(m_routes);
            netUni->setRange(c.kind == OutputKind::ArtNet ? 0 : 1, c.kind == OutputKind::ArtNet ? 32767 : 63999);
            netUni->setValue(c.protocolUniverse);
            netUni->setPrefix(c.kind == OutputKind::ArtNet ? tr("Art-Net ") : tr("sACN "));
            netUni->setToolTip(c.kind == OutputKind::ArtNet
                                   ? tr("Art-Net universe (port-address, from 0): net %1, subnet %2, universe %3")
                                         .arg((c.protocolUniverse >> 8) & 0x7f).arg((c.protocolUniverse >> 4) & 0xf).arg(c.protocolUniverse & 0xf)
                                   : tr("sACN universe, from 1"));
            connect(netUni, &QSpinBox::editingFinished, this, [this, r, netUni]() {
                const int n = netUni->value();
                apply(r, [n](OutputConfig &x) { x.protocolUniverse = n; });
            });
            m_routes->setCellWidget(r, ColNetUniverse, netUni);

            auto *nic = new QComboBox(m_routes);
            fillInterfaces(nic, c.interfaceAddress);
            connect(nic, &QComboBox::activated, this, [this, r, nic]() {
                const QString a = nic->currentData().toString();
                apply(r, [a](OutputConfig &x) { x.interfaceAddress = a; });
            });
            m_routes->setCellWidget(r, ColVia, nic);

            auto *to = new QLineEdit(c.host, m_routes);
            to->setPlaceholderText(c.kind == OutputKind::ArtNet ? tr("broadcast") : tr("multicast"));
            to->setToolTip(tr("An IP address to send to directly (unicast). Leave it empty to broadcast (Art-Net) or "
                              "multicast (sACN)."));
            connect(to, &QLineEdit::editingFinished, this, [this, r, to]() {
                const QString h = to->text().trimmed();
                apply(r, [h](OutputConfig &x) { x.host = h; });
            });
            m_routes->setCellWidget(r, ColTo, to);
        } else {
            auto *none = new QTableWidgetItem(QStringLiteral("—"));
            none->setForeground(theme::faint);
            m_routes->setItem(r, ColNetUniverse, none);
            auto *port = new QComboBox(m_routes);
            for (const auto &pl : OutputManager::serialPortLabels())
                port->addItem(pl.first, pl.second);
            if (!c.serialPort.isEmpty() && port->findData(c.serialPort) < 0)
                port->addItem(tr("%1 (not connected)").arg(c.serialPort), c.serialPort);
            port->setCurrentIndex(port->findData(c.serialPort));
            port->setPlaceholderText(tr("No USB DMX interface found"));
            port->setToolTip(tr("The USB DMX interface (serial port) to send this universe from"));
            connect(port, &QComboBox::activated, this, [this, r, port]() {
                const QString p = port->currentData().toString();
                apply(r, [p](OutputConfig &x) { x.serialPort = p; });
            });
            m_routes->setCellWidget(r, ColVia, port);
            auto *to = new QTableWidgetItem(QStringLiteral("—"));
            to->setForeground(theme::faint);
            m_routes->setItem(r, ColTo, to);
        }
        auto *st = new QTableWidgetItem(m_ctx->outputs->status(r));
        st->setForeground(theme::muted);
        m_routes->setItem(r, ColStatus, st);
    }
    if (keep >= 0 && keep < m_routes->rowCount())
        m_routes->selectRow(keep);
    m_updating = false;
}

} // namespace lumora
