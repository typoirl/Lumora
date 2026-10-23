// SPDX-License-Identifier: GPL-3.0-or-later
#include "devtools.h"

#include "appcontext.h"
#include "fixtureeditor.h"
#include "librarybrowser.h"
#include "macrosui.h"
#include "mainwindow.h"
#include "plotsui.h"
#include "presetbuilder.h"
#include "workspace.h"

#include <QApplication>
#include <QDialog>
#include <QJsonDocument>
#include <QTableWidget>
#include <QTimer>

namespace lumora {

namespace {
// Saves a picture of `w` after `ms`, then quits.
void shootAndQuit(QWidget *w, const QString &file, int ms = 800)
{
    QTimer::singleShot(ms, w, [w, file]() {
        w->grab().save(file);
        QApplication::quit();
    });
}
} // namespace

DeveloperRun::DeveloperRun(QCommandLineParser &parser) : m_parser(parser)
{
    parser.addOptions({m_screenshot, m_page, m_run, m_keypad, m_store, m_editEffect, m_libraryShot, m_librarySearch, m_addWindow});
}

bool DeveloperRun::active() const
{
    for (const char *v : {"LUMORA_EDITOR_SHOT", "LUMORA_MACRO_SHOT", "LUMORA_LINK_SHOT", "LUMORA_DIALOG_SHOT", "LUMORA_CHOICE_SHOT"})
        if (qEnvironmentVariableIsSet(v))
            return true;
    return m_parser.isSet(m_screenshot) || m_parser.isSet(m_libraryShot);
}

void DeveloperRun::start(AppContext &ctx, MainWindow &window)
{
    if (qEnvironmentVariableIsSet("LUMORA_ENCODER_CATEGORY"))   // which encoder category shows
        ctx.setUiValue("encoderCategory", qEnvironmentVariableIntValue("LUMORA_ENCODER_CATEGORY"));
    if (m_parser.isSet(m_run))
        for (const QString &c : m_parser.value(m_run).split(';'))
            ctx.run(c);

    if (qEnvironmentVariableIsSet("LUMORA_MACRO_SHOT")) {   // "macroId|file": the macro editor
        const QStringList parts = qEnvironmentVariable("LUMORA_MACRO_SHOT").split('|');
        auto *d = new MacroEditorDialog(&ctx, parts.value(0).toInt(), &window);
        d->show();
        shootAndQuit(d, parts.value(1));
    }
    if (qEnvironmentVariableIsSet("LUMORA_EDITOR_SHOT")) {   // "typeId|file": the fixture editor
        const QStringList parts = qEnvironmentVariable("LUMORA_EDITOR_SHOT").split('|');
        auto *d = new FixtureEditorDialog(&ctx, parts.value(0), &window);
        d->show();
        for (QTableWidget *table : d->findChildren<QTableWidget *>())
            if (table->rowCount() > 4)
                table->selectRow(4);
        shootAndQuit(d, parts.value(1));
    }

    // Dialogs to open for LUMORA_DIALOG_SHOT.
    if (qEnvironmentVariableIsSet("LUMORA_OPEN_BUILDER"))
        QTimer::singleShot(500, &window, [&ctx, &window]() { openPresetBuilder(&ctx, &window); });
    if (qEnvironmentVariableIsSet("LUMORA_OPEN_MACRO_LIBRARY"))
        QTimer::singleShot(500, &window, [&ctx, &window]() { MacroLibraryDialog::open(&ctx, 1, &window); });
    if (qEnvironmentVariableIsSet("LUMORA_DIALOG_SHOT")) {
        // After --run, save whichever dialog is open (Goto, say) and quit.
        const QString file = qEnvironmentVariable("LUMORA_DIALOG_SHOT");
        QTimer::singleShot(qEnvironmentVariableIntValue("LUMORA_SHOT_DELAY") + 2500, &window, [file]() {
            for (QWidget *w : QApplication::topLevelWidgets())
                if (w->isVisible() && (qobject_cast<QDialog *>(w) || w->windowFlags().testFlag(Qt::Popup))) {
                    w->grab().save(file);
                    break;
                }
            QApplication::quit();
        });
    }
    if (qEnvironmentVariableIsSet("LUMORA_LINK_SHOT")) {   // the picker for linking a plot item
        const QString file = qEnvironmentVariable("LUMORA_LINK_SHOT");
        pickPoolObject(&ctx, &window, QPoint(100, 100), QStringLiteral("Target"), QStringLiteral("Group 2"), true, {});
        QTimer::singleShot(900, &window, [file]() {
            if (QWidget *w = QApplication::activePopupWidget())
                w->grab().save(file);
            QApplication::quit();
        });
    }
    if (m_parser.isSet(m_libraryShot)) {
        auto *d = new FixtureLibraryDialog(&ctx, &window);
        d->show();
        const QStringList q = m_parser.value(m_librarySearch).split(':');
        d->debugSearch(q.value(0).toInt(), q.value(1), q.value(2) == "import");
        shootAndQuit(d, m_parser.value(m_libraryShot), q.value(2) == "import" ? 12000 : 1000);
    }

    if (!m_parser.isSet(m_screenshot))
        return;
    window.showPage(m_parser.value(m_page).toInt());
    if (m_parser.isSet(m_keypad))
        QTimer::singleShot(300, &window, [&window]() { window.setKeypadVisible(true); });
    if (m_parser.isSet(m_store))
        ctx.setStoreArmed(true);
    if (m_parser.isSet(m_addWindow))
        if (Workspace *w = window.currentWorkspace())
            w->addPanel(m_parser.value(m_addWindow));
    if (qEnvironmentVariableIsSet("LUMORA_PANEL_OPTIONS")) {   // "type|{json}": a window's options
        const QStringList parts = qEnvironmentVariable("LUMORA_PANEL_OPTIONS").split('|');
        if (Workspace *w = window.currentWorkspace())
            w->debugOptions(parts.value(0), QJsonDocument::fromJson(parts.value(1).toUtf8()).object());
    }
    if (m_parser.isSet(m_editEffect))
        ctx.requestEditEffect(m_parser.value(m_editEffect).toInt());
    shootAndQuit(&window, m_parser.value(m_screenshot),
                 qEnvironmentVariableIsSet("LUMORA_SHOT_DELAY") ? qEnvironmentVariableIntValue("LUMORA_SHOT_DELAY") : 2500);
}

} // namespace lumora
