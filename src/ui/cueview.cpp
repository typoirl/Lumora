// SPDX-License-Identifier: GPL-3.0-or-later
#include "cueview.h"
#include "colorpicker.h"

#include "appcontext.h"
#include "fixturesheet.h"
#include "media.h"
#include "poolgrid.h"
#include "quickedit.h"
#include "theme.h"
#include "uiutil.h"

#include <QRegularExpression>
#include <QTimer>
#include <QHeaderView>
#include <QCheckBox>
#include <QMouseEvent>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QTabBar>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace lumora {

namespace {
enum CueColumn { CueNumber, CueName, CueFade, CueDelay, CueTriggerCol, CueWait, CueMacro, CueNotes, CueContents };
enum LineColumn { LineName, LineGroup, LinePresets, LineEvery, LineOffset, LineSpread, LineFade, LineDelay, LineFixtures };
QString presetList(Show *show, const QVector<PresetRef> &refs)
{
    QStringList out;
    for (const PresetRef &r : refs) {
        const Preset *p = show->preset(r.first, r.second);
        if (!p)
            out << QObject::tr("%1 (deleted)").arg(presetRef(r.first, r.second));
        else
            out << (p->name.isEmpty() ? presetRef(r.first, r.second) : QStringLiteral("%1 %2").arg(presetRef(r.first, r.second), p->name));
    }
    return out.join(QStringLiteral(" · "));
}

// Markers for references that no longer exist: red on the blueprint line, orange on its cue.
const QColor kBrokenLine(0xe5, 0x48, 0x4d);
const QColor kBrokenCue(0xf0, 0x8c, 0x3a);

QIcon dotIcon(const QColor &c)
{
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter g(&pm);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(Qt::NoPen);
    g.setBrush(c);
    g.drawEllipse(QRectF(1.5, 1.5, 9, 9));
    return QIcon(pm);
}

QIcon presetIcon(Show *show, const Preset &p)
{
    QPixmap pm(18, 18);
    pm.fill(Qt::transparent);
    QPainter g(&pm);
    g.setRenderHint(QPainter::Antialiasing);
    const QImage img = presetImage(show, p);
    if (!img.isNull()) {
        g.drawPixmap(0, 0, goboPixmap(img, 18));
    } else {
        QColor c = presetSwatch(p);
        if (!c.isValid())
            c = theme::presetTypeColor(p.type);
        g.setPen(Qt::NoPen);
        g.setBrush(c);
        g.drawRoundedRect(QRectF(2, 2, 14, 14), 3, 3);
    }
    return QIcon(pm);
}

} // namespace

// Picks the presets of a blueprint line: tabs for each preset pool plus an "All presets" tab.
// Checked presets are in the line, in the order you checked them.
// A combo box whose dropdown is something richer than a list.
class PopupCombo : public QComboBox {
public:
    using QComboBox::QComboBox;
    std::function<void()> onPopup;
    void showPopup() override
    {
        if (onPopup)
            onPopup();
    }
};

// Drops down under a blueprint line's preset cell, like the group dropdown: tabs per pool, tick
// what the line should apply. Changes apply when it closes (click away or press Done).
class PresetPickerDialog : public QFrame {
public:
    PresetPickerDialog(Show *show, const QVector<PresetRef> &current, QWidget *parent,
                       std::function<void(const QVector<PresetRef> &)> apply = {})
        : QFrame(parent, Qt::Popup), m_selected(current), m_original(current), m_apply(std::move(apply))
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setObjectName("presetDrop");
        setStyleSheet(QStringLiteral("QFrame#presetDrop { background: %1; border: 1px solid #4a5260; }").arg(theme::panel.name()));
        resize(620, 380);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(6, 6, 6, 6);
        v->setSpacing(4);
        m_tabs = new QTabBar(this);
        m_tabs->setExpanding(false);
        m_tabs->setDrawBase(false);
        m_tabs->addTab(tr("All presets"));
        m_tabs->setTabData(0, 0);
        for (int t = 1; t <= kPresetTypeAll; ++t) {
            const int i = m_tabs->addTab(presetTypeName(t));
            m_tabs->setTabData(i, t);
            m_tabs->setTabTextColor(i, theme::presetTypeColor(t));
        }
        v->addWidget(m_tabs);
        m_search = new QLineEdit(this);
        m_search->setPlaceholderText(tr("Search presets"));
        m_search->setClearButtonEnabled(true);
        v->addWidget(m_search);
        m_list = new QListWidget(this);
        m_list->setIconSize(QSize(18, 18));
        v->addWidget(m_list, 1);
        m_summary = new QLabel(this);
        m_summary->setProperty("role", "muted");
        m_summary->setWordWrap(true);
        v->addWidget(m_summary);
        auto *buttons = new QHBoxLayout;
        auto *clear = new QPushButton(tr("Clear"), this);
        auto *done = new QPushButton(tr("Done"), this);
        done->setProperty("role", "primary");
        buttons->addWidget(clear);
        buttons->addStretch(1);
        buttons->addWidget(done);
        v->addLayout(buttons);
        connect(done, &QPushButton::clicked, this, &QWidget::close);
        connect(clear, &QPushButton::clicked, this, [this]() {
            m_selected.clear();
            sync();
        });

        for (int t = 1; t <= kPresetTypeAll; ++t)
            for (const Preset &p : show->presets(t)) {
                auto *it = new QListWidgetItem(presetIcon(show, p), QStringLiteral("%1   %2").arg(presetRef(t, p.id), p.name), m_list);
                it->setData(Qt::UserRole, t);
                it->setData(Qt::UserRole + 1, p.id);
                it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            }
        if (m_list->count() == 0)
            new QListWidgetItem(tr("No presets yet: set values, press Store and tap a preset slot."), m_list);
        // Ticking the box toggles it; clicking the preset's name toggles it too. A click on the box
        // shows up as a change and then a click: the click mustn't toggle it back.
        connect(m_list, &QListWidget::itemPressed, this, [this](QListWidgetItem *) { m_toggledByBox = nullptr; });
        connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
            if (m_syncing)
                return;
            m_toggledByBox = it;
            const PresetRef ref{it->data(Qt::UserRole).toInt(), it->data(Qt::UserRole + 1).toInt()};
            m_selected.removeAll(ref);
            if (it->checkState() == Qt::Checked)
                m_selected.append(ref);
            sync();
        });
        connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
            if (m_toggledByBox == it) {   // the box took the click already
                m_toggledByBox = nullptr;
                return;
            }
            if (it->flags() & Qt::ItemIsUserCheckable)
                it->setCheckState(it->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            m_toggledByBox = nullptr;
        });
        connect(m_tabs, &QTabBar::currentChanged, this, [this]() { filter(); });
        connect(m_search, &QLineEdit::textChanged, this, [this]() { filter(); });
        // Open on the pool of the first preset already in the line.
        if (!current.isEmpty())
            m_tabs->setCurrentIndex(current.first().first);
        sync();
        filter();
    }
    QVector<PresetRef> selected() const { return m_selected; }
    void popupUnder(QWidget *anchor)
    {
        resize(std::max(width(), anchor->width()), height());
        move(anchor->mapToGlobal(QPoint(0, anchor->height())));
        show();
        m_search->setFocus();
    }

protected:
    void hideEvent(QHideEvent *ev) override
    {
        if (m_apply && m_selected != m_original)
            m_apply(m_selected);
        m_apply = nullptr;
        QFrame::hideEvent(ev);
    }

private:
    void filter()
    {
        const int type = m_tabs->tabData(m_tabs->currentIndex()).toInt();
        const QString q = m_search->text();
        for (int i = 0; i < m_list->count(); ++i) {
            QListWidgetItem *it = m_list->item(i);
            const int t = it->data(Qt::UserRole).toInt();
            it->setHidden((type && t != type) || (!q.isEmpty() && !it->text().contains(q, Qt::CaseInsensitive)));
        }
    }
    void sync()
    {
        m_syncing = true;
        for (int i = 0; i < m_list->count(); ++i) {
            QListWidgetItem *it = m_list->item(i);
            if (!(it->flags() & Qt::ItemIsUserCheckable))
                continue;
            const PresetRef ref{it->data(Qt::UserRole).toInt(), it->data(Qt::UserRole + 1).toInt()};
            it->setCheckState(m_selected.contains(ref) ? Qt::Checked : Qt::Unchecked);
        }
        m_syncing = false;
        QStringList names;
        for (const PresetRef &r : m_selected)
            names << presetRef(r.first, r.second);
        m_summary->setText(names.isEmpty() ? tr("No presets chosen")
                                           : tr("In this line, applied in this order: %1").arg(names.join(QStringLiteral(", "))));
    }
    QTabBar *m_tabs;
    QLineEdit *m_search;
    QListWidget *m_list;
    QLabel *m_summary;
    QVector<PresetRef> m_selected;
    QVector<PresetRef> m_original;
    std::function<void(const QVector<PresetRef> &)> m_apply;
    bool m_syncing = false;
    QListWidgetItem *m_toggledByBox = nullptr;
};

QWidget *makePresetPicker(Show *show, const QVector<QPair<int, int>> &current, QWidget *parent)
{
    return new PresetPickerDialog(show, current, parent);
}

CueView::CueView(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    // The sequence editor edits the selected sequence: pick one in a Sequences pool (or with
    // Sequence 2), the way the effect editor follows the effects pool.
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto *right = new QWidget(this);
    root->addWidget(right);
    auto *rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(8);

    auto *head = new QHBoxLayout;
    m_title = new QLabel(right);
    m_title->setProperty("role", "title");
    m_execLabel = new QLabel(right);
    m_execLabel->setProperty("role", "muted");
    m_tracking = new QCheckBox(tr("Tracking"), right);
    m_tracking->setToolTip(tr("Values carry forward into later cues until they're changed"));
    m_wrap = new QCheckBox(tr("Wrap to first cue"), right);
    m_offOverridden = new QCheckBox(tr("Off when overridden"), right);
    m_offOverridden->setToolTip(tr("Turns the sequence off once other sequences have taken over all of its values"));
    m_priority = new QComboBox(right);
    m_priority->addItems({tr("Low"), tr("LTP"), tr("HTP"), tr("Ultra")});
    m_priority->setToolTip(tr("Low: below everything, never turns other sequences off. LTP: latest takes precedence (the default, "
                              "intensity included). HTP: highest intensity wins among HTP sequences, above LTP. Ultra: above "
                              "everything, the programmer too."));
    head->addWidget(m_title);
    head->addSpacing(10);
    head->addWidget(m_execLabel);
    head->addStretch(1);
    head->addWidget(m_tracking);
    head->addSpacing(8);
    head->addWidget(m_wrap);
    head->addSpacing(8);
    head->addWidget(m_offOverridden);
    head->addSpacing(8);
    head->addWidget(m_priority);
    rv->addLayout(head);

    m_cues = makeTable(right, {tr("Cue"), tr("Name"), tr("Fade"), tr("Delay"), tr("Trigger"), tr("Wait"), tr("Macro"), tr("Notes"), tr("Contents")});
    m_cues->setSelectionMode(QAbstractItemView::ExtendedSelection);   // several cues change together
    m_cues->setColumnWidth(CueNumber, 60);
    m_cues->setColumnWidth(CueName, 200);
    m_cues->setColumnWidth(CueFade, 84);
    m_cues->setColumnWidth(CueDelay, 84);
    m_cues->setColumnWidth(CueTriggerCol, 84);
    m_cues->setColumnWidth(CueWait, 56);
    m_cues->setColumnWidth(CueMacro, 120);
    m_cues->setColumnWidth(CueNotes, 200);
    auto *times = new TimeRangeDelegate(m_cues);
    for (int c : {CueFade, CueDelay, CueWait})
        m_cues->setItemDelegateForColumn(c, times);
    m_cues->horizontalHeaderItem(CueTriggerCol)->setToolTip(
        tr("How the cue starts. Go: on a Go. Wait: its wait time after the cue before it started. Follow: as soon as the cue "
           "before it has finished. Click (or right-click) to choose; select several cues to change them together."));
    m_cues->horizontalHeaderItem(CueWait)->setToolTip(tr("Seconds, for a Wait trigger. Typing one makes the cue a Wait."));
    m_cues->horizontalHeaderItem(CueMacro)->setToolTip(tr("A macro that runs as the cue starts. Type its number (0 for none), or right-click to pick one."));
    // One column for the cue's time and its individual times: a cue whose values fade on
    // times of their own shows the range they span.
    m_cues->horizontalHeaderItem(CueFade)->setToolTip(
        tr("The cue's fade. A range (0 thru 5, or 0 thru 3 thru 0) gives its fixtures individual fades, spread in selection "
           "order (all of them when nothing is selected); a cue with individual fades shows the range they span. One number "
           "is the same fade for everything again. T types thru."));
    m_cues->horizontalHeaderItem(CueDelay)->setToolTip(tr("The cue's delay, or a range of individual delays, like Fade."));
    rv->addWidget(m_cues, 3);

    auto *cueButtons = new QHBoxLayout;
    auto *newCue = new QPushButton(tr("New cue"), right);
    newCue->setToolTip(tr("Adds a blank cue at the end of this sequence, ready for blueprint lines"));
    auto *storeCue = new QPushButton(tr("Store new cue"), right);
    storeCue->setToolTip(tr("Stores the programmer as the next cue in this sequence"));
    auto *update = new QPushButton(tr("Update cue"), right);
    update->setToolTip(tr("Merges the programmer into the selected cue"));
    auto *gotoCue = new QPushButton(tr("Go to cue"), right);
    auto *del = new QPushButton(tr("Delete cue"), right);
    auto *top = new QPushButton(tr("Top"), right);
    top->setToolTip(tr("Back to the first cue: the list jumps to the top, and the playback goes to cue 1 in its fade if it's on an executor"));
    cueButtons->addWidget(top);
    cueButtons->addWidget(newCue);
    cueButtons->addWidget(storeCue);
    cueButtons->addWidget(update);
    cueButtons->addWidget(gotoCue);
    cueButtons->addStretch(1);
    cueButtons->addWidget(del);
    rv->addLayout(cueButtons);

    rv->addSpacing(10);
    auto *recipeHead = new QHBoxLayout;
    m_blueprintTitle = new QLabel(tr("Blueprint"), right);
    m_blueprintTitle->setProperty("role", "title");
    recipeHead->addWidget(m_blueprintTitle);
    recipeHead->addStretch(1);
    m_addLine = new QPushButton(tr("Add line"), right);
    m_removeLine = new QPushButton(tr("Remove line"), right);
    m_build = new QPushButton(tr("Build into values"), right);
    m_build->setToolTip(tr("Turns the blueprint into regular cue values (keeps preset links)"));
    recipeHead->addWidget(m_addLine);
    recipeHead->addWidget(m_removeLine);
    recipeHead->addWidget(m_build);
    rv->addLayout(recipeHead);
    m_blueprintHint = new QLabel(tr("A blueprint line takes a group and applies presets (a Dimmer preset sets the level) when the cue plays. "
                                 "Change the group or a preset and the cue follows."),
                              right);
    m_blueprintHint->setProperty("role", "muted");
    m_blueprintHint->setWordWrap(true);
    rv->addWidget(m_blueprintHint);

    m_blueprints = makeTable(right, {tr("Name"), tr("Group"), tr("Presets"), tr("Every"), tr("Offset"), tr("Spread"), tr("Fade"),
                                     tr("Delay"), tr("Fixtures")});
    m_blueprints->setItemDelegateForColumn(LineFade, new TimeRangeDelegate(m_blueprints));
    m_blueprints->setItemDelegateForColumn(LineDelay, new TimeRangeDelegate(m_blueprints));
    m_blueprints->horizontalHeaderItem(LineFade)->setToolTip(
        tr("Individual fades across the line's fixtures, in the group's order (laid out by the spread when there is one): "
           "0 thru 3, or 0 thru 3 thru 0. T types thru. Empty: the cue's fade."));
    m_blueprints->horizontalHeaderItem(LineDelay)->setToolTip(tr("Individual delays across the line's fixtures, like Fade."));
    m_blueprints->setColumnWidth(0, 140);
    m_blueprints->setColumnWidth(1, 150);
    m_blueprints->setColumnWidth(2, 300);
    m_blueprints->setColumnWidth(3, 60);
    m_blueprints->setColumnWidth(4, 60);
    m_blueprints->setColumnWidth(5, 150);
    m_blueprints->setColumnWidth(LineFade, 96);
    m_blueprints->setColumnWidth(LineDelay, 96);
    rv->addWidget(m_blueprints, 2);


    Show *show = ctx->show;

    connect(m_tracking, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating)
            editSequence([on](Sequence &s) { s.tracking = on; });
    });
    connect(m_wrap, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating)
            editSequence([on](Sequence &s) { s.wrap = on; });
    });
    connect(m_offOverridden, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating)
            editSequence([on](Sequence &s) { s.offWhenOverridden = on; });
    });
    connect(m_priority, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!m_updating && i >= 0)
            editSequence([i](Sequence &s) { s.priority = static_cast<SeqPriority>(i); });
    });

    connect(m_cues, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_updating)
            return;
        const int row = item->row(), col = item->column();
        const QString text = item->text();
        // Times go to every selected cue when the edited one is among them.
        QVector<int> rows{row};
        if ((col == CueFade || col == CueDelay || col == CueWait)
            && m_cues->selectionModel()->isRowSelected(row, QModelIndex()))
            rows = selectedCueRows();
        const Sequence *cur = m_ctx->show->sequence(m_ctx->show->selectedSequence());
        bool timesOk = false;
        const QVector<double> times = parseTimes(text, &timesOk);
        // A range in Fade or Delay: individual times spread across the cue's fixtures.
        if (cur && (col == CueFade || col == CueDelay) && timesOk && times.size() > 1) {
            QStringList pts;
            for (double v : times)
                pts << QString::number(v);
            for (int r : rows)
                if (r < cur->cues.size())
                    m_ctx->run(QStringLiteral("Cue %1 Sequence %2 %3 %4").arg(cur->cues[r].displayNumber()).arg(cur->id)
                                   .arg(col == CueDelay ? QStringLiteral("Delay") : QStringLiteral("Fade"), pts.join(QStringLiteral(" Thru "))));
            QTimer::singleShot(0, this, &CueView::refreshCues);
            return;
        }
        editSequence([rows, row, col, text](Sequence &s) {
            bool ok = false;
            double v = text.trimmed().remove(QRegularExpression(QStringLiteral("\\s*s$"))).toDouble(&ok);
            for (int r : rows) {
                if (r == s.cues.size()) {   // the Off line: its fade, delay and wait
                    if (col == CueFade && ok)
                        s.offFade = std::max(0.0, v);
                    else if (col == CueDelay && ok)
                        s.offDelay = std::max(0.0, v);
                    else if (col == CueWait && ok) {
                        s.offWait = std::max(0.0, v);
                        s.offTrigger = CueTrigger::Wait;
                    }
                    continue;
                }
                if (r > s.cues.size())
                    continue;
                Cue &c = s.cues[r];
                // One time for the whole cue: its values' individual times go. Blueprint lines
                // keep theirs: a line's own Fade and Delay override the cue's.
                auto oneTime = [&c](bool delay) {
                    for (auto f = c.values.begin(); f != c.values.end(); ++f)
                        for (auto a = f->begin(); a != f->end(); ++a)
                            (delay ? a->delay : a->fade) = -1.0;
                };
                if (col == CueFade && ok) {
                    c.fade = std::max(0.0, v);
                    oneTime(false);
                } else if (col == CueDelay && ok) {
                    c.delay = std::max(0.0, v);
                    oneTime(true);
                }
                else if (col == CueWait && ok) {
                    c.wait = std::max(0.0, v);
                    c.trigger = CueTrigger::Wait;
                }
            }
            if (row >= s.cues.size())
                return;
            Cue &c = s.cues[row];
            if (col == CueName)
                c.name = text;
            else if (col == CueNotes)
                c.notes = text;
            else if (col == CueMacro)
                c.macro = text.trimmed().isEmpty() ? 0 : std::max(0, text.section(' ', 0, 0).toInt());
            else if (col == CueNumber && ok && s.indexOf(v) < 0) {
                Cue moved = c;
                moved.number = v;
                s.cues.removeAt(row);
                s.insertSorted(moved);
            }
        });
    });
    // The Trigger column is a dropdown: click or right-click it.
    m_cues->viewport()->installEventFilter(this);
    connect(m_cues, &QTableWidget::itemSelectionChanged, this, &CueView::refreshBlueprints);
    enableRightClickEdit(m_cues, [this](int row, int col, const QPoint &global) {
        if (col == CueMacro) {
            macroMenu(row, global);
            return true;
        }
        if (col != CueTriggerCol)
            return false;
        triggerMenu(row, global);
        return true;
    });
    enableRightClickEdit(m_blueprints);

    connect(storeCue, &QPushButton::clicked, this, [this, show]() {
        double n = 0;
        if (show->storeCue(show->selectedSequence(), -1, Show::StoreMode::Merge, &n)) {
            Cue c;
            c.number = n;
            emit m_ctx->status(tr("Cue %1 stored").arg(c.displayNumber()));
        }
    });
    connect(top, &QPushButton::clicked, this, [this, show]() {
        if (m_cues->rowCount() == 0)
            return;
        m_cues->selectRow(0);
        m_cues->scrollToTop();
        if (show->executorForSequence(show->selectedSequence()))
            m_ctx->run(QStringLiteral("Top"));
    });
    connect(newCue, &QPushButton::clicked, this, [this, show]() {
        double n = 0;
        if (!show->addEmptyCue(show->selectedSequence(), -1, &n))
            return;
        // Select it, so Add line goes straight into it.
        const Sequence *sq = show->sequence(show->selectedSequence());
        if (sq)
            m_cues->selectRow(sq->indexOf(n));
        Cue c;
        c.number = n;
        emit m_ctx->status(tr("Blank cue %1 added: give it blueprint lines").arg(c.displayNumber()));
    });
    connect(update, &QPushButton::clicked, this, [this, show]() {
        bool ok;
        double n = selectedCueNumber(&ok);
        if (ok && show->storeCue(show->selectedSequence(), n, Show::StoreMode::Merge))
            emit m_ctx->status(tr("Cue updated"));
    });
    connect(gotoCue, &QPushButton::clicked, this, [this, show]() {
        Executor *e = show->executorForSequence(show->selectedSequence());
        if (!e) {
            emit m_ctx->status(tr("Assign this sequence to an executor to play it"));
            return;
        }
        const Sequence *sq = show->sequence(show->selectedSequence());
        if (sq && m_cues->currentRow() == sq->cues.size()) {   // the Off line
            show->off(*e);
            return;
        }
        show->gotoCue(*e, m_cues->currentRow(), true);
    });
    connect(del, &QPushButton::clicked, this, [this, show]() {
        bool ok;
        double n = selectedCueNumber(&ok);
        if (ok)
            show->deleteCue(show->selectedSequence(), n);
    });

    connect(m_addLine, &QPushButton::clicked, this, [this, show]() {
        bool ok;
        double n = selectedCueNumber(&ok);
        if (!ok)
            return;
        if (show->groups().isEmpty()) {
            emit m_ctx->status(tr("Store a group first: select fixtures, press Store, then tap a group slot"));
            return;
        }
        BlueprintLine line;
        line.groupId = show->groups().firstKey();
        line.name = show->groups().first().name;
        if (const int full = show->dimmerPresetFor(1.0, false))   // starts at Full when there's a Full preset
            line.presets.append({int(Feature::Dimmer), full});
        show->addBlueprintLine(show->selectedSequence(), n, line);
    });
    connect(m_removeLine, &QPushButton::clicked, this, [this]() {
        bool ok;
        double n = selectedCueNumber(&ok);
        const int line = m_blueprints->currentRow();
        if (!ok || line < 0)
            return;
        editSequence([n, line](Sequence &s) {
            int idx = s.indexOf(n);
            if (idx >= 0 && line < s.cues[idx].blueprints.size())
                s.cues[idx].blueprints.removeAt(line);
        });
    });
    connect(m_build, &QPushButton::clicked, this, [this, show]() {
        bool ok;
        double n = selectedCueNumber(&ok);
        if (ok && show->buildCue(show->selectedSequence(), n))
            emit m_ctx->status(tr("Blueprint built into cue values"));
    });
    connect(m_blueprints, &QTableWidget::itemChanged, this, [this, show](QTableWidgetItem *item) {
        if (m_updating)
            return;
        bool ok;
        const double n = selectedCueNumber(&ok);
        if (!ok)
            return;
        const int row = item->row(), col = item->column();
        const QString text = item->text().trimmed();
        editSequence([=](Sequence &s) {
            int idx = s.indexOf(n);
            if (idx < 0 || row >= s.cues[idx].blueprints.size())
                return;
            BlueprintLine &l = s.cues[idx].blueprints[row];
            if (col == 0) {
                l.name = text;
            } else if (col == 3) {
                l.every = std::max(1, text.toInt());
            } else if (col == 4) {
                l.offset = std::max(0, text.toInt() - 1);
            } else if (col == LineFade || col == LineDelay) {
                bool good = false;
                const QVector<double> t = parseTimes(text, &good);
                if (good || t.isEmpty())
                    (col == LineFade ? l.fadeTimes : l.delayTimes) = t;
            }
        });
    });

    connect(show, &Show::changed, this, [this](Show::Parts parts) {
        if (parts & (Show::SequencesPart | Show::GroupsPart | Show::PresetsPart | Show::PatchPart | Show::EffectsPart
                     | Show::SpreadsPart))
            refreshCues();
        else if (parts & Show::ExecutorsPart)
            refreshCurrentCue();
    });
    refreshCues();
}

QJsonObject CueView::saveOptions() const { return {}; }
void CueView::loadOptions(const QJsonObject &) {}
void CueView::buildOptions(QuickEditor *) {}

void CueView::editSequence(const std::function<void(Sequence &)> &fn)
{
    Show *show = m_ctx->show;
    Sequence *s = show->sequenceMutable(show->selectedSequence());
    if (!s)
        return;
    Sequence copy = *s;
    fn(copy);
    show->setSequence(copy);
}

double CueView::selectedCueNumber(bool *ok) const
{
    const Sequence *s = m_ctx->show->sequence(m_ctx->show->selectedSequence());
    const int row = m_cues->currentRow();
    *ok = s && row >= 0 && row < s->cues.size();
    return *ok ? s->cues[row].number : 0.0;
}

void editAppearance(QWidget *parent, Show *show, int sequenceId)
{
    const Sequence *s0 = show->sequence(sequenceId);
    if (!s0)
        return;
    const QString title = QObject::tr("Appearance · %1").arg(s0->name.isEmpty() ? QObject::tr("Sequence %1").arg(sequenceId) : s0->name);
    if (const auto c = pickAppearanceColor(parent, title, s0->color)) {
        if (const Sequence *s1 = show->sequence(sequenceId)) {
            Sequence s = *s1;
            s.color = *c;
            show->setSequence(s);
        }
    }
}

// ===========================================================================
// Sequence pool
// ===========================================================================

SequencePool::SequencePool(AppContext *ctx, QWidget *parent) : PoolPanel(tr("Sequences"), parent), m_ctx(ctx)
{
    setWindowTitle(tr("Sequences"));
    Show *show = ctx->show;
    // Pool interactions
    connect(tiles(), &PoolTiles::tileClicked, this, [this, show](int id, bool occupied, Qt::KeyboardModifiers) {
        if (m_ctx->storeArmed()) {
            double n = 0;
            if (show->storeCue(id, -1, Show::StoreMode::Merge, &n))
                emit m_ctx->status(tr("Stored the next cue in Sequence %1").arg(id));
            m_ctx->setStoreArmed(false);
            show->setSelectedSequence(id);
            return;
        }
        if (m_ctx->tapTarget(QStringLiteral("Sequence %1").arg(id), show->sequence(id) ? show->sequence(id)->name : QString()))
            return;
        if (!occupied)
            show->ensureSequence(id);
        show->setSelectedSequence(id);
    });
    connect(tiles(), &PoolTiles::tileMenu, this, [this, show](int id, bool occupied, const QPoint &pos) {
        if (!occupied)
            return;
        auto *ed = new QuickEditor(tr("Sequence %1").arg(id), this);
        ed->addNameField(tr("Name"), show->sequence(id)->name, [show, id](const QString &n) {
            if (const Sequence *s0 = show->sequence(id)) {
                Sequence s = *s0;
                s.name = n;
                show->setSequence(s);
            }
        });
        const Sequence *sq = show->sequence(id);
        ed->addSegmented(tr("Priority"), {tr("Low"), tr("LTP"), tr("HTP"), tr("Ultra")}, int(sq->priority), [show, id](int i) {
            if (const Sequence *s0 = show->sequence(id)) {
                Sequence s = *s0;
                s.priority = static_cast<SeqPriority>(i);
                show->setSequence(s);
            }
        });
        ed->addSegmented(tr("Off when overridden"), {tr("No"), tr("Yes")}, sq->offWhenOverridden ? 1 : 0, [show, id](int i) {
            if (const Sequence *s0 = show->sequence(id)) {
                Sequence s = *s0;
                s.offWhenOverridden = i == 1;
                show->setSequence(s);
            }
        });
        ed->addAction(tr("Appearance…"), [this, show, id]() { editAppearance(this, show, id); });
        ed->addSection(QString());
        ed->addAction(tr("Assign to executor…"), [this, show, id]() {
            QMenu menu(this);
            for (int n = 201; n <= 200 + m_ctx->executorCount(); ++n)
                menu.addAction(tr("Executor %1.%2").arg(show->currentPage()).arg(n), this,
                               [=]() { show->assignSequence(id, show->currentPage(), n); });
            menu.exec(QCursor::pos());
        });
        ed->addAction(tr("Delete sequence"), [show, id]() { show->deleteSequence(id); }, QIcon(), true);
        ed->popup(pos);
    });

    connect(show, &Show::changed, this, [this](Show::Parts parts) {
        if (parts & (Show::SequencesPart | Show::ExecutorsPart))
            refresh();
    });
    followStore(ctx);
    refresh();
}

void SequencePool::refresh()
{
    Show *show = m_ctx->show;
    QVector<PoolItem> items;
    for (const Sequence &s : show->sequences()) {
        PoolItem it;
        it.id = s.id;
        it.label = s.name;
        it.active = s.id == show->selectedSequence();
        Executor *e = show->executorForSequence(s.id);
        it.tag = e && e->pb().isOn() ? theme::live : QColor(s.color);
        if (!s.color.isEmpty())
            it.swatch = QColor(s.color);
        if (s.priority != SeqPriority::LTP)
            it.badge = s.priority == SeqPriority::Ultra ? QStringLiteral("U") : seqPriorityName(s.priority);
        items << it;
    }
    tiles()->setItems(items);
}

void CueView::refreshCurrentCue()
{
    Show *show = m_ctx->show;
    const Sequence *s = show->sequence(show->selectedSequence());
    Executor *e = show->executorForSequence(show->selectedSequence());
    const int current = e && e->pb().isOn() ? e->pb().cueIndex : -1;
    m_updating = true;
    for (int r = 0; r < m_cues->rowCount(); ++r)
        for (int c = 0; c < m_cues->columnCount(); ++c)
            if (QTableWidgetItem *it = m_cues->item(r, c); it && !it->data(Qt::UserRole).toBool()) {
                it->setForeground(r == current ? theme::live : (c == CueNotes ? theme::muted : theme::text));
                QFont f = it->font();
                f.setWeight(r == current ? QFont::DemiBold : QFont::Normal);
                it->setFont(f);
            }
    m_execLabel->setText(e ? tr("on Executor %1.%2").arg(e->page).arg(e->number)
                           : (s ? tr("not on an executor (right-click a fader to assign)") : QString()));
    m_updating = false;
}

QVector<int> CueView::selectedCueRows() const
{
    QVector<int> rows;
    for (const QModelIndex &i : m_cues->selectionModel()->selectedRows())
        rows << i.row();
    std::sort(rows.begin(), rows.end());
    return rows;
}

void CueView::triggerMenu(int row, const QPoint &globalPos)
{
    const Sequence *s = m_ctx->show->sequence(m_ctx->show->selectedSequence());
    if (!s)
        return;
    if (!m_cues->selectionModel()->isRowSelected(row, QModelIndex()))
        m_cues->selectRow(row);
    const QVector<int> rows = selectedCueRows();
    auto triggerOf = [s](int r) { return r < s->cues.size() ? s->cues[r].trigger : s->offTrigger; };
    auto waitOf = [s](int r) { return r < s->cues.size() ? s->cues[r].wait : s->offWait; };
    QMenu menu(this);
    menu.addSection(rows.size() > 1 ? tr("%1 cues").arg(rows.size()) : tr("Trigger"));
    for (CueTrigger t : {CueTrigger::Go, CueTrigger::Wait, CueTrigger::Follow}) {
        QAction *a = menu.addAction(t == CueTrigger::Wait ? tr("Wait…") : cueTriggerName(t));
        a->setCheckable(true);
        a->setChecked(std::all_of(rows.begin(), rows.end(), [&](int r) { return triggerOf(r) == t; }));
        a->setData(int(t));
    }
    QAction *chosen = menu.exec(globalPos);
    if (!chosen)
        return;
    const auto t = static_cast<CueTrigger>(chosen->data().toInt());
    double wait = waitOf(rows.first());
    if (t == CueTrigger::Wait) {
        bool ok = false;
        wait = QInputDialog::getDouble(this, tr("Wait"), tr("Seconds after the cue before it starts"), wait > 0 ? wait : 1.0, 0.0,
                                       3600.0, 2, &ok);
        if (!ok)
            return;
    }
    editSequence([rows, t, wait](Sequence &seq) {
        for (int r : rows) {
            if (r < seq.cues.size()) {
                seq.cues[r].trigger = t;
                if (t == CueTrigger::Wait)
                    seq.cues[r].wait = wait;
            } else if (r == seq.cues.size()) {
                seq.offTrigger = t;
                if (t == CueTrigger::Wait)
                    seq.offWait = wait;
            }
        }
    });
}

void CueView::macroMenu(int row, const QPoint &globalPos)
{
    const Sequence *s = m_ctx->show->sequence(m_ctx->show->selectedSequence());
    if (!s || row >= s->cues.size())
        return;
    if (!m_cues->selectionModel()->isRowSelected(row, QModelIndex()))
        m_cues->selectRow(row);
    const QVector<int> rows = selectedCueRows();
    QMenu menu(this);
    menu.addSection(rows.size() > 1 ? tr("Macro for %1 cues").arg(rows.size()) : tr("Macro as the cue starts"));
    QAction *none = menu.addAction(tr("None"));
    none->setData(0);
    menu.addSeparator();
    for (const Macro &m : m_ctx->show->macros()) {
        QAction *a = menu.addAction(QStringLiteral("%1  %2").arg(m.id).arg(m.displayName()));
        a->setData(m.id);
        a->setCheckable(true);
        a->setChecked(std::all_of(rows.begin(), rows.end(), [&](int r) { return r < s->cues.size() && s->cues[r].macro == m.id; }));
    }
    if (m_ctx->show->macros().isEmpty())
        menu.addAction(tr("No macros yet: make one in the Macros pool"))->setEnabled(false);
    QAction *chosen = menu.exec(globalPos);
    if (!chosen)
        return;
    const int id = chosen->data().toInt();
    editSequence([rows, id](Sequence &seq) {
        for (int r : rows)
            if (r < seq.cues.size())
                seq.cues[r].macro = id;
    });
}

bool CueView::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_cues->viewport() && ev->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(ev);
        const QModelIndex i = m_cues->indexAt(me->pos());
        if (me->button() == Qt::LeftButton && i.isValid() && i.column() == CueTriggerCol && !(me->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))) {
            triggerMenu(i.row(), m_cues->viewport()->mapToGlobal(me->pos()));
            return true;   // keeps a multi-cue selection
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void CueView::refreshCues()
{
    Show *show = m_ctx->show;
    const Sequence *s = show->sequence(show->selectedSequence());
    m_updating = true;
    const int keepRow = m_cues->currentRow();
    m_title->setText(s ? QStringLiteral("%1  %2").arg(s->id).arg(s->name) : tr("No sequence selected"));
    setWindowTitle(s ? tr("Sequence editor · %1").arg(s->name.isEmpty() ? tr("Sequence %1").arg(s->id) : s->name) : tr("Sequence editor"));
    m_tracking->setEnabled(s);
    m_wrap->setEnabled(s);
    m_tracking->setChecked(s ? s->tracking : true);
    m_wrap->setChecked(s ? s->wrap : true);
    m_offOverridden->setEnabled(s);
    m_priority->setEnabled(s);
    m_offOverridden->setChecked(s ? s->offWhenOverridden : true);
    m_priority->setCurrentIndex(s ? int(s->priority) : 1);
    m_cues->setRowCount(s ? s->cues.size() + 1 : 0);   // and the Off line
    if (s) {
        for (int r = 0; r < s->cues.size(); ++r) {
            const Cue &c = s->cues[r];
            QStringList content;
            if (!c.blueprints.isEmpty())
                content << countOf(int(c.blueprints.size()), tr("blueprint line"), tr("blueprint lines"));
            if (!c.values.isEmpty())
                content << countOf(int(c.values.size()), tr("fixture programmed"), tr("fixtures programmed"));
            auto set = [&](int col, const QString &text, bool editable = true) {
                auto *it = new QTableWidgetItem(text);
                if (!editable)
                    it->setFlags(it->flags() & ~Qt::ItemIsEditable);
                m_cues->setItem(r, col, it);
            };
            set(0, c.displayNumber());
            const QStringList problems = show->cueProblems(c);
            if (!problems.isEmpty()) {
                m_cues->item(r, 0)->setIcon(dotIcon(kBrokenCue));
                m_cues->item(r, 0)->setToolTip(tr("Something this cue uses was deleted:\n%1").arg(problems.join('\n')));
            }
            set(1, c.name);
            // Individual times: the range across the cue's fixtures (blueprint lines' own included).
            double fLo = 1e9, fHi = -1, dLo = 1e9, dHi = -1;
            const TimingTable times = show->cueTiming(c);
            for (const auto &f : times)
                for (const AttrTiming &t : f) {
                    if (t.fade >= 0) { fLo = std::min(fLo, t.fade); fHi = std::max(fHi, t.fade); }
                    if (t.delay >= 0) { dLo = std::min(dLo, t.delay); dHi = std::max(dHi, t.delay); }
                }
            // Values left on the cue's own time count in the range too.
            bool fadeOwn = false, delayOwn = false;
            const RawTable everything = show->evaluateCue(c);
            for (auto f = everything.begin(); f != everything.end(); ++f)
                for (auto a = f->begin(); a != f->end(); ++a) {
                    const auto ft = times.constFind(f.key());
                    AttrTiming t;
                    if (ft != times.constEnd()) {
                        auto at = ft->constFind(a.key());
                        if (at == ft->constEnd())
                            at = ft->constFind(QStringLiteral("*"));
                        if (at != ft->constEnd())
                            t = *at;
                    }
                    fadeOwn |= t.fade < 0;
                    delayOwn |= t.delay < 0;
                }
            auto shown = [](double own, bool ownUsed, double lo, double hi) {
                if (hi < 0)
                    return formatSeconds(own);
                if (ownUsed) {
                    lo = std::min(lo, own);
                    hi = std::max(hi, own);
                }
                return lo == hi ? formatSeconds(lo) : formatTimes({lo, hi});
            };
            set(CueFade, shown(c.fade, fadeOwn, fLo, fHi));
            set(CueDelay, shown(c.delay, delayOwn, dLo, dHi));
            if (fHi >= 0 || dHi >= 0) {
                const QString tip = tr("Individual times. The cue's own: fade %1, delay %2. Type one number to give everything the same "
                                       "time again, or a range to spread new ones.")
                                        .arg(formatSeconds(c.fade), formatSeconds(c.delay));
                m_cues->item(r, CueFade)->setToolTip(tip);
                m_cues->item(r, CueDelay)->setToolTip(tip);
            }
            set(CueTriggerCol, cueTriggerName(c.trigger) + QStringLiteral("  ▾"), false);
            set(CueWait, c.trigger == CueTrigger::Wait ? formatSeconds(c.wait) : QString());
            {
                const Macro *mc = c.macro ? show->macro(c.macro) : nullptr;
                set(CueMacro, !c.macro ? QString() : mc ? QStringLiteral("%1 %2").arg(c.macro).arg(mc->displayName()) : tr("%1 (missing)").arg(c.macro));
            }
            set(CueNotes, c.notes);
            set(CueContents, content.isEmpty() ? tr("Empty") : content.join(", "), false);
        }
        // The Off line: what turning the executor off does. Only its fade and delay are edited;
        // the rest is greyed out.
        const int r = int(s->cues.size());
        auto off = [&](int col, const QString &text, bool editable) {
            auto *it = new QTableWidgetItem(text);
            if (!editable)
                it->setFlags(it->flags() & ~Qt::ItemIsEditable);
            it->setForeground(editable ? theme::muted : theme::faint);
            it->setData(Qt::UserRole, true);   // the Off line keeps its colors
            m_cues->setItem(r, col, it);
        };
        off(CueNumber, tr("Off"), false);
        off(CueName, tr("Off"), false);
        off(CueFade, formatSeconds(s->offFade), true);
        off(CueDelay, formatSeconds(s->offDelay), true);
        off(CueTriggerCol, cueTriggerName(s->offTrigger) + QStringLiteral("  ▾"), false);
        off(CueWait, s->offTrigger == CueTrigger::Wait ? formatSeconds(s->offWait) : QString(), true);
        off(CueMacro, QString(), false);
        off(CueNotes, QString(), false);
        off(CueContents, s->offTrigger == CueTrigger::Go ? tr("Turning the executor off fades out with these times")
                                               : tr("Turns itself off after the last cue, with these times"),
            false);
    }
    if (m_cues->rowCount() > 0)
        m_cues->selectRow(std::clamp(keepRow, 0, m_cues->rowCount() - 1));
    m_updating = false;
    refreshCurrentCue();
    refreshBlueprints();
}

void CueView::refreshBlueprints()
{
    if (m_updating && sender() == m_cues)
        return;
    Show *show = m_ctx->show;
    bool ok;
    const double n = selectedCueNumber(&ok);
    const Sequence *s = show->sequence(show->selectedSequence());
    const Cue *cue = ok ? &s->cues[s->indexOf(n)] : nullptr;
    const bool wasUpdating = m_updating;
    m_updating = true;
    m_blueprintTitle->setText(cue ? tr("Blueprint for cue %1").arg(cue->displayNumber()) : tr("Blueprint"));
    m_addLine->setEnabled(cue);
    m_removeLine->setEnabled(cue && !cue->blueprints.isEmpty());
    m_build->setEnabled(cue && !cue->blueprints.isEmpty());
    m_blueprints->setRowCount(cue ? cue->blueprints.size() : 0);
    if (cue) {
        for (int r = 0; r < cue->blueprints.size(); ++r) {
            const BlueprintLine &l = cue->blueprints[r];
            auto set = [&](int col, const QString &text, bool editable = true) {
                auto *it = new QTableWidgetItem(text);
                if (!editable) {
                    it->setFlags(it->flags() & ~Qt::ItemIsEditable);
                    it->setForeground(theme::muted);
                }
                m_blueprints->setItem(r, col, it);
            };
            set(0, l.name);
            const QStringList problems = show->blueprintProblems(l);
            if (!problems.isEmpty()) {
                m_blueprints->item(r, 0)->setIcon(dotIcon(kBrokenLine));
                m_blueprints->item(r, 0)->setToolTip(problems.join('\n'));
            }
            set(2, QString(), false);
            auto *pick = new PopupCombo(m_blueprints);
            pick->addItem(l.presets.isEmpty() ? tr("Choose presets…") : presetList(show, l.presets));
            pick->setToolTip(tr("Choose this line's presets"));
            pick->onPopup = [this, show, r, n, pick]() {
                const Sequence *sq = show->sequence(show->selectedSequence());
                const int ci = sq ? sq->indexOf(n) : -1;
                if (ci < 0 || r >= sq->cues[ci].blueprints.size())
                    return;
                // Parent it to the view (not the cell), since applying rebuilds the cells.
                auto *drop = new PresetPickerDialog(show, sq->cues[ci].blueprints[r].presets, this,
                                                    [this, n, r](const QVector<PresetRef> &chosen) {
                                                        editSequence([=](Sequence &seq) {
                                                            int idx = seq.indexOf(n);
                                                            if (idx >= 0 && r < seq.cues[idx].blueprints.size())
                                                                seq.cues[idx].blueprints[r].presets = chosen;
                                                        });
                                                    });
                drop->popupUnder(pick);
            };
            m_blueprints->setCellWidget(r, 2, pick);
            set(3, QString::number(l.every));
            set(4, QString::number(l.offset + 1));
            set(5, QString(), false);
            set(LineFade, formatTimes(l.fadeTimes));
            set(LineDelay, formatTimes(l.delayTimes));
            set(LineFixtures, QString::number(show->blueprintFixtures(l).size()), false);
            // spread from the pool: thins the group and lays out the line's effects.
            auto *mtx = new QComboBox(m_blueprints);
            mtx->addItem(tr("None"), 0);
            for (const Spread &m : show->spreads())
                mtx->addItem(QStringLiteral("%1  %2").arg(m.id).arg(m.name), m.id);
            if (l.spreadId && !show->spread(l.spreadId))
                mtx->insertItem(1, dotIcon(kBrokenLine), tr("%1  (deleted)").arg(l.spreadId), l.spreadId);
            mtx->setCurrentIndex(std::max(0, mtx->findData(l.spreadId)));
            mtx->setToolTip(tr("A spread from the pool: which of the group's fixtures, and how the line's effects spread across them"));
            connect(mtx, &QComboBox::activated, this, [this, mtx, r, n]() {
                const int id = mtx->currentData().toInt();
                editSequence([=](Sequence &seq) {
                    int idx = seq.indexOf(n);
                    if (idx >= 0 && r < seq.cues[idx].blueprints.size())
                        seq.cues[idx].blueprints[r].spreadId = id;
                });
            });
            m_blueprints->setCellWidget(r, 5, mtx);

            auto *combo = new QComboBox(m_blueprints);
            for (const Group &g : show->groups())
                combo->addItem(QStringLiteral("%1  %2").arg(g.id).arg(g.name), g.id);
            if (l.groupId && !show->group(l.groupId)) {
                combo->insertItem(0, dotIcon(kBrokenLine), tr("%1  (deleted)").arg(l.groupId), l.groupId);
                combo->setToolTip(tr("Group %1 was deleted. Store it again or pick another group.").arg(l.groupId));
            }
            combo->setCurrentIndex(combo->findData(l.groupId));
            connect(combo, &QComboBox::activated, this, [this, combo, r, n]() {
                const int gid = combo->currentData().toInt();
                editSequence([=](Sequence &seq) {
                    int idx = seq.indexOf(n);
                    if (idx >= 0 && r < seq.cues[idx].blueprints.size())
                        seq.cues[idx].blueprints[r].groupId = gid;
                });
            });
            m_blueprints->setCellWidget(r, 1, combo);
        }
    }
    m_updating = wasUpdating;
}

} // namespace lumora
