// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcontext.h"
#include "devtools.h"
#include "mainwindow.h"
#include "splash.h"
#include "theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QJsonDocument>
#include <QSettings>

using namespace lumora;

int main(int argc, char **argv)
{
    // Interface size (Show → Config) scales everything uniformly.
    {
        const int scale = QSettings(QSettings::NativeFormat, QSettings::UserScope, "Lumora", "Lumora").value("ui/scale", 100).toInt();
        if (scale != 100 && qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR"))
            qputenv("QT_SCALE_FACTOR", QByteArray::number(scale / 100.0));
    }
    QApplication app(argc, argv);
    QApplication::setOrganizationName("Lumora");
    QApplication::setApplicationName("Lumora");
    QApplication::setApplicationVersion("a.7.2.3");
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/lumora.png")));
    theme::apply(app);

    QCommandLineParser parser;
    parser.setApplicationDescription("Lumora lighting console");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("show", "Show file to open");
    QCommandLineOption demo("demo", "Start with the demo show");
    parser.addOption(demo);
    DeveloperRun dev(parser);   // the guide's pictures (docs/manual/make_screenshots.py)
    parser.process(app);

    const bool developerRun = dev.active();

    // The loading screen, until the main window shows (not for the developer screenshot runs).
    Splash splash(!developerRun);
    splash.step(0, QObject::tr("Starting"));

    AppContext ctx;

    // The fixture library to pick from: the files Lumora ships with (and the source tree, for
    // development). Imported, downloaded and edited types live in each show file, not here.
    {
        // The installed copy next to the program; a build that has none (still in its build
        // folder) reads the source tree's. Not both: they're the same files.
        QStringList found;
        for (const QString &d : {QCoreApplication::applicationDirPath() + "/fixtures", QStringLiteral(LUMORA_SOURCE_FIXTURES)})
            if (found.isEmpty() && QDir(d).exists())
                found << d;
        // The library is most of the wait: 2–60 % of the bar, a file at a time.
        int types = 0;
        for (int i = 0; i < found.size(); ++i)
            types += ctx.show->library().loadDirectory(found[i], [&](int done, int total, const QString &file) {
                if (total <= 0)
                    return;
                const double part = (i + double(done) / total) / found.size();
                splash.step(2 + int(58 * part), file.isEmpty() ? QObject::tr("Fixture library loaded")
                                                                : QObject::tr("Loading fixture library: %1").arg(QFileInfo(file).fileName()),
                            file.isEmpty());
            });
        splash.step(60, QObject::tr("%n fixture types in the library", nullptr, types));
    }

    // DMX outputs are network settings: they stay on this computer, whatever show is open.
    splash.step(64, QObject::tr("Setting up DMX outputs"));
    {
        const QByteArray saved = QSettings().value("network/outputs").toByteArray();
        // Nothing saved yet: universes get outputs (default Art-Net) as they're patched.
        if (!saved.isEmpty())
            ctx.outputs->fromJson(QJsonDocument::fromJson(saved).object());
        QObject::connect(ctx.outputs, &OutputManager::changed, &ctx, [&ctx]() {
            QSettings().setValue("network/outputs", QJsonDocument(ctx.outputs->toJson()).toJson(QJsonDocument::Compact));
        });
    }
    // OSC is network setup too: its lines (ports, addresses, prefixes) stay on this computer.
    splash.step(68, QObject::tr("Setting up OSC"));
    {
        const QByteArray saved = QSettings().value("network/osc").toByteArray();
        if (!saved.isEmpty())
            ctx.osc->fromJson(QJsonDocument::fromJson(saved).object());
        auto save = [&ctx]() { QSettings().setValue("network/osc", QJsonDocument(ctx.osc->toJson()).toJson(QJsonDocument::Compact)); };
        QObject::connect(ctx.osc, &OscManager::linesChanged, &ctx, save);
        QObject::connect(ctx.osc, &OscManager::statusChanged, &ctx, save);
    }

    splash.step(72, QObject::tr("Building the interface"));
    MainWindow window(&ctx);
    window.setDeveloperRun(developerRun);
    splash.step(80, QObject::tr("Opening the show"));
    const QString lastShow = QSettings().value("lastShow").toString();
    if (!parser.positionalArguments().isEmpty())
        window.openShow(parser.positionalArguments().first());
    else if (!parser.isSet(demo) && !lastShow.isEmpty() && QFileInfo::exists(lastShow))
        window.openShow(lastShow);
    else
        window.newDemoShow();

    splash.step(94, QObject::tr("Starting DMX output"));
    ctx.engine->start(44);   // DMX's full frame rate (live views redraw at most 30 times a second)
    splash.step(97, QObject::tr("Opening windows"));
    window.show();
    if (!developerRun)
        window.restoreScreens();   // the other screens that were open last time
    splash.step(100, QObject::tr("Ready"));
    splash.finish();
    if (qEnvironmentVariableIsSet("LUMORA_SPLASH_SHOT"))
        return 0;   // the loading screen's picture is taken
    dev.start(ctx, window);
    return app.exec();
}
