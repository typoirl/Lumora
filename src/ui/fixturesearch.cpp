// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixturesearch.h"

#include "appcontext.h"
#include "librarybrowser.h"
#include "theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QTabBar>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace lumora {

namespace {
constexpr int kLibrary = -1;   // ready to patch: in the show, or in the shipped library
constexpr int kBuiltIn = -3;   // the filter for the library Lumora ships with
constexpr int kOnlineLimit = 250;   // rows per search: refine the search for more
enum Role { TypeIdRole = Qt::UserRole, SourceRole, EntryRole };

bool matches(const QStringList &words, const QString &hay)
{
    for (const QString &w : words)
        if (!hay.contains(w, Qt::CaseInsensitive))
            return false;
    return true;
}
} // namespace

FixtureSearch::FixtureSearch(AppContext *ctx, QWidget *parent)
    : QWidget(parent), m_ctx(ctx), m_src(FixtureSources::instance(ctx)), m_debounce(new QTimer(this))
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(6);
    m_tabs = new QTabBar(this);
    m_tabs->setDocumentMode(true);
    m_tabs->setExpanding(false);
    m_tabs->addTab(tr("In Show"));
    m_tabs->addTab(tr("All fixtures"));
    v->addWidget(m_tabs);
    auto *row = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search every fixture: name or manufacturer"));
    m_search->setClearButtonEnabled(true);
    m_filter = new QComboBox(this);
    m_filter->addItem(tr("All sources"), -2);
    m_filter->addItem(tr("Built in"), kBuiltIn);
    for (int s = 0; s < FixtureSources::SourceCount; ++s)
        m_filter->addItem(FixtureSources::sourceName(s), s);
    row->addWidget(m_search, 1);
    row->addWidget(m_filter);
    v->addLayout(row);

    m_list = new QTreeWidget(this);
    m_list->setColumnCount(3);
    m_list->setHeaderLabels({tr("Fixture"), tr("Manufacturer"), tr("From")});
    m_list->setRootIsDecorated(false);
    m_list->setStyleSheet(QStringLiteral("QTreeView::item { padding: 3px 0; }"));
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    v->addWidget(m_list, 1);
    m_status = new QLabel(this);
    m_status->setProperty("role", "muted");
    m_status->setWordWrap(true);
    v->addWidget(m_status);

    m_debounce->setSingleShot(true);
    m_debounce->setInterval(150);
    connect(m_debounce, &QTimer::timeout, this, &FixtureSearch::populate);
    connect(m_search, &QLineEdit::textChanged, m_debounce, qOverload<>(&QTimer::start));
    connect(m_filter, &QComboBox::currentIndexChanged, this, &FixtureSearch::populate);
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int tab) {
        m_filter->setVisible(tab == 1);
        m_search->setPlaceholderText(tab == 0 ? tr("Search the fixtures in this show") : tr("Search every fixture: name or manufacturer"));
        populate();
    });
    connect(m_tabs, &QTabBar::tabBarClicked, this, [this]() { m_tabPicked = true; });
    // In Show, unless the show has no fixture types yet (a new show starts on All fixtures).
    auto pickTab = [this]() {
        if (!m_tabPicked)
            m_tabs->setCurrentIndex(m_ctx->show->typeIdsInShow().isEmpty() ? 1 : 0);
    };
    connect(m_ctx->show, &Show::changed, this, [this, pickTab](int parts) {
        if (!(parts & Show::PatchPart))
            return;
        pickTab();
        if (m_tabs->currentIndex() == 0)
            m_debounce->start();
    });
    pickTab();
    m_filter->setVisible(m_tabs->currentIndex() == 1);
    connect(m_list, &QTreeWidget::currentItemChanged, this, &FixtureSearch::currentChanged);
    connect(m_src, &FixtureSources::indexChanged, this, &FixtureSearch::populate);
    connect(m_src, &FixtureSources::imported, this, [this](int source, int entry, const QString &typeId) {
        // Whatever row stood for this download now stands for the library type.
        for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = m_list->topLevelItem(i);
            if (it->data(0, SourceRole).toInt() == source && it->data(0, EntryRole).toInt() == entry) {
                it->setData(0, TypeIdRole, typeId);
                it->setData(0, SourceRole, kLibrary);
                it->setText(2, tr("In show"));
                it->setForeground(2, theme::live);
            }
        }
        if (source == m_pendingSource && entry == m_pendingEntry) {
            m_pendingSource = m_pendingEntry = -1;
            const FixtureSources::Entry &e = m_src->entries(source)[entry];
            setStatus(tr("%1 %2 is downloaded: patch it to bring it into the show").arg(e.manufacturer, e.name));
            selectType(typeId);
            currentChanged();
        }
    });
    connect(m_src, &FixtureSources::failed, this, [this](int source, int entry, const QString &why) {
        if (source == m_pendingSource && entry == m_pendingEntry) {
            m_pendingSource = m_pendingEntry = -1;
            setStatus(tr("Couldn't get it: %1").arg(why), true);
        }
    });
    populate();
}

void FixtureSearch::setStatus(const QString &text, bool error)
{
    m_status->setText(text);
    m_status->setStyleSheet(error ? QStringLiteral("color: %1;").arg(theme::programmer.name()) : QString());
}

void FixtureSearch::setSearchText(const QString &text)
{
    m_search->setText(text);
    m_debounce->stop();
    populate();
}

void FixtureSearch::focusSearch()
{
    m_search->setFocus();
    m_search->selectAll();
}

void FixtureSearch::selectType(const QString &typeId)
{
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (m_list->topLevelItem(i)->data(0, TypeIdRole).toString() == typeId) {
            m_list->setCurrentItem(m_list->topLevelItem(i));
            m_list->scrollToItem(m_list->currentItem());
            return;
        }
}

void FixtureSearch::populate()
{
    const QString keep = m_typeId;
    const int filter = m_filter->currentData().toInt();
    const QStringList words = m_search->text().split(' ', Qt::SkipEmptyParts);
    const FixtureLibrary &lib = m_ctx->show->library();

    m_list->blockSignals(true);
    m_list->clear();
    QList<QTreeWidgetItem *> rows;

    const bool inShowTab = m_tabs->currentIndex() == 0;
    const QStringList showIds = m_ctx->show->typeIdsInShow();
    auto libraryRow = [&](const FixtureType &t, const QString &from) {
        auto *it = new QTreeWidgetItem({t.name, t.manufacturer, from});
        it->setData(0, TypeIdRole, t.id);
        it->setData(0, SourceRole, kLibrary);
        it->setForeground(2, theme::live);
        QStringList modes;
        for (const FixtureMode &m : t.modes)
            modes << QStringLiteral("%1 (%2 ch)").arg(m.name).arg(m.footprint);
        it->setToolTip(0, modes.join('\n'));
        return it;
    };
    auto byName = [](const FixtureType &a, const FixtureType &b) {
        return QString::compare(a.manufacturer + a.name, b.manufacturer + b.name, Qt::CaseInsensitive) < 0;
    };
    int online = 0, more = 0, available = 0;
    bool searchOnline = false;

    if (inShowTab) {
        // The types this show holds.
        QList<FixtureType> types;
        for (const QString &id : showIds)
            if (const FixtureType *t = lib.find(id))
                types << *t;
        std::sort(types.begin(), types.end(), byName);
        for (const FixtureType &t : types)
            if (matches(words, t.manufacturer + ' ' + t.name))
                rows << libraryRow(t, tr("In show"));
    } else {
        // Everything Lumora can get, once there's something to search for (or one source picked).
        searchOnline = filter != -2 || m_search->text().trimmed().size() >= 2;
        const QSet<QString> inShow(showIds.begin(), showIds.end());
        if (filter == -2 || filter == kBuiltIn) {
            QList<FixtureType> types = lib.shippedTypes();
            available += types.size();
            if (searchOnline) {
                std::sort(types.begin(), types.end(), byName);
                for (const FixtureType &t : types)
                    if (matches(words, t.manufacturer + ' ' + t.name))
                        rows << libraryRow(lib.isShowType(t.id) ? *lib.find(t.id) : t, inShow.contains(t.id) ? tr("In show") : tr("Built in"));
            }
        }
        for (int s = 0; s < FixtureSources::SourceCount; ++s) {
            if (filter != -2 && filter != s)
                continue;
            const QVector<FixtureSources::Entry> &entries = m_src->entries(s);
            available += entries.size();
            if (!searchOnline)
                continue;
            for (int i = 0; i < entries.size(); ++i) {
                const FixtureSources::Entry &e = entries[i];
                if (!matches(words, e.manufacturer + ' ' + e.name + ' ' + e.details))
                    continue;
                if (const FixtureType *t = lib.find(e.typeId)) {   // downloaded already: patch it as it is
                    if (!lib.shippedTypes().isEmpty() && (filter == -2 || filter == kBuiltIn) && !lib.isShowType(t->id))
                        continue;   // listed above as built in
                    rows << libraryRow(*t, inShow.contains(t->id) ? tr("In show") : FixtureSources::sourceName(s));
                    continue;
                }
                if (online >= kOnlineLimit) {
                    ++more;
                    continue;
                }
                auto *it = new QTreeWidgetItem({e.name, e.manufacturer, FixtureSources::sourceName(s)});
                it->setData(0, SourceRole, s);
                it->setData(0, EntryRole, i);
                it->setToolTip(0, e.details.isEmpty() ? tr("Not downloaded yet: pick it to get it")
                                                      : tr("%1\nNot downloaded yet: pick it to get it").arg(e.details));
                it->setForeground(2, theme::muted);
                rows << it;
                ++online;
            }
        }
    }
    for (QTreeWidgetItem *it : rows)
        it->setSizeHint(0, QSize(0, 24));
    m_list->addTopLevelItems(rows);
    m_list->blockSignals(false);

    if (inShowTab && rows.isEmpty())
        setStatus(showIds.isEmpty() ? tr("No fixture types in this show yet: find them under All fixtures.")
                                    : tr("Nothing in this show matches."));
    else if (inShowTab)
        setStatus(QString());
    else if (!searchOnline)
        setStatus(tr("Type to search %1 fixtures: built in, GDTF Share, the Open Fixture Library and QLC+.")
                      .arg(QLocale().toString(available)));
    else if (more)
        setStatus(tr("%1 more match: add a word to narrow it down.").arg(more));
    else if (rows.isEmpty())
        setStatus(m_src->entries(FixtureSources::Gdtf).isEmpty() && (filter == -2 || filter == FixtureSources::Gdtf)
                      ? tr("Nothing found. To include GDTF Share, sign in under Fixture library.")
                      : tr("Nothing found."));
    else
        setStatus(QString());

    // Keep the chosen type selected; otherwise start on the first library match. Online rows are
    // never picked automatically: picking one downloads it.
    if (!keep.isEmpty())
        selectType(keep);
    if (!m_list->currentItem() && m_list->topLevelItemCount()
        && m_list->topLevelItem(0)->data(0, SourceRole).toInt() == kLibrary)
        m_list->setCurrentItem(m_list->topLevelItem(0));
    if (!m_list->currentItem())
        currentChanged();
}

void FixtureSearch::currentChanged()
{
    QTreeWidgetItem *it = m_list->currentItem();
    QString id;
    if (it && it->data(0, SourceRole).toInt() == kLibrary) {
        id = it->data(0, TypeIdRole).toString();
    } else if (it) {
        // Not in the library yet: get it now.
        const int s = it->data(0, SourceRole).toInt(), e = it->data(0, EntryRole).toInt();
        m_pendingSource = s;
        m_pendingEntry = e;
        setStatus(tr("Getting %1 %2 from %3…").arg(it->text(1), it->text(0), FixtureSources::sourceName(s)));
        m_src->fetch(s, e);
    }
    if (id != m_typeId || id.isEmpty()) {
        m_typeId = id;
        emit typeChosen(id);
    }
}

} // namespace lumora
