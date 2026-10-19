// SPDX-License-Identifier: GPL-3.0-or-later
#include "parkview.h"

#include "appcontext.h"
#include "theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace lumora {

ParkView::ParkView(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    setWindowTitle(tr("Park"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);
    auto *title = new QLabel(tr("Park"), this);
    title->setProperty("role", "title");
    v->addWidget(title);

    m_list = new QTreeWidget(this);
    m_list->setColumnCount(3);
    m_list->setHeaderLabels({tr("Fixture"), tr("Attribute"), tr("Held at")});
    m_list->setRootIsDecorated(true);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_list->header()->resizeSection(1, 160);
    m_list->header()->setStretchLastSection(true);
    m_list->setToolTip(tr("Parked attributes hold their output whatever plays or is programmed. "
                          "Park with Fixture 101 Park or Park Fixture 101 Attribute Pan."));
    v->addWidget(m_list, 1);
    m_empty = new QLabel(tr("Nothing is parked. Select fixtures and type Park, or Fixture 101 Intensity Park."), this);
    m_empty->setProperty("role", "muted");
    m_empty->setWordWrap(true);
    v->addWidget(m_empty);

    auto *h = new QHBoxLayout;
    auto *unpark = new QToolButton(this);
    unpark->setText(tr("Unpark"));
    unpark->setToolTip(tr("Unpark the selected fixtures or attributes"));
    auto *all = new QToolButton(this);
    all->setText(tr("Unpark all"));
    h->addWidget(unpark);
    h->addWidget(all);
    h->addStretch(1);
    v->addLayout(h);

    connect(unpark, &QToolButton::clicked, this, &ParkView::unparkSelected);
    connect(all, &QToolButton::clicked, this, [this]() { m_ctx->run(QStringLiteral("Unpark All")); });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this]() { unparkSelected(); });
    connect(ctx->show, &Show::changed, this, [this](Show::Parts p) {
        if (p & (Show::ParkPart | Show::PatchPart))
            reload();
    });
    reload();
}

void ParkView::reload()
{
    // Keep which fixtures were open.
    QSet<int> open;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (m_list->topLevelItem(i)->isExpanded())
            open.insert(m_list->topLevelItem(i)->data(0, Qt::UserRole).toInt());
    m_list->clear();
    Show *s = m_ctx->show;
    const RawTable &parked = s->parked();
    QList<int> fids = parked.keys();
    std::sort(fids.begin(), fids.end(), [s](int a, int b) { return fixtureSortKey(a) < fixtureSortKey(b); });
    for (int fid : fids) {
        const RawAttr &attrs = parked.value(fid);
        const Fixture *f = s->fixture(fid);
        auto *top = new QTreeWidgetItem(m_list);
        top->setText(0, s->idText(fid) + (f && !f->name.isEmpty() ? QStringLiteral("  ") + f->name : QString()));
        top->setText(1, countOf(int(attrs.size()), tr("attribute"), tr("attributes")));
        top->setData(0, Qt::UserRole, fid);
        top->setForeground(0, theme::live);
        QStringList names = attrs.keys();
        std::sort(names.begin(), names.end());
        for (const QString &a : names) {
            auto *it = new QTreeWidgetItem(top);
            it->setText(1, attributeLabel(a));
            const ChannelDef *c = s->channelOf(fid, a);
            it->setText(2, c ? c->valueText(attrs.value(a)) : QString::number(attrs.value(a)));
            it->setData(0, Qt::UserRole, fid);
            it->setData(1, Qt::UserRole, a);
            it->setForeground(1, theme::featureColor(featureOf(a)));
        }
        top->setExpanded(open.contains(fid));
    }
    m_empty->setVisible(fids.isEmpty());
}

void ParkView::unparkSelected()
{
    int n = 0;
    for (QTreeWidgetItem *it : m_list->selectedItems()) {
        const int fid = it->data(0, Qt::UserRole).toInt();
        const QString a = it->data(1, Qt::UserRole).toString();
        n += m_ctx->show->unpark({fid}, a);   // a fixture row: all of it
    }
    if (n)
        emit m_ctx->status(tr("Unparked %1").arg(countOf(n, tr("attribute"), tr("attributes"))));
}

} // namespace lumora
