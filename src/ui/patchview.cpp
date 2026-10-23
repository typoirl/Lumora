// SPDX-License-Identifier: GPL-3.0-or-later
#include "patchview.h"

#include "appcontext.h"
#include "fixtureeditor.h"
#include "fixturesearch.h"
#include "librarybrowser.h"
#include "quickedit.h"
#include "theme.h"
#include "uiutil.h"

#include <QHeaderView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QStyledItemDelegate>

#include <algorithm>
#include <QTimer>
#include <QVBoxLayout>

namespace lumora {

namespace {
// Type and Mode cells edit through a dropdown and apply straight to the patch.
class PatchDelegate : public QStyledItemDelegate {
public:
    PatchDelegate(AppContext *ctx, QObject *parent) : QStyledItemDelegate(parent), m_ctx(ctx) {}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &index) const override
    {
        if (index.column() != 2 && index.column() != 3)
            return new QLineEdit(parent);
        auto *combo = new QComboBox(parent);
        const int fid = index.sibling(index.row(), 0).data().toInt();
        const Fixture *f = m_ctx->show->fixture(fid);
        if (index.column() == 2) {
            // The show's own types; Change type… finds any other.
            QList<FixtureType> types;
            for (const QString &id : m_ctx->show->typeIdsInShow())
                if (const FixtureType *t = m_ctx->show->library().find(id))
                    types << *t;
            std::sort(types.begin(), types.end(), [](const FixtureType &a, const FixtureType &b) {
                return a.manufacturer + a.name < b.manufacturer + b.name;
            });
            for (const FixtureType &t : types)
                combo->addItem(QStringLiteral("%1 %2").arg(t.manufacturer, t.name), t.id);
            combo->setCurrentIndex(combo->findData(f ? f->typeId : QString()));
        } else if (const FixtureType *t = m_ctx->show->fixtureType(fid)) {
            for (const FixtureMode &m : t->modes)
                combo->addItem(QStringLiteral("%1  (%2 ch)").arg(m.name).arg(m.footprint), m.name);
            combo->setCurrentIndex(combo->findData(f->mode));
        }
        return combo;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (qobject_cast<QComboBox *>(editor))
            return;
        QStyledItemDelegate::setEditorData(editor, index);
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo) {
            QStyledItemDelegate::setModelData(editor, model, index);
            return;
        }
        const int fid = index.sibling(index.row(), 0).data().toInt();
        const Fixture *f = m_ctx->show->fixture(fid);
        if (!f)
            return;
        Fixture copy = *f;
        if (index.column() == 2) {
            if (combo->currentData().toString() == f->typeId)
                return;
            copy.typeId = combo->currentData().toString();
            copy.mode.clear();   // first mode of the new type
        } else {
            if (combo->currentData().toString() == f->mode)
                return;
            copy.mode = combo->currentData().toString();
        }
        AppContext *ctx = m_ctx;
        // Apply after the editor closes: the table is rebuilt when the patch changes.
        QMetaObject::invokeMethod(ctx, [ctx, copy]() {
            QString err;
            if (ctx->show->updateFixtures({copy}, &err))
                emit ctx->status(QObject::tr("Fixture %1 changed").arg(copy.fid));
            else
                emit ctx->status(QObject::tr("Fixture %1 not changed: %2. Try Change type… and re-address").arg(copy.fid).arg(err));
        }, Qt::QueuedConnection);
    }

private:
    AppContext *m_ctx;
};
} // namespace

// ===========================================================================
// Change type dialog
// ===========================================================================

ChangeTypeDialog::ChangeTypeDialog(AppContext *ctx, const QVector<int> &fids, QWidget *parent)
    : QDialog(parent), m_ctx(ctx), m_fids(fids)
{
    setWindowTitle(tr("Change fixture type"));
    resize(900, 520);
    auto *root = new QHBoxLayout(this);
    root->setSpacing(16);
    m_types = new FixtureSearch(ctx, this);
    root->addWidget(m_types, 3);

    auto *right = new QVBoxLayout;
    auto *intro = new QLabel(tr("Changing %1. Values for attributes the new type shares are kept.")
                                 .arg(countOf(int(fids.size()), tr("fixture"), tr("fixtures"))), this);
    intro->setWordWrap(true);
    right->addWidget(intro);
    auto *form = new QFormLayout;
    form->setVerticalSpacing(10);
    m_mode = new QComboBox(this);
    m_addressing = new QComboBox(this);
    m_addressing->addItem(tr("Keep current addresses"));
    m_addressing->addItem(tr("Re-address sequentially"));
    m_universe = new QSpinBox(this);
    m_universe->setRange(1, kMaxUniverses);
    m_address = new QSpinBox(this);
    m_address->setRange(1, 512);
    form->addRow(tr("Mode"), m_mode);
    form->addRow(tr("Addresses"), m_addressing);
    form->addRow(tr("Universe"), m_universe);
    form->addRow(tr("Start address"), m_address);
    right->addLayout(form);
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setProperty("role", "muted");
    right->addWidget(m_summary);
    right->addStretch(1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *apply = buttons->addButton(tr("Change type"), QDialogButtonBox::AcceptRole);
    apply->setProperty("role", "primary");
    connect(buttons, &QDialogButtonBox::accepted, this, &ChangeTypeDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    right->addWidget(buttons);
    root->addLayout(right, 2);

    const Fixture *first = ctx->show->fixture(fids.value(0));
    if (first) {
        m_universe->setValue(first->universe);
        m_address->setValue(first->address);
        m_types->selectType(first->typeId);
    }
    auto syncAddressing = [this]() {
        const bool re = m_addressing->currentIndex() == 1;
        m_universe->setEnabled(re);
        m_address->setEnabled(re);
    };
    connect(m_addressing, &QComboBox::currentIndexChanged, this, syncAddressing);
    connect(m_types, &FixtureSearch::typeChosen, this, &ChangeTypeDialog::typeChanged);
    syncAddressing();
    typeChanged();
    if (first)
        m_mode->setCurrentIndex(std::max(0, m_mode->findData(first->mode)));
}

void ChangeTypeDialog::typeChanged()
{
    m_mode->clear();
    const FixtureType *t = m_ctx->show->library().find(m_types->typeId());
    if (!t)
        return;
    for (const FixtureMode &m : t->modes)
        m_mode->addItem(QStringLiteral("%1  (%2 ch)").arg(m.name).arg(m.footprint), m.name);
}

void ChangeTypeDialog::accept()
{
    const FixtureType *t = m_ctx->show->library().find(m_types->typeId());
    const FixtureMode *m = t ? t->mode(m_mode->currentData().toString()) : nullptr;
    if (!m) {
        m_summary->setText(tr("Choose a fixture type first"));
        return;
    }
    QVector<Fixture> list;
    for (int fid : m_fids) {
        const Fixture *f = m_ctx->show->fixture(fid);
        if (!f)
            continue;
        Fixture c = *f;
        c.typeId = t->id;
        c.mode = m->name;
        list << c;
    }
    QString err;
    if (m_addressing->currentIndex() == 1 && !m_ctx->show->layOut(list, m_universe->value(), m_address->value(), &err)) {
        m_summary->setText(err);
        return;
    }
    if (!m_ctx->show->updateFixtures(list, &err)) {
        m_summary->setText(err + tr(". Nothing was changed."));
        m_summary->setStyleSheet(QStringLiteral("color: %1;").arg(theme::programmer.name()));
        return;
    }
    emit m_ctx->status(tr("Changed %1 to %2 %3").arg(countOf(int(list.size()), tr("fixture"), tr("fixtures")), t->name, m->name));
    QDialog::accept();
}

// ===========================================================================
// Add fixtures dialog
// ===========================================================================

AddFixturesPanel::AddFixturesPanel(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(14, 0, 0, 0);
    v->setSpacing(8);
    auto *title = new QLabel(tr("Add to patch"), this);
    title->setFont(theme::uiFont(11, QFont::DemiBold));
    v->addWidget(title);
    m_search = new FixtureSearch(ctx, this);
    v->addWidget(m_search, 1);

    m_chosen = new QLabel(this);
    m_chosen->setWordWrap(true);
    m_chosen->setFont(theme::uiFont(10, QFont::DemiBold));
    v->addWidget(m_chosen);
    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setVerticalSpacing(8);
    m_mode = new QComboBox(this);
    m_quantity = new QSpinBox(this);
    m_quantity->setRange(1, 512);
    m_firstId = new QSpinBox(this);
    m_firstId->setRange(1, 99999);
    m_firstId->setValue(ctx->show->nextFreeFid());
    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(tr("Uses the fixture's short name"));
    // Universe and address are plain fields: clear the address to add the fixtures unpatched.
    m_universe = new QLineEdit(QStringLiteral("1"), this);
    m_universe->setValidator(new QIntValidator(1, kMaxUniverses, m_universe));
    m_address = new QLineEdit(this);
    m_address->setValidator(new QIntValidator(1, 512, m_address));
    m_address->setPlaceholderText(tr("unpatched"));
    m_address->setClearButtonEnabled(true);
    auto *addrRow = new QHBoxLayout;
    addrRow->setSpacing(4);
    auto *dot = new QLabel(QStringLiteral("."), this);
    dot->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    m_universe->setToolTip(tr("Universe"));
    m_address->setToolTip(tr("Start address. Leave it empty to add the fixtures unpatched: program them now, patch them later."));
    addrRow->addWidget(m_universe, 1);
    addrRow->addWidget(dot);
    addrRow->addWidget(m_address, 1);
    form->addRow(tr("Mode"), m_mode);
    form->addRow(tr("Quantity"), m_quantity);
    form->addRow(tr("First ID"), m_firstId);
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("Address"), addrRow);
    v->addLayout(form);
    m_summary = new QLabel(this);
    m_summary->setProperty("role", "muted");
    m_summary->setWordWrap(true);
    v->addWidget(m_summary);
    m_add = new QPushButton(tr("Add to patch"), this);
    m_add->setProperty("role", "primary");
    v->addWidget(m_add);

    connect(m_search, &FixtureSearch::typeChosen, this, &AddFixturesPanel::typeChanged);
    connect(m_mode, &QComboBox::currentIndexChanged, this, &AddFixturesPanel::updateAddress);
    connect(m_quantity, &QSpinBox::valueChanged, this, &AddFixturesPanel::updateAddress);
    connect(m_universe, &QLineEdit::textEdited, this, [this]() {
        m_unpatched = false;   // picking a universe means patching: fill in its next free address
        updateAddress();
    });
    connect(m_address, &QLineEdit::textEdited, this, [this](const QString &t) {
        m_unpatched = t.trimmed().isEmpty();   // cleared on purpose: stay unpatched
        updateSummary();
    });
    connect(m_firstId, &QSpinBox::valueChanged, this, &AddFixturesPanel::updateSummary);
    connect(m_add, &QPushButton::clicked, this, &AddFixturesPanel::add);
    connect(ctx->show, &Show::changed, this, [this](Show::Parts p) {
        if (p & Show::PatchPart) {
            if (m_ctx->show->fixture(m_firstId->value()))
                m_firstId->setValue(m_ctx->show->nextFreeFid());
            updateAddress();
        }
    });
    typeChanged();
}

void AddFixturesPanel::focusSearch()
{
    m_search->focusSearch();
}

void AddFixturesPanel::typeChanged()
{
    m_mode->clear();
    const FixtureType *t = m_ctx->show->library().find(m_search->typeId());
    m_add->setEnabled(t);
    m_chosen->setText(t ? QStringLiteral("%1 %2").arg(t->manufacturer, t->name) : tr("Pick a fixture above"));
    if (!t) {
        m_summary->clear();
        return;
    }
    for (const FixtureMode &m : t->modes)
        m_mode->addItem(QStringLiteral("%1  (%2 ch)").arg(m.name).arg(m.footprint), m.name);
    updateAddress();
}

int AddFixturesPanel::universe() const
{
    bool ok = false;
    const int u = m_universe->text().trimmed().toInt(&ok);
    return ok && u >= 1 && u <= kMaxUniverses ? u : 0;
}

int AddFixturesPanel::address() const
{
    bool ok = false;
    const int a = m_address->text().trimmed().toInt(&ok);
    return ok && a >= 1 && a <= 512 ? a : 0;
}

void AddFixturesPanel::updateAddress()
{
    const FixtureType *t = m_ctx->show->library().find(m_search->typeId());
    const FixtureMode *m = t ? t->mode(m_mode->currentData().toString()) : nullptr;
    if (!m)
        return;
    if (!m_unpatched && universe() > 0) {
        // Where the first one would go: the next gap in this universe, or the next universe.
        QVector<Fixture> one{Fixture{0, QString(), t->id, m->name, universe(), 1}};
        if (m_ctx->show->layOut(one, universe(), 1)) {
            m_universe->setText(QString::number(one[0].universe));
            m_address->setText(QString::number(one[0].address));
        }
    }
    updateSummary();
}

void AddFixturesPanel::updateSummary()
{
    const FixtureType *t = m_ctx->show->library().find(m_search->typeId());
    const FixtureMode *m = t ? t->mode(m_mode->currentData().toString()) : nullptr;
    if (!m)
        return;
    const int n = m_quantity->value();
    if (address() <= 0 || universe() <= 0) {
        m_summary->setText(n == 1 ? tr("ID %1, unpatched: program it now, give it an address later").arg(m_firstId->value())
                                  : tr("IDs %1–%2, unpatched: program them now, give them addresses later")
                                        .arg(m_firstId->value()).arg(m_firstId->value() + n - 1));
        return;
    }
    QVector<Fixture> plan = planned(t, m);
    QString err;
    if (!m_ctx->show->layOut(plan, universe(), address(), &err)) {
        m_summary->setText(err);
        return;
    }
    const Fixture &a = plan.first(), &b = plan.last();
    const QString from = QStringLiteral("%1.%2").arg(a.universe).arg(a.address, 3, 10, QChar('0'));
    const QString to = QStringLiteral("%1.%2").arg(b.universe).arg(b.address + m->footprint - 1, 3, 10, QChar('0'));
    QString text = n == 1 ? tr("ID %1, %2, %3 to %4").arg(m_firstId->value()).arg(countOf(m->footprint, tr("channel"), tr("channels")), from, to)
                          : tr("IDs %1–%2, %3 channels each, %4 to %5")
                                .arg(m_firstId->value()).arg(m_firstId->value() + n - 1).arg(m->footprint).arg(from, to);
    if (b.universe != a.universe)
        text += tr(" (carries on in the next universe, from address 1)");
    if (m->cellCount) {
        const int heads = m->partPath(m->cellCount).first;
        text += heads == m->cellCount
                    ? tr(". Subfixtures: %1 each, %2.1 to %2.%1").arg(heads).arg(m_firstId->value())
                    : tr(". Subfixtures: %1 each (%2.1 to %2.%1), and %3 inside them (%2.1.1…)")
                          .arg(heads).arg(m_firstId->value()).arg(m->cellCount - heads);
    }
    m_summary->setText(text);
}

QVector<Fixture> AddFixturesPanel::planned(const FixtureType *t, const FixtureMode *m) const
{
    // "Spot 5" numbers the fixtures Spot 5, Spot 6…; a name without a number starts at 1.
    const QString base = m_name->text().trimmed().isEmpty() ? t->shortName : m_name->text().trimmed();
    const QStringList names = numberedNames(base, m_quantity->value());
    QVector<Fixture> list;
    for (int i = 0; i < m_quantity->value(); ++i)
        list << Fixture{m_firstId->value() + i, names.value(i), t->id, m->name, std::max(1, universe()), 0};
    return list;
}

void AddFixturesPanel::add()
{
    const FixtureType *t = m_ctx->show->library().find(m_search->typeId());
    const FixtureMode *m = t ? t->mode(m_mode->currentData().toString()) : nullptr;
    if (!m) {
        m_summary->setText(tr("Pick a fixture first"));
        return;
    }
    // Validate everything before adding anything.
    QVector<Fixture> list = planned(t, m);
    for (const Fixture &f : list)
        if (m_ctx->show->fixture(f.fid)) {
            m_summary->setText(tr("ID %1 is already used: change the first ID").arg(f.fid));
            return;
        }
    const bool patch = address() > 0 && universe() > 0;
    QString err;
    if (patch && !m_ctx->show->layOut(list, universe(), address(), &err)) {
        m_summary->setText(err);
        return;
    }
    const int first = m_firstId->value(), count = m_quantity->value();
    for (const Fixture &f : list)
        m_ctx->show->addFixture(f);
    m_firstId->setValue(m_ctx->show->nextFreeFid());
    updateAddress();
    emit m_ctx->status(tr("Added %1 %2 (IDs %3–%4)%5").arg(count).arg(t->name).arg(first).arg(first + count - 1)
                           .arg(address() > 0 && universe() > 0 ? QString() : tr(", unpatched")));
}

// ===========================================================================
// Patch view
// ===========================================================================

PatchView::PatchView(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(10, 8, 10, 6);
    v->setSpacing(10);
    auto *head = new QHBoxLayout;
    auto *title = new QLabel(tr("Patch"), this);
    title->setProperty("role", "title");
    head->addWidget(title);
    m_libraryInfo = new QLabel(this);
    m_libraryInfo->setProperty("role", "muted");
    head->addSpacing(10);
    head->addWidget(m_libraryInfo);
    head->addStretch(1);
    auto *browse = new QPushButton(tr("Fixture library…"), this);
    browse->setToolTip(tr("Browse GDTF Share, the Open Fixture Library and QLC+ and import fixture types"));
    auto *import = new QPushButton(tr("Import file…"), this);
    import->setToolTip(tr("GDTF (.gdtf), Open Fixture Library (.json) or QLC+ (.qxf) files. Imported files load again next time."));
    auto *remove = new QPushButton(tr("Remove"), this);
    auto *change = new QPushButton(tr("Change type…"), this);
    change->setToolTip(tr("Change the fixture type or mode of the selected fixtures"));
    auto *editor = new QPushButton(tr("Fixture editor…"), this);
    editor->setToolTip(tr("Build a new fixture type, or edit the type of the selected fixture"));
    head->addWidget(change);
    head->addWidget(remove);
    head->addSpacing(12);
    head->addWidget(editor);
    head->addSpacing(12);
    head->addWidget(browse);
    head->addWidget(import);
    v->addLayout(head);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels({tr("ID"), tr("Name"), tr("Type"), tr("Mode"), tr("Universe"), tr("Address"),
                                        tr("Invert pan"), tr("Invert tilt")});
    m_table->horizontalHeaderItem(6)->setToolTip(tr("Hung the other way round: pan runs backwards on the wire. Applies to every selected fixture."));
    m_table->horizontalHeaderItem(7)->setToolTip(tr("Tilt runs backwards on the wire. Applies to every selected fixture."));
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(28);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setColumnWidth(0, 60);
    m_table->setColumnWidth(1, 160);
    m_table->setColumnWidth(2, 220);
    m_table->setColumnWidth(3, 170);
    m_table->setColumnWidth(4, 80);
    m_table->setColumnWidth(5, 80);
    m_table->setColumnWidth(6, 90);
    auto *split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);
    split->addWidget(m_table);
    m_addPanel = new AddFixturesPanel(ctx, split);
    m_addPanel->setMinimumWidth(340);
    split->addWidget(m_addPanel);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setSizes({900, 460});
    v->addWidget(split, 1);
    connect(FixtureSources::instance(ctx), &FixtureSources::imported, this, [this]() { refresh(); });
    connect(remove, &QPushButton::clicked, this, [this]() {
        QList<int> fids;
        for (const QModelIndex &i : m_table->selectionModel()->selectedRows())
            fids << m_table->item(i.row(), 0)->text().toInt();
        if (fids.isEmpty())
            return;
        if (QMessageBox::question(this, tr("Remove fixtures"),
                                  tr("Remove %1 from the patch? Their values in cues and groups are removed too.")
                                      .arg(countOf(int(fids.size()), tr("fixture"), tr("fixtures"))))
            != QMessageBox::Yes)
            return;
        for (int fid : fids)
            m_ctx->show->removeFixture(fid);
    });
    connect(import, &QPushButton::clicked, this, &PatchView::importFiles);
    connect(browse, &QPushButton::clicked, this, [this]() {
        FixtureLibraryDialog d(m_ctx, this);
        d.exec();
        refresh();
        m_addPanel->search()->setSearchText(QString());
    });
    connect(change, &QPushButton::clicked, this, &PatchView::changeType);
    connect(editor, &QPushButton::clicked, this, [this, editor]() {
        QMenu menu(this);
        menu.addAction(tr("New fixture type…"), this, [this]() {
            FixtureEditorDialog d(m_ctx, QString(), this);
            if (d.exec() == QDialog::Accepted)
                m_addPanel->search()->setSearchText(QString());
        });
        const QVector<int> fids = selectedFids();
        const FixtureType *t = fids.isEmpty() ? nullptr : m_ctx->show->fixtureType(fids.first());
        QAction *edit = menu.addAction(t ? tr("Edit %1 %2…").arg(t->manufacturer, t->name) : tr("Edit the selected fixture's type…"),
                                       this, [this, id = t ? t->id : QString()]() {
                                           FixtureEditorDialog d(m_ctx, id, this);
                                           d.exec();
                                       });
        edit->setEnabled(t);
        menu.exec(editor->mapToGlobal(QPoint(0, editor->height())));
    });
    m_table->setItemDelegate(new PatchDelegate(m_ctx, m_table));
    // Right-click a cell to edit it; on the type, choose another fixture type.
    enableRightClickEdit(m_table, [this](int row, int column, const QPoint &global) {
        if (column == 4 || column == 5) {
            QVector<int> fids = selectedFids();
            const int fid = m_table->item(row, 0)->text().toInt();
            if (!fids.contains(fid))
                fids = {fid};
            auto *ed = new QuickEditor(fids.size() == 1 ? tr("Fixture %1").arg(fid)
                                                        : countOf(int(fids.size()), tr("fixture"), tr("fixtures")),
                                       this);
            const Fixture *f = m_ctx->show->fixture(fid);
            if (fids.size() == 1 && f) {
                // Type universe.address (or just an address); clear it to unpatch.
                const QString current = f->patched() ? QStringLiteral("%1.%2").arg(f->universe).arg(f->address) : QString();
                ed->addNameField(tr("Address"), current, [this, fid](const QString &text) { setAddress(fid, text); }, true,
                                 tr("unpatched"));
            }
            ed->addAction(tr("Unpatch"), [this, fids]() { unpatch(fids); }, QIcon(), true);
            if (fids.size() > 1) {
                const int universe = f && f->patched() ? f->universe : 1;
                m_patchUniverse = universe;
                ed->addNumber(tr("Patch sequentially in universe"), 1, kMaxUniverses, universe,
                              [this](int u) { m_patchUniverse = u; });
                ed->addAction(tr("Patch"), [this, fids]() { patchSequential(fids, m_patchUniverse); });
            }
            ed->popup(global);
            return true;
        }
        if (column == 0) {
            QVector<int> fids = selectedFids();
            const int fid = m_table->item(row, 0)->text().toInt();
            if (!fids.contains(fid))
                fids = {fid};
            auto *ed = new QuickEditor(fids.size() == 1 ? tr("Fixture %1").arg(fid)
                                                        : countOf(int(fids.size()), tr("fixture"), tr("fixtures")),
                                       this);
            // One fixture: its new ID. Several: they're numbered on from the ID typed, in order.
            ed->addNameField(fids.size() == 1 ? tr("ID") : tr("Number from"), QString::number(fids.first()),
                             [this, fids](const QString &text) {
                                 bool ok = false;
                                 const int start = text.trimmed().toInt(&ok);
                                 if (!ok || start < 1) {
                                     emit m_ctx->status(tr("Type a number"));
                                     return;
                                 }
                                 QMap<int, int> map;
                                 for (int i = 0; i < fids.size(); ++i)
                                     map.insert(fids[i], start + i);
                                 QString err;
                                 if (m_ctx->show->renumberFixtures(map, &err))
                                     emit m_ctx->status(fids.size() == 1 ? tr("Fixture %1 is now %2").arg(fids.first()).arg(start)
                                                                         : tr("%1 renumbered %2–%3")
                                                                               .arg(countOf(int(fids.size()), tr("fixture"), tr("fixtures")))
                                                                               .arg(start).arg(start + fids.size() - 1));
                                 else
                                     emit m_ctx->status(tr("Not renumbered: %1").arg(err));
                             });
            ed->popup(global);
            return true;
        }
        if (column == 1) {
            // One fixture: edit its name in place. Several: one name for all, numbered in ID order.
            QVector<int> fids = selectedFids();
            const int fid = m_table->item(row, 0)->text().toInt();
            if (fids.size() < 2 || !fids.contains(fid))
                return false;
            auto *ed = new QuickEditor(tr("Name %1").arg(countOf(int(fids.size()), tr("fixture"), tr("fixtures"))), this);
            QString current = m_ctx->show->fixture(fids.first()) ? m_ctx->show->fixture(fids.first())->name : QString();
            ed->addNameField(tr("Name"), current, [this, fids](const QString &text) {
                if (text.trimmed().isEmpty())
                    return;
                const QStringList names = numberedNames(text, int(fids.size()));
                QVector<Fixture> list;
                for (int i = 0; i < fids.size(); ++i)
                    if (const Fixture *o = m_ctx->show->fixture(fids[i])) {
                        Fixture c = *o;
                        c.name = names[i];
                        list << c;
                    }
                QString err;
                if (m_ctx->show->updateFixtures(list, &err))
                    emit m_ctx->status(tr("%1 renamed %2 to %3").arg(countOf(int(list.size()), tr("fixture"), tr("fixtures")),
                                                                     names.first(), names.last()));
                else
                    emit m_ctx->status(tr("Not renamed: %1").arg(err));
            });
            ed->popup(global);
            return true;
        }
        if (column == 3) {
            QVector<int> fids = selectedFids();
            const int fid = m_table->item(row, 0)->text().toInt();
            if (!fids.contains(fid))
                fids = {fid};
            const Fixture *f = m_ctx->show->fixture(fid);
            const FixtureType *t = m_ctx->show->fixtureType(fid);
            if (!f || !t)
                return false;
            // Modes of this fixture's type; selected fixtures of other types are left alone.
            QVector<int> same;
            for (int id : fids)
                if (const Fixture *o = m_ctx->show->fixture(id); o && o->typeId == f->typeId)
                    same << id;
            auto *ed = new QuickEditor(same.size() == 1 ? tr("Mode of fixture %1").arg(fid)
                                                        : tr("Mode of %1").arg(countOf(int(same.size()), tr("fixture"), tr("fixtures"))),
                                       this);
            for (const FixtureMode &m : t->modes) {
                const QString name = m.name;
                const QString label = QStringLiteral("%1 %2  (%3 ch)").arg(name == f->mode ? QStringLiteral("●") : QStringLiteral("○"), name)
                                          .arg(m.footprint);
                ed->addAction(label, [this, same, name, skipped = int(fids.size() - same.size())]() {
                    QVector<Fixture> list;
                    for (int id : same)
                        if (const Fixture *o = m_ctx->show->fixture(id); o && o->mode != name) {
                            Fixture c = *o;
                            c.mode = name;
                            list << c;
                        }
                    if (list.isEmpty())
                        return;
                    QString err;
                    if (m_ctx->show->updateFixtures(list, &err))
                        emit m_ctx->status(tr("%1 set to %2%3").arg(countOf(int(list.size()), tr("fixture"), tr("fixtures")), name,
                                                                    skipped ? tr(" (%1 of another type left as they were)").arg(skipped)
                                                                            : QString()));
                    else
                        emit m_ctx->status(tr("Mode not changed: %1. Unpatch them or patch them sequentially, then try again").arg(err));
                });
            }
            ed->popup(global);
            return true;
        }
        if (column != 2)
            return false;
        QVector<int> fids = selectedFids();
        const int fid = m_table->item(row, 0)->text().toInt();
        if (!fids.contains(fid))
            fids = {fid};
        const Fixture *f = m_ctx->show->fixture(fid);
        const FixtureType *t = m_ctx->show->fixtureType(fid);
        auto *ed = new QuickEditor(t ? QStringLiteral("%1 %2").arg(t->manufacturer, t->name) : tr("Fixture type"), this);
        ed->addAction(tr("Change type or mode…"), [this, fids]() {
            ChangeTypeDialog d(m_ctx, fids, this);
            d.exec();
        });
        const QString typeId = f ? f->typeId : QString();
        ed->addAction(tr("Edit this fixture type…"), [this, typeId]() {
            FixtureEditorDialog d(m_ctx, typeId, this);
            d.exec();
        });
        ed->popup(global);
        return true;
    });
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_updating)
            return;
        const int fid = m_table->item(item->row(), 0)->text().toInt();
        const Fixture *f = m_ctx->show->fixture(fid);
        if (!f)
            return;
        Fixture copy = *f;
        if (item->column() == 6 || item->column() == 7) {
            // Invert pan / tilt: for every selected fixture when this one is among them.
            const bool on = item->checkState() == Qt::Checked;
            QVector<int> fids = selectedFids();
            if (!fids.contains(fid))
                fids = {fid};
            QVector<Fixture> list;
            for (int id : fids)
                if (const Fixture *x = m_ctx->show->fixture(id)) {
                    Fixture c = *x;
                    (item->column() == 6 ? c.invertPan : c.invertTilt) = on;
                    list << c;
                }
            QString err;
            if (!m_ctx->show->updateFixtures(list, &err))
                emit m_ctx->status(err);
            QTimer::singleShot(0, this, &PatchView::refresh);
            return;
        }
        if (item->column() == 1)
            copy.name = item->text();
        else if (item->column() == 4) {
            // Clearing the universe (like clearing the address) unpatches the fixture.
            const int u = item->text().trimmed().toInt();
            if (u < 1)
                copy.address = 0;
            else
                copy.universe = u;
        }
        else if (item->column() == 5)
            copy.address = item->text().trimmed().toInt();   // empty (or "-") unpatches
        if ((item->column() == 4 || item->column() == 5) && !copy.patched()) {
            // Cleared with several fixtures selected: unpatch them all.
            QVector<int> fids = selectedFids();
            if (!fids.contains(fid))
                fids = {fid};
            QTimer::singleShot(0, this, [this, fids]() { unpatch(fids); });
            return;
        }
        QString err;
        if (!m_ctx->show->updateFixture(copy, &err)) {
            emit m_ctx->status(tr("Fixture %1 not changed: %2").arg(fid).arg(err));
            refresh();
        }
    });
    onShowChanged(this, ctx->show, [this](Show::Parts p) {
        if (p & Show::PatchPart)
            refresh();
    });
    refresh();
}

QVector<int> PatchView::selectedFids() const
{
    QVector<int> fids;
    for (const QModelIndex &i : m_table->selectionModel()->selectedRows())
        fids << m_table->item(i.row(), 0)->text().toInt();
    std::sort(fids.begin(), fids.end());
    return fids;
}

void PatchView::changeType()
{
    const QVector<int> fids = selectedFids();
    if (fids.isEmpty()) {
        emit m_ctx->status(tr("Select the fixtures to change first"));
        return;
    }
    ChangeTypeDialog d(m_ctx, fids, this);
    d.exec();
}

void PatchView::importFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import fixture files"), QString(),
                                                            tr("Fixture files (*.gdtf *.json *.qxf *.lft)"));
    int ok = 0;
    QStringList errors;
    for (const QString &path : files) {
        QString err;
        // Into this show: saved with it, not on this computer.
        if (m_ctx->show->importFixtureFile(path, &err))
            ++ok;
        else
            errors << QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), err);
    }
    if (!errors.isEmpty())
        QMessageBox::warning(this, tr("Some files weren't imported"), errors.join('\n'));
    if (ok)
        emit m_ctx->status(tr("Imported %1").arg(countOf(ok, tr("fixture type"), tr("fixture types"))));
    refresh();
}

void PatchView::unpatch(const QVector<int> &fids)
{
    QVector<Fixture> list;
    for (int id : fids)
        if (const Fixture *x = m_ctx->show->fixture(id); x && x->patched()) {
            Fixture c = *x;
            c.address = 0;
            list << c;
        }
    QString err;
    if (!list.isEmpty() && m_ctx->show->updateFixtures(list, &err))
        emit m_ctx->status(tr("%1 unpatched: still programmable, no DMX").arg(countOf(int(list.size()), tr("fixture"), tr("fixtures"))));
}

void PatchView::setAddress(int fid, const QString &text)
{
    const Fixture *f = m_ctx->show->fixture(fid);
    if (!f)
        return;
    const QString t = text.trimmed();
    if (t.isEmpty() || t == QLatin1String("-")) {
        unpatch({fid});
        return;
    }
    Fixture c = *f;
    bool ok = false;
    if (t.contains('.')) {
        const int u = t.section('.', 0, 0).toInt(&ok);
        const int a = ok ? t.section('.', 1, 1).toInt(&ok) : 0;
        if (!ok || u < 1 || a < 1) {
            emit m_ctx->status(tr("Type an address like 2.101, or clear it to unpatch"));
            return;
        }
        c.universe = u;
        c.address = a;
    } else {
        const int a = t.toInt(&ok);
        if (!ok || a < 1) {
            emit m_ctx->status(tr("Type an address like 2.101, or clear it to unpatch"));
            return;
        }
        c.address = a;
    }
    QString err;
    if (m_ctx->show->updateFixture(c, &err))
        emit m_ctx->status(tr("Fixture %1 patched at %2.%3").arg(fid).arg(c.universe).arg(c.address));
    else
        emit m_ctx->status(tr("Fixture %1 not patched: %2").arg(fid).arg(err));
}

void PatchView::patchSequential(const QVector<int> &fids, int universe)
{
    // One after another from the first gap big enough; a fixture that doesn't fit in what's left
    // of a universe starts the next one at 1.
    Show *show = m_ctx->show;
    QVector<Fixture> list;
    for (int fid : fids)
        if (const Fixture *f = show->fixture(fid); f && !f->cell)
            list << *f;
    QString err;
    if (list.isEmpty())
        return;
    if (!show->layOut(list, universe, 1, &err) || !show->updateFixtures(list, &err)) {
        emit m_ctx->status(err);
        return;
    }
    const int lastUniverse = list.last().universe;
    emit m_ctx->status(lastUniverse == universe
                           ? tr("%1 patched in universe %2").arg(countOf(int(list.size()), tr("fixture"), tr("fixtures"))).arg(universe)
                           : tr("%1 patched in universes %2–%3").arg(countOf(int(list.size()), tr("fixture"), tr("fixtures"))).arg(universe).arg(lastUniverse));
}

void PatchView::refresh()
{
    m_updating = true;
    Show *show = m_ctx->show;
    m_libraryInfo->setText(tr("%1 in this show").arg(countOf(int(show->typeIdsInShow().size()), tr("fixture type"), tr("fixture types"))));
    m_table->setRowCount(show->fixtures().size());
    int r = 0;
    for (const Fixture &f : show->fixtures()) {
        const FixtureType *t = show->fixtureType(f.fid);
        const FixtureMode *m = show->fixtureMode(f.fid);
        auto set = [&](int col, const QString &text, bool editable) {
            auto *it = new QTableWidgetItem(text);
            if (!editable) {
                it->setFlags(it->flags() & ~Qt::ItemIsEditable);
                it->setForeground(col == 0 ? theme::text : theme::muted);
            }
            m_table->setItem(r, col, it);
        };
        set(0, QString::number(f.fid), false);
        set(1, f.name, true);
        set(2, t ? QStringLiteral("%1 %2").arg(t->manufacturer, t->name) : f.typeId, true);
        const int cells = int(show->cellsOf(f.fid).size()), heads = int(show->childrenOf(f.fid).size());
        set(3, !m ? f.mode
                  : cells == heads && cells ? tr("%1  (%2 ch, %3 subfixtures)").arg(m->name).arg(m->footprint).arg(cells)
                  : cells ? tr("%1  (%2 ch, %3 subfixtures with %4 inside)").arg(m->name).arg(m->footprint).arg(heads).arg(cells - heads)
                          : QStringLiteral("%1  (%2 ch)").arg(m->name).arg(m->footprint),
            true);
        set(4, f.patched() ? QString::number(f.universe) : QStringLiteral("–"), true);
        set(5, f.patched() ? QString::number(f.address) : tr("unpatched"), true);
        if (!f.patched())
            for (int c : {4, 5})
                m_table->item(r, c)->setForeground(theme::faint);
        // Invert pan / tilt: only for fixtures (or their subfixtures) that have them.
        bool hasPan = false, hasTilt = false;
        for (int id : QVector<int>{f.fid} + show->cellsOf(f.fid)) {
            hasPan |= show->fixtureHas(id, QStringLiteral("Pan"));
            hasTilt |= show->fixtureHas(id, QStringLiteral("Tilt"));
        }
        for (int c : {6, 7}) {
            auto *it = new QTableWidgetItem;
            const bool has = c == 6 ? hasPan : hasTilt;
            it->setFlags(has ? Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable : Qt::ItemIsSelectable);
            if (has)
                it->setCheckState((c == 6 ? f.invertPan : f.invertTilt) ? Qt::Checked : Qt::Unchecked);
            m_table->setItem(r, c, it);
        }
        ++r;
    }
    m_updating = false;
}

} // namespace lumora
