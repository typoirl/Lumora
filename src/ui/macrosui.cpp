// SPDX-License-Identifier: GPL-3.0-or-later
#include "macrosui.h"

#include "appcontext.h"
#include "macrolibrary.h"
#include "colorpicker.h"
#include "quickedit.h"
#include "shortcuts.h"
#include "theme.h"
#include "uiutil.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QKeyEvent>
#include <QStyledItemDelegate>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QSpinBox>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace lumora {

namespace {
enum MacroColumn { ColCommand, ColWait, ColTime, ColEnabled, ColExecute, ColInfo, MacroColumns };

// The Command cell edits like the command line: with shortcuts off, keywords complete as you
// type; with shortcuts on, the shortcut keys type their keywords (S Store, G Group, T Thru…),
// except inside quotes, where names are typed as they are.
class CommandCellDelegate : public QStyledItemDelegate {
public:
    CommandCellDelegate(AppContext *ctx, QObject *parent) : QStyledItemDelegate(parent), m_ctx(ctx) {}
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QWidget *w = QStyledItemDelegate::createEditor(parent, option, index);
        if (auto *line = qobject_cast<QLineEdit *>(w)) {
            if (!m_ctx->shortcutsEnabled()) {
                auto *completer = new QCompleter(CommandLine::keywords(), line);
                completer->setCaseSensitivity(Qt::CaseInsensitive);
                completer->setCompletionMode(QCompleter::InlineCompletion);
                line->setCompleter(completer);
            } else {
                line->installEventFilter(const_cast<CommandCellDelegate *>(this));
            }
            line->setPlaceholderText(m_ctx->shortcutsEnabled() ? tr("Shortcut keys type commands, as on the command line")
                                                               : tr("Type a command: keywords complete as you type"));
        }
        return w;
    }
    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        auto *line = qobject_cast<QLineEdit *>(obj);
        if (line && ev->type() == QEvent::KeyPress && m_ctx->shortcutsEnabled()) {
            auto *k = static_cast<QKeyEvent *>(ev);
            const QString before = line->text().left(line->cursorPosition());
            const bool inQuotes = before.count('"') % 2 == 1;
            const QString cmd = inQuotes ? QString() : commandForKey(k);
            // Editing keys keep their usual meaning in a cell.
            if (!cmd.isEmpty() && cmd != QLatin1String("Enter") && cmd != QLatin1String("Back") && cmd != QLatin1String("Clear")
                && cmd != QLatin1String("Delete") && !(cmd.size() == 1 && (cmd[0].isDigit() || cmd == "." || cmd == "+" || cmd == "-"))) {
                const QString after = line->text().mid(line->cursorPosition());
                QString typed = typeCommandKey(before, cmd);
                line->setText(typed + after.trimmed());
                line->setCursorPosition(int(typed.size()));
                return true;
            }
        }
        return QStyledItemDelegate::eventFilter(obj, ev);
    }

private:
    AppContext *m_ctx = nullptr;
};
}

// ===========================================================================
// Pool
// ===========================================================================

MacrosPanel::MacrosPanel(AppContext *ctx, QWidget *parent) : PoolPanel(tr("Macros"), parent), m_ctx(ctx)
{
    Show *show = ctx->show;
    connect(tiles(), &PoolTiles::tileClicked, this, [this, show](int id, bool occupied, Qt::KeyboardModifiers) {
        if (m_ctx->storeArmed()) {
            m_ctx->setStoreArmed(false);
            m_ctx->run(QStringLiteral("Store Macro %1").arg(id));
            if (!occupied)
                openMacroEditor(m_ctx, id, this);
            return;
        }
        if (m_ctx->tapTarget(QStringLiteral("Macro %1").arg(id), show->macro(id) ? show->macro(id)->name : QString()))
            return;
        if (!occupied) {
            emit m_ctx->status(tr("Empty macro slot: press Store and tap it, or right-click it, to make a macro"));
            return;
        }
        m_ctx->run(QStringLiteral("Go Macro %1").arg(id));
    });
    connect(tiles(), &PoolTiles::tileMenu, this, [this, show](int id, bool occupied, const QPoint &pos) {
        auto *ed = new QuickEditor(occupied ? show->macro(id)->displayName() : tr("Macro %1").arg(id), this);
        if (!occupied) {
            ed->addAction(tr("New macro…"), [this, id]() {
                m_ctx->run(QStringLiteral("Store Macro %1").arg(id));
                openMacroEditor(m_ctx, id, this);
            });
            ed->addAction(tr("From library…"), [this, id]() { MacroLibraryDialog::open(m_ctx, id, this); });
            ed->popup(pos);
            return;
        }
        const Macro *m = show->macro(id);
        ed->addNameField(tr("Name"), m->name, [this, id](const QString &n) {
            QString q = n.trimmed();
            q.replace('"', '\'');
            m_ctx->run(QStringLiteral("Label Macro %1 \"%2\"").arg(id).arg(q));
        }, true, tr("Macro %1").arg(id));
        ed->addSegmented(tr("Runs in the"), {tr("Foreground"), tr("Background")}, m->background ? 1 : 0, [show, id](int i) {
            if (const Macro *cur = show->macro(id)) {
                Macro x = *cur;
                x.background = i == 1;
                show->setMacro(x);
            }
        });
        ed->addSection(QString());
        ed->addAction(tr("Edit lines…"), [this, id]() { openMacroEditor(m_ctx, id, this); });
        ed->addAction(tr("Color…"), [this, show, id]() {
            const Macro *cur = show->macro(id);
            if (!cur)
                return;
            if (const auto c = pickAppearanceColor(this, tr("Color of %1").arg(cur->displayName()), cur->color))
                m_ctx->run(c->isEmpty() ? QStringLiteral("Appearance Macro %1 Off").arg(id)
                                        : QStringLiteral("Appearance Macro %1 \"%2\"").arg(id).arg(*c));
        });
        if (show->macroRun(id))
            ed->addAction(tr("Stop"), [this, id]() { m_ctx->run(QStringLiteral("Off Macro %1").arg(id)); });
        ed->addAction(tr("Delete macro"), [this, id]() { m_ctx->run(QStringLiteral("Delete Macro %1").arg(id)); }, QIcon(), true);
        ed->popup(pos);
    });
    onShowChanged(this, show, [this](Show::Parts p) {
        if (p & Show::MacrosPart)
            refresh();
    });
    // The library, from the pool's header (or right-click an empty slot → From library…).
    auto *lib = new QToolButton(this);
    lib->setText(tr("Library…"));
    lib->setToolTip(tr("Ready-made macros to import into the pool"));
    connect(lib, &QToolButton::clicked, this, [this]() { MacroLibraryDialog::open(m_ctx, 1, this); });
    headerLayout()->addWidget(lib);
    followStore(ctx);
    refresh();
}

void MacrosPanel::refresh()
{
    Show *show = m_ctx->show;
    QVector<PoolItem> items;
    for (const Macro &m : show->macros()) {
        PoolItem it;
        it.id = m.id;
        it.label = m.displayName();
        const MacroRun *run = show->macroRun(m.id);
        it.active = run != nullptr;
        if (!m.color.isEmpty()) {
            it.swatch = QColor(m.color);
            it.tag = QColor(m.color);
        }
        // Waiting for a tap: "Go"; running: the line it's on; background: "BG".
        if (run && run->waiting)
            it.badge = tr("Go");
        else if (run)
            it.badge = tr("▶ %1").arg(run->line);
        else if (m.background)
            it.badge = tr("BG");
        it.tooltip = tr("%1: %n line(s), %2. Tap to run%3; right-click to edit.", nullptr, int(m.lines.size()))
                         .arg(m.displayName(), m.background ? tr("background") : tr("foreground"),
                              run && run->waiting ? tr(" (it's waiting: tap to go on)") : QString());
        items << it;
    }
    tiles()->setItems(items);
}

// ===========================================================================
// Editor
// ===========================================================================

void openMacroEditor(AppContext *ctx, int id, QWidget *parent)
{
    if (!ctx->show->macro(id))
        return;
    auto *d = new MacroEditorDialog(ctx, id, parent);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->show();
}

MacroEditorDialog::MacroEditorDialog(AppContext *ctx, int id, QWidget *parent) : QDialog(parent), m_ctx(ctx)
{
    m_macro = *ctx->show->macro(id);
    setWindowTitle(tr("Macro %1").arg(id));
    resize(860, 460);
    auto *v = new QVBoxLayout(this);

    auto *top = new QHBoxLayout;
    m_name = new QLineEdit(m_macro.name, this);
    m_name->setPlaceholderText(tr("Macro %1").arg(id));
    m_mode = new QComboBox(this);
    m_mode->addItems({tr("Foreground: lines show on the command line"), tr("Background: out of sight")});
    m_mode->setCurrentIndex(m_macro.background ? 1 : 0);
    m_color = new QPushButton(tr("Color…"), this);
    auto paintColor = [this]() {
        m_color->setStyleSheet(m_macro.color.isEmpty() ? QString()
                                                       : QStringLiteral("border-left: 8px solid %1;").arg(m_macro.color));
    };
    paintColor();
    connect(m_color, &QPushButton::clicked, this, [this, paintColor]() {
        if (const auto c = pickAppearanceColor(this, tr("Macro color"), m_macro.color)) {
            m_macro.color = *c;
            paintColor();
        }
    });
    top->addWidget(new QLabel(tr("Name"), this));
    top->addWidget(m_name, 2);
    top->addWidget(m_mode, 2);
    top->addWidget(m_color);
    v->addLayout(top);

    m_lines = new QTableWidget(0, MacroColumns, this);
    m_lines->setHorizontalHeaderLabels({tr("Command"), tr("Then"), tr("Time"), tr("On"), tr("Run"), tr("Note")});
    m_lines->horizontalHeaderItem(ColWait)->setToolTip(tr("Follow: the next line after the time. Wait: stop until the macro is tapped again."));
    m_lines->horizontalHeaderItem(ColTime)->setToolTip(tr("Follow: seconds before the next line"));
    m_lines->horizontalHeaderItem(ColEnabled)->setToolTip(tr("Off: the line is skipped"));
    m_lines->horizontalHeaderItem(ColExecute)->setToolTip(
        tr("Off: the command is left on the command line for you to finish (like ending it with @). Background macros run it anyway."));
    m_lines->horizontalHeader()->setSectionResizeMode(ColCommand, QHeaderView::Stretch);
    m_lines->setColumnWidth(ColWait, 96);
    m_lines->setColumnWidth(ColTime, 84);
    m_lines->setColumnWidth(ColEnabled, 40);
    m_lines->setColumnWidth(ColExecute, 44);
    m_lines->setColumnWidth(ColInfo, 180);
    m_lines->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_lines->verticalHeader()->setDefaultSectionSize(30);
    m_lines->setItemDelegateForColumn(ColCommand, new CommandCellDelegate(ctx, m_lines));
    enableRightClickEdit(m_lines);
    v->addWidget(m_lines, 1);

    auto *hint = new QLabel(tr("Any command works, one per line. (Prompt) asks for text when it runs; @ at the end leaves the line "
                               "for you to finish, @ at the start adds what's on the command line in front. SetVar $name = 5 "
                               "sets a variable for later lines ($name). Go Macro on its own last line loops it."),
                            this);
    hint->setWordWrap(true);
    hint->setProperty("role", "muted");
    v->addWidget(hint);

    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton(tr("Add line"), this);
    auto *insert = new QPushButton(tr("Insert above"), this);
    auto *last = new QPushButton(tr("Add last command"), this);
    last->setToolTip(tr("The last command from the command history"));
    auto *remove = new QPushButton(tr("Remove"), this);
    auto *up = new QPushButton(QStringLiteral("↑"), this);
    auto *down = new QPushButton(QStringLiteral("↓"), this);
    for (QPushButton *b : {add, insert, last, remove, up, down})
        buttons->addWidget(b);
    buttons->addStretch(1);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->addWidget(box);
    v->addLayout(buttons);

    connect(add, &QPushButton::clicked, this, [this]() {
        addRow(m_lines->rowCount(), MacroLine());
        m_lines->setCurrentCell(m_lines->rowCount() - 1, ColCommand);
        m_lines->editItem(m_lines->item(m_lines->rowCount() - 1, ColCommand));
    });
    connect(insert, &QPushButton::clicked, this, [this]() {
        const int at = std::max(0, m_lines->currentRow());
        addRow(at, MacroLine());
        m_lines->setCurrentCell(at, ColCommand);
        m_lines->editItem(m_lines->item(at, ColCommand));
    });
    connect(last, &QPushButton::clicked, this, [this]() {
        const QVector<HistoryEntry> &h = m_ctx->history();
        for (int i = int(h.size()) - 1; i >= 0; --i)
            if (h[i].command && h[i].source.isEmpty()) {
                MacroLine l;
                l.command = h[i].text;
                addRow(m_lines->rowCount(), l);
                return;
            }
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        QList<int> rows;
        for (const QModelIndex &i : m_lines->selectionModel()->selectedRows())
            rows << i.row();
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int r : rows)
            m_lines->removeRow(r);
    });
    connect(up, &QPushButton::clicked, this, [this]() { move(-1); });
    connect(down, &QPushButton::clicked, this, [this]() { move(1); });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(box, &QDialogButtonBox::accepted, this, [this]() {
        const Macro m = collect();
        m_ctx->show->setMacro(m);
        emit m_ctx->status(tr("%1 saved: %n line(s)", nullptr, int(m.lines.size())).arg(m.displayName()));
        accept();
    });
    load();
}

void MacroEditorDialog::load()
{
    m_lines->setRowCount(0);
    for (const MacroLine &l : m_macro.lines)
        addRow(m_lines->rowCount(), l);
}

void MacroEditorDialog::addRow(int at, const MacroLine &line)
{
    m_lines->insertRow(at);
    m_lines->setItem(at, ColCommand, new QTableWidgetItem(line.command));
    auto *wait = new QComboBox(m_lines);
    wait->addItems({tr("Follow"), tr("Wait")});
    wait->setCurrentIndex(line.wait == MacroWait::Wait ? 1 : 0);
    m_lines->setCellWidget(at, ColWait, wait);
    auto *time = new QDoubleSpinBox(m_lines);
    time->setRange(0.0, 3600.0);
    time->setDecimals(2);
    time->setSingleStep(0.5);
    time->setSuffix(QStringLiteral(" s"));
    time->setValue(line.time);
    time->setEnabled(line.wait == MacroWait::Follow);
    connect(wait, &QComboBox::currentIndexChanged, time, [time](int i) { time->setEnabled(i == 0); });
    m_lines->setCellWidget(at, ColTime, time);
    auto check = [](bool on) {
        auto *it = new QTableWidgetItem;
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        return it;
    };
    m_lines->setItem(at, ColEnabled, check(line.enabled));
    m_lines->setItem(at, ColExecute, check(line.execute));
    auto *info = new QTableWidgetItem(line.info);
    info->setForeground(theme::muted);
    m_lines->setItem(at, ColInfo, info);
}

Macro MacroEditorDialog::collect() const
{
    Macro m = m_macro;
    m.name = m_name->text().trimmed();
    m.background = m_mode->currentIndex() == 1;
    m.lines.clear();
    for (int r = 0; r < m_lines->rowCount(); ++r) {
        MacroLine l;
        l.command = m_lines->item(r, ColCommand) ? m_lines->item(r, ColCommand)->text().trimmed() : QString();
        if (auto *w = qobject_cast<QComboBox *>(m_lines->cellWidget(r, ColWait)))
            l.wait = w->currentIndex() == 1 ? MacroWait::Wait : MacroWait::Follow;
        if (auto *t = qobject_cast<QDoubleSpinBox *>(m_lines->cellWidget(r, ColTime)))
            l.time = t->value();
        l.enabled = m_lines->item(r, ColEnabled)->checkState() == Qt::Checked;
        l.execute = m_lines->item(r, ColExecute)->checkState() == Qt::Checked;
        l.info = m_lines->item(r, ColInfo) ? m_lines->item(r, ColInfo)->text() : QString();
        if (!l.command.isEmpty() || !l.info.isEmpty())
            m.lines << l;
    }
    return m;
}

void MacroEditorDialog::move(int delta)
{
    const int r = m_lines->currentRow();
    const int to = r + delta;
    if (r < 0 || to < 0 || to >= m_lines->rowCount())
        return;
    Macro m = collect();
    // collect() drops blank rows; move within what's kept.
    if (r >= m.lines.size() || to >= m.lines.size())
        return;
    m.lines.swapItemsAt(r, to);
    m_macro.lines = m.lines;
    m_macro.name = m.name;
    m_macro.background = m.background;
    load();
    m_lines->selectRow(to);
}

// ===========================================================================
// Library
// ===========================================================================

void MacroLibraryDialog::open(AppContext *ctx, int fromSlot, QWidget *parent)
{
    auto *d = new MacroLibraryDialog(ctx, fromSlot, parent);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->show();
    d->raise();
    d->activateWindow();
}

MacroLibraryDialog::MacroLibraryDialog(AppContext *ctx, int fromSlot, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Macro library"));
    resize(900, 560);
    QFile f(QStringLiteral(":/macros/library.json"));
    const QVector<LibraryMacro> library = f.open(QIODevice::ReadOnly) ? parseMacroLibrary(f.readAll()) : QVector<LibraryMacro>();

    auto *v = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Tick the macros to import, or pick one. They go into free slots of the Macros pool, "
                                "where they edit like any other macro."), this);
    intro->setProperty("role", "muted");
    intro->setWordWrap(true);
    v->addWidget(intro);

    auto *split = new QSplitter(this);
    auto *tree = new QTreeWidget(split);
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(true);
    QHash<QString, QTreeWidgetItem *> cats;
    for (int i = 0; i < library.size(); ++i) {
        const LibraryMacro &lm = library[i];
        QTreeWidgetItem *&cat = cats[lm.category];
        if (!cat) {
            cat = new QTreeWidgetItem(tree, {lm.category});
            cat->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
            cat->setCheckState(0, Qt::Unchecked);
            QFont bold = cat->font(0);
            bold.setBold(true);
            cat->setFont(0, bold);
            cat->setExpanded(true);
        }
        auto *it = new QTreeWidgetItem(cat, {lm.macro.name});
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        it->setCheckState(0, Qt::Unchecked);
        it->setData(0, Qt::UserRole, i);
        QPixmap dot(10, 10);   // its tile color (a plain one when it has none)
        dot.fill(lm.macro.color.isEmpty() ? theme::faint : QColor(lm.macro.color));
        it->setIcon(0, QIcon(dot));
    }

    auto *detail = new QWidget(split);
    auto *dv = new QVBoxLayout(detail);
    dv->setContentsMargins(8, 0, 0, 0);
    auto *title = new QLabel(detail);
    title->setFont(theme::uiFont(14, QFont::DemiBold));
    auto *about = new QLabel(detail);
    about->setWordWrap(true);
    auto *mode = new QLabel(detail);
    mode->setProperty("role", "muted");
    auto *lines = makeTable(detail, {tr("Command"), tr("Then"), tr("Note")}, 26, QAbstractItemView::NoEditTriggers);
    dv->addWidget(title);
    dv->addWidget(about);
    dv->addWidget(mode);
    dv->addWidget(lines, 1);
    split->addWidget(tree);
    split->addWidget(detail);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    v->addWidget(split, 1);

    auto show = [=](QTreeWidgetItem *it) {
        const QVariant idx = it ? it->data(0, Qt::UserRole) : QVariant();
        if (!idx.isValid()) {
            title->setText(it ? it->text(0) : QString());
            about->setText(it ? tr("Tick the category to import all of it.") : QString());
            mode->clear();
            lines->setRowCount(0);
            return;
        }
        const LibraryMacro &lm = library[idx.toInt()];
        title->setText(lm.macro.name);
        about->setText(lm.description);
        mode->setText(lm.macro.background ? tr("Runs in the background") : tr("Runs in the foreground"));
        lines->setRowCount(int(lm.macro.lines.size()));
        for (int r = 0; r < lm.macro.lines.size(); ++r) {
            const MacroLine &l = lm.macro.lines[r];
            QString cmd = l.command;
            cmd.replace(QStringLiteral("{self}"), tr("(its own number)"));
            const QString then = l.wait == MacroWait::Wait ? tr("Wait") : l.time > 0 ? tr("Follow %1 s").arg(l.time) : tr("Follow");
            lines->setItem(r, 0, new QTableWidgetItem(cmd));
            lines->setItem(r, 1, new QTableWidgetItem(then));
            lines->setItem(r, 2, new QTableWidgetItem(l.info));
        }
        lines->resizeColumnToContents(0);
    };
    connect(tree, &QTreeWidget::currentItemChanged, this, [show](QTreeWidgetItem *cur) { show(cur); });

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(new QLabel(tr("From slot"), this));
    auto *slot = new QSpinBox(this);
    slot->setRange(1, 9999);
    slot->setValue(std::max(1, fromSlot));
    bottom->addWidget(slot);
    auto *note = new QLabel(this);
    note->setProperty("role", "muted");
    bottom->addWidget(note, 1);
    auto *import = new QPushButton(tr("Import"), this);
    import->setProperty("role", "primary");
    auto *close = new QPushButton(tr("Close"), this);
    bottom->addWidget(import);
    bottom->addWidget(close);
    v->addLayout(bottom);

    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(import, &QPushButton::clicked, this, [=]() {
        // The ticked macros, or the one picked when none are ticked.
        QVector<int> chosen;
        for (int c = 0; c < tree->topLevelItemCount(); ++c)
            for (int k = 0; k < tree->topLevelItem(c)->childCount(); ++k)
                if (QTreeWidgetItem *it = tree->topLevelItem(c)->child(k); it->checkState(0) == Qt::Checked)
                    chosen << it->data(0, Qt::UserRole).toInt();
        if (chosen.isEmpty() && tree->currentItem() && tree->currentItem()->data(0, Qt::UserRole).isValid())
            chosen << tree->currentItem()->data(0, Qt::UserRole).toInt();
        if (chosen.isEmpty()) {
            note->setText(tr("Tick or pick a macro first"));
            return;
        }
        Show *sh = ctx->show;
        int id = slot->value(), first = 0, last = 0;
        for (int i : chosen) {
            while (sh->macro(id))
                ++id;
            sh->setMacro(library[i].numbered(id));
            if (!first)
                first = id;
            last = id++;
        }
        const QString where = first == last ? tr("Macro %1").arg(first) : tr("Macros %1 thru %2").arg(first).arg(last);
        note->setText(tr("Imported: %1").arg(where));
        emit ctx->status(tr("Macro library: %1 imported").arg(where));
        slot->setValue(id);
    });
    if (tree->topLevelItemCount() && tree->topLevelItem(0)->childCount())
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
}

} // namespace lumora
