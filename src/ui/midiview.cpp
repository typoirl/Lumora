// SPDX-License-Identifier: GPL-3.0-or-later
#include "midiview.h"

#include "appcontext.h"
#include "theme.h"

#include <QHeaderView>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTime>
#include <QVBoxLayout>

namespace lumora {

namespace {
QLabel *title(const QString &t, QWidget *parent)
{
    auto *l = new QLabel(t, parent);
    l->setProperty("role", "title");
    return l;
}
QLabel *hint(const QString &t, QWidget *parent)
{
    auto *l = new QLabel(t, parent);
    l->setProperty("role", "muted");
    l->setWordWrap(true);
    return l;
}
} // namespace

MidiView::MidiView(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 6);
    auto *split = new QSplitter(Qt::Horizontal, this);
    split->setHandleWidth(16);
    root->addWidget(split);

    // ---- Devices
    auto *devices = new QWidget(split);
    auto *dv = new QVBoxLayout(devices);
    dv->setContentsMargins(0, 0, 0, 0);
    dv->setSpacing(8);
    dv->addWidget(title(tr("MIDI devices"), devices));
    m_autoOpen = new QCheckBox(tr("Listen to every input"), devices);
    dv->addWidget(m_autoOpen);
    m_inputs = new QListWidget(devices);
    m_inputs->setWordWrap(true);
    dv->addWidget(m_inputs, 1);
    dv->addWidget(hint(tr("Feedback output: sends fader positions and button lights back to the controller. "
                          "Pick your controller if it has motor faders or LEDs."),
                       devices));
    m_feedback = new QComboBox(devices);
    dv->addWidget(m_feedback);
    auto *rescan = new QPushButton(tr("Rescan devices"), devices);
    dv->addWidget(rescan);
    m_backend = hint(QString(), devices);
    m_backend->setStyleSheet(QStringLiteral("color: %1;").arg(theme::programmer.name()));
    dv->addWidget(m_backend);
    split->addWidget(devices);

    // ---- Mappings
    auto *maps = new QWidget(split);
    auto *mv = new QVBoxLayout(maps);
    mv->setContentsMargins(0, 0, 0, 0);
    mv->setSpacing(8);
    mv->addWidget(title(tr("Mappings"), maps));
    mv->addWidget(hint(tr("Fastest way: turn on Learn MIDI in the top bar, click any executor fader or button, then move "
                          "the control on your device. Or pick an action below and press Learn."),
                       maps));
    auto *learnRow = new QHBoxLayout;
    m_target = new QComboBox(maps);
    auto addTarget = [this](const MidiTarget &t) { m_target->addItem(t.describe(), QVariant::fromValue(int(m_target->count()))); };
    QList<MidiTarget> targets;
    for (MidiTarget::Kind k : {MidiTarget::GrandMaster, MidiTarget::Blackout, MidiTarget::PageNext, MidiTarget::PagePrev,
                               MidiTarget::ClearProgrammer}) {
        MidiTarget t;
        t.kind = k;
        targets << t;
    }
    for (int n = 201; n <= 210; ++n) {
        MidiTarget f;
        f.kind = MidiTarget::ExecutorFader;
        f.executor = n;
        targets << f;
        for (int b = 0; b < 3; ++b) {
            MidiTarget bt;
            bt.kind = MidiTarget::ExecutorButton;
            bt.executor = n;
            bt.button = b;
            targets << bt;
        }
    }
    // The encoder bar's encoders, by position (their push maps from the encoder's right-click).
    for (int n = 1; n <= 10; ++n) {
        MidiTarget e;
        e.kind = MidiTarget::Encoder;
        e.encoder = n;
        targets << e;
    }
    for (const MidiTarget &t : targets)
        addTarget(t);
    m_learn = new QPushButton(tr("Learn"), maps);
    learnRow->addWidget(m_target, 1);
    learnRow->addWidget(m_learn);
    mv->addLayout(learnRow);

    m_bindings = new QTableWidget(maps);
    m_bindings->setColumnCount(5);
    m_bindings->setHorizontalHeaderLabels({tr("Control"), tr("Action"), tr("Pickup"), tr("Invert"), tr("Endless")});
    m_bindings->verticalHeader()->hide();
    m_bindings->verticalHeader()->setDefaultSectionSize(28);
    m_bindings->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_bindings->horizontalHeader()->setStretchLastSection(true);
    m_bindings->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_bindings->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_bindings->setShowGrid(false);
    m_bindings->setAlternatingRowColors(true);
    m_bindings->setColumnWidth(0, 160);
    m_bindings->setColumnWidth(1, 170);
    m_bindings->setColumnWidth(2, 70);
    m_bindings->setColumnWidth(3, 70);
    m_bindings->horizontalHeaderItem(4)->setToolTip(
        tr("Endless: the encoder sends steps (65 / 63, or 1 / 127) rather than where a knob is. Guessed when it's learned."));
    m_bindings->horizontalHeaderItem(2)->setToolTip(
        tr("Pickup: the fader only takes over once the physical fader passes the current level, so nothing jumps"));
    mv->addWidget(m_bindings, 1);
    auto *remove = new QPushButton(tr("Remove mapping"), maps);
    auto *rmRow = new QHBoxLayout;
    rmRow->addStretch(1);
    rmRow->addWidget(remove);
    mv->addLayout(rmRow);
    split->addWidget(maps);

    // ---- Monitor
    auto *mon = new QWidget(split);
    auto *mo = new QVBoxLayout(mon);
    mo->setContentsMargins(0, 0, 0, 0);
    mo->setSpacing(8);
    mo->addWidget(title(tr("Incoming"), mon));
    m_monitor = new QListWidget(mon);
    m_monitor->setSelectionMode(QAbstractItemView::NoSelection);
    mo->addWidget(m_monitor, 1);
    split->addWidget(mon);
    split->setSizes({260, 560, 260});

    MidiManager *midi = ctx->midi;
    connect(m_learn, &QPushButton::clicked, this, [this, midi, targets]() {
        if (midi->isArmed()) {
            midi->disarm();
            return;
        }
        const MidiTarget t = targets.value(m_target->currentIndex());
        midi->arm(t);
        emit m_ctx->status(tr("Move a control on your MIDI device to map %1").arg(t.describe()));
    });
    connect(remove, &QPushButton::clicked, this, [this, midi]() {
        QList<int> rows;
        for (const QModelIndex &i : m_bindings->selectionModel()->selectedRows())
            rows << i.row();
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int r : rows)
            midi->removeBinding(r);
    });
    connect(m_bindings, &QTableWidget::itemChanged, this, [this, midi](QTableWidgetItem *it) {
        if (m_updating || it->column() < 2)
            return;
        MidiBinding b = midi->bindings().value(it->row());
        if (it->column() == 2)
            b.pickup = it->checkState() == Qt::Checked;
        else if (it->column() == 3)
            b.invert = it->checkState() == Qt::Checked;
        else
            b.relative = it->checkState() == Qt::Checked;
        midi->updateBinding(it->row(), b);
    });
    connect(m_autoOpen, &QCheckBox::toggled, this, [this, midi](bool on) {
        if (!m_updating)
            midi->setAutoOpen(on);
    });
    connect(m_inputs, &QListWidget::itemChanged, this, [this, midi]() {
        if (m_updating)
            return;
        QStringList wanted;
        for (int i = 0; i < m_inputs->count(); ++i)
            if (m_inputs->item(i)->checkState() == Qt::Checked)
                wanted << m_inputs->item(i)->text();
        midi->setOpenInputs(wanted);
    });
    connect(m_feedback, &QComboBox::activated, this, [this, midi]() {
        midi->setFeedbackOutput(m_feedback->currentData().toString());
    });
    connect(rescan, &QPushButton::clicked, this, [midi]() { midi->setOpenInputs(midi->openInputs()); });

    connect(midi, &MidiManager::portsChanged, this, &MidiView::refreshPorts);
    connect(midi, &MidiManager::bindingsChanged, this, &MidiView::refreshBindings);
    connect(midi, &MidiManager::learnStateChanged, this, &MidiView::refreshLearnButton);
    connect(midi, &MidiManager::learned, this, [this](const MidiBinding &b) {
        emit m_ctx->status(tr("Mapped %1 to %2").arg(b.source.describe(), b.target.describe()));
    });
    connect(midi, &MidiManager::messageReceived, this, [this](const MidiMessage &m) {
        auto *item = new QListWidgetItem(QStringLiteral("%1   %2   %3").arg(QTime::currentTime().toString("hh:mm:ss"), m.describe(), m.port));
        item->setForeground(m_ctx->midi->isArmed() ? theme::learn : theme::text);
        m_monitor->insertItem(0, item);
        while (m_monitor->count() > 80)
            delete m_monitor->takeItem(m_monitor->count() - 1);
    });
    refreshPorts();
    refreshBindings();
    refreshLearnButton();
}

void MidiView::refreshPorts()
{
    m_updating = true;
    MidiManager *midi = m_ctx->midi;
    const QStringList available = midi->availableInputs();
    const QStringList open = midi->openInputs();
    m_autoOpen->setChecked(midi->autoOpen());
    m_inputs->clear();
    for (const QString &n : available) {
        auto *it = new QListWidgetItem(n, m_inputs);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        it->setCheckState(open.contains(n) ? Qt::Checked : Qt::Unchecked);
        if (midi->autoOpen())
            it->setFlags(Qt::ItemIsUserCheckable);   // greyed out: all inputs are open
    }
    if (available.isEmpty()) {
        auto *it = new QListWidgetItem(tr("No MIDI inputs found. Plug in a controller and rescan."), m_inputs);
        it->setFlags(Qt::NoItemFlags);
    }
    m_feedback->clear();
    m_feedback->addItem(tr("No feedback"), QString());
    for (const QString &n : midi->availableOutputs())
        m_feedback->addItem(n, n);
    m_feedback->setCurrentIndex(std::max(0, m_feedback->findData(midi->feedbackOutput())));
    m_backend->setText(midi->backendError().isEmpty() ? QString() : tr("MIDI system unavailable: %1").arg(midi->backendError()));
    m_backend->setVisible(!midi->backendError().isEmpty());
    m_updating = false;
}

void MidiView::refreshBindings()
{
    m_updating = true;
    const auto &bs = m_ctx->midi->bindings();
    m_bindings->setRowCount(bs.size());
    for (int r = 0; r < bs.size(); ++r) {
        const MidiBinding &b = bs[r];
        m_bindings->setItem(r, 0, new QTableWidgetItem(b.source.describe()));
        m_bindings->setItem(r, 1, new QTableWidgetItem(b.target.describe()));
        const bool continuous = b.target.kind == MidiTarget::ExecutorFader || b.target.kind == MidiTarget::GrandMaster;
        const bool encoder = b.target.kind == MidiTarget::Encoder;
        for (int c : {2, 3, 4}) {
            auto *it = new QTableWidgetItem;
            const bool has = c == 2 ? continuous : c == 3 ? continuous || encoder : encoder;
            if (has) {
                it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
                it->setCheckState((c == 2 ? b.pickup : c == 3 ? b.invert : b.relative) ? Qt::Checked : Qt::Unchecked);
            } else {
                it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            }
            m_bindings->setItem(r, c, it);
        }
    }
    m_updating = false;
}

void MidiView::refreshLearnButton()
{
    const bool armed = m_ctx->midi->isArmed();
    m_learn->setText(armed ? tr("Listening… (click to cancel)") : tr("Learn"));
    m_learn->setStyleSheet(armed ? QStringLiteral("QPushButton { border-color: %1; color: %1; }").arg(theme::learn.name())
                                 : QString());
}

} // namespace lumora
