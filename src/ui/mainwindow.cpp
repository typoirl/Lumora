// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"
#include "controlview.h"
#include "gotodialog.h"
#include "helpview.h"
#include "presetbuilder.h"
#include "plotsui.h"

#include "appcontext.h"
#include "colorpicker.h"
#include "commandbar.h"
#include "encoderbar.h"
#include "executorbar.h"
#include "keypad.h"
#include "midiview.h"
#include "outputview.h"
#include "patchview.h"
#include "theme.h"
#include "workspace.h"

#include <QApplication>
#include <QCursor>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDir>
#include <QUrl>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QMap>
#include <QPointer>
#include <QShortcut>
#include <QLayout>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include "shortcuts.h"
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace lumora {

namespace {
QToolButton *topButton(const QString &text, const QString &objectName, const QString &tip, QWidget *parent,
                       bool checkable = true)
{
    auto *b = new QToolButton(parent);
    b->setText(text);
    b->setObjectName(objectName);
    b->setCheckable(checkable);
    b->setToolTip(tip);
    b->setMinimumHeight(30);
    return b;
}

QString railButtonStyle()
{
    return QStringLiteral(
               "QToolButton { background: transparent; border: none; border-radius: 6px; color: %1; text-align: left; padding: 6px 10px; }"
               "QToolButton:hover { background: %2; color: %3; }"
               "QToolButton:checked { background: %2; color: %3; font-weight: 600; }")
        .arg(theme::muted.name(), theme::raised.name(), theme::text.name());
}

QJsonObject frameJson(int x, int y, int w, int h, const QStringList &tabs)
{
    return {{"x", x}, {"y", y}, {"w", w}, {"h", h}, {"tabs", QJsonArray::fromStringList(tabs)}, {"current", 0}};
}
} // namespace

namespace {
// The open screens, by number.
QMap<int, QPointer<MainWindow>> &screens()
{
    static QMap<int, QPointer<MainWindow>> map;
    return map;
}
void rememberOpenScreens()
{
    QStringList open;
    for (auto it = screens().begin(); it != screens().end(); ++it)
        if (it.key() > 1 && it.value())
            open << QString::number(it.key());
    QSettings().setValue("screens/open", open);
}
} // namespace

namespace {
QJsonObject execFrame(int x, int y, int w, int h, int first)
{
    QJsonObject options;   // as the executor window writes them, so the view reads as unchanged
    if (first != 201)
        options["first"] = first;
    return {{"x", x}, {"y", y}, {"w", w}, {"h", h},
            {"tabs", QJsonArray{QJsonObject{{"type", "executors"}, {"options", options}}}}, {"current", 0}};
}
} // namespace

namespace {
// While Store is on, views are targets like pool slots: outlined in the programmer color.
QString railStoreStyle()
{
    return railButtonStyle()
           + QStringLiteral("QToolButton { border: 1px dashed %1; color: %1; } QToolButton:hover { background: #3a2422; color: %1; }")
                 .arg(theme::programmer.name());
}
} // namespace

// In grid cells (24 x 16).
QList<StoredView> standardViews()
{
    return {
        // Pools along the top; the fixture sheet (with the help beside it as a tab), the selection
        // matrix with the spreads modifier, and the spreads pool.
        {1, QObject::tr("Program"),
         QJsonArray{frameJson(0, 0, 7, 6, {"groups"}), frameJson(7, 0, 17, 6, {"presets"}),
                    frameJson(0, 6, 15, 10, {"fixtures", "help"}), frameJson(15, 6, 9, 6, {"selectionmatrix", "spreadmodifier"}),
                    frameJson(15, 12, 9, 4, {"spreads"})}},
        {2, QObject::tr("Sequences"), QJsonArray{frameJson(0, 0, 7, 16, {"sequences"}), frameJson(7, 0, 17, 16, {"cues"})}},
        // The rate masters under the effects pool, ten faders to set their speed.
        {3, QObject::tr("Effects"), QJsonArray{frameJson(0, 0, 10, 9, {"effects"}), frameJson(0, 9, 10, 7, {"ratemasters"}),
                                               frameJson(10, 0, 14, 16, {"effecteditor"})}},
        // Encoders over the faders, keys under them.
        {4, QObject::tr("Playback"),
         QJsonArray{execFrame(0, 0, 24, 5, 301), execFrame(0, 5, 24, 7, 201), execFrame(0, 12, 24, 4, 101)}},
        // The plots pool beside a plot: tap fixtures to select them, objects to use them.
        {5, QObject::tr("Plot"), QJsonArray{frameJson(0, 0, 4, 16, {"plots"}), frameJson(4, 0, 20, 16, {"plot"})}},
    };
}

namespace {
MainWindow *g_storingScreen = nullptr;   // the screen storing a view from its own buttons
}

MainWindow *MainWindow::activeScreen()
{
    if (g_storingScreen)
        return g_storingScreen;
    if (QWidget *a = QApplication::activeWindow())
        if (auto *m = qobject_cast<MainWindow *>(a->window()))
            return m;
    return screens().value(1);
}

QString MainWindow::settingsPrefix() const
{
    return m_screen == 1 ? QString() : QStringLiteral("screen%1/").arg(m_screen);
}

// Shows live in Documents\Lumora unless you choose somewhere else.
static QString showFolder()
{
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString dir = (docs.isEmpty() ? QDir::homePath() : docs) + QStringLiteral("/Lumora");
    QDir().mkpath(dir);
    return dir;
}

QString MainWindow::saveSuggestion() const
{
    if (!m_path.isEmpty())
        return m_path;
    QString name = m_ctx->show->name().trimmed();
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("-"));
    return showFolder() + '/' + (name.isEmpty() ? tr("Show") : name) + QStringLiteral(".lumora");
}

MainWindow::MainWindow(AppContext *ctx, int screen, QWidget *parent) : QMainWindow(parent), m_ctx(ctx), m_screen(screen)
{
    if (screen == 1)
        showFolder();   // Documents\Lumora, where shows are saved
    screens()[m_screen] = this;
    setProperty("lumoraScreen", m_screen);
    if (m_screen > 1)
        setAttribute(Qt::WA_DeleteOnClose);
    const QString version = QApplication::applicationVersion();
    setWindowTitle(m_screen == 1 ? QStringLiteral("Lumora %1").arg(version)
                                 : QStringLiteral("Lumora %1 · Screen %2").arg(version).arg(m_screen));
    resize(m_screen == 1 ? 1500 : 1200, m_screen == 1 ? 940 : 800);
    restoreGeometry(QSettings().value(settingsPrefix() + "ui/window").toByteArray());
    // Developer: screenshots at another size ("1920x1080").
    if (const QStringList wh = qEnvironmentVariable("LUMORA_WINDOW_SIZE").split('x'); wh.size() == 2)
        resize(wh[0].toInt(), wh[1].toInt());

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildTopBar());

    auto *middle = new QHBoxLayout;
    middle->setContentsMargins(0, 0, 0, 0);
    middle->setSpacing(0);

    // Navigation rail: user views on top, setup screens below.
    m_rail = new QWidget(central);
    m_rail->setFixedWidth(118);
    m_rail->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Ignored);   // never sets the window's minimum height
    m_rail->setObjectName("rail");
    m_rail->setAttribute(Qt::WA_StyledBackground);
    m_rail->setStyleSheet(QStringLiteral("QWidget#rail { background: %1; border-right: 1px solid %2; }").arg(theme::panel.name(), theme::line.name()));
    auto *rv = new QVBoxLayout(m_rail);
    rv->setContentsMargins(8, 10, 8, 12);
    rv->setSpacing(4);
    auto *viewsLabel = new QLabel(tr("Views"), m_rail);
    viewsLabel->setProperty("role", "muted");
    viewsLabel->setContentsMargins(10, 2, 0, 2);
    rv->addWidget(viewsLabel);
    m_viewButtons = new QVBoxLayout;
    m_viewButtons->setSpacing(4);
    rv->addLayout(m_viewButtons);
    auto *addView = new QToolButton(m_rail);
    m_newViewButton = addView;
    addView->setText(tr("+ New view"));
    addView->setToolTip(tr("Make a new, empty view and fill it with the windows you want"));
    addView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    addView->setStyleSheet(QStringLiteral("QToolButton { background: transparent; border: 1px dashed %1; border-radius: 6px; color: %2;"
                                          " text-align: left; padding: 5px 10px; } QToolButton:hover { color: %3; border-color: %2; }")
                               .arg(theme::line.name(), theme::muted.name(), theme::text.name()));
    m_newViewStyle = addView->styleSheet();
    rv->addWidget(addView);
    connect(addView, &QToolButton::clicked, this, &MainWindow::newView);

    m_nav = new QButtonGroup(this);
    m_nav->setExclusive(true);
    m_stack = new QStackedWidget(central);
    const QStringList setupNames = {tr("Patch"), tr("Outputs"), tr("Control")};
    if (m_screen == 1) {
        auto *setupLabel = new QLabel(tr("Setup"), m_rail);
        setupLabel->setProperty("role", "muted");
        setupLabel->setContentsMargins(10, 18, 0, 2);
        rv->addWidget(setupLabel);
        m_setupPages << new PatchView(ctx, m_stack) << new OutputView(ctx, m_stack) << new ControlView(ctx, m_stack);
    }
    for (int i = 0; i < m_setupPages.size(); ++i) {
        auto *b = new QToolButton(m_rail);
        b->setText(setupNames[i]);
        b->setCheckable(true);
        b->setMinimumHeight(36);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setStyleSheet(railButtonStyle());
        m_nav->addButton(b);
        rv->addWidget(b);
        m_setupButtons << b;
        m_stack->addWidget(m_setupPages[i]);
        QWidget *page = m_setupPages[i];
        connect(b, &QToolButton::clicked, this, [this, page]() { showWidget(page); });
    }
    rv->addStretch(1);
    middle->addWidget(m_rail);
    // The view may shrink freely: bars that appear below (Store, Learn) must never push the
    // encoders or the command line out of the window.
    m_stack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    middle->addWidget(m_stack, 1);
    root->addLayout(middle, 1);

    // Executors slide in here while Store or Learn MIDI is on and the view has no executor window.
    m_execStrip = new ExecutorBar(ctx, central, ExecutorBar::Mode::Learn);
    m_execStrip->setObjectName("execStrip");
    m_execStrip->setAttribute(Qt::WA_StyledBackground);
    m_execStrip->setFixedHeight(262);
    m_execStrip->hide();
    root->addWidget(m_execStrip);
    m_storePicker = new StoreExecutorPicker(ctx, central);
    m_storePicker->hide();
    root->addWidget(m_storePicker);

    m_encoders = new EncoderBar(ctx, central);
    root->addWidget(m_encoders);

    auto *divider = new QFrame(central);
    divider->setFixedHeight(1);
    divider->setStyleSheet(QStringLiteral("background: %1;").arg(theme::line.name()));
    root->addWidget(divider);
    m_commandBar = new CommandBar(ctx, central);
    m_commandBar->setContentsMargins(0, 0, 0, 8);
    root->addWidget(m_commandBar);
    setCentralWidget(central);

    m_keypad = new CommandKeypad(ctx, false, central);
    m_keypad->hide();
    if (m_screen == 1)
        qApp->installEventFilter(this);   // keyboard shortcuts, for every screen
    connect(m_keypad, &CommandKeypad::closeRequested, this, [this]() { setKeypadVisible(false); });

    {
        // Views live in the show now; forget the per-computer layouts older versions kept.
        QSettings s;
        s.remove(settingsPrefix() + "views");
        if (m_screen == 1)
            s.remove("canvas");
    }
    syncViews(true);
    if (View *v = viewById(m_ctx->uiValue(QStringLiteral("view%1").arg(m_screen)).toInt()))
        showWidget(v->page);
    else if (!m_views.isEmpty())
        showWidget(m_views.first().page);
    if (m_screen == 1)
        // Store View takes the windows of the screen in front.
        ctx->show->setViewLayoutProvider([]() -> std::optional<QJsonArray> {
            MainWindow *m = activeScreen();
            Workspace *w = m ? m->currentWorkspace() : nullptr;
            if (!w)
                return std::nullopt;
            return w->layout();
        });
    connect(ctx->show, &Show::viewRecallRequested, this, [this](int id) {
        if (activeScreen() == this)
            recallView(id);
    });
    // Goto (a button or "Goto Executor 201"): one cue list, on the screen the request came from.
    connect(ctx->show, &Show::gotoRequested, this, [this](int page, int number) {
        if (m_screen == 1)
            GotoDialog::open(m_ctx, page, number, this);
    });

    Show *show = ctx->show;
    connect(show, &Show::changed, this, [this](Show::Parts parts) {
        if (parts & Show::ViewsPart)
            syncViews(parts == Show::AllParts);
        if (parts == Show::AllParts) {
            // Another show: its own view on this screen, executor count and shortcut setting.
            if (View *v = viewById(m_ctx->uiValue(QStringLiteral("view%1").arg(m_screen)).toInt()))
                showWidget(v->page);
            else
                showWidget(m_stack->currentWidget());   // note the view on screen in the new show
            if (m_shortcuts) {
                QSignalBlocker b(m_shortcuts);
                m_shortcuts->setChecked(m_ctx->shortcutsEnabled());
            }
            if (m_screen == 1)
                emit m_ctx->interfaceChanged();
        }
        refreshTopBar();
        // Programmer edits and playback aren't show data; everything else marks the show as changed.
        if (m_screen == 1 && !m_loading && (parts & ~(Show::ProgrammerPart | Show::ExecutorsPart)))
            setDirty(true);
    });
    connect(ctx->midi, &MidiManager::learnStateChanged, this, [this]() {
        refreshTopBar();
        updateBottomBars();
    });
    connect(ctx, &AppContext::editEffect, this, [this]() {
        if (m_screen != 1)
            return;
        Workspace *w = currentWorkspace();
        if (w && w->raisePanel("effecteditor"))
            return;
        // Prefer a view that already has an editor, otherwise add one here.
        for (const View &v : m_views)
            if (v.workspace->raisePanel("effecteditor")) {
                showWidget(v.page);
                return;
            }
        if (!w)
            w = m_views.first().workspace;
        w->addPanel("effecteditor");
    });
    connect(ctx->midi, &MidiManager::bindingsChanged, this, [this]() {
        if (m_screen == 1 && !m_loading)
            setDirty(true);
    });
    connect(ctx, &AppContext::storeArmedChanged, this, [this](bool on) {
        for (View &v : m_views)
            updateViewButton(v);
        if (m_newViewButton)
            m_newViewButton->setStyleSheet(on ? railStoreStyle() : m_newViewStyle);
        {
            QSignalBlocker b(m_store);
            m_store->setChecked(on);
        }
        updateBottomBars();
        if (on)
            m_commandBar->showMessage(tr("Store: tap a group, preset, sequence or executor, or type the target"));
    });

    auto *save = new QShortcut(QKeySequence::Save, this);
    save->setEnabled(m_screen == 1);
    connect(save, &QShortcut::activated, this, [this]() {
        if (m_path.isEmpty()) {
            QString p = QFileDialog::getSaveFileName(this, tr("Save show"), saveSuggestion(), tr("Lumora shows (*.lumora)"));
            if (!p.isEmpty())
                saveShow(p);
        } else {
            saveShow(m_path);
        }
    });
    auto *open = new QShortcut(QKeySequence::Open, this);
    open->setEnabled(m_screen == 1);
    connect(open, &QShortcut::activated, this, [this]() {
        if (!confirmDiscard())
            return;
        QString p = QFileDialog::getOpenFileName(this, tr("Open show"), showFolder(), tr("Lumora shows (*.lumora)"));
        if (!p.isEmpty())
            openShow(p);
    });
    auto *focusCmd = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), this);
    connect(focusCmd, &QShortcut::activated, m_commandBar, &CommandBar::focusInput);
    auto *keys = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
    connect(keys, &QShortcut::activated, this, [this]() { setKeypadVisible(!m_keypad->isVisible()); });
    // Undo: Ctrl+Z. Over a plot it takes back the plot's last edit; anywhere else the last command,
    // store, delete or edit. Ctrl+Y (or Ctrl+Shift+Z) redoes.
    auto undoHere = [this](bool redo) {
        QWidget *w = QApplication::widgetAt(QCursor::pos());
        if (!w || w->window() != this)
            w = QApplication::focusWidget();
        bool inPlot = false;
        for (QWidget *p = w; p; p = p->parentWidget())
            if (qobject_cast<PlotView *>(p)) {
                inPlot = true;
                break;
            }
        const CommandResult r = m_ctx->undoStep(inPlot, redo);
        m_commandBar->showMessage(r.message);
        emit m_ctx->status(r.message);
    };
    auto *undoKey = new QShortcut(QKeySequence::Undo, this);
    connect(undoKey, &QShortcut::activated, this, [undoHere]() { undoHere(false); });
    QList<QKeySequence> redoKeys = QKeySequence::keyBindings(QKeySequence::Redo);
    for (const QKeySequence &k : {QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)})
        if (!redoKeys.contains(k))
            redoKeys << k;
    for (const QKeySequence &k : std::as_const(redoKeys)) {
        auto *redoKey = new QShortcut(k, this);
        connect(redoKey, &QShortcut::activated, this, [undoHere]() { undoHere(true); });
    }
    auto *esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(esc, &QShortcut::activated, this, [this]() {
        m_ctx->setStoreArmed(false);
        m_ctx->midi->disarm();
    });
    refreshTopBar();
}

// ---------------------------------------------------------------------------
// Views
// ---------------------------------------------------------------------------

MainWindow::View *MainWindow::viewById(int id)
{
    for (View &v : m_views)
        if (v.id == id)
            return &v;
    return nullptr;
}

void MainWindow::syncViews(bool reloadAll)
{
    Show *show = m_ctx->show;
    if (show->views().isEmpty() && m_screen == 1) {
        // A show without views (new, or from an older version) starts with the standard ones.
        show->setViews(standardViews());
    }
    QWidget *current = m_stack->currentWidget();
    // Views deleted from the show
    for (int i = int(m_views.size()) - 1; i >= 0; --i) {
        if (show->view(m_views[i].id))
            continue;
        View v = m_views.takeAt(i);
        if (v.button) {
            m_nav->removeButton(v.button);
            v.button->deleteLater();
        }
        m_stack->removeWidget(v.page);
        v.page->deleteLater();
        if (current == v.page)
            current = nullptr;
    }
    // New and changed ones, in number order
    int index = 0;
    for (const StoredView &sv : show->views()) {
        View *v = viewById(sv.id);
        if (!v) {
            View nv;
            nv.id = sv.id;
            nv.workspace = new Workspace(m_ctx, m_stack);
            if (m_screen != 1)   // no encoder bar here: its room becomes more grid rows
                nv.workspace->setRows(Workspace::kRows + Workspace::kExtraRows);
            nv.page = new QWidget(m_stack);
            auto *pl = new QVBoxLayout(nv.page);
            pl->setContentsMargins(3, 3, 3, 3);
            pl->addWidget(nv.workspace);
            const int id = sv.id;
            connect(nv.workspace, &Workspace::panelsChanged, this, &MainWindow::updateBottomBars);
            connect(nv.workspace, &Workspace::layoutEdited, this, [this, id]() {
                if (View *x = viewById(id))
                    updateViewButton(*x);
            });
            nv.workspace->loadLayout(sv.layout);
            nv.recalled = nv.workspace->layout();   // as the windows write it back
            nv.stored = sv.layout;
            m_views.insert(index, nv);
            v = &m_views[index];
        } else {
            const int at = int(v - m_views.data());
            if (at != index) {
                m_views.move(at, index);
                v = &m_views[index];
            }
            if (reloadAll || v->stored != sv.layout) {
                // Stored again (here or on another screen). Windows nobody has touched since the
                // last recall follow; changed ones stay as they are until the view is pressed.
                const bool untouched = Workspace::sameLayout(v->workspace->layout(), v->recalled);
                if (reloadAll || untouched) {
                    v->workspace->loadLayout(sv.layout);
                    v->recalled = v->workspace->layout();
                } else {
                    v->recalled = sv.layout;
                }
                v->stored = sv.layout;
            }
        }
        v->name = sv.name;
        m_stack->insertWidget(index, v->page);   // keeps the stack in rail order
        ++index;
    }
    rebuildRail();
    if (!current || m_stack->indexOf(current) < 0)
        current = m_views.isEmpty() ? (m_setupPages.isEmpty() ? nullptr : m_setupPages.first()) : m_views.first().page;
    if (current)
        showWidget(current);
}

void MainWindow::rebuildRail()
{
    for (View &v : m_views)
        if (v.button) {
            m_nav->removeButton(v.button);
            v.button->deleteLater();
            v.button = nullptr;
        }
    for (int i = 0; i < m_views.size(); ++i) {
        View &v = m_views[i];
        auto *b = new QToolButton(m_rail);
        b->setCheckable(true);
        b->setMinimumHeight(36);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setStyleSheet(railButtonStyle());
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        b->setChecked(m_stack->currentWidget() == v.page);
        m_nav->addButton(b);
        m_viewButtons->addWidget(b);
        const int id = v.id;
        connect(b, &QToolButton::clicked, this, [this, id]() { recallView(id); });
        connect(b, &QToolButton::customContextMenuRequested, this, [this, b, id](const QPoint &pos) {
            View *v = viewById(id);
            if (!v)
                return;
            QMenu menu(this);
            const bool changed = !Workspace::sameLayout(v->workspace->layout(), v->recalled);
            menu.addAction(changed ? tr("Store the changes") : tr("Store the windows here"), this, [this, id]() { storeView(id); });
            QAction *recall = menu.addAction(tr("Recall the stored windows"), this, [this, id]() {
                recallView(id);
            });
            recall->setEnabled(changed);
            menu.addSeparator();
            menu.addAction(tr("Rename…"), this, [this, id]() { renameView(id); });
            QAction *del = menu.addAction(tr("Delete view"), this, [this, id]() { deleteView(id); });
            del->setEnabled(m_views.size() > 1);
            menu.exec(b->mapToGlobal(pos));
        });
        v.button = b;
        updateViewButton(v);
    }
}

void MainWindow::updateViewButton(View &v)
{
    if (!v.button)
        return;
    const bool changed = !Workspace::sameLayout(v.workspace->layout(), v.recalled);
    v.button->setStyleSheet(m_ctx->storeArmed() ? railStoreStyle() : railButtonStyle());
    const QString mark = changed ? QStringLiteral(" •") : QString();
    QFont f = v.button->font();
    f.setItalic(changed);
    v.button->setFont(f);
    v.button->setText(v.button->fontMetrics().elidedText(v.name, Qt::ElideRight, m_rail->width() - 34
                                                          - v.button->fontMetrics().horizontalAdvance(mark)) + mark);
    v.button->setToolTip(changed ? tr("View %1 \"%2\": changed since it was stored.\nPress it to go back to the stored windows; "
                                      "Store, then tap it (or Store View %1) to keep the changes. Right-click for more.")
                                       .arg(v.id).arg(v.name)
                                 : tr("View %1 \"%2\". Right-click to store, rename or delete it.").arg(v.id).arg(v.name));
}

void MainWindow::recallView(int id)
{
    if (m_ctx->storeArmed()) {
        m_ctx->setStoreArmed(false);
        storeView(id);
        return;
    }
    View *v = viewById(id);
    if (m_ctx->tapTarget(QStringLiteral("View %1").arg(id), v ? v->name : QString()))
        return;
    if (!v)
        return;
    // Leaving a changed view drops its changes: it's back to what was stored, unmarked.
    for (View &other : m_views)
        if (other.id != id && m_stack->currentWidget() == other.page) {
            if (const StoredView *osv = m_ctx->show->view(other.id);
                osv && !Workspace::sameLayout(other.workspace->layout(), other.recalled)) {
                other.workspace->loadLayout(osv->layout);
                other.recalled = other.workspace->layout();
                other.stored = osv->layout;
                updateViewButton(other);
            }
        }
    if (const StoredView *sv = m_ctx->show->view(id)) {
        // Pressing a view always brings back what was stored.
        if (v->workspace->layout() != v->recalled || v->stored != sv->layout)
            v->workspace->loadLayout(sv->layout);
        v->recalled = v->workspace->layout();
        v->stored = sv->layout;
    }
    updateViewButton(*v);
    showWidget(v->page);
}

void MainWindow::storeView(int id)
{
    // Stores what this screen shows: make sure it's the screen the provider asks.
    if (!currentWorkspace()) {
        m_commandBar->showMessage(tr("Show a view first: Store View keeps the windows on screen"), true);
        return;
    }
    g_storingScreen = this;
    const CommandResult r = m_ctx->run(QStringLiteral("Store View %1").arg(id));
    g_storingScreen = nullptr;
    if (r.ok)
        if (View *v = viewById(id))
            showWidget(v->page);
}

void MainWindow::newView()
{
    // With Store on, the windows on screen become the new view; otherwise it starts empty.
    const bool fromScreen = m_ctx->storeArmed() && currentWorkspace();
    m_ctx->setStoreArmed(false);
    const int id = m_ctx->show->nextFreeViewId();
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New view"), tr("Name"), QLineEdit::Normal,
                                               tr("View %1").arg(id), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    QString quoted = name;
    quoted.replace('"', '\'');
    if (fromScreen) {
        g_storingScreen = this;
        m_ctx->run(QStringLiteral("Store View %1 \"%2\"").arg(id).arg(quoted));
        g_storingScreen = nullptr;
    } else {
        m_ctx->show->setView({id, name, QJsonArray()});
        emit m_ctx->status(tr("View %1 \"%2\" made").arg(id).arg(name));
    }
    if (View *v = viewById(id))
        showWidget(v->page);
    if (!fromScreen)
        m_commandBar->showMessage(tr("New view \"%1\": add windows with +, then Store and tap the view to keep them").arg(name));
}

void MainWindow::renameView(int id)
{
    const StoredView *sv = m_ctx->show->view(id);
    if (!sv)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename view"), tr("Name"), QLineEdit::Normal, sv->name, &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    QString quoted = name;
    quoted.replace('"', '\'');
    m_ctx->run(QStringLiteral("Label View %1 \"%2\"").arg(id).arg(quoted));
}

void MainWindow::deleteView(int id)
{
    const StoredView *sv = m_ctx->show->view(id);
    if (!sv || m_ctx->show->views().size() <= 1)
        return;
    if (QMessageBox::question(this, tr("Delete view"), tr("Delete the view \"%1\"? Its windows close; show data isn't affected.")
                                                           .arg(sv->name))
        != QMessageBox::Yes)
        return;
    m_ctx->run(QStringLiteral("Delete View %1").arg(id));
}

void MainWindow::showWidget(QWidget *page)
{
    m_stack->setCurrentWidget(page);
    for (const View &v : m_views) {
        if (v.button)
            v.button->setChecked(v.page == page);
        if (v.page == page) {
            m_currentView = v.id;
            m_ctx->setUiValue(QStringLiteral("view%1").arg(m_screen), v.id);   // saved with the show
        }
    }
    for (int i = 0; i < m_setupPages.size(); ++i)
        m_setupButtons[i]->setChecked(m_setupPages[i] == page);
    updateBottomBars();
}

void MainWindow::showPage(int index)
{
    if (index < m_views.size())
        showWidget(m_views[index].page);
    else if (index - m_views.size() < m_setupPages.size())
        showWidget(m_setupPages[index - int(m_views.size())]);
}

Workspace *MainWindow::currentWorkspace() const
{
    for (const View &v : m_views)
        if (m_stack->currentWidget() == v.page)
            return v.workspace;
    return nullptr;
}

void MainWindow::updateBottomBars()
{
    Workspace *w = currentWorkspace();
    const bool hasExecWindow = w && w->hasPanel("executors");
    // Store gets compact tiles for every row; Learn MIDI needs the real faders and buttons.
    const bool learning = m_ctx->midi->learnMode();
    m_storePicker->setVisible(m_ctx->storeArmed() && !learning && !hasExecWindow);
    m_execStrip->setVisible(false);   // Learn MIDI works on the executor windows already on screen
    // Encoders belong to programming; setup screens get the room instead.
    // While the executor strip is up (Store, Learn MIDI) it takes the encoders' place, so the
    // window never has to grow taller than the screen.
    m_encoders->setVisible(m_screen == 1 && w != nullptr && !m_execStrip->isVisible() && !m_storePicker->isVisible());
    // Re-lay out now, so bars that appear or disappear never leave the others squeezed until
    // the next window resize.
    if (QLayout *l = centralWidget() ? centralWidget()->layout() : nullptr) {
        l->invalidate();
        l->activate();
    }
}

void MainWindow::setKeypadVisible(bool on)
{
    if (on && !m_keypad->isVisible()) {
        QWidget *area = centralWidget();
        const int w = std::min(m_keypad->width(), area->width() - 40);
        m_keypad->resize(w, m_keypad->height());
        const QPoint saved = QSettings().value("ui/keypadPos", QPoint(-1, -1)).toPoint();
        if (saved.x() >= 0) {
            m_keypad->move(qBound(0, saved.x(), area->width() - w), qBound(0, saved.y(), area->height() - m_keypad->height()));
        } else {
            // First time: centered above the bottom bars so it hides neither.
            QWidget *bottom = m_execStrip->isVisible() ? static_cast<QWidget *>(m_execStrip)
                              : m_storePicker->isVisible() ? static_cast<QWidget *>(m_storePicker)
                              : m_encoders->isVisible() ? static_cast<QWidget *>(m_encoders)
                                                        : static_cast<QWidget *>(m_commandBar);
            m_keypad->move((area->width() - w) / 2, std::max(60, bottom->y() - m_keypad->height() - 12));
        }
    }
    m_keypad->setVisible(on);
    if (on)
        m_keypad->raise();
    QSignalBlocker b(m_keys);
    m_keys->setChecked(on);
}

namespace {
// Somewhere you type: a line, a text box, a number box, an editable list.
bool isTextInput(const QWidget *w)
{
    for (; w; w = w->parentWidget()) {
        if (qobject_cast<const QLineEdit *>(w) || qobject_cast<const QTextEdit *>(w) || qobject_cast<const QPlainTextEdit *>(w)
            || qobject_cast<const QAbstractSpinBox *>(w))
            return true;
        if (auto *c = qobject_cast<const QComboBox *>(w); c && c->isEditable())
            return true;
        if (w->isWindow())
            break;
    }
    return false;
}
} // namespace

bool MainWindow::eventFilter(QObject *obj, QEvent *ev)
{
    if (ev->type() == QEvent::MouseButtonPress) {
        // Clicking anything that isn't a text field (a pool tile, a window, a button) ends typing,
        // so the caret goes away and keyboard shortcuts work again.
        auto *w = qobject_cast<QWidget *>(obj);
        QWidget *f = QApplication::focusWidget();
        if (w && f && f != w && !f->isAncestorOf(w) && w->window() == f->window()
            && w->window()->property("lumoraScreen").isValid() && isTextInput(f) && !isTextInput(w))
            f->clearFocus();
        return false;
    }
    if (ev->type() != QEvent::KeyPress)
        return QMainWindow::eventFilter(obj, ev);
    auto *k = static_cast<QKeyEvent *>(ev);
    auto *w = qobject_cast<QWidget *>(obj);
    // Only keys aimed at this window, with no dialog or menu open.
    if (!w || !w->window()->property("lumoraScreen").isValid() || QApplication::activeModalWidget()
        || QApplication::activePopupWidget())
        return false;
    // Typing into a text field (a name, a number, the command line) is never a shortcut.
    QWidget *f = QApplication::focusWidget();
    if (f && (qobject_cast<QLineEdit *>(f) || qobject_cast<QTextEdit *>(f) || qobject_cast<QPlainTextEdit *>(f)
              || qobject_cast<QAbstractSpinBox *>(f) || (qobject_cast<QComboBox *>(f) && static_cast<QComboBox *>(f)->isEditable())))
        return false;
    if (k->key() == Qt::Key_Insert && !(k->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        m_ctx->setShortcutsEnabled(!m_ctx->shortcutsEnabled());
        return true;
    }
    if (!m_ctx->shortcutsEnabled())
        return false;
    const QString cmd = commandForKey(k);
    if (cmd.isEmpty())
        return false;
    m_ctx->pressKey(cmd);
    return true;
}

void MainWindow::resizeEvent(QResizeEvent *ev)
{
    QMainWindow::resizeEvent(ev);
    if (m_keypad && m_keypad->isVisible()) {
        QWidget *area = centralWidget();
        m_keypad->move(qBound(0, m_keypad->x(), std::max(0, area->width() - m_keypad->width())),
                       qBound(0, m_keypad->y(), std::max(0, area->height() - m_keypad->height())));
    }
}

QMenu *MainWindow::buildWindowMenu(QWidget *parent)
{
    auto *menu = new QMenu(parent);
    for (const PanelType &t : panelTypes()) {
        const QString id = t.id;
        menu->addAction(t.title, this, [this, id]() {
            Workspace *w = currentWorkspace();
            if (!w) {
                m_commandBar->showMessage(tr("Open a view first: windows go into views"));
                return;
            }
            w->addPanel(id);
            updateBottomBars();
            m_commandBar->showMessage(tr("Drag a window's header to move it, its corners to resize it, its tabs to stack it with another"));
        });
    }
    connect(menu, &QMenu::aboutToShow, this, [this, menu]() {
        const bool ok = currentWorkspace() != nullptr;
        for (QAction *a : menu->actions())
            a->setEnabled(ok);
    });
    return menu;
}

void MainWindow::showInterfaceSettings()
{
    QDialog d(this);
    d.setWindowTitle(tr("Config"));
    auto *v = new QVBoxLayout(&d);
    auto *form = new QFormLayout;
    form->setVerticalSpacing(12);
    auto *scale = new QComboBox(&d);
    for (int pct : {90, 100, 115, 130, 150, 175})
        scale->addItem(QStringLiteral("%1%").arg(pct), pct);
    scale->setCurrentIndex(std::max(0, scale->findData(QSettings().value("ui/scale", 100).toInt())));
    auto *execs = new QSpinBox(&d);
    execs->setRange(1, 99);
    execs->setValue(m_ctx->executorCount());
    form->addRow(tr("Interface size"), scale);
    form->addRow(tr("Executors per row"), execs);
    auto *encs = new QSpinBox(&d);
    encs->setRange(1, 20);
    encs->setValue(m_ctx->encodersPerPage());
    form->addRow(tr("Encoders per page"), encs);
    auto *liveOn = new QCheckBox(tr("On"), &d);
    liveOn->setChecked(m_ctx->show->progFadeOn());
    auto *liveTime = new QDoubleSpinBox(&d);
    liveTime->setRange(0.0, 600.0);
    liveTime->setDecimals(1);
    liveTime->setSuffix(tr(" s"));
    liveTime->setValue(m_ctx->show->progFadeTime());
    auto *liveRow = new QHBoxLayout;
    liveRow->addWidget(liveOn);
    liveRow->addWidget(liveTime, 1);
    form->addRow(tr("Programmer fade"), liveRow);
    auto seconds = [&d](double v) {
        auto *b = new QDoubleSpinBox(&d);
        b->setRange(0.0, 3600.0);
        b->setDecimals(1);
        b->setSuffix(QObject::tr(" s"));
        b->setValue(v);
        return b;
    };
    auto *cueFade = seconds(m_ctx->show->defaultCueFade());
    auto *cueDelay = seconds(m_ctx->show->defaultCueDelay());
    form->addRow(tr("New cues: fade"), cueFade);
    form->addRow(tr("New cues: delay"), cueDelay);
    v->addLayout(form);
    auto *note = new QLabel(tr("Executors per row (up to 99: 101–199, 201–299…) sets how many an executor window shows by default; encoders per page caps the encoder bar (fewer show when they don't fit). Programmer fade is how long programmer changes take while it's on (ProgFade 3 in a macro sets it too). New cues get the fade and delay set here. These are saved with the show. Interface size is for this computer (it depends on the screen) "
                             "and applies the next time you open Lumora; larger sizes suit touch screens."), &d);
    note->setProperty("role", "muted");
    note->setWordWrap(true);
    v->addWidget(note);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &d);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    v->addWidget(bb);
    if (d.exec() != QDialog::Accepted)
        return;
    const int oldScale = QSettings().value("ui/scale", 100).toInt();
    QSettings().setValue("ui/scale", scale->currentData().toInt());
    m_ctx->setExecutorCount(execs->value());
    m_ctx->setEncodersPerPage(encs->value());
    m_ctx->show->setProgFade(liveOn->isChecked(), liveTime->value());
    m_ctx->show->setDefaultCueTimes(cueFade->value(), cueDelay->value());
    if (oldScale != scale->currentData().toInt())
        m_commandBar->showMessage(tr("Interface size changes after you restart Lumora"));
}

QWidget *MainWindow::buildTopBar()
{
    auto *bar = new QWidget(this);
    bar->setFixedHeight(52);
    bar->setStyleSheet(QStringLiteral("QWidget#topbar { background: %1; border-bottom: 1px solid %2; }")
                           .arg(theme::panel.name(), theme::line.name()));
    bar->setObjectName("topbar");
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(12, 8, 14, 8);
    h->setSpacing(8);

    auto *menuButton = new QToolButton(bar);
    menuButton->setVisible(m_screen == 1);
    menuButton->setText(tr("Show"));
    menuButton->setPopupMode(QToolButton::InstantPopup);
    menuButton->setMinimumHeight(30);
    auto *menu = new QMenu(menuButton);
    menu->addAction(tr("New show"), this, [this]() {
        if (!confirmDiscard())
            return;
        m_loading = true;
        m_ctx->show->clearShow();
        m_ctx->midi->fromJson(QJsonObject());
        m_loading = false;
        m_path.clear();
        setDirty(false);
        showWidget(m_setupPages.first());
    });
    menu->addAction(tr("New demo show"), this, [this]() {
        if (confirmDiscard())
            newDemoShow();
    });
    menu->addSeparator();
    menu->addAction(tr("Open…"), this, [this]() {
        if (!confirmDiscard())
            return;
        QString p = QFileDialog::getOpenFileName(this, tr("Open show"), showFolder(), tr("Lumora shows (*.lumora)"));
        if (!p.isEmpty())
            openShow(p);
    });
    menu->addAction(tr("Save"), this, [this]() {
        if (m_path.isEmpty()) {
            QString p = QFileDialog::getSaveFileName(this, tr("Save show"), saveSuggestion(), tr("Lumora shows (*.lumora)"));
            if (!p.isEmpty())
                saveShow(p);
        } else {
            saveShow(m_path);
        }
    });
    menu->addAction(tr("Save as…"), this, [this]() {
        QString p = QFileDialog::getSaveFileName(this, tr("Save show"), saveSuggestion(), tr("Lumora shows (*.lumora)"));
        if (!p.isEmpty())
            saveShow(p);
    });
    menu->addSeparator();
    menu->addAction(tr("New view…"), this, &MainWindow::newView);
    menu->addAction(tr("Store this view"), this, [this]() {
        if (currentWorkspace())
            storeView(m_currentView);
    });
    menu->addAction(tr("Preset builder…"), this, [this]() { openPresetBuilder(m_ctx, this); });
    menu->addAction(tr("Config…"), this, &MainWindow::showInterfaceSettings);
    menu->addSeparator();
    // F1: the guide built in, searchable. The PDF is still there to print or read elsewhere.
    QAction *help = menu->addAction(tr("Help"), this, [this]() { HelpView::openWindow(m_ctx, this); });
    help->setShortcut(QKeySequence(Qt::Key_F1));
    auto *f1 = new QShortcut(QKeySequence(Qt::Key_F1), this);
    f1->setContext(Qt::WindowShortcut);
    connect(f1, &QShortcut::activated, this, [this]() { HelpView::openWindow(m_ctx, this); });
    menu->addAction(tr("User guide (PDF)"), this, [this]() { openUserGuide(); });
    menu->addSeparator();
    menu->addAction(tr("Quit"), this, &QWidget::close);
    menuButton->setMenu(menu);
    h->addWidget(menuButton);

    // Screens: a small button that opens into 1–6. Each extra screen is another window
    // (put it on another monitor) that recalls the show's views by itself.
    m_screenButton = new QToolButton(bar);
    m_screenButton->setText(QStringLiteral("▣ %1").arg(m_screen));
    m_screenButton->setToolTip(tr("Screens: open more windows of Lumora (up to %1)").arg(kMaxScreens));
    m_screenButton->setCheckable(true);
    m_screenButton->setMinimumHeight(30);   // placed at the far right, below
    m_screenRow = new QWidget(bar);
    auto *sr = new QHBoxLayout(m_screenRow);
    sr->setContentsMargins(0, 0, 6, 0);
    sr->setSpacing(3);
    for (int n = 1; n <= kMaxScreens; ++n) {
        auto *b = new QToolButton(m_screenRow);
        b->setText(QString::number(n));
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        sr->addWidget(b);
        m_screenButtons << b;
        connect(b, &QToolButton::clicked, this, [this, n]() {
            m_screenRow->hide();
            m_screenButton->setChecked(false);
            if (n == m_screen)
                refreshScreenButtons();
            else if (MainWindow *main = screens().value(1))
                main->openScreen(n);
        });
    }
    m_screenRow->hide();
    connect(m_screenButton, &QToolButton::toggled, this, [this](bool on) {
        refreshScreenButtons();
        m_screenRow->setVisible(on);
    });


    m_showName = new QLabel(bar);
    m_showName->setFont(theme::uiFont(11, QFont::DemiBold));
    h->addSpacing(6);
    h->addWidget(m_showName);
    h->addStretch(1);

    m_store = topButton(tr("Store"), "store", tr("Then tap a pool slot or executor to store the programmer there"), bar);
    m_learn = topButton(tr("Learn MIDI"), "learn",
                        tr("While on, click any executor fader or button, then move a control on your MIDI device"), bar);
    m_highlight = topButton(tr("Highlight"), "highlight", tr("Selected fixtures go to full white"), bar);
    auto *clear = topButton(tr("Clear"), "clear", tr("First clears the selection, then the programmer values"), bar, false);
    for (QToolButton *b : {m_store, m_highlight, clear})
        h->addWidget(b);
    h->addSpacing(10);
    m_keys = topButton(tr("Keypad"), "keypad", tr("Command keys on screen (Ctrl+K)"), bar);
    h->addWidget(m_keys);
    m_shortcuts = topButton(tr("Shortcuts"), "shortcuts",
                            tr("Single-key shortcuts: S Store, M Move, E Executor… They pause while you type in a text field. Insert turns them on and off."),
                            bar);
    m_shortcuts->setChecked(m_ctx->shortcutsEnabled());
    connect(m_shortcuts, &QToolButton::toggled, m_ctx, &AppContext::setShortcutsEnabled);
    connect(m_ctx, &AppContext::shortcutsChanged, this, [this](bool on) {
        QSignalBlocker b(m_shortcuts);
        m_shortcuts->setChecked(on);
        m_commandBar->showMessage(on ? tr("Keyboard shortcuts on (Insert turns them off)") : tr("Keyboard shortcuts off (Insert turns them on)"));
    });
    h->addWidget(m_shortcuts);
    h->addWidget(m_learn);
    auto *window = topButton(QStringLiteral("+"), "addWindow", tr("Add a window to this view (or double-click empty space)"), bar, false);
    window->setPopupMode(QToolButton::InstantPopup);
    window->setMenu(buildWindowMenu(window));
    window->setFixedWidth(36);
    window->setStyleSheet(QStringLiteral("QToolButton { font-size: 18px; font-weight: 600; padding: 0; }"
                                         "QToolButton::menu-indicator { image: none; width: 0; }"));
    h->addWidget(window);

    connect(m_store, &QToolButton::toggled, m_ctx, &AppContext::setStoreArmed);
    connect(m_learn, &QToolButton::toggled, this, [this](bool on) {
        m_ctx->midi->setLearnMode(on);
        m_commandBar->showMessage(on ? tr("Learn MIDI: click a fader or button, then move a control. Turn off when done.")
                                     : tr("Learn MIDI off"));
    });
    connect(m_highlight, &QToolButton::toggled, m_ctx->show, &Show::setHighlight);
    connect(clear, &QToolButton::clicked, this, [this]() {
        m_ctx->setStoreArmed(false);
        m_commandBar->showMessage(m_ctx->show->clearStep());
    });
    connect(m_keys, &QToolButton::toggled, this, &MainWindow::setKeypadVisible);

    // Grand master and blackout sit together; both pulse slowly while either holds output down.
    h->addSpacing(14);
    m_masterBox = new QFrame(bar);
    m_masterBox->setObjectName("masterBox");
    auto *mh = new QHBoxLayout(m_masterBox);
    mh->setContentsMargins(8, 2, 3, 2);
    mh->setSpacing(6);
    auto *gmLabel = new QLabel(tr("GM"), m_masterBox);
    gmLabel->setToolTip(tr("Grand master"));
    gmLabel->setFont(theme::uiFont(9, QFont::DemiBold));
    m_gm = new QSlider(Qt::Horizontal, m_masterBox);
    m_gm->setRange(0, 100);
    m_gm->setFixedWidth(110);
    m_gm->setToolTip(tr("Grand master"));
    m_gmValue = new QLabel(m_masterBox);
    m_gmValue->setMinimumWidth(34);
    m_gmValue->setFont(theme::uiFont(9));
    m_blackout = topButton(QStringLiteral("BO"), "blackout", tr("Blackout: all intensity to zero"), m_masterBox);
    m_blackout->setFixedWidth(44);
    mh->addWidget(gmLabel);
    mh->addWidget(m_gm);
    mh->addWidget(m_gmValue);
    mh->addWidget(m_blackout);
    h->addWidget(m_masterBox);
    connect(m_blackout, &QToolButton::toggled, m_ctx->show, &Show::setBlackout);
    auto *pulse = new QTimer(this);
    connect(pulse, &QTimer::timeout, this, [this]() {
        const bool down = m_ctx->show->blackout() || m_ctx->show->grandMaster() < 0.999;
        m_pulse = down && !m_pulse;
        const QString alert = theme::programmer.name();
        m_masterBox->setStyleSheet(
            m_pulse ? QStringLiteral("QFrame#masterBox { background: #3a2422; border: 1px solid %1; border-radius: 6px; }"
                                     "QFrame#masterBox QLabel { color: %1; }").arg(alert)
            : down  ? QStringLiteral("QFrame#masterBox { border: 1px solid #4a2b28; border-radius: 6px; }")
                    : QStringLiteral("QFrame#masterBox { border: 1px solid transparent; border-radius: 6px; }"));
    });
    pulse->start(650);
    connect(m_gm, &QSlider::valueChanged, this, [this](int v) {
        if (std::lround(m_ctx->show->grandMaster() * 100) != v)
            m_ctx->show->setGrandMaster(v / 100.0);
    });
    // Screens at the far right: the numbers open out to the left of the button.
    h->addSpacing(10);
    h->addWidget(m_screenRow);
    h->addWidget(m_screenButton);
    return bar;
}

void MainWindow::refreshTopBar()
{
    Show *show = m_ctx->show;
    QString name = show->name();
    if (m_dirty)
        name += QStringLiteral("  •");
    m_showName->setText(m_screen == 1 ? name : tr("Screen %1  ·  %2").arg(m_screen).arg(show->name()));
    QSignalBlocker b1(m_gm), b2(m_learn), b3(m_highlight), b4(m_blackout);
    m_gm->setValue(int(std::lround(show->grandMaster() * 100)));
    m_gmValue->setText(QStringLiteral("%1%").arg(std::lround(show->grandMaster() * 100)));
    m_learn->setChecked(m_ctx->midi->learnMode());
    m_highlight->setChecked(show->highlight());
    m_blackout->setChecked(show->blackout());
}

void MainWindow::setDirty(bool d)
{
    m_dirty = d;
    setWindowTitle(QStringLiteral("%1%2 — Lumora %3").arg(m_ctx->show->name(), d ? QStringLiteral(" (unsaved)") : QString(),
                                                          QApplication::applicationVersion()));
    refreshTopBar();
}

bool MainWindow::confirmDiscard()
{
    if (m_developerRun)
        return true;
    if (!m_dirty)
        return true;
    auto r = QMessageBox::question(this, tr("Unsaved changes"), tr("Save changes to \"%1\" first?").arg(m_ctx->show->name()),
                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel)
        return false;
    if (r == QMessageBox::Save) {
        QString p = m_path;
        if (p.isEmpty())
            p = QFileDialog::getSaveFileName(this, tr("Save show"), saveSuggestion(), tr("Lumora shows (*.lumora)"));
        return !p.isEmpty() && saveShow(p);
    }
    return true;
}

bool MainWindow::saveShow(const QString &path)
{
    QString p = path;
    if (!p.endsWith(".lumora"))
        p += ".lumora";
    Show *show = m_ctx->show;
    // The show is called what its file is called: Save as (from the demo, or anything else)
    // renames it, and the top bar follows.
    show->setName(QFileInfo(p).completeBaseName());
    show->setExtra("midi", m_ctx->midi->toJson());
    show->removeExtra("outputs");   // network settings stay on this computer
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Couldn't save"), tr("%1: %2").arg(p, f.errorString()));
        return false;
    }
    f.write(QJsonDocument(show->toJson()).toJson(QJsonDocument::Indented));
    m_path = p;
    QSettings().setValue("lastShow", p);
    setDirty(false);
    m_commandBar->showMessage(tr("Saved %1").arg(QFileInfo(p).fileName()));
    return true;
}

bool MainWindow::openShow(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Couldn't open"), tr("%1: %2").arg(path, f.errorString()));
        return false;
    }
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    QString err;
    m_loading = true;
    const bool hadOutputs = QSettings().contains("network/outputs");   // before the show adds any
    const bool ok = !doc.isNull() && m_ctx->show->fromJson(doc.object(), &err);
    if (ok) {
        m_ctx->midi->fromJson(m_ctx->show->extra("midi"));
        // Outputs are this computer's network settings. A show from before that carries some is
        // only used when this computer had none yet.
        if (!hadOutputs && !m_ctx->show->extra("outputs").isEmpty()) {
            m_ctx->outputs->fromJson(m_ctx->show->extra("outputs"));
            m_ctx->refreshOutputs();
        }
        m_ctx->show->removeExtra("outputs");
        m_ctx->show->setName(QFileInfo(path).completeBaseName());   // named after its file
    }
    m_loading = false;
    if (!ok) {
        QMessageBox::warning(this, tr("Couldn't open"), doc.isNull() ? pe.errorString() : err);
        return false;
    }
    m_path = path;
    QSettings().setValue("lastShow", path);
    setDirty(false);
    m_commandBar->showMessage(tr("Opened %1").arg(QFileInfo(path).fileName()));
    return true;
}

void MainWindow::newDemoShow()
{
    m_loading = true;
    buildDemoShow(m_ctx);
    m_loading = false;
    m_path.clear();
    setDirty(false);
    if (!m_views.isEmpty())
        showWidget(m_views.first().page);
    m_commandBar->showMessage(tr("Demo show loaded: raise a fader in Playback, or type Group 1 At 50"));
}

void MainWindow::closeEvent(QCloseEvent *ev)
{
    if (m_screen == 1 && !confirmDiscard()) {
        ev->ignore();
        return;
    }
    QSettings().setValue(settingsPrefix() + "ui/window", saveGeometry());
    if (m_screen == 1) {
        // Closing the main window closes the other screens; they reopen next time.
        rememberOpenScreens();
        for (auto it = screens().begin(); it != screens().end(); ++it)
            if (it.key() > 1 && it.value()) {
                it.value()->m_closingAll = true;
                it.value()->close();
            }
    } else if (!m_closingAll) {
        screens().remove(m_screen);
        rememberOpenScreens();
        for (const QPointer<MainWindow> &w : screens())
            if (w)
                w->refreshScreenButtons();
    }
    ev->accept();
}

void MainWindow::openScreen(int n)
{
    if (n < 1 || n > kMaxScreens)
        return;
    MainWindow *w = screens().value(n);
    if (!w) {
        w = new MainWindow(m_ctx, n);
        w->show();
        rememberOpenScreens();
    }
    w->showNormal();
    w->raise();
    w->activateWindow();
    for (const QPointer<MainWindow> &s : screens())
        if (s)
            s->refreshScreenButtons();
}

void MainWindow::restoreScreens()
{
    for (const QString &n : QSettings().value("screens/open").toStringList())
        if (n.toInt() > 1 && n.toInt() <= kMaxScreens)
            openScreen(n.toInt());
    activateWindow();
}

void MainWindow::refreshScreenButtons()
{
    if (!m_screenButton)
        return;
    for (int i = 0; i < m_screenButtons.size(); ++i) {
        const int n = i + 1;
        QToolButton *b = m_screenButtons[i];
        const bool open = screens().value(n);
        b->setChecked(n == m_screen);
        b->setProperty("open", open);
        b->setToolTip(n == m_screen ? tr("This is screen %1").arg(n)
                      : open        ? tr("Bring screen %1 to the front").arg(n)
                                    : tr("Open screen %1").arg(n));
        b->setStyleSheet(QStringLiteral("QToolButton { min-width: 24px; padding: 2px 6px; %1 }"
                                        "QToolButton:checked { background: #3a3220; color: %2; border-color: %2; }")
                             .arg(open ? QStringLiteral("border-color: %1; color: %2;").arg(theme::line.name(), theme::text.name())
                                       : QStringLiteral("color: %1;").arg(theme::muted.name()),
                                  theme::live.name()));
    }
}

// ===========================================================================
// Demo show
// ===========================================================================

void buildDemoShow(AppContext *ctx)
{
    Show *s = ctx->show;
    s->clearShow();
    ctx->midi->fromJson(QJsonObject());
    s->setName(QObject::tr("Demo show"));
    s->setViews(standardViews());

    // The rig: 8 moving washes, 4 profiles (framing shutters, gobos, prism), 8 pixel bars of 8
    // cells and 4 two-cell blinders.
    //   Washes 1–8      universe 1 from 1      Profiles 11–14   universe 1 from 121
    //   Blinders 31–34  universe 1 from 241    Pixel bars 21–28 universe 2 from 1 (cells 21.1–21.8…)
    for (int i = 0; i < 8; ++i)
        s->addFixture({1 + i, QStringLiteral("Wash %1").arg(i + 1), "generic/moving-wash", "15-channel", 1, 1 + i * 15});
    for (int i = 0; i < 4; ++i)
        s->addFixture({11 + i, QStringLiteral("Profile %1").arg(i + 1), "generic/moving-profile", "27-channel", 1, 121 + i * 27});
    for (int i = 0; i < 8; ++i)
        s->addFixture({21 + i, QStringLiteral("Bar %1").arg(i + 1), "generic/pixel-bar", "26-channel", 2, 1 + i * 26});
    for (int i = 0; i < 4; ++i)
        s->addFixture({31 + i, QStringLiteral("Blinder %1").arg(i + 1), "generic/blinder-2", "2-channel", 1, 241 + i * 2});

    CommandLine cl(s);
    auto storeGroup = [&](const QString &select, int id, const QString &name) {
        s->clearSelection();
        cl.execute(select);
        cl.execute(QStringLiteral("Store Group %1 \"%2\"").arg(id).arg(name));
    };
    storeGroup("Fixture 1 Thru 8", 1, QObject::tr("Washes"));
    storeGroup("Fixture 11 Thru 14", 2, QObject::tr("Profiles"));
    // The pixel bars as an 8 × 8 selection matrix: each bar's cells along X, one bar per Y.
    {
        QStringList parts;
        for (int bar = 0; bar < 8; ++bar)
            parts << QStringLiteral("Fixture %1.1 Thru %1.8").arg(21 + bar);
        storeGroup(parts.join(QStringLiteral(" NextY ")), 3, QObject::tr("Pixel bars"));
    }
    // The blinders 4 × 2: the four along X, top cells at Y 0 and bottom cells at Y 1.
    storeGroup("Fixture 31.1 + 32.1 + 33.1 + 34.1 NextY Fixture 31.2 + 32.2 + 33.2 + 34.2", 4, QObject::tr("Blinders"));
    storeGroup("Fixture 1 Thru 8 + 11 Thru 14", 5, QObject::tr("Movers"));
    s->clearSelection();

    // Spreads to start from.
    {
        auto add = [s](int id, const QString &name, std::function<void(Spread &)> fn) {
            Spread m;
            m.id = id;
            m.name = name;
            fn(m);
            s->setSpread(m);
        };
        add(1, QObject::tr("Odd"), [](Spread &m) { m.every = 2; });
        add(2, QObject::tr("Even"), [](Spread &m) { m.every = 2; m.offset = 1; });
        add(3, QObject::tr("Together"), [](Spread &m) { m.phaseTo = 0; });
        add(4, QObject::tr("Wings 2"), [](Spread &m) { m.wings = 2; });
        add(5, QObject::tr("Blocks 2"), [](Spread &m) { m.blocks = 2; });
        add(6, QObject::tr("Middle out"), [](Spread &m) { m.direction = SpreadDirection::Outward; });
        add(7, QObject::tr("Alternate bars"), [](Spread &m) {
            m.every = 2;
            m.onX = false;
            m.onY = true;
        });
    }

    // The standard colors as Universal color presets 1–13, then a 3200K warm white as 14.
    {
        auto colorPreset = [&](int id, const QString &name, const LinearRgb &lin, const QColor &display) {
            const double peak = std::max({lin.r, lin.g, lin.b, 1e-6});
            const double r = lin.r / peak, g = lin.g / peak, b = lin.b / peak;
            Preset p;
            p.type = int(Feature::Color);
            p.id = id;
            p.name = name;
            p.universal = {{"ColorAdd_R", r}, {"ColorAdd_G", g}, {"ColorAdd_B", b},
                           {"ColorAdd_W", std::min({r, g, b})}, {"ColorAdd_A", 0.0}, {"ColorAdd_UV", 0.0},
                           {"ColorSub_C", 1 - r}, {"ColorSub_M", 1 - g}, {"ColorSub_Y", 1 - b}};
            s->setPreset(p);
        };
        const QVector<Swatch> &colors = standardColors();
        for (int i = 0; i < colors.size(); ++i)
            colorPreset(i + 1, colors[i].name, toLinear(colors[i].color), colors[i].color);
        const LinearRgb warm = kelvinToLinear(3200);
        colorPreset(int(colors.size()) + 1, QStringLiteral("3200K"), warm, toDisplay(warm));
    }

    // Universal Dimmer presets: blueprint lines get their level from these.
    {
        const QPair<double, QString> levels[] = {{1.0, QObject::tr("Full")}, {0.25, QObject::tr("25%")}, {0.0, QObject::tr("Out")}};
        for (int i = 0; i < 3; ++i) {
            Preset p;
            p.type = int(Feature::Dimmer);
            p.id = i + 1;
            p.name = levels[i].second;
            p.universal = {{"Dimmer", levels[i].first}};
            s->setPreset(p);
        }
    }

    // Positions for the washes and the profiles: Center, Audience, Fan.
    s->clearAll();
    cl.execute("Group 5");
    s->setAttribute("Pan", 0.0);   // degrees: straight ahead
    s->setAttribute("Tilt", 0.0);
    s->storePreset(2, 1, Show::StoreMode::Overwrite, QObject::tr("Center"));
    s->setAttribute("Tilt", -50.0);
    s->storePreset(2, 2, Show::StoreMode::Overwrite, QObject::tr("Audience"));
    for (int i = 0; i < 8; ++i) {
        s->setAttribute(1 + i, "Pan", -70.0 + i * 20.0);
        s->setAttribute(1 + i, "Tilt", -25.0);
    }
    const double fan[] = {-60.0, -20.0, 20.0, 60.0};
    for (int i = 0; i < 4; ++i) {
        s->setAttribute(11 + i, "Pan", fan[i]);
        s->setAttribute(11 + i, "Tilt", -20.0);
    }
    s->storePreset(2, 3, Show::StoreMode::Overwrite, QObject::tr("Fan"));

    // Gobos, prism and frost on the profiles; zoom on the washes (Focus presets).
    s->clearAll();
    cl.execute("Group 2");
    const QStringList gobos = {QObject::tr("Dots"), QObject::tr("Ring"), QObject::tr("Breakup"), QObject::tr("Star"), QObject::tr("Triangles")};
    for (int i = 0; i < gobos.size(); ++i) {
        s->setAttribute("Gobo1", (30.0 + i * 20.0) / 255.0);   // the middle of slots 2–6
        s->storePreset(3, i + 1, Show::StoreMode::Overwrite, gobos[i]);
    }
    s->clearAll();
    cl.execute("Group 2");
    s->setAttribute("Prism1", 0.8);
    s->storePreset(5, 1, Show::StoreMode::Overwrite, QObject::tr("Prism"));
    s->clearAll();
    cl.execute("Group 2");
    s->setAttribute("Frost1", 0.6);
    s->storePreset(5, 2, Show::StoreMode::Overwrite, QObject::tr("Frost"));
    s->clearAll();
    cl.execute("Group 1");
    s->setAttribute("Zoom", 1.0);   // zoom is a Focus attribute
    s->storePreset(6, 1, Show::StoreMode::Overwrite, QObject::tr("Zoom wide"));
    s->setAttribute("Zoom", 0.0);
    s->storePreset(6, 2, Show::StoreMode::Overwrite, QObject::tr("Zoom narrow"));
    s->clearAll();

    // Effects: the whole library in the pool, then three for the pixel matrix.
    const QVector<Effect> templates = effectTemplates();
    for (int i = 0; i < templates.size(); ++i) {
        Effect e = templates[i];
        e.id = i + 1;
        s->setEffect(e);
    }
    auto effectId = [s](const QString &name) {
        for (auto it = s->effects().begin(); it != s->effects().end(); ++it)
            if (it->name == name)
                return it.key();
        return 0;
    };
    int nextEffect = s->effects().isEmpty() ? 1 : s->effects().lastKey() + 1;
    auto pixelEffect = [&](const QString &from, const QString &name, bool x, bool y) {
        const int src = effectId(from);
        if (!src)
            return 0;
        Effect e = *s->effect(src);
        e.id = nextEffect++;
        e.name = name;
        e.axisX = x;
        e.axisY = y;
        s->setEffect(e);
        return e.id;
    };
    const int across = pixelEffect(QObject::tr("Chase"), QObject::tr("Pixels across"), true, false);
    const int down = pixelEffect(QObject::tr("Chase"), QObject::tr("Pixels down"), false, true);
    const int diagonal = pixelEffect(QObject::tr("Rainbow"), QObject::tr("Pixel rainbow"), true, true);

    // Presets that carry effects: each one is used by a cue below.
    auto effectPreset = [&](const QString &select, const QString &before, const QString &effect, const QString &store) {
        s->clearAll();
        cl.execute(select);
        if (!before.isEmpty())
            cl.execute(before);
        if (const int id = effectId(effect))
            cl.execute(QStringLiteral("Effect %1").arg(id));
        cl.execute(store);
    };
    effectPreset("Group 1", QString(), QObject::tr("Chase"), "Store Dimmer.4 \"Chase\" /universal");
    effectPreset("Group 1", QString(), QObject::tr("Breathe"), "Store Dimmer.5 \"Breathe\" /universal");
    effectPreset("Group 1", QString(), QObject::tr("Rainbow"), "Store Color.15 \"Rainbow\" /universal");
    effectPreset("Group 1", QString(), QObject::tr("Color swap"), "Store Color.16 \"Color swap\" /universal");
    effectPreset("Group 5", "At Position.1", QObject::tr("Circle"), "Store Position.4 \"Circle\" /global");
    effectPreset("Group 5", "At Position.2", QObject::tr("Ballyhoo"), "Store Position.5 \"Ballyhoo\" /global");
    effectPreset("Group 1", QString(), QObject::tr("Zoom breathe"), "Store Focus.3 \"Zoom breathe\" /universal");
    effectPreset("Group 4", QString(), QObject::tr("Random strobe"), "Store All.1 \"Random strobe\" /universal");
    s->clearAll();

    const PresetRef full{1, 1}, quarter{1, 2};
    auto line = [](const QString &name, int group, QVector<PresetRef> presets, int spread = 0) {
        BlueprintLine l;
        l.name = name;
        l.groupId = group;
        l.presets = presets;
        l.spreadId = spread;
        return l;
    };
    auto withEffects = [](BlueprintLine l, QVector<int> fx) {
        l.effects = fx;
        return l;
    };
    // 1 Washes: blue, a two-color split on spreads, a rainbow circle, a chase to the audience.
    {
        BlueprintLine blue = line("Washes blue", 1, {full, {4, 9}, {2, 1}});
        blue.fadeTimes = {0.0, 3.0};   // they come up one after another
        s->addBlueprintLine(1, 1, blue);
        s->addBlueprintLine(1, 2, line("Odd red", 1, {full, {4, 1}, {2, 3}}, 1));
        s->addBlueprintLine(1, 2, line("Even orange", 1, {full, {4, 2}, {2, 3}}, 2));
        s->addBlueprintLine(1, 3, line("Rainbow circle", 1, {full, {4, 15}, {2, 4}, {6, 1}}, 4));
        s->addBlueprintLine(1, 4, line("Chase to the audience", 1, {{1, 4}, {4, 13}, {2, 2}, {6, 3}}, 6));
        s->addBlueprintLine(1, 5, line("Breathing color swap", 1, {{1, 5}, {4, 16}, {2, 3}}, 5));
    }
    // 2 Profiles: dots in warm white, a fanned prism, a ballyhoo.
    {
        s->addBlueprintLine(2, 1, line("Profiles center dots", 2, {full, {2, 1}, {3, 1}, {4, 14}}));
        s->addBlueprintLine(2, 2, line("Fan, ring, prism", 2, {full, {2, 3}, {3, 2}, {5, 1}, {4, 11}}));
        s->addBlueprintLine(2, 3, line("Ballyhoo stars", 2, {full, {2, 5}, {3, 4}, {4, 9}, {5, 2}}, 4));
    }
    // 3 Pixel bars: chases across and down the matrix, a rainbow corner to corner, alternate bars.
    {
        s->addBlueprintLine(3, 1, withEffects(line("White chase across", 3, {full, {4, 13}}), {across}));
        s->addBlueprintLine(3, 2, withEffects(line("Blue chase down", 3, {full, {4, 9}}), {down}));
        s->addBlueprintLine(3, 3, withEffects(line("Rainbow corner to corner", 3, {full}), {diagonal}));
        s->addBlueprintLine(3, 4, line("Alternate bars red", 3, {full, {4, 1}}, 7));
    }
    // 4 Blinders: full, with a random strobe, while the Temp key is held.
    s->addBlueprintLine(4, 1, line("Blinders", 4, {full, {8, 1}}));

    // Macros.
    {
        auto macro = [s](int id, const QString &name, const QString &color, bool background, const QVector<MacroLine> &lines) {
            Macro m;
            m.id = id;
            m.name = name;
            m.color = color;
            m.background = background;
            m.lines = lines;
            s->setMacro(m);
        };
        macro(1, QObject::tr("Blue washes"), "#0050ff", false, {{"ClearAll"}, {"Group 1 At Color.9"}, {"At 100 Fade 0 Thru 3"}});
        macro(2, QObject::tr("Washes, tap for next"), "#ff7800", true,
              {{"Go Executor 201", MacroWait::Wait}, {"Go Executor 201", MacroWait::Wait}, {"Off Executor 201"}});
        macro(3, QObject::tr("Profiles in"), "#a0ff00", false,
              {{"ClearAll"}, {"Group 2 At 100"}, {"Attribute Blade1A At 30"}, {"Attribute Blade3A At 30"}});
        macro(4, QObject::tr("Profiles to the audience"), "#e0a030", false, {{"Goto Cue 3 Executor 202"}});
    }

    // The cue lists, named and colored, on faders 201–204.
    {
        struct SeqInfo {
            const char *name;
            const char *color;
            QVector<const char *> cues;
            QVector<double> fades;
        };
        const SeqInfo info[4] = {
            {"Washes", "#3d7be0", {"Blue wash", "Red and orange", "Rainbow circle", "Chase", "Breathing swap"}, {2, 3, 2, 1.5, 3}},
            {"Profiles", "#e0a030", {"Center dots", "Fan with a prism", "Ballyhoo"}, {2, 2.5, 3}},
            {"Pixel bars", "#30c0a0", {"White chase across", "Blue chase down", "Rainbow corner to corner", "Alternate bars"}, {1, 2, 3, 1}},
            {"Blinders", "#f0f0f0", {"Blinders"}, {0}},
        };
        for (int sid = 1; sid <= 4; ++sid) {
            const Sequence *have = s->sequence(sid);
            if (!have)
                continue;
            Sequence q = *have;
            q.name = QString::fromLatin1(info[sid - 1].name);
            q.color = QString::fromLatin1(info[sid - 1].color);
            for (int i = 0; i < q.cues.size() && i < info[sid - 1].cues.size(); ++i) {
                q.cues[i].name = QString::fromLatin1(info[sid - 1].cues[i]);
                q.cues[i].fade = info[sid - 1].fades[i];
            }
            if (sid == 3 && q.cues.size() >= 4) {
                q.cues[1].trigger = CueTrigger::Wait;   // eight seconds after the white chase, by itself
                q.cues[1].wait = 8;
                q.cues[1].notes = QObject::tr("Follows on by itself");
                q.cues[3].macro = 4;
                q.cues[3].notes = QObject::tr("Runs macro 4: profiles to the audience");
            }
            if (sid == 4) {
                q.offFade = 0.7;   // the blinders snap on and fall away
                q.cues[0].notes = QObject::tr("Hold the Temp key");
            }
            s->setSequence(q);
            s->assignSequence(sid, 1, 200 + sid);
        }
        // The blinders' executor has only a Temp key.
        if (Executor *e = s->executor(1, 204)) {
            e->buttons[0] = ButtonFn::Temp;
            e->buttons[1] = ButtonFn::None;
            e->buttons[2] = ButtonFn::None;
        }
    }

    // A plot of the rig: profiles on the upstage truss, washes on the mid truss, the pixel bars'
    // cells as a wall, blinders downstage, and things to tap down the side.
    {
        Plot plot;
        plot.id = 1;
        plot.name = QObject::tr("Rig");
        auto fixture = [&plot](int fid, double x, double y, bool round = false, double size = 48) {
            PlotItem it;
            it.kind = PlotItem::Fixture;
            it.fid = fid;
            it.round = round;
            it.rect = QRectF(x, y, size, size);
            plot.items << it;
        };
        auto text = [&plot](const QString &t, double x, double y, double w, double size, const QString &color) {
            PlotItem it;
            it.kind = PlotItem::Text;
            it.text = t;
            it.fontSize = size;
            it.color = color;
            it.rect = QRectF(x, y, w, size * 1.8);
            plot.items << it;
        };
        auto object = [&plot](const QString &o, double x, double y) {
            PlotItem it;
            it.kind = PlotItem::Object;
            it.object = o;
            it.rect = QRectF(x, y, 128, 48);
            plot.items << it;
        };
        auto icon = [&plot](int index, double x, double y, double size) {
            const auto icons = builtinPlotIcons();
            PlotItem it;
            it.kind = PlotItem::Image;
            it.image = icons[index].second;
            it.imageName = icons[index].first;
            it.color = QStringLiteral("#5b6472");
            it.rect = QRectF(x, y, size, size);
            plot.items << it;
        };
        auto truss = [&plot](double y) {
            PlotItem t;
            t.kind = PlotItem::Image;
            t.image = builtinPlotIcons()[1].second;
            t.imageName = QStringLiteral("Truss");
            t.color = QStringLiteral("#5b6472");
            t.rect = QRectF(0, y, 600, 16);
            plot.items << t;
        };
        text(QObject::tr("UPSTAGE"), 0, 0, 200, 13, QStringLiteral("#7d8796"));
        plot.items.last().link = QStringLiteral("Group 2");   // tapping the label selects the profiles
        truss(56);
        for (int i = 0; i < 4; ++i)
            fixture(11 + i, 84 + i * 128, 40);
        // The pixel wall: eight bars, one above the other.
        for (int bar = 0; bar < 8; ++bar)
            for (int c = 1; c <= 8; ++c)
                fixture(s->parseId(QStringLiteral("%1.%2").arg(21 + bar).arg(c)), 140 + (c - 1) * 40, 108 + bar * 26, true, 22);
        truss(334);
        for (int i = 0; i < 8; ++i)
            fixture(1 + i, 12 + i * 74, 318, true);
        icon(4, 280, 384, 40);   // a performer
        for (int i = 0; i < 4; ++i)
            for (int c = 1; c <= 2; ++c)
                fixture(s->parseId(QStringLiteral("%1.%2").arg(31 + i).arg(c)), 40 + i * 148 + (c - 1) * 40, 440, false, 36);
        text(QObject::tr("DOWNSTAGE"), 0, 488, 200, 13, QStringLiteral("#7d8796"));
        icon(3, 0, 200, 40);     // speakers either side, grouped so they move together
        plot.items.last().group = 1;
        icon(3, 560, 200, 40);
        plot.items.last().group = 1;
        object(QStringLiteral("Group 1"), 640, 40);
        object(QStringLiteral("Group 2"), 640, 96);
        object(QStringLiteral("Group 3"), 640, 152);
        object(QStringLiteral("Group 4"), 640, 208);
        object(QStringLiteral("Preset 4.1"), 784, 40);
        object(QStringLiteral("Preset 4.9"), 784, 96);
        object(QStringLiteral("Preset 4.14"), 784, 152);
        object(QStringLiteral("Preset 4.15"), 784, 208);
        object(QStringLiteral("Executor 1.201"), 640, 296);
        object(QStringLiteral("Executor 1.204"), 784, 296);
        object(QStringLiteral("Macro 1"), 640, 352);
        object(QStringLiteral("Macro 3"), 784, 352);
        s->setPlot(plot);
    }
    s->setSelectedSequence(1);
    s->clearAll();
}

// Opens Lumora-Manual.pdf: next to the program (where builds and releases put it), in a docs
// folder beside it, or in the source tree.
void MainWindow::openUserGuide()
{
    const QString name = QStringLiteral("Lumora-Manual.pdf");
    const QString app = QCoreApplication::applicationDirPath();
    const QStringList candidates = {app + '/' + name, app + QStringLiteral("/docs/") + name,
                                    app + QStringLiteral("/../Resources/") + name,
                                    QStringLiteral(LUMORA_SOURCE_DIR "/docs/") + name};
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c)) {
            if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::cleanPath(c))))
                QMessageBox::information(this, tr("User guide"),
                                         tr("Couldn't open a PDF viewer. The guide is here:\n%1").arg(QDir::toNativeSeparators(QDir::cleanPath(c))));
            return;
        }
    }
    QMessageBox::information(this, tr("User guide"),
                             tr("The user guide (%1) wasn't found next to Lumora. It's in the docs folder of the Lumora download.").arg(name));
}

} // namespace lumora
