// SPDX-License-Identifier: GPL-3.0-or-later
#include "gotodialog.h"

#include "appcontext.h"
#include "theme.h"
#include "uiutil.h"

#include <QCursor>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QHeaderView>
#include <QTableWidget>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

namespace lumora {

namespace {
QPointer<GotoDialog> &openDialog()
{
    static QPointer<GotoDialog> d;
    return d;
}
} // namespace

void GotoDialog::open(AppContext *ctx, int page, int executor, QWidget *parent, bool load)
{
    // One at a time: a second Goto replaces the first.
    if (openDialog())
        openDialog()->close();
    auto *d = new GotoDialog(ctx, page, executor, parent, load);
    d->setAttribute(Qt::WA_DeleteOnClose);
    openDialog() = d;
    // In the middle of the screen the tap came from (or the one the window is on).
    const QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = parent ? parent->screen() : QGuiApplication::primaryScreen();
    d->adjustSize();
    if (screen) {
        const QRect avail = screen->availableGeometry();
        d->resize(std::min(d->width(), avail.width() - 40), std::min(d->height(), avail.height() - 60));
        d->move(avail.center() - QPoint(d->width() / 2, d->height() / 2));
    }
    d->show();
    d->raise();
    d->activateWindow();
}

GotoDialog::GotoDialog(AppContext *ctx, int page, int executor, QWidget *parent, bool load)
    : QDialog(parent, Qt::Dialog | Qt::WindowStaysOnTopHint), m_ctx(ctx), m_page(page), m_executor(executor), m_load(load)
{
    Show *show = ctx->show;
    const Executor *e = show->executor(page, executor);
    const Sequence *seq = e ? show->sequence(e->sequenceId) : nullptr;
    const QString seqName = seq ? (seq->name.isEmpty() ? tr("Sequence %1").arg(seq->id) : seq->name) : QString();
    const QString verb = load ? tr("Load") : tr("Goto");
    setWindowTitle(tr("%1 · Executor %2.%3").arg(verb).arg(page).arg(executor));
    auto *v = new QVBoxLayout(this);
    v->setSpacing(10);
    auto *title = new QLabel(tr("%1  ·  %2  ·  Executor %3.%4").arg(verb, seqName).arg(page).arg(executor), this);
    title->setFont(theme::uiFont(13, QFont::DemiBold));
    v->addWidget(title);

    auto *body = new QHBoxLayout;
    body->setSpacing(12);
    // The cues, one big row each: number, label, fade.
    m_list = new QTableWidget(this);
    m_list->setColumnCount(3);
    m_list->setHorizontalHeaderLabels({tr("Cue"), tr("Label"), tr("Fade")});
    m_list->verticalHeader()->hide();
    m_list->verticalHeader()->setDefaultSectionSize(56);
    m_list->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_list->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_list->setColumnWidth(0, 80);
    m_list->setColumnWidth(2, 90);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setShowGrid(false);
    m_list->setFont(theme::uiFont(14));
    m_list->setMinimumWidth(460);
    m_list->setMinimumHeight(380);
    const int live = e && e->pb().isOn() ? e->pb().cueIndex : -1;
    const int loaded = e ? e->pb().loadedIndex : -1;
    if (seq) {
        m_list->setRowCount(int(seq->cues.size()));
        for (int i = 0; i < seq->cues.size(); ++i) {
            const Cue &c = seq->cues[i];
            // The loaded cue (the next Go fires it) is marked in both windows.
            const QString label = i == loaded ? tr("%1  ·  loaded").arg(c.name) : c.name;
            const QString cells[3] = {c.displayNumber(), label, tr("%1 s").arg(formatSeconds(c.fade))};
            for (int col = 0; col < 3; ++col) {
                auto *it = new QTableWidgetItem(cells[col]);
                it->setData(Qt::UserRole, i);
                if (i == live) {
                    it->setForeground(theme::live);
                    QFont f = m_list->font();
                    f.setBold(true);
                    it->setFont(f);
                }
                if (col == 2)
                    it->setForeground(i == live ? theme::live : theme::muted);
                if (i == loaded && i != live && col < 2)
                    it->setForeground(theme::programmer);
                m_list->setItem(i, col, it);
            }
            if (i == live)
                m_list->selectRow(i);
        }
    }
    body->addWidget(m_list, 1);

    // Type a cue number: here or on the keys, then Enter.
    auto *side = new QVBoxLayout;
    side->setSpacing(6);
    m_number = new QLineEdit(this);
    m_number->setPlaceholderText(tr("Cue"));
    m_number->setFont(theme::uiFont(18, QFont::DemiBold));
    m_number->setAlignment(Qt::AlignRight);
    m_number->setMinimumHeight(52);
    side->addWidget(m_number);
    auto *keys = new QGridLayout;
    keys->setSpacing(6);
    const QStringList labels = {"7", "8", "9", "4", "5", "6", "1", "2", "3", ".", "0", "⌫"};
    for (int i = 0; i < labels.size(); ++i) {
        auto *b = new QPushButton(labels[i], this);
        b->setFont(theme::uiFont(16, QFont::DemiBold));
        b->setFixedSize(64, 56);
        b->setFocusPolicy(Qt::NoFocus);
        connect(b, &QPushButton::clicked, this, [this, l = labels[i]]() {
            if (l == QStringLiteral("⌫"))
                m_number->backspace();
            else
                m_number->insert(l);
        });
        keys->addWidget(b, i / 3, i % 3);
    }
    side->addLayout(keys);
    auto *go = new QPushButton(load ? tr("Load cue") : tr("Go to cue"), this);
    go->setProperty("role", "primary");
    go->setFont(theme::uiFont(14, QFont::DemiBold));
    go->setMinimumHeight(56);
    go->setFocusPolicy(Qt::NoFocus);
    side->addWidget(go);
    side->addStretch(1);
    auto *cancel = new QPushButton(tr("Cancel"), this);
    cancel->setMinimumHeight(48);
    cancel->setFocusPolicy(Qt::NoFocus);
    side->addWidget(cancel);
    body->addLayout(side);
    v->addLayout(body, 1);
    m_note = new QLabel(load ? tr("Tap a cue, or type its number and press Enter. The next Go fires it, in its own fade.")
                             : tr("Tap a cue, or type its number and press Enter. It goes in the cue's own fade."), this);
    m_note->setProperty("role", "muted");
    v->addWidget(m_note);

    connect(m_list, &QTableWidget::cellClicked, this, [this](int row, int) { goTo(row); });
    connect(m_number, &QLineEdit::returnPressed, this, &GotoDialog::goTyped);
    connect(go, &QPushButton::clicked, this, &GotoDialog::goTyped);
    connect(cancel, &QPushButton::clicked, this, &QWidget::close);
    m_number->setFocus();
}

void GotoDialog::keyPressEvent(QKeyEvent *ev)
{
    // Digits typed anywhere go into the number.
    if (!ev->text().isEmpty() && (ev->text().at(0).isDigit() || ev->text() == QLatin1String(".")) && !m_number->hasFocus()) {
        m_number->setFocus();
        m_number->insert(ev->text());
        return;
    }
    if (ev->key() == Qt::Key_Return || ev->key() == Qt::Key_Enter) {
        if (!m_number->text().isEmpty())
            goTyped();
        else if (m_list->currentRow() >= 0)
            goTo(m_list->currentRow());
        return;
    }
    QDialog::keyPressEvent(ev);
}

void GotoDialog::goTyped()
{
    bool ok = false;
    const double n = m_number->text().trimmed().toDouble(&ok);
    Show *show = m_ctx->show;
    const Executor *e = show->executor(m_page, m_executor);
    const Sequence *seq = e ? show->sequence(e->sequenceId) : nullptr;
    const int idx = ok && seq ? seq->indexOf(n) : -1;
    if (idx < 0) {
        m_note->setText(tr("No cue %1 in this sequence").arg(m_number->text().trimmed()));
        m_number->selectAll();
        return;
    }
    goTo(idx);
}

void GotoDialog::goTo(int cueIndex)
{
    Show *show = m_ctx->show;
    if (m_load) {
        if (Executor *e = show->executor(m_page, m_executor)) {
            show->loadCue(*e, cueIndex);
            if (const Sequence *seq = show->sequence(e->sequenceId); seq && cueIndex < seq->cues.size())
                emit m_ctx->status(tr("Executor %1.%2: cue %3 loaded, the next Go fires it").arg(m_page).arg(m_executor).arg(seq->cues[cueIndex].displayNumber()));
        }
        close();
        return;
    }
    if (Executor *e = show->executor(m_page, m_executor)) {
        show->gotoCue(*e, cueIndex, true);
        if (const Sequence *seq = show->sequence(e->sequenceId); seq && cueIndex < seq->cues.size())
            emit m_ctx->status(tr("Executor %1.%2: goto cue %3").arg(m_page).arg(m_executor).arg(seq->cues[cueIndex].displayNumber()));
    }
    close();
}

} // namespace lumora
