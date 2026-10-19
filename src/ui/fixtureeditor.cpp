// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixtureeditor.h"

#include "appcontext.h"
#include "parameters.h"
#include "quickedit.h"
#include "theme.h"
#include "uiutil.h"

#include <QHeaderView>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QMenu>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace lumora {

namespace {

enum ChannelColumn {
    ColSlot, ColFine, ColCell, ColAttr, ColName, ColDefault, ColHighlight, ColMin, ColMax, ColDmxFrom, ColDmxTo, ColRanges, ChannelColumns
};
enum RangeColumn { ColFrom, ColTo, ColRangeName, RangeColumns };

// DMX values as shown: 0..255, or 0..65535 for 16-bit channels.
int toDmx(double v, int max = 255) { return int(std::lround(std::clamp(v, 0.0, 1.0) * max)); }
double fromDmx(const QString &s, double fallback, int max = 255)
{
    bool ok = false;
    const int v = s.trimmed().toInt(&ok);
    return ok ? std::clamp(v, 0, max) / double(max) : fallback;
}
// A number typed in a cell, without a unit ("540°", "50 %")
bool number(QString s, double *out)
{
    s.remove(QChar(0x00b0)).remove('%');
    bool ok = false;
    const double v = s.trimmed().toDouble(&ok);
    if (ok)
        *out = v;
    return ok;
}

QString attrText(const QString &a)
{
    const QString label = attributeLabel(a);
    return label == a ? a : QStringLiteral("%1  ·  %2").arg(label, a);
}

const QString kVirtualText = QStringLiteral("virtual");

// The attribute column offers every standard parameter (shown with its readable name) and
// still accepts any custom name.
class AttributeDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        auto *c = new QComboBox(parent);
        c->setEditable(true);
        for (const QString &a : FixtureEditorDialog::knownAttributes())
            c->addItem(QStringLiteral("%1   (%2)").arg(a, attributeLabel(a)), a);
        return c;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *c = static_cast<QComboBox *>(editor);
        const QString a = index.data(Qt::UserRole).toString();
        const int i = c->findData(a);
        if (i >= 0)
            c->setCurrentIndex(i);
        else
            c->setEditText(a);
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        auto *c = static_cast<QComboBox *>(editor);
        QString a = c->currentIndex() >= 0 && c->currentText() == c->itemText(c->currentIndex())
                        ? c->currentData().toString()
                        : c->currentText().section(' ', 0, 0).trimmed();
        if (a.isEmpty())
            return;
        model->setData(index, a, Qt::UserRole);
        model->setData(index, attrText(a), Qt::DisplayRole);
    }
};

QTableWidget *makeTable26(QWidget *parent, const QStringList &headers)
{
    return makeTable(parent, headers, 26,
                     QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
}

QToolButton *smallButton(const QString &text, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(tip);
    return b;
}

} // namespace

QStringList FixtureEditorDialog::knownAttributes()
{
    QStringList out;
    for (const ParameterInfo &p : parameterLibrary())
        out << p.attribute;
    return out;
}

int FixtureEditorDialog::nextFreeSlot(const FixtureMode &m, int width)
{
    QSet<int> used;
    for (const ChannelDef &c : m.channels)
        for (int s : {c.coarse, c.fine})
            if (s >= 0)
                used.insert(s);
    for (int start = 0;; ++start) {
        bool free = true;
        for (int k = 0; k < width && free; ++k)
            free = !used.contains(start + k);
        if (free)
            return start;
    }
}

FixtureEditorDialog::FixtureEditorDialog(AppContext *ctx, const QString &typeId, QWidget *parent)
    : QDialog(parent), m_ctx(ctx)
{
    if (const FixtureType *t = ctx->show->library().find(typeId)) {
        m_type = *t;
        m_isNew = false;
    } else {
        m_type.manufacturer = tr("Generic");
        m_type.name = tr("New fixture");
        m_type.highlightsExplicit = true;
        FixtureMode m;
        m.name = tr("Standard");
        ChannelDef d;
        d.attribute = "Dimmer";
        d.name = tr("Dimmer");
        m.channels << d;
        m_type.modes << m;
    }
    setWindowTitle(m_isNew ? tr("New fixture type") : tr("Edit fixture type"));
    resize(1360, 700);

    auto *root = new QVBoxLayout(this);
    root->setSpacing(8);

    auto *head = new QHBoxLayout;
    auto *form = new QFormLayout;
    m_maker = new QLineEdit(m_type.manufacturer, this);
    m_name = new QLineEdit(m_type.name, this);
    m_short = new QLineEdit(m_type.shortName, this);
    m_short->setPlaceholderText(tr("Used to name patched fixtures"));
    form->addRow(tr("Manufacturer"), m_maker);
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("Short name"), m_short);
    head->addLayout(form, 2);
    auto *about = new QLabel(m_isNew ? tr("Build a fixture type from the parameter library: double-click a parameter (or add a group like RGBW) "
                                          "to put it on the lowest free DMX slot. Clear a channel's DMX slot to make it virtual. Double-click or "
                                          "right-click a cell to edit it.")
                                     : tr("Editing %1 (from %2). Saving changes it in this show, everywhere it is patched, "
                                          "and the show file keeps your version.").arg(m_type.id, m_type.source.isEmpty() ? tr("a show") : m_type.source),
                             this);
    about->setWordWrap(true);
    about->setProperty("role", "muted");
    head->addWidget(about, 3);
    root->addLayout(head);

    auto *split = new QSplitter(Qt::Horizontal, this);

    // Modes
    auto *modeBox = new QWidget(split);
    auto *mv = new QVBoxLayout(modeBox);
    mv->setContentsMargins(0, 0, 0, 0);
    mv->addWidget(new QLabel(tr("Modes"), modeBox));
    m_modes = new QListWidget(modeBox);
    mv->addWidget(m_modes, 1);
    auto *mb = new QGridLayout;
    mb->setSpacing(4);
    auto *addMode = smallButton(tr("Add"), tr("Add an empty mode"), modeBox);
    auto *dupMode = smallButton(tr("Copy"), tr("Copy this mode as a starting point"), modeBox);
    auto *renMode = smallButton(tr("Rename"), QString(), modeBox);
    auto *delMode = smallButton(tr("Remove"), QString(), modeBox);
    int i = 0;
    for (QToolButton *b : {addMode, dupMode, renMode, delMode}) {
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        mb->addWidget(b, i / 2, i % 2);
        ++i;
    }
    mv->addLayout(mb);

    // Parameter library
    auto *libBox = new QWidget(split);
    auto *lv = new QVBoxLayout(libBox);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->addWidget(new QLabel(tr("Parameters"), libBox));
    auto *search = new QLineEdit(libBox);
    search->setPlaceholderText(tr("Search: red, gobo, zoom…"));
    search->setClearButtonEnabled(true);
    lv->addWidget(search);
    m_library = new QTreeWidget(libBox);
    m_library->setHeaderHidden(true);
    m_library->setRootIsDecorated(false);
    m_library->setIndentation(10);
    m_library->setSelectionMode(QAbstractItemView::ExtendedSelection);
    lv->addWidget(m_library, 1);
    auto *lb = new QGridLayout;
    lb->setSpacing(4);
    auto *addParam = smallButton(tr("Add"), tr("Adds the selected parameters on the lowest free DMX slots (double-click does the same). "
                                               "Clear a channel's DMX slot to make it virtual."), libBox);
    auto *addGroup = smallButton(tr("Add a group…"), tr("RGB, RGBW, CMY, pan and tilt, a spot's or a wash's basics…"), libBox);
    addGroup->setPopupMode(QToolButton::InstantPopup);
    auto *groups = new QMenu(addGroup);
    for (const ParameterBundle &b : parameterBundles()) {
        const QStringList attrs = b.attributes;
        groups->addAction(b.name, this, [this, attrs]() { addParameters(attrs); });
    }
    addGroup->setMenu(groups);
    addParam->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    lb->addWidget(addParam, 0, 0);
    addGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    lb->addWidget(addGroup, 0, 1);
    lv->addLayout(lb);
    auto selectedParams = [this]() {
        QStringList attrs;
        for (QTreeWidgetItem *it : m_library->selectedItems())
            if (!it->data(0, Qt::UserRole).toString().isEmpty())
                attrs << it->data(0, Qt::UserRole).toString();
        return attrs;
    };
    connect(search, &QLineEdit::textChanged, this, &FixtureEditorDialog::fillLibrary);
    connect(m_library, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
        const QString a = it->data(0, Qt::UserRole).toString();
        if (!a.isEmpty())
            addParameters({a});
    });
    connect(addParam, &QToolButton::clicked, this, [this, selectedParams]() { addParameters(selectedParams()); });

    // Channels
    auto *chanBox = new QWidget(split);
    auto *cv = new QVBoxLayout(chanBox);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->addWidget(new QLabel(tr("Channels  ·  Default, Highlight, Min and Max in the channel's own values  ·  no DMX slot: virtual"),
                             chanBox));
    m_channels = makeTable26(chanBox, {tr("DMX"), tr("Fine"), tr("Cell"), tr("Parameter"), tr("Name"), tr("Default"), tr("Highlight"),
                                       tr("Min"), tr("Max"), tr("DMX from"), tr("DMX to"), tr("Ranges")});
    m_channels->setSelectionMode(QAbstractItemView::ExtendedSelection);   // several: the channels of a pixel
    m_channels->setItemDelegateForColumn(ColAttr, new AttributeDelegate(m_channels));
    m_channels->setColumnWidth(ColSlot, 48);
    m_channels->setColumnWidth(ColFine, 48);
    m_channels->setColumnWidth(ColCell, 56);
    for (int col : {ColMin, ColMax})
        m_channels->setColumnWidth(col, 58);
    for (int col : {ColDmxFrom, ColDmxTo})
        m_channels->setColumnWidth(col, 70);
    m_channels->horizontalHeaderItem(ColMin)->setToolTip(
        tr("The value at the DMX from end: 0 for a dimmer (0 %), -270 for a 540° pan. Pan and tilt are in degrees, 0 the "
           "middle. Make Min bigger than Max when the channel runs the other way."));
    m_channels->horizontalHeaderItem(ColMax)->setToolTip(tr("The value at the DMX to end: 100 for a dimmer (100 %), 270 for a 540° pan."));
    m_channels->horizontalHeaderItem(ColDmxFrom)->setToolTip(
        tr("The DMX range the values cover: 0–255, or 0–65535 for 16-bit channels. Narrow it when only part of the channel "
           "does this (a dimmer on 0–200, say)."));
    m_channels->horizontalHeaderItem(ColHighlight)->setToolTip(
        tr("Where Highlight puts this channel. Empty: Highlight leaves it alone."));
    m_channels->horizontalHeaderItem(ColCell)->setToolTip(
        tr("Subfixtures: which one the channel belongs to. Empty: the whole fixture (its master dimmer, strobe…). "
           "3: subfixture 3 (a pixel or a head). 3.2: the second part of head 3 (its ring, its beam)."));
    m_channels->setColumnWidth(ColAttr, 170);
    m_channels->setColumnWidth(ColName, 120);
    m_channels->setColumnWidth(ColDefault, 64);
    m_channels->setColumnWidth(ColHighlight, 70);
    // Right-click with several channels selected: one value for all of them (the cell of R, G
    // and B at once, say). DMX slots and parameters stay one by one.
    enableRightClickEdit(m_channels, [this](int row, int col, const QPoint &global) {
        if (col == ColSlot || col == ColFine || col == ColAttr || col == ColRanges)
            return false;
        QVector<int> rows;
        for (const QModelIndex &i : m_channels->selectionModel()->selectedRows())
            rows << i.row();
        if (rows.size() < 2 || !rows.contains(row))
            return false;
        std::sort(rows.begin(), rows.end());
        const QString header = m_channels->horizontalHeaderItem(col)->text();
        const QString current = m_channels->item(row, col) ? m_channels->item(row, col)->text() : QString();
        auto *ed = new QuickEditor(tr("%1 for %n channels", nullptr, int(rows.size())).arg(header), this);
        auto apply = [this, rows, col](const QString &text) {
            m_loading = true;
            for (int r : rows)
                if (QTableWidgetItem *it = m_channels->item(r, col))
                    it->setText(text.trimmed());
            m_loading = false;
            readChannels();
            loadChannels();   // shows the values as stored (units, rounding)
            QItemSelection sel;
            for (int r : rows)
                sel.select(m_channels->model()->index(r, 0), m_channels->model()->index(r, m_channels->columnCount() - 1));
            m_channels->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
            validate();
            QTimer::singleShot(0, this, &FixtureEditorDialog::ensureVirtualDimmer);
        };
        ed->addNameField(header, current, apply, true);
        ed->popup(global);
        return true;
    });
    cv->addWidget(m_channels, 1);
    auto *cb = new QHBoxLayout;
    auto *addChan = smallButton(tr("Custom channel"), tr("Adds a channel of your own after the selected one"), chanBox);
    auto *toggleVirtual = smallButton(tr("Virtual / DMX"), tr("Makes the selected channel virtual (no DMX slot), or gives a virtual one the next free slot"),
                                      chanBox);
    auto *delChan = smallButton(tr("Remove"), QString(), chanBox);
    auto *up = smallButton(QStringLiteral("↑"), tr("Move up"), chanBox);
    auto *down = smallButton(QStringLiteral("↓"), tr("Move down"), chanBox);
    auto *renumber = smallButton(tr("Number in order"), tr("DMX slots in list order, fine channels right after their coarse one"), chanBox);
    auto *cells = smallButton(tr("Cells…"), tr("Pixel fixtures: select one pixel's channels (or keep cell 1 as it is) and repeat them as N cells. "
                                                "The rest belong to the whole fixture."),
                              chanBox);
    for (QToolButton *b : {addChan, toggleVirtual, delChan, up, down, renumber, cells})
        cb->addWidget(b);
    cb->addStretch(1);
    cv->addLayout(cb);

    // Ranges
    auto *rangeBox = new QWidget(split);
    auto *rv = new QVBoxLayout(rangeBox);
    rv->setContentsMargins(0, 0, 0, 0);
    m_rangeTitle = new QLabel(rangeBox);
    rv->addWidget(m_rangeTitle);
    m_ranges = makeTable26(rangeBox, {tr("From"), tr("To"), tr("Name")});
    m_ranges->setColumnWidth(ColFrom, 56);
    m_ranges->setColumnWidth(ColTo, 56);
    enableRightClickEdit(m_ranges);
    rv->addWidget(m_ranges, 1);
    auto *rb = new QHBoxLayout;
    auto *addRange = smallButton(tr("Add range"), tr("Adds a range after the last one"), rangeBox);
    auto *splitRange = smallButton(tr("Split into…"), tr("Replace the ranges with N equal steps (wheel slots)"), rangeBox);
    auto *delRange = smallButton(tr("Remove"), QString(), rangeBox);
    for (QToolButton *b : {addRange, splitRange, delRange})
        rb->addWidget(b);
    rb->addStretch(1);
    rv->addLayout(rb);

    split->addWidget(modeBox);
    split->addWidget(libBox);
    split->addWidget(chanBox);
    split->addWidget(rangeBox);
    modeBox->setMaximumWidth(240);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 3);
    split->setStretchFactor(3, 1);
    split->setSizes({150, 220, 680, 300});
    root->addWidget(split, 1);

    auto *bottom = new QHBoxLayout;
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setProperty("role", "muted");
    auto *cancel = new QPushButton(tr("Cancel"), this);
    m_save = new QPushButton(m_isNew ? tr("Create fixture type") : tr("Save fixture type"), this);
    m_save->setProperty("role", "primary");
    bottom->addWidget(m_status, 1);
    bottom->addWidget(cancel);
    bottom->addWidget(m_save);
    root->addLayout(bottom);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_save, &QPushButton::clicked, this, &FixtureEditorDialog::save);

    // ---- modes
    connect(m_modes, &QListWidget::currentRowChanged, this, [this]() {
        if (!m_loading)
            modeChanged();
    });
    connect(addMode, &QToolButton::clicked, this, [this]() {
        readChannels();
        FixtureMode m;
        m.name = tr("Mode %1").arg(m_type.modes.size() + 1);
        m_type.modes << m;
        loadModes();
        m_modes->setCurrentRow(int(m_type.modes.size()) - 1);
    });
    connect(dupMode, &QToolButton::clicked, this, [this]() {
        readChannels();
        if (!mode())
            return;
        FixtureMode m = *mode();
        m.name = tr("%1 copy").arg(m.name);
        m_type.modes << m;
        loadModes();
        m_modes->setCurrentRow(int(m_type.modes.size()) - 1);
    });
    connect(renMode, &QToolButton::clicked, this, [this]() {
        if (!mode())
            return;
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("Rename mode"), tr("Name"), QLineEdit::Normal, mode()->name, &ok);
        if (ok && !n.trimmed().isEmpty()) {
            mode()->name = n.trimmed();
            m_modes->currentItem()->setText(mode()->name);
            validate();
        }
    });
    connect(delMode, &QToolButton::clicked, this, [this]() {
        if (m_type.modes.size() <= 1 || m_mode < 0)
            return;
        m_type.modes.removeAt(m_mode);
        m_mode = -1;
        loadModes();
        m_modes->setCurrentRow(0);
    });

    // ---- channels
    connect(m_channels, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (!m_loading)
            channelChanged();
    });
    connect(m_channels, &QTableWidget::itemChanged, this, [this]() {
        if (m_loading)
            return;
        readChannels();
        validate();
        // A new color channel may need a virtual dimmer (or a real one replace it): after the edit.
        QTimer::singleShot(0, this, &FixtureEditorDialog::ensureVirtualDimmer);
    });
    connect(toggleVirtual, &QToolButton::clicked, this, [this]() {
        readChannels();
        FixtureMode *m = mode();
        ChannelDef *c = channel();
        if (!m || !c)
            return;
        if (c->isVirtual()) {
            c->coarse = nextFreeSlot(*m);
            if (c->attribute == QLatin1String("Dimmer"))
                m->noAutoDimmer = false;
        } else {
            c->coarse = -1;
            c->fine = -1;
        }
        const int keep = m_channel;
        loadChannels();
        m_channels->selectRow(keep);
        ensureVirtualDimmer();
    });
    connect(addChan, &QToolButton::clicked, this, [this]() {
        readChannels();
        FixtureMode *m = mode();
        if (!m)
            return;
        const int slot = nextFreeSlot(*m);
        ChannelDef d;
        d.attribute = QStringLiteral("Ctl_Channel%1").arg(slot + 1);
        for (int n = 2; m->channel(d.attribute); ++n)
            d.attribute = QStringLiteral("Ctl_Channel%1_%2").arg(slot + 1).arg(n);
        d.name = tr("Channel %1").arg(slot + 1);
        d.coarse = slot;
        d.highlightValue = 0;
        const int at = m_channel >= 0 ? m_channel + 1 : int(m->channels.size());
        m->channels.insert(at, d);
        loadChannels();
        m_channels->selectRow(at);
        m_channels->editItem(m_channels->item(at, ColAttr));
    });
    connect(delChan, &QToolButton::clicked, this, [this]() {
        readChannels();
        if (FixtureMode *m = mode(); m && m_channel >= 0 && m_channel < m->channels.size()) {
            if (m->channels[m_channel].attribute == QLatin1String("Dimmer") && m->channels[m_channel].isVirtual())
                m->noAutoDimmer = true;   // removed on purpose: adding other channels won't bring it back
            m->channels.removeAt(m_channel);
            const int keep = std::min(m_channel, int(m->channels.size()) - 1);
            loadChannels();
            if (keep >= 0)
                m_channels->selectRow(keep);
            ensureVirtualDimmer();
        }
    });
    auto move = [this](int dir) {
        readChannels();
        FixtureMode *m = mode();
        const int to = m_channel + dir;
        if (!m || m_channel < 0 || to < 0 || to >= m->channels.size())
            return;
        m->channels.swapItemsAt(m_channel, to);
        loadChannels();
        m_channels->selectRow(to);
    };
    connect(up, &QToolButton::clicked, this, [move]() { move(-1); });
    connect(down, &QToolButton::clicked, this, [move]() { move(1); });
    connect(renumber, &QToolButton::clicked, this, [this]() {
        readChannels();
        FixtureMode *m = mode();
        if (!m)
            return;
        int slot = 0;
        for (ChannelDef &c : m->channels) {
            if (c.isVirtual())
                continue;   // virtual channels stay off the wire
            c.coarse = slot++;
            if (c.fine >= 0)
                c.fine = slot++;
        }
        const int keep = m_channel;
        loadChannels();
        if (keep >= 0)
            m_channels->selectRow(keep);
    });

    connect(cells, &QToolButton::clicked, this, &FixtureEditorDialog::makeCells);

    // ---- ranges
    connect(m_ranges, &QTableWidget::itemChanged, this, [this]() {
        if (m_loading)
            return;
        readRanges();
        validate();
    });
    connect(addRange, &QToolButton::clicked, this, [this]() {
        ChannelDef *c = channel();
        if (!c)
            return;
        readRanges();
        const double from = c->sets.isEmpty() ? 0.0 : std::min(1.0, c->sets.last().to + 1.0 / c->dmxMax());
        c->sets.append({from, 1.0, tr("Range %1").arg(c->sets.size() + 1), QString()});
        loadRanges();
        m_ranges->selectRow(int(c->sets.size()) - 1);
        m_ranges->editItem(m_ranges->item(int(c->sets.size()) - 1, ColRangeName));
    });
    connect(splitRange, &QToolButton::clicked, this, [this]() {
        ChannelDef *c = channel();
        if (!c)
            return;
        bool ok = false;
        const int n = QInputDialog::getInt(this, tr("Split into ranges"), tr("How many equal ranges (e.g. wheel slots)?"), 8, 2, 64,
                                           1, &ok);
        if (!ok)
            return;
        c->sets.clear();
        for (int i = 0; i < n; ++i) {
            const int a = int(std::lround(i * 256.0 / n)), b = int(std::lround((i + 1) * 256.0 / n)) - 1;
            c->sets.append({a / 255.0, b / 255.0, tr("Slot %1").arg(i + 1), QString()});
        }
        loadRanges();
        validate();
    });
    connect(delRange, &QToolButton::clicked, this, [this]() {
        ChannelDef *c = channel();
        const int r = m_ranges->currentRow();
        if (!c || r < 0 || r >= c->sets.size())
            return;
        readRanges();
        c->sets.removeAt(r);
        loadRanges();
        validate();
    });
    connect(m_maker, &QLineEdit::textChanged, this, &FixtureEditorDialog::validate);
    connect(m_name, &QLineEdit::textChanged, this, &FixtureEditorDialog::validate);

    fillLibrary(QString());
    loadModes();
    m_modes->setCurrentRow(0);
}

void FixtureEditorDialog::fillLibrary(const QString &filter)
{
    m_library->clear();
    const QString f = filter.trimmed();
    for (Feature feature : allFeatures()) {
        QTreeWidgetItem *group = nullptr;
        for (const ParameterInfo &p : parameterLibrary()) {
            if (p.feature() != feature)
                continue;
            if (!f.isEmpty() && !p.label.contains(f, Qt::CaseInsensitive) && !p.attribute.contains(f, Qt::CaseInsensitive)
                && !p.description.contains(f, Qt::CaseInsensitive))
                continue;
            if (!group) {
                group = new QTreeWidgetItem(m_library, {featureName(feature)});
                group->setFlags(Qt::ItemIsEnabled);
                group->setForeground(0, theme::featureColor(feature));
                QFont bold = group->font(0);
                bold.setBold(true);
                group->setFont(0, bold);
            }
            auto *it = new QTreeWidgetItem(group, {p.label});
            it->setData(0, Qt::UserRole, p.attribute);
            it->setToolTip(0, QStringLiteral("%1\n%2%3").arg(p.description, p.attribute,
                                                              p.sets.isEmpty() ? QString() : tr("  ·  %1 named ranges").arg(p.sets.size())));
        }
    }
    m_library->expandAll();
}

void FixtureEditorDialog::addParameters(const QStringList &attributes)
{
    readChannels();
    FixtureMode *m = mode();
    if (!m || attributes.isEmpty())
        return;
    QStringList skipped;
    int at = m_channel >= 0 ? m_channel + 1 : int(m->channels.size());
    // Added to the cell of the selected channel (the master when none is selected).
    const int cell = m_channel >= 0 && m_channel < m->channels.size() ? m->channels[m_channel].cell : 0;
    const int sub = m_channel >= 0 && m_channel < m->channels.size() ? m->channels[m_channel].sub : 0;
    int last = -1;
    for (const QString &a : attributes) {
        const ParameterInfo *p = parameterInfo(a);
        if (!p)
            continue;
        if (ChannelDef *existing = [&]() -> ChannelDef * {
                for (ChannelDef &c : m->channels)
                    if (c.attribute == a && c.cell == cell && c.sub == sub)
                        return &c;
                return nullptr;
            }()) {
            // Adding Dimmer to an RGB fixture makes its virtual dimmer a real one. Other virtual
            // channels keep having no slot: that was chosen by hand.
            if (existing->isVirtual() && a == QLatin1String("Dimmer")) {
                existing->coarse = nextFreeSlot(*m, p->fine ? 2 : 1);
                if (p->fine)
                    existing->fine = existing->coarse + 1;
                last = int(existing - m->channels.data());
            } else {
                skipped << p->label;
            }
            continue;
        }
        if (a == QLatin1String("Dimmer"))
            m->noAutoDimmer = false;
        ChannelDef c = p->channel();
        c.cell = cell;
        c.sub = sub;
        c.coarse = nextFreeSlot(*m, p->fine ? 2 : 1);   // fills the gap a removed channel left
        if (p->fine)
            c.fine = c.coarse + 1;
        at = std::clamp(at, 0, int(m->channels.size()));
        m->channels.insert(at, c);
        last = at++;
    }
    const bool dimmerAdded = m->ensureVirtualDimmer();
    if (dimmerAdded && last >= 0)
        ++last;   // the virtual dimmer went in first
    loadChannels();
    if (last >= 0)
        m_channels->selectRow(std::min(last, m_channels->rowCount() - 1));
    if (!skipped.isEmpty())
        m_status->setText(tr("Already in this mode: %1").arg(skipped.join(", ")));
    else if (dimmerAdded && m->channel("Dimmer") && m->channel("Dimmer")->isVirtual())
        m_status->setText(tr("RGB without a dimmer: a virtual Dimmer was added. It dims the colors like a real one."));
}

void FixtureEditorDialog::makeCells()
{
    readChannels();
    FixtureMode *m = mode();
    if (!m)
        return;
    // The pixel to repeat: cell 1 when there is one, else the selected channels.
    QVector<ChannelDef> pixel;
    for (const ChannelDef &c : m->channels)
        if (c.cell == 1)
            pixel << c;
    if (pixel.isEmpty())
        for (const QModelIndex &i : m_channels->selectionModel()->selectedRows())
            if (i.row() < m->channels.size())
                pixel << m->channels[i.row()];
    std::sort(pixel.begin(), pixel.end(), [](const ChannelDef &a, const ChannelDef &b) {
        return (a.isVirtual() ? 1 << 20 : a.coarse) < (b.isVirtual() ? 1 << 20 : b.coarse);
    });
    if (pixel.isEmpty()) {
        m_status->setText(tr("Select one pixel's channels first (e.g. its red, green and blue), then Cells…"));
        return;
    }
    int current = 0;
    for (const ChannelDef &c : m->channels)
        current = std::max(current, c.cell);
    bool ok = false;
    const int n = QInputDialog::getInt(this, tr("Cells"), tr("How many cells (pixels)? The selected channels are cell 1; the others are "
                                                             "copied after the last DMX slot."),
                                       std::max(current, 8), 1, kCellsPerFixture - 1, 1, &ok);
    if (!ok)
        return;
    // Start again from the template: every other cell's channels go.
    QVector<ChannelDef> keep;
    for (const ChannelDef &c : m->channels) {
        bool isTemplate = false;
        for (const ChannelDef &p : pixel)
            isTemplate |= p.attribute == c.attribute && p.coarse == c.coarse && p.cell == c.cell && p.sub == c.sub;
        if (c.cell == 0 && !isTemplate)
            keep << c;
    }
    // A virtual dimmer the master no longer needs (its colors moved to the cells) goes too.
    const bool masterColors = std::any_of(keep.begin(), keep.end(), [](const ChannelDef &c) { return isColorAdd(c.attribute); });
    if (!masterColors)
        keep.erase(std::remove_if(keep.begin(), keep.end(),
                                  [](const ChannelDef &c) { return c.attribute == QLatin1String("Dimmer") && c.isVirtual(); }),
                   keep.end());
    pixel.erase(std::remove_if(pixel.begin(), pixel.end(),
                               [](const ChannelDef &c) { return c.attribute == QLatin1String("Dimmer") && c.isVirtual(); }),
                pixel.end());   // each cell gets its own again below
    int width = 0, end = 0;   // DMX slots one pixel takes, and where the fixture's slots end
    int first = 1 << 20;
    for (const ChannelDef &c : pixel)
        if (!c.isVirtual()) {
            first = std::min(first, c.coarse);
            width = std::max(width, std::max(c.coarse, c.fine) - first + 1);
        }
    for (const ChannelDef &c : keep)
        if (!c.isVirtual())
            end = std::max(end, std::max(c.coarse, c.fine) + 1);
    // Cell 1 keeps its slots; the copies follow on after everything else.
    const int next = std::max(end, first + width);
    for (int cell = 1; cell <= n; ++cell)
        for (ChannelDef c : pixel) {
            c.cell = cell;
            if (!c.isVirtual() && cell > 1) {
                const int shift = next + (cell - 2) * width - first;
                c.coarse += shift;
                if (c.fine >= 0)
                    c.fine += shift;
            }
            keep << c;
        }
    m->channels = keep;
    // The master's dimmer rests at full, so the cells light on their own.
    for (ChannelDef &c : m->channels)
        if (c.cell == 0 && c.attribute == QLatin1String("Dimmer") && !c.isVirtual())
            c.defaultValue = 1.0;
    m->ensureVirtualDimmer();
    loadChannels();
    m_status->setText(tr("%1 cells. Patched, the fixture is 101 (its own channels) with cells 101.1 to 101.%1.").arg(n));
}

void FixtureEditorDialog::ensureVirtualDimmer()
{
    FixtureMode *m = mode();
    if (!m || !m->ensureVirtualDimmer())
        return;
    const int keep = m_channel;
    loadChannels();
    if (keep >= 0)
        m_channels->selectRow(std::min(keep, m_channels->rowCount() - 1));
}

FixtureMode *FixtureEditorDialog::mode()
{
    return m_mode >= 0 && m_mode < m_type.modes.size() ? &m_type.modes[m_mode] : nullptr;
}

ChannelDef *FixtureEditorDialog::channel()
{
    FixtureMode *m = mode();
    return m && m_channel >= 0 && m_channel < m->channels.size() ? &m->channels[m_channel] : nullptr;
}

void FixtureEditorDialog::loadModes()
{
    m_loading = true;
    m_modes->clear();
    for (const FixtureMode &m : m_type.modes)
        m_modes->addItem(m.name);
    m_loading = false;
}

void FixtureEditorDialog::modeChanged()
{
    readChannels();
    m_mode = m_modes->currentRow();
    m_channel = -1;
    loadChannels();
    if (m_channels->rowCount())
        m_channels->selectRow(0);
    else
        channelChanged();
}

void FixtureEditorDialog::loadChannels()
{
    m_loading = true;
    FixtureMode *m = mode();
    m_channels->setRowCount(m ? m->channels.size() : 0);
    if (m) {
        for (int r = 0; r < m->channels.size(); ++r) {
            const ChannelDef &c = m->channels[r];
            auto set = [&](int col, const QString &text, bool editable = true) {
                auto *it = new QTableWidgetItem(text);
                if (!editable) {
                    it->setFlags(it->flags() & ~Qt::ItemIsEditable);
                    it->setForeground(theme::muted);
                }
                m_channels->setItem(r, col, it);
                return it;
            };
            QTableWidgetItem *slot = set(ColSlot, c.isVirtual() ? kVirtualText : QString::number(c.coarse + 1));
            if (c.isVirtual()) {
                QFont italic = slot->font();
                italic.setItalic(true);
                slot->setFont(italic);
                slot->setForeground(theme::muted);
                slot->setToolTip(c.attribute == QLatin1String("Dimmer")
                                     ? tr("Virtual dimmer: no DMX slot. It scales the color channels, so the fixture dims like one with a dimmer.")
                                     : tr("Virtual: no DMX slot. Programmable and stored in cues, never output. Type a slot to give it one."));
            }
            set(ColFine, c.fine >= 0 ? QString::number(c.fine + 1) : QString());
            set(ColCell, c.cell > 0 ? (c.sub > 0 ? QStringLiteral("%1.%2").arg(c.cell).arg(c.sub) : QString::number(c.cell)) : QString());
            QTableWidgetItem *attr = set(ColAttr, attrText(c.attribute));
            attr->setData(Qt::UserRole, c.attribute);
            attr->setForeground(theme::featureColor(featureOf(c.attribute)));
            set(ColName, c.name);
            auto value = [&](double v) { return QString::number(c.toPhysical(v), 'g', 6) + c.unit(); };
            set(ColDefault, value(c.defaultValue));
            set(ColHighlight, c.hasHighlight ? value(c.highlightValue) : QString());
            set(ColMin, QString::number(c.rangeFrom(), 'g', 6) + c.unit());
            set(ColMax, QString::number(c.rangeTo(), 'g', 6) + c.unit());
            set(ColDmxFrom, QString::number(toDmx(c.dmxFrom, c.dmxMax())));
            set(ColDmxTo, QString::number(toDmx(c.dmxTo, c.dmxMax())));
            set(ColRanges, c.sets.isEmpty() ? QString() : QString::number(c.sets.size()), false);
        }
    }
    m_loading = false;
    validate();
}

void FixtureEditorDialog::readChannels()
{
    FixtureMode *m = mode();
    if (!m || m_channels->rowCount() != m->channels.size())
        return;
    for (int r = 0; r < m->channels.size(); ++r) {
        ChannelDef &c = m->channels[r];
        auto text = [&](int col) { return m_channels->item(r, col) ? m_channels->item(r, col)->text().trimmed() : QString(); };
        bool ok = false;
        const QString slotText = text(ColSlot);
        const int slot = slotText.toInt(&ok);
        if (ok && slot >= 1)
            c.coarse = slot - 1;
        else if (slotText.isEmpty() || slotText == QLatin1String("-") || slotText.compare(kVirtualText, Qt::CaseInsensitive) == 0)
            c.coarse = -1;   // no slot: virtual
        const int fine = text(ColFine).toInt(&ok);
        c.fine = ok && fine >= 1 && !c.isVirtual() ? fine - 1 : -1;
        // "3" is subfixture 3; "3.2" the second part of it (a head's ring or beam).
        const QStringList cellParts = text(ColCell).split('.');
        const int cell = cellParts.value(0).toInt(&ok);
        c.cell = ok ? std::clamp(cell, 0, kCellsPerFixture - 1) : 0;
        c.sub = c.cell > 0 && cellParts.size() > 1 ? std::clamp(cellParts[1].toInt(), 0, 99) : 0;
        const QString a = m_channels->item(r, ColAttr) ? m_channels->item(r, ColAttr)->data(Qt::UserRole).toString() : QString();
        if (!a.isEmpty())
            c.attribute = a;
        c.name = text(ColName).isEmpty() ? attributeLabel(c.attribute) : text(ColName);
        // Value range, then DMX range (a fine channel added or removed rescales the numbers).
        double lo = c.rangeFrom(), hi = c.rangeTo();
        number(text(ColMin), &lo);
        number(text(ColMax), &hi);
        if (lo == hi) {
            c.physFrom = c.physTo = 0;   // back to the usual range
        } else if (!c.isAngle() && lo == 0 && hi == 100) {
            c.physFrom = c.physTo = 0;
        } else {
            c.physFrom = lo;
            c.physTo = hi;
        }
        const int max = c.dmxMax();
        c.dmxFrom = fromDmx(text(ColDmxFrom), c.dmxFrom, max);
        c.dmxTo = fromDmx(text(ColDmxTo), c.dmxTo, max);
        if (c.dmxFrom == c.dmxTo) {
            c.dmxFrom = 0.0;
            c.dmxTo = 1.0;
        }
        // Default and highlight, in the channel's values. An empty highlight means none; an
        // empty default rests at the low end of the range (0° for pan and tilt).
        auto stored = [&](double physical) {
            const double v = c.fromPhysical(physical);
            return std::clamp(v, c.minValue(), c.maxValue());
        };
        double p = 0;
        if (number(text(ColDefault), &p))
            c.defaultValue = stored(p);
        else if (text(ColDefault).isEmpty())
            c.defaultValue = c.isAngle() ? stored(0.0) : 0.0;
        if (text(ColHighlight).isEmpty())
            c.hasHighlight = false;
        else if (number(text(ColHighlight), &p)) {
            c.hasHighlight = true;
            c.highlightValue = stored(p);
        }
    }
}

void FixtureEditorDialog::channelChanged()
{
    readRanges();
    const QModelIndexList rows = m_channels->selectionModel()->selectedRows();
    m_channel = rows.isEmpty() ? -1 : rows.first().row();
    loadRanges();
}

void FixtureEditorDialog::loadRanges()
{
    m_loading = true;
    ChannelDef *c = channel();
    m_rangeTitle->setText(c ? tr("Ranges of %1  ·  named values (slots, colors, shutter modes)  ·  DMX 0–%2").arg(c->name).arg(c->dmxMax())
                            : tr("Ranges: pick a channel"));
    m_ranges->setRowCount(c ? c->sets.size() : 0);
    if (c)
        for (int r = 0; r < c->sets.size(); ++r) {
            const ChannelSet &s = c->sets[r];
            m_ranges->setItem(r, ColFrom, new QTableWidgetItem(QString::number(toDmx(s.from, c->dmxMax()))));
            m_ranges->setItem(r, ColTo, new QTableWidgetItem(QString::number(toDmx(s.to, c->dmxMax()))));
            auto *name = new QTableWidgetItem(s.name);
            if (!s.media.isEmpty())
                name->setToolTip(tr("Has an image: %1").arg(s.media));
            m_ranges->setItem(r, ColRangeName, name);
        }
    m_loading = false;
}

void FixtureEditorDialog::readRanges()
{
    ChannelDef *c = channel();
    if (!c || m_ranges->rowCount() != c->sets.size())
        return;
    for (int r = 0; r < c->sets.size(); ++r) {
        ChannelSet &s = c->sets[r];
        s.from = fromDmx(m_ranges->item(r, ColFrom)->text(), s.from, c->dmxMax());
        s.to = fromDmx(m_ranges->item(r, ColTo)->text(), s.to, c->dmxMax());
        if (s.to < s.from)
            std::swap(s.from, s.to);
        s.name = m_ranges->item(r, ColRangeName)->text().trimmed();
    }
    std::stable_sort(c->sets.begin(), c->sets.end(), [](const ChannelSet &a, const ChannelSet &b) { return a.from < b.from; });
    if (QTableWidgetItem *it = m_channels->item(m_channel, ColRanges)) {
        const bool was = m_loading;
        m_loading = true;
        it->setText(c->sets.isEmpty() ? QString() : QString::number(c->sets.size()));
        m_loading = was;
    }
}

void FixtureEditorDialog::validate()
{
    QStringList problems;
    if (m_maker->text().trimmed().isEmpty() || m_name->text().trimmed().isEmpty())
        problems << tr("Give it a manufacturer and a name.");
    for (const FixtureMode &m : m_type.modes) {
        if (m.channels.isEmpty()) {
            problems << tr("%1 has no channels.").arg(m.name);
            continue;
        }
        QSet<QString> attrs;
        QSet<int> usedSlots, cellsUsed;
        int footprint = 0, virtuals = 0;
        for (const ChannelDef &c : m.channels) {
            const QString cellName = c.sub ? QStringLiteral("%1.%2").arg(c.cell).arg(c.sub) : QString::number(c.cell);
            const QString key = QStringLiteral("%1|%2").arg(cellName, c.attribute);   // unique per subfixture
            if (attrs.contains(key))
                problems << (c.cell ? tr("%1: %2 is used twice in cell %3.").arg(m.name, c.attribute, cellName)
                                    : tr("%1: %2 is used twice (number the second, e.g. %2 2).").arg(m.name, c.attribute));
            attrs.insert(key);
            if (c.cell > 0)
                cellsUsed.insert(c.cell);
            virtuals += c.isVirtual() ? 1 : 0;
            for (int s : {c.coarse, c.fine}) {
                if (s < 0)
                    continue;
                if (usedSlots.contains(s))
                    problems << tr("%1: DMX slot %2 is used twice.").arg(m.name).arg(s + 1);
                usedSlots.insert(s);
                footprint = std::max(footprint, s + 1);
            }
        }
        if (&m == mode()) {
            QString text = virtuals ? tr("%1: %2, %3 virtual").arg(m.name, countOf(footprint, tr("DMX channel"), tr("DMX channels"))).arg(virtuals)
                                    : tr("%1: %2").arg(m.name, countOf(footprint, tr("DMX channel"), tr("DMX channels")));
            if (!cellsUsed.isEmpty())
                text += tr(", %1: patched as 101 with cells 101.1, 101.2…").arg(countOf(int(cellsUsed.size()), tr("cell"), tr("cells")));
            m_status->setText(text);
        }
    }
    if (!problems.isEmpty()) {
        problems.removeDuplicates();
        m_status->setText(problems.join(' '));
        m_status->setStyleSheet(QStringLiteral("color: %1;").arg(theme::programmer.name()));
    } else {
        m_status->setStyleSheet(QString());
    }
    m_save->setEnabled(problems.isEmpty());
}

void FixtureEditorDialog::save()
{
    readChannels();
    readRanges();
    validate();
    if (!m_save->isEnabled())
        return;
    m_type.manufacturer = m_maker->text().trimmed();
    m_type.name = m_name->text().trimmed();
    m_type.shortName = m_short->text().trimmed().isEmpty() ? m_type.name : m_short->text().trimmed();
    if (m_isNew || m_type.id.isEmpty()) {
        m_type.id = FixtureLibrary::makeTypeId(m_type.manufacturer, m_type.name);
        if (m_ctx->show->library().find(m_type.id))
            m_type.id += QStringLiteral("-custom");
    }
    if (m_type.source.isEmpty() || m_isNew)
        m_type.source = QStringLiteral("Lumora");
    // Exactly the defaults typed here: no automatic "open" rest values.
    m_type.openDefaults = true;
    for (FixtureMode &m : m_type.modes)
        m.footprint = 0;
    m_type.finalize();

    // The type is saved in the show (and with it, in the show file), not on this computer.
    m_ctx->show->updateFixtureType(m_type);
    emit m_ctx->status(m_isNew ? tr("%1 %2 created in this show: patch it from Add to patch").arg(m_type.manufacturer, m_type.name)
                               : tr("%1 %2 saved").arg(m_type.manufacturer, m_type.name));
    accept();
}

} // namespace lumora
