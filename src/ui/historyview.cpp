// SPDX-License-Identifier: GPL-3.0-or-later
#include "historyview.h"

#include "appcontext.h"
#include "quickedit.h"
#include "theme.h"

#include <QScrollBar>
#include <QClipboard>
#include <QApplication>
#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace lumora {

HistoryView::HistoryView(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);
    auto *title = new QLabel(tr("Command history"), this);
    title->setProperty("role", "title");
    v->addWidget(title);

    m_list = new QTreeWidget(this);
    m_list->setColumnCount(3);
    m_list->setHeaderLabels({tr("Time"), tr("Command or edit"), tr("Result")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setAlternatingRowColors(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_list->header()->resizeSection(1, 320);
    m_list->header()->setStretchLastSection(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    v->addWidget(m_list, 1);

    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
        if (it->data(0, Qt::UserRole).toBool())
            m_ctx->setCommandLine(it->text(1));
    });
    connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *it = m_list->itemAt(pos);
        auto *ed = new QuickEditor(tr("History"), this);
        if (it && it->data(0, Qt::UserRole).toBool()) {
            const QString line = it->text(1);
            ed->addAction(tr("Run again"), [this, line]() { m_ctx->run(line); });
            ed->addAction(tr("Put on the command line"), [this, line]() { m_ctx->setCommandLine(line); });
        }
        if (!m_list->selectedItems().isEmpty())
            ed->addAction(tr("Copy"), [this]() {
                QStringList lines;
                for (QTreeWidgetItem *s : m_list->selectedItems())
                    lines << QStringLiteral("%1  %2%3").arg(s->text(0), s->text(1),
                                                            s->text(2).isEmpty() ? QString() : QStringLiteral("  → ") + s->text(2));
                QApplication::clipboard()->setText(lines.join('\n'));
            });
        ed->addAction(tr("Clear history"), [this]() { m_ctx->clearHistory(); }, QIcon(), true);
        ed->popup(m_list->viewport()->mapToGlobal(pos));
    });
    connect(ctx, &AppContext::historyAdded, this, &HistoryView::append);
    connect(ctx, &AppContext::historyCleared, this, &HistoryView::reload);
    reload();
}

bool HistoryView::shows(const HistoryEntry &e) const
{
    return m_filter == 0 || (m_filter == 1 && e.command) || (m_filter == 2 && !e.command);
}

void HistoryView::append(const HistoryEntry &e)
{
    if (!shows(e))
        return;
    const bool atBottom = !m_list->verticalScrollBar()
                          || m_list->verticalScrollBar()->value() >= m_list->verticalScrollBar()->maximum() - 2;
    auto *it = new QTreeWidgetItem({e.time.toString(QStringLiteral("HH:mm:ss")), e.text,
                                    e.source.isEmpty() ? e.result : QStringLiteral("%1  ·  %2").arg(e.source, e.result)});
    if (!e.source.isEmpty())
        it->setToolTip(2, tr("Run by %1").arg(e.source));
    it->setData(0, Qt::UserRole, e.command);
    it->setForeground(0, theme::faint);
    if (e.command) {
        QFont f = it->font(1);
        f.setFamily(QStringLiteral("monospace"));
        it->setFont(1, f);
        it->setForeground(1, theme::text);
        it->setForeground(2, e.ok ? theme::muted : theme::programmer);
        it->setToolTip(1, tr("Double-click to put it back on the command line"));
    } else {
        it->setText(1, tr("edit  ·  %1").arg(e.text));
        it->setForeground(1, theme::muted);
    }
    m_list->addTopLevelItem(it);
    if (atBottom)
        m_list->scrollToBottom();
}

void HistoryView::reload()
{
    setWindowTitle(m_filter == 1 ? tr("Command history · commands") : m_filter == 2 ? tr("Command history · edits") : tr("Command history"));
    m_list->clear();
    for (const HistoryEntry &e : m_ctx->history())
        append(e);
    m_list->scrollToBottom();
}

QJsonObject HistoryView::saveOptions() const
{
    return m_filter ? QJsonObject{{"show", m_filter}} : QJsonObject();
}

void HistoryView::loadOptions(const QJsonObject &o)
{
    m_filter = std::clamp(o["show"].toInt(0), 0, 2);
    reload();
}

void HistoryView::buildOptions(QuickEditor *ed)
{
    ed->addSegmented(tr("Show"), {tr("Everything"), tr("Commands"), tr("Edits")}, m_filter, [this](int i) {
        m_filter = i;
        reload();
    });
}

} // namespace lumora
