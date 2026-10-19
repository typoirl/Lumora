// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcontext.h"
#include "macrosui.h"
#include "mainwindow.h"
#include "cueview.h"
#include "effectsui.h"
#include "librarybrowser.h"
#include "fixtureeditor.h"
#include "fixturesearch.h"
#include "patchview.h"
#include "quickedit.h"
#include "theme.h"
#include "workspace.h"

#include <QApplication>
#include <QIcon>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>

using namespace lumora;

int main(int argc, char **argv)
{
    // Interface size (Layout → Interface settings) scales everything uniformly.
    {
        const int scale = QSettings(QSettings::NativeFormat, QSettings::UserScope, "Lumora", "Lumora").value("ui/scale", 100).toInt();
        if (scale != 100 && qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR"))
            qputenv("QT_SCALE_FACTOR", QByteArray::number(scale / 100.0));
    }
    QApplication app(argc, argv);
    QApplication::setOrganizationName("Lumora");
    QApplication::setApplicationName("Lumora");
    QApplication::setApplicationVersion("a.7.2.1");
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/lumora.png")));
    theme::apply(app);

    QCommandLineParser parser;
    parser.setApplicationDescription("Lumora lighting console");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("show", "Show file to open");
    QCommandLineOption demo("demo", "Start with the demo show");
    QCommandLineOption screenshot("screenshot", "Developer: save a window screenshot to <file> and quit", "file");
    QCommandLineOption page("page", "Developer: page to show (views in rail order, then Patch, Outputs, MIDI)", "index", "0");
    QCommandLineOption script("run", "Developer: run command lines (separated by ;) after startup", "commands");
    QCommandLineOption learn("learn-preview", "Developer: start in MIDI learn mode with executor 202's fader armed");
    QCommandLineOption keypad("keypad", "Developer: open the command keypad");
    QCommandLineOption store("store", "Developer: arm Store");
    QCommandLineOption editEffect("edit-effect", "Developer: open an effect in the editor", "id");
    QCommandLineOption shapeShot("shape-editor-shot", "Developer: save a screenshot of the shape editor and quit", "file");
    QCommandLineOption libraryShot("library-shot", "Developer: open the fixture library, save a screenshot to <file> and quit", "file");
    QCommandLineOption librarySearch("library-search", "Developer: source:search[:import] for --library-shot", "query", "1:");
    QCommandLineOption keys("keys", "Developer: type these keys into the window after startup (one char each, ~=Enter)", "keys");
    QCommandLineOption taps("tap", "Developer: tap these objects after --keys, separated by ; (e.g. Color.1;Color.9)", "objects");
    QCommandLineOption addWindow("add-window", "Developer: add a window of this type to the shown view", "type");
    parser.addOptions({keys, taps, addWindow});
    parser.addOptions({demo, screenshot, page, script, learn, keypad, store, editEffect, shapeShot, libraryShot, librarySearch});
    parser.process(app);

    AppContext ctx;

    // The fixture library to pick from: the files Lumora ships with (and the source tree, for
    // development). Imported, downloaded and edited types live in each show file, not here.
    const QStringList dirs = {QCoreApplication::applicationDirPath() + "/fixtures", QStringLiteral(LUMORA_SOURCE_FIXTURES)};
    for (const QString &d : dirs)
        if (QDir(d).exists())
            ctx.show->library().loadDirectory(d);

    // DMX outputs are network settings: they stay on this computer, whatever show is open.
    {
        const QByteArray saved = QSettings().value("network/outputs").toByteArray();
        // Nothing saved yet: universes get outputs (default Art-Net) as they're patched.
        if (!saved.isEmpty())
            ctx.outputs->fromJson(QJsonDocument::fromJson(saved).object());
        QObject::connect(ctx.outputs, &OutputManager::changed, &ctx, [&ctx]() {
            QSettings().setValue("network/outputs", QJsonDocument(ctx.outputs->toJson()).toJson(QJsonDocument::Compact));
        });
    }

    MainWindow window(&ctx);
    window.setDeveloperRun(qEnvironmentVariableIsSet("LUMORA_SCREEN_SHOT") || parser.isSet(screenshot) || parser.isSet(libraryShot) || parser.isSet(shapeShot)
                           || qEnvironmentVariableIsSet("LUMORA_PICKER_SHOT") || qEnvironmentVariableIsSet("LUMORA_QE_SHOT")
                           || qEnvironmentVariableIsSet("LUMORA_EDITOR_SHOT") || qEnvironmentVariableIsSet("LUMORA_MACRO_SHOT"));
    const QString lastShow = QSettings().value("lastShow").toString();
    if (!parser.positionalArguments().isEmpty())
        window.openShow(parser.positionalArguments().first());
    else if (!parser.isSet(demo) && !lastShow.isEmpty() && QFileInfo::exists(lastShow))
        window.openShow(lastShow);
    else
        window.newDemoShow();

    ctx.engine->start(40);
    window.show();
    if (!parser.isSet(screenshot) && !parser.isSet(libraryShot) && !parser.isSet(shapeShot))
        window.restoreScreens();   // the other screens that were open last time

    if (qEnvironmentVariableIsSet("LUMORA_SCREEN_SHOT")) {
        // "n|file": open screen n, expand the screen switcher, grab that window.
        const QStringList parts = qEnvironmentVariable("LUMORA_SCREEN_SHOT").split('|');
        const int n = parts.value(0).toInt();
        window.openScreen(n);
        const QString file = parts.value(1);
        QTimer::singleShot(1500, &window, [n, file]() {
            for (QWidget *w : QApplication::topLevelWidgets())
                if (w->property("lumoraScreen").toInt() == n) {
                    if (auto *b = w->findChild<QToolButton *>())
                        Q_UNUSED(b);
                    w->grab().save(file);
                }
            QApplication::quit();
        });
    }

    if (qEnvironmentVariableIsSet("LUMORA_DEV_UNPATCHED"))   // developer: two unpatched pars
        for (int fid : {30, 31})
            ctx.show->addFixture({fid, QString(), "generic/rgbw-par", "6-channel", 1, 0});

    if (qEnvironmentVariableIsSet("LUMORA_ENCODER_CATEGORY"))   // developer: which encoder category shows
        ctx.setUiValue("encoderCategory", qEnvironmentVariableIntValue("LUMORA_ENCODER_CATEGORY"));
    if (parser.isSet(script))
        for (const QString &c : parser.value(script).split(';'))
            ctx.run(c);

    if (parser.isSet(learn)) {
        ctx.midi->setLearnMode(true);
        MidiTarget t;
        t.executor = 202;
        ctx.midi->arm(t);
    }

    if (parser.isSet(shapeShot)) {
        Shape s;
        s.id = 1;
        s.name = "Double hit";
        s.points = {{0.0, 1.0}, {0.12, 0.1}, {0.25, 0.85}, {0.4, 0.0}, {0.9, 0.0}};
        auto *d = new ShapeEditorDialog(s, &window);
        d->show();
        const QString file = parser.value(shapeShot);
        QTimer::singleShot(800, d, [d, file]() {
            d->grab().save(file);
            QApplication::quit();
        });
    }

    if (qEnvironmentVariableIsSet("LUMORA_QE_SHOT")) {
        auto *ed = new QuickEditor(QStringLiteral("Preset Color.2"), &window);
        ed->addNameField(QStringLiteral("Name"), QStringLiteral("Deep blue"), [](const QString &) {});
        ed->addSection(QStringLiteral("Applies to"));
        ed->addAction(QStringLiteral("    Any fixture (Universal)"), {});
        ed->addAction(QStringLiteral("✓  Same type (Type)"), {});
        ed->addAction(QStringLiteral("    Same fixtures (Fixture)"), {});
        ed->addSection(QString());
        ed->addAction(QStringLiteral("Update with programmer"), {});
        ed->addAction(QStringLiteral("Delete"), {}, QIcon(), true);
        ed->popup(QPoint(100, 100));
        const QString file = qEnvironmentVariable("LUMORA_QE_SHOT");
        QTimer::singleShot(800, ed, [ed, file]() {
            ed->grab().save(file);
            QApplication::quit();
        });
    }

    if (qEnvironmentVariableIsSet("LUMORA_MACRO_SHOT")) {
        // "macroId|file": the macro editor
        const QStringList parts = qEnvironmentVariable("LUMORA_MACRO_SHOT").split('|');
        auto *d = new MacroEditorDialog(&ctx, parts.value(0).toInt(), &window);
        d->show();
        const QString file = parts.value(1);
        QTimer::singleShot(800, d, [d, file]() {
            d->grab().save(file);
            QApplication::quit();
        });
    }
    if (qEnvironmentVariableIsSet("LUMORA_EDITOR_SHOT")) {
        // "typeId|file"
        const QStringList parts = qEnvironmentVariable("LUMORA_EDITOR_SHOT").split('|');
        auto *d = new FixtureEditorDialog(&ctx, parts.value(0), &window);
        d->show();
        for (QTableWidget *table : d->findChildren<QTableWidget *>())
            if (table->rowCount() > 4)
                table->selectRow(4);
        const QString file = parts.value(1);
        QTimer::singleShot(800, d, [d, file]() {
            d->grab().save(file);
            QApplication::quit();
        });
    }

    if (qEnvironmentVariableIsSet("LUMORA_PICKER_SHOT")) {
        QWidget *d = makePresetPicker(ctx.show, {{4, 11}, {2, 1}}, &window);
        d->move(100, 100);
        d->show();
        const QString file = qEnvironmentVariable("LUMORA_PICKER_SHOT");
        QTimer::singleShot(800, d, [d, file]() {
            d->grab().save(file);
            QApplication::quit();
        });
    }

    if (parser.isSet(libraryShot)) {
        auto *d = new FixtureLibraryDialog(&ctx, &window);
        d->show();
        const QStringList q = parser.value(librarySearch).split(':');
        d->debugSearch(q.value(0).toInt(), q.value(1), q.value(2) == "import");
        const QString file = parser.value(libraryShot);
        QTimer::singleShot(q.value(2) == "import" ? 12000 : 1000, d, [d, file]() {
            d->grab().save(file);
            QApplication::quit();
        });
    }

    if (parser.isSet(keys) || parser.isSet(taps)) {
        const QString k = parser.value(keys);
        const QStringList t = parser.value(taps).split(';', Qt::SkipEmptyParts);
        QTimer::singleShot(600, &window, [&window, &ctx, k, t]() {
            for (QChar c : k) {
                const bool enter = c == '~';
                const int code = enter ? Qt::Key_Return : int(c.toUpper().unicode());
                const Qt::KeyboardModifiers mods = c.isUpper() ? Qt::ShiftModifier : Qt::NoModifier;
                QKeyEvent press(QEvent::KeyPress, code, mods, enter ? QString() : QString(c));
                QApplication::sendEvent(&window, &press);
            }
            for (const QString &o : t)
                ctx.tapTarget(o);
        });
    }

    if (parser.isSet(screenshot)) {
        if (qEnvironmentVariableIsSet("LUMORA_WINDOW_SIZE")) {
            const QStringList wh = qEnvironmentVariable("LUMORA_WINDOW_SIZE").split('x');
            window.resize(wh.value(0).toInt(), wh.value(1).toInt());
        }
        window.showPage(parser.value(page).toInt());
        if (parser.isSet(keypad))
            QTimer::singleShot(300, &window, [&window]() { window.setKeypadVisible(true); });
        if (parser.isSet(store))
            ctx.setStoreArmed(true);
        if (qEnvironmentVariableIsSet("LUMORA_PATCH_SEARCH"))
            if (auto *panel = window.findChild<AddFixturesPanel *>())
            {
                panel->search()->setSearchText(qEnvironmentVariable("LUMORA_PATCH_SEARCH"));
                if (qEnvironmentVariableIsSet("LUMORA_PATCH_PICK"))
                    panel->search()->debugPick(qEnvironmentVariableIntValue("LUMORA_PATCH_PICK"));
            }
        if (parser.isSet(addWindow))
            if (Workspace *w = window.currentWorkspace())
                w->addPanel(parser.value(addWindow));
        if (qEnvironmentVariableIsSet("LUMORA_PANEL_OPTIONS")) {
            // "type|{json}|editor"
            const QStringList parts = qEnvironmentVariable("LUMORA_PANEL_OPTIONS").split('|');
            if (Workspace *w = window.currentWorkspace()) {
                QWidget *pop = w->debugOptions(parts.value(0), QJsonDocument::fromJson(parts.value(1).toUtf8()).object(),
                                               parts.value(2) == "editor");
                if (pop && parts.value(2) == "editor")
                    QTimer::singleShot(1500, pop, [pop]() { pop->grab().save(qEnvironmentVariable("LUMORA_POPUP_SHOT")); });
            }
        }
        if (parser.isSet(editEffect))
            ctx.requestEditEffect(parser.value(editEffect).toInt());
        const QString file = parser.value(screenshot);
        QTimer::singleShot(qEnvironmentVariableIsSet("LUMORA_SHOT_DELAY") ? qEnvironmentVariableIntValue("LUMORA_SHOT_DELAY") : 2500,
                           &window, [&window, file]() {
            window.grab().save(file);
            QApplication::quit();
        });
    }
    return app.exec();
}
