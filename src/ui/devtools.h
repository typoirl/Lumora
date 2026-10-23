// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QCommandLineOption>
#include <QCommandLineParser>

namespace lumora {

class AppContext;
class MainWindow;

// The developer options docs/manual/make_screenshots.py uses to take the guide's pictures:
// open a page, run commands, open a window or dialog, save a picture and quit. Kept apart from
// main() so the program's own startup reads plainly.
class DeveloperRun {
public:
    explicit DeveloperRun(QCommandLineParser &parser);
    // A picture run: no loading screen, and the screens that were open last time stay closed.
    bool active() const;
    // After the show is open and the main window shows: sets up whatever was asked for.
    void start(AppContext &ctx, MainWindow &window);

private:
    QCommandLineParser &m_parser;
    QCommandLineOption m_screenshot{"screenshot", "Developer: save a window screenshot to <file> and quit", "file"};
    QCommandLineOption m_page{"page", "Developer: page to show (views in rail order, then Patch, Outputs, Control)", "index", "0"};
    QCommandLineOption m_run{"run", "Developer: run command lines (separated by ;) after startup", "commands"};
    QCommandLineOption m_keypad{"keypad", "Developer: open the command keypad"};
    QCommandLineOption m_store{"store", "Developer: arm Store"};
    QCommandLineOption m_editEffect{"edit-effect", "Developer: open an effect in the editor", "id"};
    QCommandLineOption m_libraryShot{"library-shot", "Developer: open the fixture library, save a screenshot to <file> and quit", "file"};
    QCommandLineOption m_librarySearch{"library-search", "Developer: source:search[:import] for --library-shot", "query", "1:"};
    QCommandLineOption m_addWindow{"add-window", "Developer: add a window of this type to the shown view", "type"};
};

} // namespace lumora
