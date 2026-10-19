// SPDX-License-Identifier: GPL-3.0-or-later
#include "macrosui.h"

#include "appcontext.h"
#include "colorpicker.h"
#include "quickedit.h"
#include "shortcuts.h"
#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QKeyEvent>
#include <QStyledItemDelegate>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
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
    connect(show, &Show::changed, this, [this](Show::Parts p) {
        if (p & Show::MacrosPart)
            refresh();
    });
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

} // namespace lumora
