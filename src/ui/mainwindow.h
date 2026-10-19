// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonArray>
#include <QList>
#include <QMainWindow>

class QButtonGroup;
class QFrame;
class QLabel;
class QMenu;
class QSlider;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace lumora {

class AppContext;
class CommandBar;
class CommandKeypad;
class EncoderBar;
class ExecutorBar;
class StoreExecutorPicker;
class Workspace;
struct StoredView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // Screen 1 is the main window; screens 2–6 are extra windows (for more monitors) with their
    // own views. Only screen 1 has the setup pages, the encoders and the show file.
    explicit MainWindow(AppContext *ctx, int screen = 1, QWidget *parent = nullptr);
    static constexpr int kMaxScreens = 6;
    void openScreen(int n);           // opens (or brings to the front) screen n
    void restoreScreens();            // screen 1: reopen the screens that were open last time

    bool openShow(const QString &path);
    bool saveShow(const QString &path);
    void newDemoShow();
    // The screen in front (the one keys and Store View go to); screen 1 when none is.
    static MainWindow *activeScreen();
    // Developer helper: 0..n-1 are the views in rail order, then Patch, Outputs, MIDI.
    void showPage(int index);
    void setKeypadVisible(bool on);
    Workspace *currentWorkspace() const;
    // Developer runs (screenshots) quit without the unsaved-changes question.
    void setDeveloperRun(bool on) { m_developerRun = on; }

protected:
    void closeEvent(QCloseEvent *ev) override;
    void resizeEvent(QResizeEvent *ev) override;
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    // A stored view as this screen shows it. The windows can be changed freely; pressing the
    // view again recalls what was stored, and Store View keeps the changes.
    struct View {
        int id = 0;
        QString name;
        Workspace *workspace = nullptr;
        QWidget *page = nullptr;
        QToolButton *button = nullptr;
        QJsonArray recalled;   // the windows as this screen last recalled them
        QJsonArray stored;     // the show's layout when that happened
    };

    QWidget *buildTopBar();
    QMenu *buildWindowMenu(QWidget *parent);
    void syncViews(bool reloadAll = false);   // follow the show's stored views
    View *viewById(int id);
    void recallView(int id);                  // press a view button (or "View n")
    void storeView(int id);                   // the windows on this screen into view n
    void newView();
    void renameView(int id);
    void deleteView(int id);
    void updateViewButton(View &v);
    void rebuildRail();
    void showWidget(QWidget *page);
    void showInterfaceSettings();
    void openUserGuide();
    QString saveSuggestion() const;   // Documents/Lumora/<show name>.lumora, or where it was saved
    void refreshTopBar();
    void updateBottomBars();
    bool confirmDiscard();
    bool m_developerRun = false;
    void setDirty(bool d);

    AppContext *m_ctx = nullptr;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_rail = nullptr;
    QVBoxLayout *m_viewButtons = nullptr;
    QButtonGroup *m_nav = nullptr;
    QList<View> m_views;
    QList<QWidget *> m_setupPages;
    QList<QToolButton *> m_setupButtons;
    CommandBar *m_commandBar = nullptr;
    ExecutorBar *m_execStrip = nullptr;            // Learn MIDI: real faders and buttons to click
    StoreExecutorPicker *m_storePicker = nullptr;   // Store: a tile per executor, 101 to 4xx
    EncoderBar *m_encoders = nullptr;
    CommandKeypad *m_keypad = nullptr;
    QLabel *m_showName = nullptr;
    QLabel *m_fps = nullptr;
    QSlider *m_gm = nullptr;
    QToolButton *m_store = nullptr;
    QToolButton *m_learn = nullptr;
    QToolButton *m_highlight = nullptr;
    QToolButton *m_blackout = nullptr;
    QToolButton *m_keys = nullptr;
    QToolButton *m_shortcuts = nullptr;
    QFrame *m_masterBox = nullptr;
    QLabel *m_gmValue = nullptr;
    bool m_pulse = false;
    int m_screen = 1;
    QWidget *m_screenRow = nullptr;
    QToolButton *m_screenButton = nullptr;
    QList<QToolButton *> m_screenButtons;
    bool m_closingAll = false;
    void refreshScreenButtons();
    QString settingsPrefix() const;
    int m_currentView = 0;
    QToolButton *m_newViewButton = nullptr;
    QString m_newViewStyle;
    QString m_path;
    bool m_dirty = false;
    bool m_loading = false;
};

// Builds a small show (pars, spots, groups, presets, sequences with blueprints)
// so the console does something useful on first launch.
void buildDemoShow(AppContext *ctx);
// The starter views (Program, Sequences, Effects, Playback) new shows begin with.
QList<StoredView> standardViews();

} // namespace lumora
