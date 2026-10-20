// SPDX-License-Identifier: GPL-3.0-or-later
#include "controlview.h"

#include "appcontext.h"
#include "midiview.h"
#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTime>
#include <QVBoxLayout>

namespace lumora {

namespace {
enum Col { Name, Protocol, Destination, InPort, OutPort, Prefix, Receive, Commands, Send, ColCount };

QLabel *title(const QString &t, QWidget *parent)
{
    auto *l = new QLabel(t, parent);
    l->setProperty("role", "title");
    return l;
}

QWidget *centered(QWidget *w, QWidget *parent)
{
    auto *holder = new QWidget(parent);
    auto *h = new QHBoxLayout(holder);
    h->setContentsMargins(0, 0, 0, 0);
    h->addStretch(1);
    h->addWidget(w);
    h->addStretch(1);
    return holder;
}

const char *kReference = R"(<p><b>What Lumora understands</b> (after the line's prefix, if it has one):</p>
<table cellspacing="2">
<tr><td><code>/cmd "Fixture 1 At 50"</code></td><td>a command line (needs <i>Commands</i>); <code>%1</code>, <code>%2</code>… take the arguments</td></tr>
<tr><td><code>/key/Go</code>, <code>/key/Clear</code>, <code>/key/7</code></td><td>a key, as on the command keys</td></tr>
<tr><td><code>/exec/1/201/fader 0.5</code></td><td>an executor's fader (page 1; leave the page out for the current one)</td></tr>
<tr><td><code>/exec/1/201/button/1 1</code> … <code>0</code></td><td>its button pressed, released</td></tr>
<tr><td><code>/exec/201/go</code>, <code>goback</code>, <code>off</code>, <code>on</code>, <code>top</code>, <code>pause</code></td><td>playback</td></tr>
<tr><td><code>/Page1/Fader201 50</code>, <code>/Page1/Key201 1</code></td><td>the same, written page and executor</td></tr>
<tr><td><code>/cue/2/5/fire</code></td><td>sequence 2 to cue 5</td></tr>
<tr><td><code>/group/3</code>, <code>/group/3 80</code></td><td>select a group, or set its intensity</td></tr>
<tr><td><code>/fixture/101 75</code>, <code>/fixture/101/pan 45</code></td><td>a fixture's intensity, or any attribute</td></tr>
<tr><td><code>/preset/4/2/fire</code></td><td>Color.2 on the selection</td></tr>
<tr><td><code>/master/1 0.5</code>, <code>/master/1/key</code>, <code>/master/1/tap</code></td><td>a master's fader, its key, tap tempo</td></tr>
<tr><td><code>/encoder/1 3</code>, <code>/encoder/fine/1 -1</code></td><td>turn the encoder bar's first encoder</td></tr>
<tr><td><code>/macro/2/fire</code>, <code>/gm 0.8</code>, <code>/blackout 1</code>, <code>/page 2</code>, <code>/progfade 3</code></td><td></td></tr>
<tr><td><code>/ping</code>, <code>/get/version</code>, <code>/get/cmd</code></td><td>answered under <code>/out/…</code></td></tr>
</table>
<p>Levels are 0–1, or 0–100 as whole numbers. Lines with <i>Send</i> on get feedback whenever something changes:
<code>/out/exec/1/201/fader</code>, <code>/name</code>, <code>/cue</code>, <code>/button/1</code>; <code>/out/master/1/…</code>;
<code>/out/encoder/1/name</code> and <code>/value</code>; <code>/out/cmd</code>, <code>/out/page</code>, <code>/out/gm</code>.
TCP uses SLIP framing (and reads length-prefixed packets too). Macros can send OSC: <code>SendOSC 1 "/address,i,50"</code>.</p>)";
} // namespace

OscView::OscView(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 6);
    root->setSpacing(8);

    auto *top = new QHBoxLayout;
    top->addWidget(title(tr("OSC lines"), this));
    top->addSpacing(16);
    m_input = new QCheckBox(tr("Receive"), this);
    m_output = new QCheckBox(tr("Send"), this);
    m_input->setToolTip(tr("Listen on every line's ports (off: nothing comes in)"));
    m_output->setToolTip(tr("Send feedback and SendOSC messages (off: nothing goes out)"));
    top->addWidget(m_input);
    top->addWidget(m_output);
    top->addStretch(1);
    auto *add = new QPushButton(tr("Add line"), this);
    auto *remove = new QPushButton(tr("Remove line"), this);
    top->addWidget(add);
    top->addWidget(remove);
    root->addLayout(top);

    m_lines = new QTableWidget(0, ColCount, this);
    m_lines->setHorizontalHeaderLabels({tr("Name"), tr("Protocol"), tr("Other device (IP)"), tr("In port"), tr("Out port"), tr("Prefix"),
                                        tr("Receive"), tr("Commands"), tr("Send")});
    m_lines->verticalHeader()->hide();
    m_lines->verticalHeader()->setDefaultSectionSize(34);
    m_lines->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_lines->horizontalHeader()->setSectionResizeMode(Destination, QHeaderView::Stretch);
    m_lines->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_lines->setShowGrid(false);
    m_lines->setColumnWidth(Name, 140);
    m_lines->setColumnWidth(Protocol, 80);
    m_lines->setColumnWidth(InPort, 90);
    m_lines->setColumnWidth(OutPort, 90);
    m_lines->setColumnWidth(Prefix, 110);
    for (int c : {Receive, Commands, Send})
        m_lines->setColumnWidth(c, 84);
    m_lines->horizontalHeaderItem(Receive)->setToolTip(tr("Playback and data: executors, faders, keys, fixtures, groups, masters…"));
    m_lines->horizontalHeaderItem(Commands)->setToolTip(tr("Whole command lines (/cmd): anything you could type"));
    m_lines->horizontalHeaderItem(Send)->setToolTip(tr("Feedback: fader levels, cues, button lights, encoders, the command line"));
    m_lines->horizontalHeaderItem(Prefix)->setToolTip(tr("Optional, without slashes: messages must start /prefix/, and those sent do"));
    m_lines->setMinimumHeight(150);

    auto *split = new QSplitter(Qt::Vertical, this);
    split->addWidget(m_lines);
    auto *bottom = new QWidget(split);
    auto *bh = new QHBoxLayout(bottom);
    bh->setContentsMargins(0, 0, 0, 0);
    auto *left = new QVBoxLayout;
    left->addWidget(title(tr("Listening"), bottom));
    m_status = new QListWidget(bottom);
    m_status->setSelectionMode(QAbstractItemView::NoSelection);
    m_status->setMaximumHeight(110);
    left->addWidget(m_status);
    auto *monHead = new QHBoxLayout;
    monHead->addWidget(title(tr("Monitor"), bottom));
    monHead->addStretch(1);
    m_showSent = new QCheckBox(tr("Show what's sent"), bottom);
    monHead->addWidget(m_showSent);
    left->addLayout(monHead);
    m_monitor = new QListWidget(bottom);
    m_monitor->setSelectionMode(QAbstractItemView::NoSelection);
    m_monitor->setFont(theme::uiFont(9));
    left->addWidget(m_monitor, 1);
    bh->addLayout(left, 1);
    auto *ref = new QTextBrowser(bottom);
    ref->setHtml(QString::fromUtf8(kReference));
    ref->setOpenLinks(false);
    bh->addWidget(ref, 1);
    split->addWidget(bottom);
    split->setSizes({180, 400});
    root->addWidget(split, 1);

    OscManager *osc = ctx->osc;
    connect(m_input, &QCheckBox::toggled, this, [this, osc](bool on) {
        if (!m_updating)
            osc->setInputEnabled(on);
    });
    connect(m_output, &QCheckBox::toggled, this, [this, osc](bool on) {
        if (!m_updating)
            osc->setOutputEnabled(on);
    });
    connect(add, &QPushButton::clicked, this, [this, osc]() {
        QVector<OscLine> lines = osc->lines();
        OscLine l;
        l.name = tr("Line %1").arg(lines.size() + 1);
        l.inPort = 8000 + int(lines.size());
        l.outPort = 9000 + int(lines.size());
        l.destination = QStringLiteral("127.0.0.1");
        lines << l;
        osc->setLines(lines);
    });
    connect(remove, &QPushButton::clicked, this, [this, osc]() {
        const int row = m_lines->currentRow();
        QVector<OscLine> lines = osc->lines();
        if (row < 0 || row >= lines.size())
            return;
        lines.removeAt(row);
        osc->setLines(lines);
    });
    connect(osc, &OscManager::linesChanged, this, &OscView::refreshLines);
    connect(osc, &OscManager::statusChanged, this, &OscView::refreshStatus);
    connect(osc, &OscManager::messageSeen, this, [this](const QString &text, bool incoming, int line) {
        if (!incoming && !m_showSent->isChecked())
            return;
        const QString name = m_ctx->osc->lines().value(line).name;
        m_monitor->addItem(QStringLiteral("%1  %2 %3  %4").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")),
                                                             incoming ? QStringLiteral("←") : QStringLiteral("→"), name, text));
        while (m_monitor->count() > 300)
            delete m_monitor->takeItem(0);
        m_monitor->scrollToBottom();
    });
    refreshLines();
    refreshStatus();
}

void OscView::refreshLines()
{
    m_updating = true;
    const QVector<OscLine> &lines = m_ctx->osc->lines();
    m_lines->setRowCount(int(lines.size()));
    for (int r = 0; r < lines.size(); ++r) {
        const OscLine &l = lines[r];
        auto *name = new QLineEdit(l.name, m_lines);
        auto *proto = new QComboBox(m_lines);
        proto->addItems({QStringLiteral("UDP"), QStringLiteral("TCP")});
        proto->setCurrentIndex(l.tcp ? 1 : 0);
        auto *dest = new QLineEdit(l.destination, m_lines);
        dest->setPlaceholderText(tr("e.g. 192.168.1.20"));
        auto port = [this](int v) {
            auto *b = new QSpinBox(m_lines);
            b->setRange(0, 65535);
            b->setValue(v);
            return b;
        };
        auto *in = port(l.inPort);
        auto *out = port(l.outPort);
        auto *prefix = new QLineEdit(l.prefix, m_lines);
        prefix->setPlaceholderText(tr("none"));
        auto box = [this](bool on) {
            auto *b = new QCheckBox(m_lines);
            b->setChecked(on);
            return b;
        };
        auto *rec = box(l.receive);
        auto *cmd = box(l.receiveCommands);
        auto *snd = box(l.send);
        m_lines->setCellWidget(r, Name, name);
        m_lines->setCellWidget(r, Protocol, proto);
        m_lines->setCellWidget(r, Destination, dest);
        m_lines->setCellWidget(r, InPort, in);
        m_lines->setCellWidget(r, OutPort, out);
        m_lines->setCellWidget(r, Prefix, prefix);
        m_lines->setCellWidget(r, Receive, centered(rec, m_lines));
        m_lines->setCellWidget(r, Commands, centered(cmd, m_lines));
        m_lines->setCellWidget(r, Send, centered(snd, m_lines));
        for (QLineEdit *e : {name, dest, prefix})
            connect(e, &QLineEdit::editingFinished, this, [this, r]() { commitRow(r); });
        connect(proto, &QComboBox::currentIndexChanged, this, [this, r]() { commitRow(r); });
        for (QSpinBox *b : {in, out})
            connect(b, &QSpinBox::editingFinished, this, [this, r]() { commitRow(r); });
        for (QCheckBox *b : {rec, cmd, snd})
            connect(b, &QCheckBox::toggled, this, [this, r]() { commitRow(r); });
    }
    m_input->setChecked(m_ctx->osc->inputEnabled());
    m_output->setChecked(m_ctx->osc->outputEnabled());
    m_updating = false;
}

void OscView::commitRow(int row)
{
    if (m_updating)
        return;
    QVector<OscLine> lines = m_ctx->osc->lines();
    if (row < 0 || row >= lines.size())
        return;
    OscLine &l = lines[row];
    auto cell = [&](int c) { return m_lines->cellWidget(row, c); };
    auto check = [&](int c) { return cell(c)->findChild<QCheckBox *>()->isChecked(); };
    l.name = static_cast<QLineEdit *>(cell(Name))->text().trimmed();
    l.tcp = static_cast<QComboBox *>(cell(Protocol))->currentIndex() == 1;
    l.destination = static_cast<QLineEdit *>(cell(Destination))->text().trimmed();
    l.inPort = static_cast<QSpinBox *>(cell(InPort))->value();
    l.outPort = static_cast<QSpinBox *>(cell(OutPort))->value();
    l.prefix = static_cast<QLineEdit *>(cell(Prefix))->text().trimmed().remove('/');
    l.receive = check(Receive);
    l.receiveCommands = check(Commands);
    l.send = check(Send);
    if (lines == m_ctx->osc->lines())
        return;
    // Rebuilding the table would drop the field being typed in: keep the widgets, change the lines.
    QSignalBlocker block(m_ctx->osc);
    m_ctx->osc->setLines(lines);
    block.unblock();
    emit m_ctx->osc->statusChanged();
}

void OscView::refreshStatus()
{
    m_status->clear();
    const QStringList st = m_ctx->osc->status();
    if (st.isEmpty())
        m_status->addItem(m_ctx->osc->lines().isEmpty() ? tr("No lines: Add line to talk OSC with an app or another device")
                                                        : tr("Not listening (Receive is off, or no line receives)"));
    for (const QString &s : st)
        m_status->addItem(s);
    if (m_input->isChecked() != m_ctx->osc->inputEnabled() || m_output->isChecked() != m_ctx->osc->outputEnabled()) {
        m_updating = true;
        m_input->setChecked(m_ctx->osc->inputEnabled());
        m_output->setChecked(m_ctx->osc->outputEnabled());
        m_updating = false;
    }
}

ControlView::ControlView(AppContext *ctx, QWidget *parent) : QWidget(parent)
{
    setWindowTitle(tr("Control"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    // Developer: screenshots of the OSC tab, with sample lines that aren't saved to this computer.
    const int devTab = qEnvironmentVariableIsSet("LUMORA_CONTROL_TAB") ? qEnvironmentVariableIntValue("LUMORA_CONTROL_TAB") : -1;
    if (devTab >= 0) {
        if (ctx->osc->lines().isEmpty()) {
            OscLine a;
            a.name = tr("Tablet");
            a.destination = QStringLiteral("192.168.1.40");
            a.inPort = 8000;
            a.outPort = 9000;
            a.receiveCommands = true;
            a.send = true;
            OscLine b;
            b.name = tr("Show control");
            b.tcp = true;
            b.destination = QStringLiteral("192.168.1.12");
            b.inPort = 3032;
            b.outPort = 3032;
            b.prefix = QStringLiteral("lumora");
            b.send = false;
            const QSignalBlocker block(ctx->osc);
            ctx->osc->setLines({a, b});
        }
    }
    auto *tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    tabs->addTab(new MidiView(ctx, tabs), tr("MIDI"));
    tabs->addTab(new OscView(ctx, tabs), tr("OSC"));
    v->addWidget(tabs);
    if (devTab >= 0)
        tabs->setCurrentIndex(devTab);
}

} // namespace lumora
