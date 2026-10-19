// SPDX-License-Identifier: GPL-3.0-or-later
#include "playbackmonitor.h"

#include "appcontext.h"
#include "quickedit.h"
#include "theme.h"
#include "uiutil.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace lumora {

namespace {
const QColor kLiveRow(0x3a, 0x32, 0x20);
const QColor kNextRow(0x24, 0x2a, 0x33);
} // namespace

QStringList PlaybackMonitor::allColumns()
{
    return {"cue", "label", "fade", "delay", "notes", "contents"};
}

QString PlaybackMonitor::columnTitle(const QString &id)
{
    if (id == "cue") return tr("Cue");
    if (id == "label") return tr("Label");
    if (id == "fade") return tr("Fade");
    if (id == "delay") return tr("Delay");
    if (id == "notes") return tr("Notes");
    if (id == "contents") return tr("Contents");
    return id;
}

PlaybackMonitor::PlaybackMonitor(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    m_columns = {"cue", "label", "fade", "delay", "notes"};
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);

    // Banner: what's live, what's next, and a progress bar while a cue fades in.
    m_banner = new QWidget(this);
    auto *bh = new QHBoxLayout(m_banner);
    bh->setContentsMargins(4, 2, 4, 2);
    bh->setSpacing(14);
    auto *texts = new QVBoxLayout;
    texts->setSpacing(0);
    m_title = new QLabel(m_banner);
    m_title->setProperty("role", "muted");
    m_now = new QLabel(m_banner);
    m_now->setFont(theme::uiFont(14, QFont::DemiBold));
    m_next = new QLabel(m_banner);
    m_next->setProperty("role", "muted");
    texts->addWidget(m_title);
    texts->addWidget(m_now);
    texts->addWidget(m_next);
    bh->addLayout(texts, 1);
    m_progress = new QProgressBar(m_banner);
    m_progress->setRange(0, 1000);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    m_progress->setStyleSheet(QStringLiteral("QProgressBar { background: %1; border: none; border-radius: 3px; }"
                                             "QProgressBar::chunk { background: %2; border-radius: 3px; }")
                                  .arg(theme::raised.name(), theme::live.name()));
    m_progress->setFixedWidth(140);
    bh->addWidget(m_progress, 0, Qt::AlignVCenter);
    v->addWidget(m_banner);

    m_table = new QTableWidget(this);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(28);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionsMovable(true);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setFocusPolicy(Qt::NoFocus);
    v->addWidget(m_table, 1);

    // Dragging a column header reorders the columns (and is remembered with the window).
    connect(m_table->horizontalHeader(), &QHeaderView::sectionMoved, this, [this]() {
        QHeaderView *h = m_table->horizontalHeader();
        QStringList order;
        QList<int> widths;   // each column keeps its width as it moves
        for (int visual = 0; visual < h->count(); ++visual) {
            order << m_columns.value(h->logicalIndex(visual));
            widths << h->sectionSize(h->logicalIndex(visual));
        }
        m_columns = order;
        QTimer::singleShot(0, this, [this, widths]() {
            m_signature.clear();
            rebuild();
            for (int i = 0; i < widths.size() && i + 1 < m_table->columnCount(); ++i)
                m_table->setColumnWidth(i, widths[i]);
            panelStateChanged(this);
        });
    });
    // Double-click or right-click a cell to edit its label, notes, fade or delay.
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &PlaybackMonitor::editCell);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QModelIndex i = m_table->indexAt(pos);
        if (i.isValid())
            editCell(i.row(), i.column());
    });

    connect(ctx->show, &Show::changed, this, [this](Show::Parts p) {
        if (p & (Show::SequencesPart | Show::ExecutorsPart | Show::SettingsPart))
            rebuild();
    });
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &PlaybackMonitor::tick);
    timer->start(50);
    rebuild();
}

int PlaybackMonitor::sequenceId() const
{
    // Unlocked: the sequence the last Go went to (before any, the selected one).
    if (m_locked > 0)
        return m_locked;
    const int last = m_ctx->show->lastGoSequence();
    return last > 0 && m_ctx->show->sequence(last) ? last : m_ctx->show->selectedSequence();
}

const Executor *PlaybackMonitor::executor() const
{
    return m_ctx->show->executorForSequence(sequenceId());
}

void PlaybackMonitor::rebuild()
{
    Show *show = m_ctx->show;
    const Sequence *s = show->sequence(sequenceId());
    const Executor *e = executor();
    // Rebuild only when the cue list itself changed; the live cue is repainted by tick().
    QString sig = m_columns.join(',') + '|' + QString::number(sequenceId());
    if (s) {
        sig += '|' + s->name + (e ? QStringLiteral("@%1.%2").arg(e->page).arg(e->number) : QString());
        for (const Cue &c : s->cues)
            sig += QStringLiteral("|%1/%2/%3/%4/%5/%6/%7").arg(c.number).arg(c.name).arg(c.fade).arg(c.delay).arg(c.notes)
                       .arg(c.blueprints.size() + c.values.size() * 1000).arg(c.effects.size());
    }
    if (sig == m_signature) {
        tick();
        return;
    }
    m_signature = sig;
    m_liveRow = -2;

    m_banner->setVisible(m_showBanner);
    setWindowTitle(m_locked && s ? tr("Playback monitor · %1").arg(s->name) : tr("Playback monitor"));
    m_title->setText(s ? QStringLiteral("%1  %2%3%4")
                             .arg(s->id)
                             .arg(s->name)
                             .arg(e ? tr("  ·  Executor %1.%2").arg(e->page).arg(e->number) : tr("  ·  not on an executor"))
                             .arg(m_locked ? tr("  ·  locked") : QString())
                       : tr("Sequence %1 doesn't exist").arg(sequenceId()));

    m_table->clear();
    m_table->setColumnCount(m_columns.size());
    QStringList headers;
    for (const QString &c : m_columns)
        headers << columnTitle(c);
    m_table->setHorizontalHeaderLabels(headers);
    {
        // Reset any header drags: the order lives in m_columns now.
        QHeaderView *h = m_table->horizontalHeader();
        QSignalBlocker b(h);
        for (int i = 0; i < h->count(); ++i)
            h->moveSection(h->visualIndex(i), i);
    }
    m_table->setRowCount(s ? s->cues.size() : 0);
    if (s)
        for (int r = 0; r < s->cues.size(); ++r) {
            const Cue &c = s->cues[r];
            for (int col = 0; col < m_columns.size(); ++col) {
                const QString &id = m_columns[col];
                QString text;
                if (id == "cue")
                    text = c.displayNumber();
                else if (id == "label")
                    text = c.name;
                else if (id == "fade")
                    text = formatSeconds(c.fade);
                else if (id == "delay")
                    text = c.delay > 0 ? formatSeconds(c.delay) : QString();
                else if (id == "notes")
                    text = c.notes;
                else if (id == "contents") {
                    QStringList parts;
                    if (!c.blueprints.isEmpty())
                        parts << countOf(int(c.blueprints.size()), tr("blueprint line"), tr("blueprint lines"));
                    if (!c.values.isEmpty())
                        parts << countOf(int(c.values.size()), tr("fixture"), tr("fixtures"));
                    if (!c.effects.isEmpty())
                        parts << countOf(int(c.effects.size()), tr("effect"), tr("effects"));
                    text = parts.join(", ");
                }
                auto *it = new QTableWidgetItem(text);
                if (id == "notes" || id == "contents")
                    it->setForeground(theme::muted);
                m_table->setItem(r, col, it);
            }
        }
    for (int col = 0; col < m_columns.size(); ++col) {
        const QString &id = m_columns[col];
        m_table->setColumnWidth(col, id == "cue" ? 64 : id == "label" ? 220 : id == "notes" ? 260
                                     : id == "contents" ? 180 : 70);
    }
    tick();
}

void PlaybackMonitor::tick()
{
    if (!isVisible())
        return;
    Show *show = m_ctx->show;
    const Sequence *s = show->sequence(sequenceId());
    const Executor *e = executor();
    const int live = e && e->pb().isOn() ? e->pb().cueIndex : -1;
    const int next = s && !s->cues.isEmpty() ? (live + 1 < s->cues.size() ? live + 1 : (s->wrap ? 0 : -1)) : -1;
    const bool fading = e && e->pb().isOn() && e->pb().fading;

    // Banner
    if (s) {
        const Cue *lc = live >= 0 && live < s->cues.size() ? &s->cues[live] : nullptr;
        const Cue *nc = next >= 0 && next < s->cues.size() ? &s->cues[next] : nullptr;
        m_now->setText(lc ? tr("Cue %1  %2").arg(lc->displayNumber(), lc->name) : tr("No cue live"));
        m_next->setText(nc ? tr("Next: Cue %1  %2").arg(nc->displayNumber(), nc->name) : QString());
    } else {
        m_now->clear();
        m_next->clear();
    }
    m_progress->setVisible(fading);
    if (fading)
        m_progress->setValue(int(e->pb().progress() * 1000));

    // Rows: live cue lit, next cue marked. Only rows whose state changed are restyled.
    auto styleRow = [&](int r) {
        if (r < 0 || r >= m_table->rowCount())
            return;
        const bool isLive = r == live, isNext = r == next;
        for (int col = 0; col < m_table->columnCount(); ++col) {
            QTableWidgetItem *it = m_table->item(r, col);
            if (!it)
                continue;
            it->setBackground(isLive ? kLiveRow : isNext ? kNextRow : QColor(Qt::transparent));
            if (m_columns[col] == "cue" || m_columns[col] == "label") {
                it->setForeground(isLive ? theme::live : theme::text);
                QFont f = it->font();
                f.setBold(isLive);
                it->setFont(f);
            }
        }
    };
    if (live != m_liveRow || next != m_nextRow) {
        const bool all = m_liveRow == -2;   // just rebuilt
        for (int r = 0; r < m_table->rowCount(); ++r)
            if (all || r == live || r == next || r == m_liveRow || r == m_nextRow)
                styleRow(r);
        m_now->setStyleSheet(live >= 0 ? QStringLiteral("color: %1;").arg(theme::live.name()) : QString());
    }
    if (live != m_liveRow && live >= 0 && live < m_table->rowCount())
        m_table->scrollToItem(m_table->item(live, 0), QAbstractItemView::PositionAtCenter);
    m_liveRow = live;
    m_nextRow = next;
}

void PlaybackMonitor::editCell(int row, int column)
{
    Show *show = m_ctx->show;
    const Sequence *s = show->sequence(sequenceId());
    if (!s || row < 0 || row >= s->cues.size() || column < 0 || column >= m_columns.size())
        return;
    const QString id = m_columns[column];
    const Cue &c = s->cues[row];
    const int seqId = s->id;
    const double number = c.number;
    auto apply = [show, seqId, number](const std::function<void(Cue &)> &fn) {
        Sequence copy = *show->sequence(seqId);
        const int i = copy.indexOf(number);
        if (i < 0)
            return;
        fn(copy.cues[i]);
        show->setSequence(copy);
    };
    auto *ed = new QuickEditor(tr("Cue %1").arg(c.displayNumber()), this);
    if (id == "notes")
        ed->addNameField(tr("Notes"), c.notes, [apply](const QString &t) { apply([t](Cue &q) { q.notes = t; }); }, true);
    else if (id == "fade" || id == "delay") {
        const bool fade = id != "delay";
        ed->addNameField(fade ? tr("Fade (seconds)") : tr("Delay (seconds)"), formatSeconds(fade ? c.fade : c.delay),
                         [apply, fade](const QString &t) {
                             bool ok = false;
                             const double v = t.toDouble(&ok);
                             if (ok)
                                 apply([fade, v](Cue &q) { (fade ? q.fade : q.delay) = std::max(0.0, v); });
                         });
    } else {
        ed->addNameField(tr("Label"), c.name, [apply](const QString &t) { apply([t](Cue &q) { q.name = t; }); }, true);
    }
    const Executor *e = executor();
    if (e) {
        ed->addSection(QString());
        const int page = e->page, num = e->number;
        ed->addAction(tr("Go to this cue"), [this, page, num, row]() {
            if (Executor *x = m_ctx->show->executor(page, num))
                m_ctx->show->gotoCue(*x, row, true);
        });
    }
    ed->popup(m_table->viewport()->mapToGlobal(m_table->visualRect(m_table->model()->index(row, column)).bottomLeft()));
}

QJsonObject PlaybackMonitor::saveOptions() const
{
    QJsonObject o;
    if (m_locked)
        o["sequence"] = m_locked;
    o["columns"] = QJsonArray::fromStringList(m_columns);
    if (!m_showBanner)
        o["banner"] = false;
    return o;
}

void PlaybackMonitor::loadOptions(const QJsonObject &o)
{
    m_locked = std::max(0, o["sequence"].toInt(0));
    if (o.contains("columns")) {
        QStringList cols;
        for (const QJsonValue &v : o["columns"].toArray())
            if (allColumns().contains(v.toString()) && !cols.contains(v.toString()))
                cols << v.toString();
        if (!cols.isEmpty())
            m_columns = cols;
    }
    m_showBanner = o["banner"].toBool(true);
    m_signature.clear();
    rebuild();
}

void PlaybackMonitor::buildOptions(QuickEditor *ed)
{
    ed->addNumber(tr("Sequence (0 follows the last one started with Go)"), 0, 9999, m_locked, [this](int n) {
        m_locked = n;
        m_signature.clear();
        rebuild();
    });
    ed->addSegmented(tr("Banner"), {tr("Show"), tr("Hide")}, m_showBanner ? 0 : 1, [this](int i) {
        m_showBanner = i == 0;
        m_banner->setVisible(m_showBanner);
    });
    // Columns: tick to show, drag to reorder.
    ed->addSection(tr("Columns (tick to show, drag to reorder)"));
    auto *list = new QListWidget;
    list->setDragDropMode(QAbstractItemView::InternalMove);
    list->setDefaultDropAction(Qt::MoveAction);
    QStringList order = m_columns;
    for (const QString &c : allColumns())
        if (!order.contains(c))
            order << c;
    for (const QString &c : order) {
        auto *it = new QListWidgetItem(columnTitle(c), list);
        it->setData(Qt::UserRole, c);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled | Qt::ItemIsSelectable);
        it->setCheckState(m_columns.contains(c) ? Qt::Checked : Qt::Unchecked);
    }
    list->setFixedHeight(int(order.size()) * 26 + 6);
    auto readBack = [this, list]() {
        QStringList cols;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->checkState() == Qt::Checked)
                cols << list->item(i)->data(Qt::UserRole).toString();
        if (cols.isEmpty())
            return;
        m_columns = cols;
        m_signature.clear();
        rebuild();
    };
    connect(list, &QListWidget::itemChanged, this, readBack);
    connect(list->model(), &QAbstractItemModel::rowsMoved, this, readBack);
    ed->addWidget(list);
}

} // namespace lumora
