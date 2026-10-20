// SPDX-License-Identifier: GPL-3.0-or-later
#include "parameters.h"
#include "commandline.h"
#include "dmxoutput.h"
#include "engine.h"
#include "midi.h"
#include "osc.h"

#include <QtEndian>
#include "show.h"
#include "undo.h"

#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QElapsedTimer>
#include <QUdpSocket>
#include <QtTest>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

using namespace lumora;

namespace {
int dmx(const Engine &e, int universe, int address)
{
    return quint8(e.universes().value(universe).at(address - 1));
}

MidiMessage cc(int number, int value, int channel = 1)
{
    MidiMessage m;
    m.type = MidiMessage::ControlChange;
    m.channel = channel;
    m.number = number;
    m.value = value;
    return m;
}

MidiMessage note(int number, bool on)
{
    MidiMessage m;
    m.type = on ? MidiMessage::NoteOn : MidiMessage::NoteOff;
    m.number = number;
    m.value = on ? 127 : 0;
    return m;
}
} // namespace

class CoreTest : public QObject {
    Q_OBJECT

private:
    // Patch: 1-4 RGBW pars (6ch) at 1.001, 5-6 spots (16ch) at 1.101, 7 RGB par (virtual dimmer) at 2.001
    void setupShow(Show &s)
    {
        // The generic fixtures and GDTF samples (the Chauvet range is checked once, in oflImport).
        QVERIFY(s.library().loadDirectory(LUMORA_SOURCE_FIXTURES "/generic") >= 4);
        s.library().loadDirectory(LUMORA_SOURCE_FIXTURES "/gdtf");
        for (int i = 0; i < 4; ++i)
            QVERIFY(s.addFixture({i + 1, QString(), "generic/rgbw-par", "6-channel", 1, 1 + i * 6}));
        QVERIFY(s.addFixture({5, QString(), "generic/moving-spot", "16-channel", 1, 101}));
        QVERIFY(s.addFixture({6, QString(), "generic/moving-spot", "16-channel", 1, 117}));
        QVERIFY(s.addFixture({7, QString(), "generic/rgb-par", "3-channel", 2, 1}));
    }

private slots:
    // The user guide is rebuilt for every version: run python3 docs/manual/build_manual.py after
    // changing the version in src/ui/main.cpp, with a "What's new" entry for it in manual.md.
    void manualMatchesVersion()
    {
        QFile mainCpp(QStringLiteral(LUMORA_SOURCE_DIR "/src/ui/main.cpp"));
        QVERIFY(mainCpp.open(QIODevice::ReadOnly));
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("setApplicationVersion\\(\"([^\"]+)\""))
                                              .match(QString::fromUtf8(mainCpp.readAll()));
        QVERIFY(m.hasMatch());
        const QString version = m.captured(1);

        QFile stamp(QStringLiteral(LUMORA_SOURCE_DIR "/docs/manual/built-for.txt"));
        QVERIFY2(stamp.open(QIODevice::ReadOnly), "docs/manual/built-for.txt is missing: build the manual");
        QCOMPARE(QString::fromUtf8(stamp.readAll()).trimmed(), version);
        QVERIFY(QFileInfo::exists(QStringLiteral(LUMORA_SOURCE_DIR "/docs/Lumora-Manual.pdf")));

        QFile md(QStringLiteral(LUMORA_SOURCE_DIR "/docs/manual/manual.md"));
        QVERIFY(md.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(md.readAll());
        const int whatsNew = text.indexOf(QStringLiteral("{#whats-new}"));
        QVERIFY(whatsNew > 0);
        QVERIFY2(text.indexOf(version, whatsNew) > 0, "manual.md has no What's new entry for this version");
    }

    void effectLibrary()
    {
        QStringList names;
        for (const Effect &e : effectTemplates())
            names << e.name;
        QVERIFY(names.contains("Rainbow"));
        QVERIFY(names.contains("Strobe"));
        QVERIFY(names.contains("Random strobe"));
        QCOMPARE(names.indexOf("Rainbow"), 11);   // effect 12 in the demo show
    }

    void copyPresetRuns()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        cl.execute("Fixture 1");
        for (int i = 1; i <= 3; ++i) {
            cl.execute(QStringLiteral("At %1").arg(i * 10));
            QVERIFY(cl.execute(QStringLiteral("Store Dimmer.%1").arg(i)).ok);
        }
        QVERIFY(cl.execute("Copy Dimmer.1 Thru 3 At Dimmer.11").ok);
        QVERIFY(s.preset(1, 11) && s.preset(1, 12) && s.preset(1, 13));
        QVERIFY(s.preset(1, 1));
        QVERIFY(!cl.execute("Copy Dimmer.1 Thru 3 At Dimmer.12").ok);   // 12 and 13 are used
        QVERIFY(cl.execute("Move Dimmer.11 Thru Dimmer.13 At Dimmer.12").ok);   // onto itself, shifted
        QVERIFY(!s.preset(1, 11) && s.preset(1, 12) && s.preset(1, 14));
        QCOMPARE(s.preset(1, 14)->universal.value("Dimmer"), 0.3);
    }

    void parkAndStep()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        Engine e(&s);
        cl.execute("Fixture 1 Thru 4 At 60");
        e.tick(0.01);
        // Park 1 entirely, and only the intensity of 2.
        QVERIFY(cl.execute("Park Fixture 1").ok);
        QVERIFY(cl.execute("ClearAll").ok);
        QVERIFY(cl.execute("Fixture 2 Intensity Park").ok);
        QVERIFY(s.isParked(1, "Dimmer") && s.isParked(1, "ColorAdd_R"));
        QVERIFY(s.isParked(2, "Dimmer") && !s.isParked(2, "ColorAdd_R"));
        cl.execute("ClearAll");
        cl.execute("Fixture 1 Thru 4 At 10");
        e.tick(0.01);
        QCOMPARE(s.lastOutput()[1]["Dimmer"], 0.6);
        QCOMPARE(s.lastOutput()[2]["Dimmer"], 0.6);
        QCOMPARE(s.lastOutput()[3]["Dimmer"], 0.1);
        // Attribute Pan Park, as the encoder bar's name fills it in.
        cl.execute("ClearAll");
        cl.execute("Fixture 5");
        QVERIFY(cl.execute("Attribute Pan Park").ok);
        QVERIFY(s.isParked(5, "Pan") && !s.isParked(5, "Tilt"));
        // Saved with the show.
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        QVERIFY(t.isParked(2, "Dimmer") && t.isParked(5, "Pan"));
        QVERIFY(cl.execute("Unpark Fixture 2").ok);
        QVERIFY(!s.isParked(2, "Dimmer"));
        QVERIFY(cl.execute("Unpark All").ok);
        QVERIFY(s.parked().isEmpty());
        // Other forms.
        cl.execute("ClearAll");
        QVERIFY(cl.execute("3 Park").ok);
        QVERIFY(s.isParked(3, "Dimmer"));
        cl.execute("ClearAll");
        cl.execute("Fixture 1 Thru 3");
        QVERIFY(cl.execute("Store Group 1").ok);
        cl.execute("ClearAll");
        QVERIFY(cl.execute("Park Group 1 Color").ok);
        QVERIFY(s.isParked(1, "ColorAdd_R") && !s.isParked(1, "Dimmer"));
        QVERIFY(cl.execute("Unpark Group 1").ok);
        QVERIFY(!s.isParked(3, "Dimmer"));
        QVERIFY(cl.execute("Fixture 5 Attribute Pan At 45").ok);
        QCOMPARE(s.programmerValues()[5]["Pan"].value, 45.0);

        // Next / Prev step through the selection; after the last comes the whole selection.
        cl.execute("ClearAll");
        cl.execute("Fixture 1 Thru 3");
        QVERIFY(cl.execute("Next").ok);
        QCOMPARE(s.selection(), QVector<int>({1}));
        cl.execute("Next");
        QCOMPARE(s.selection(), QVector<int>({2}));
        cl.execute("At 70");   // only fixture 2
        QCOMPARE(s.programmerValues()[2]["Dimmer"].value, 0.7);
        QVERIFY(!s.programmerValues()[1].contains("Dimmer"));
        cl.execute("Next");
        cl.execute("Next");
        QCOMPARE(s.selection(), QVector<int>({1, 2, 3}));
        cl.execute("Prev");
        QCOMPARE(s.selection(), QVector<int>({3}));
        cl.execute("All");
        QCOMPARE(s.selection(), QVector<int>({1, 2, 3}));

        // Select Last brings back the selection from before Clear, and swaps on a second go.
        cl.execute("Clear");
        QVERIFY(s.selection().isEmpty());
        QVERIFY(cl.execute("Last").ok);
        QCOMPARE(s.selection(), QVector<int>({1, 2, 3}));
        cl.execute("Clear");
        cl.execute("Fixture 5");
        cl.execute("Last");
        QCOMPARE(s.selection(), QVector<int>({1, 2, 3}));
        cl.execute("Last");
        QCOMPARE(s.selection(), QVector<int>({5}));
    }

    void channelRanges()
    {
        // Typed values are in the channel's own values: a zoom of 5 to 50 at 30 is 30.
        {
            Show z;
            setupShow(z);
            FixtureType t = *z.library().find("generic/moving-spot");
            for (FixtureMode &m : t.modes)
                for (ChannelDef &c : m.channels)
                    if (c.attribute == "Zoom") {
                        c.physFrom = 5;
                        c.physTo = 50;
                    }
            z.library().addToShow(t);
            CommandLine cl(&z);
            cl.execute("Fixture 5");
            QVERIFY(cl.execute("Attribute Zoom At 30").ok);
            const ChannelDef *zc = z.channelOf(5, "Zoom");
            QVERIFY(zc);
            QCOMPARE(zc->toPhysical(z.programmerValues()[5]["Zoom"].value), 30.0);
            QVERIFY(cl.execute("Attribute Zoom At Full").ok);
            QCOMPARE(z.programmerValues()[5]["Zoom"].value, 1.0);
        }
        ChannelDef dim;
        dim.attribute = "Dimmer";
        QCOMPARE(dim.valueText(0.5), QString("50"));
        dim.dmxFrom = 0.0;
        dim.dmxTo = 200 / 255.0;   // a dimmer that's full at DMX 200
        QCOMPARE(dim.toDmx(1.0), 200 / 255.0);
        QCOMPARE(dim.fromDmx(100 / 255.0), 0.5);
        ChannelDef zoom;
        zoom.attribute = "Zoom";
        zoom.physFrom = 5;
        zoom.physTo = 50;
        QCOMPARE(zoom.toPhysical(1.0), 50.0);
        QCOMPARE(zoom.fromPhysical(27.5), 0.5);
        QCOMPARE(zoom.valueText(0.0), QString("5"));
        ChannelDef pan;
        pan.attribute = "Pan";
        pan.fine = 1;
        QCOMPARE(pan.dmxMax(), 65535);
        QCOMPARE(pan.toPhysical(90.0), 90.0);

        // Highlight can be left out of a channel, and that's kept.
        Show s;
        setupShow(s);
        FixtureType t = *s.library().find("generic/rgbw-par");
        QVERIFY(t.highlightsExplicit);
        auto red = [](FixtureType &type) -> ChannelDef & {
            for (ChannelDef &c : type.modes[0].channels)
                if (c.attribute == "ColorAdd_R")
                    return c;
            return type.modes[0].channels[0];
        };
        red(t).hasHighlight = false;
        red(t).dmxTo = 0.5;
        FixtureType back = FixtureType::fromJson(t.toJson());
        QVERIFY(!red(back).hasHighlight);
        QCOMPARE(red(back).dmxTo, 0.5);
        // Old files: highlight on dimmer, color and shutter only.
        QJsonObject old = t.toJson();
        old.remove("highlights");
        const FixtureType legacy = FixtureType::fromJson(old);
        for (const ChannelDef &c : legacy.modes[0].channels)
            QCOMPARE(c.hasHighlight, featureOf(c.attribute) == Feature::Dimmer || featureOf(c.attribute) == Feature::Color
                                          || c.attribute == "Shutter");
    }

    void colorPresetNames()
    {
        Show s;
        setupShow(s);
        s.select({1, 2});
        s.setAttribute("ColorAdd_R", 1.0);
        s.setAttribute("ColorAdd_B", 0.2);
        s.setColorName("L106 Primary Red");
        QVERIFY(s.storePreset(int(Feature::Color), 20));
        QCOMPARE(s.preset(int(Feature::Color), 20)->name, QString("L106 Primary Red"));
        // A changed color loses the name; a given name always wins.
        s.setAttribute("ColorAdd_G", 0.5);
        QVERIFY(s.colorName().isEmpty());
        QVERIFY(s.storePreset(int(Feature::Color), 21));
        QCOMPARE(s.preset(int(Feature::Color), 21)->name, QString("Color 21"));
        s.setColorName("Red");
        QVERIFY(s.storePreset(int(Feature::Color), 22, Show::StoreMode::Merge, "Mine"));
        QCOMPARE(s.preset(int(Feature::Color), 22)->name, QString("Mine"));
        // Only Color presets take it.
        s.setAttribute("Dimmer", 1.0);
        s.setColorName("Red");
        QVERIFY(s.storePreset(int(Feature::Dimmer), 30));
        QVERIFY(s.preset(int(Feature::Dimmer), 30)->name != "Red");
    }

    void oflMatrix()
    {
        const QByteArray json = R"({
          "name": "Bar", "matrix": {"pixelCount": [3, 1, 1], "pixelGroups": {"Master": "all"}},
          "availableChannels": {"Strobe": {"capability": {"type": "ShutterStrobe", "shutterEffect": "Strobe"}}},
          "templateChannels": {
            "Red $pixelKey": {"capability": {"type": "ColorIntensity", "color": "Red"}},
            "Green $pixelKey": {"fineChannelAliases": ["Green $pixelKey fine"], "capability": {"type": "ColorIntensity", "color": "Green"}}
          },
          "modes": [
            {"name": "1-cell", "channels": ["Red Master", "Green Master", "Strobe"]},
            {"name": "3-cell", "channels": ["Strobe", {"insert": "matrixChannels", "repeatFor": "eachPixelABC",
               "channelOrder": "perPixel", "templateChannels": ["Red $pixelKey", "Green $pixelKey", "Green $pixelKey fine"]}]},
            {"name": "per-channel", "channels": [{"insert": "matrixChannels", "repeatFor": ["3", "1"],
               "channelOrder": "perChannel", "templateChannels": ["Red $pixelKey", "Green $pixelKey"]}]}
          ]})";
        FixtureType t;
        QString err;
        QVERIFY2(FixtureLibrary::parseOfl(QJsonDocument::fromJson(json).object(), "x", "bar", &t, &err), qPrintable(err));
        QCOMPARE(t.modes.size(), 3);
        QCOMPARE(t.mode("1-cell")->channel("ColorAdd_G")->coarse, 1);
        // Each pixel is a cell: the strobe belongs to the master, RGB to the cells.
        const FixtureMode *m = t.mode("3-cell");
        QCOMPARE(m->footprint, 10);
        QCOMPARE(m->cellCount, 3);
        QVERIFY(m->part(0)->channel("Shutter"));
        QVERIFY(!m->part(0)->channel("ColorAdd_R"));
        QCOMPARE(m->part(1)->channel("ColorAdd_R")->coarse, 1);
        QCOMPARE(m->part(1)->channel("ColorAdd_G")->fine, 3);
        QCOMPARE(m->part(3)->channel("ColorAdd_R")->coarse, 7);
        QCOMPARE(m->part(3)->footprint, 10);
        QVERIFY(m->part(2)->virtualDimmer);   // RGB pixels each get a virtual Dimmer
        QVERIFY(!t.mode("1-cell")->cellCount);
        // Cells are numbered in matrix order, whatever order the mode lists them in.
        const FixtureMode *pc = t.mode("per-channel");
        QCOMPARE(pc->cellCount, 2);
        QCOMPARE(pc->part(1)->channel("ColorAdd_R")->name, QString("Red 1"));
        QCOMPARE(pc->part(2)->channel("ColorAdd_R")->name, QString("Red 3"));
        QCOMPARE(pc->part(2)->channel("ColorAdd_G")->coarse, 2);
    }

    void pixelFixtures()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        QString err;
        QVERIFY2(s.addFixture({101, "Bar", "generic/pixel-bar", "26-channel", 3, 1}, &err), qPrintable(err));
        QCOMPARE(s.cellsOf(101).size(), 8);
        QCOMPARE(s.idText(s.cellsOf(101)[2]), QString("101.3"));
        QCOMPARE(s.idText(s.cellsOf(101).first()), QString("101.1"));   // cells start at .1
        QCOMPARE(s.parseId("101.8"), cellId(101, 8));
        QCOMPARE(s.parseId("101.9"), 0);                 // an 8-cell bar
        QCOMPARE(s.parseId("150.10"), cellId(150, 10));  // not patched: by number
        QCOMPARE(s.parseId("101.1"), cellId(101, 1));
        QCOMPARE(s.fixture(cellId(101, 3))->name, QString("Bar"));
        // The master has the fixture-wide channels and rests at full; the cells have the pixels.
        QCOMPARE(s.fixtureAttributes(101), (QStringList{"Dimmer", "Shutter"}));
        QCOMPARE(s.defaultValue(101, "Dimmer"), 1.0);
        QVERIFY(s.fixtureHas(cellId(101, 1), "ColorAdd_R"));
        QVERIFY(s.fixtureHas(cellId(101, 1), "Dimmer"));   // virtual
        QVERIFY(!s.fixtureHas(cellId(101, 1), "Shutter"));
        QCOMPARE(s.fixtureIds().indexOf(cellId(101, 1)), s.fixtureIds().indexOf(101) + 1);

        // Both ways of writing cells.
        QVERIFY(cl.execute("Fixture 101.2").ok);
        QCOMPARE(s.selection(), (QVector<int>{cellId(101, 2)}));
        cl.execute("ClearAll");
        QVERIFY(cl.execute("Fixture 101 Cell 1 Thru 4").ok);
        QCOMPARE(s.selection().size(), 4);
        QCOMPARE(s.selection().last(), cellId(101, 4));
        cl.execute("ClearAll");
        QVERIFY(cl.execute("Fixture 101.1 Thru 8 - 101.5").ok);
        QCOMPARE(s.selection().size(), 7);
        cl.execute("ClearAll");
        QVERIFY(cl.execute("101.8 Thru 101.6").ok);   // backwards
        QCOMPARE(s.selection(), (QVector<int>{cellId(101, 8), cellId(101, 7), cellId(101, 6)}));
        cl.execute("ClearAll");
        QVERIFY(cl.execute("Fixture 101").ok);   // the master itself
        QCOMPARE(s.selection(), (QVector<int>{101}));

        // Cells are fixtures: groups, values and output.
        cl.execute("ClearAll");
        cl.execute("Fixture 101.1 Thru 101.8");
        QVERIFY(cl.execute("Store Group 9 \"Cells\"").ok);
        QCOMPARE(s.group(9)->fixtures.size(), 8);
        cl.execute("ClearAll");
        cl.execute("Fixture 101.3 At 100");
        s.setAttribute("ColorAdd_G", 0.0);
        s.setAttribute("ColorAdd_B", 0.0);
        Engine engine(&s);
        engine.tick(0.01);
        const QByteArray u3 = engine.universes().value(3);
        QCOMPARE(quint8(u3[0]), quint8(255));              // master dimmer at its default: full
        QCOMPARE(quint8(u3[2 + 6]), quint8(255));          // cell 3 red
        QCOMPARE(quint8(u3[2 + 7]), quint8(0));            // cell 3 green
        QCOMPARE(quint8(u3[2 + 3]), quint8(0));            // cell 2: virtual dimmer at 0
        QCOMPARE(engine.merged().value(cellId(101, 3)).value("ColorAdd_R"), 1.0);

        // A master with no channels of its own stands for its cells.
        QVERIFY(s.addFixture({102, "Bare", "generic/pixel-bar", "24-channel", 3, 100}, &err));
        cl.execute("ClearAll");
        QVERIFY(cl.execute("Fixture 102").ok);
        QCOMPARE(s.selection().size(), 8);
        QCOMPARE(s.selection().first(), cellId(102, 1));

        // Renumbering the master moves its cells, and everything that refers to them.
        QVERIFY(s.renumberFixtures({{101, 201}}));
        QVERIFY(!s.fixture(cellId(101, 1)));
        QCOMPARE(s.group(9)->fixtures.first(), cellId(201, 1));

        // Save and load: cells come back from the master.
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        QCOMPARE(t.cellsOf(201).size(), 8);
        QCOMPARE(t.group(9)->fixtures.size(), 8);
        QVERIFY(t.fixtureHas(t.group(9)->fixtures[4], "ColorAdd_B"));

        // Removing the master removes its cells everywhere.
        s.removeFixture(201);
        QVERIFY(s.cellsOf(201).isEmpty());
        QVERIFY(s.group(9)->fixtures.isEmpty());
    }

    void twoLevelSubfixtures()
    {
        // Like an Acme Tornado: the fixture, four heads, and in each head a ring and a main beam.
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        QString err, id;
        QVERIFY2(s.library().loadGdtfFile(QStringLiteral(LUMORA_SOURCE_DIR "/tests/data/quad-head.gdtf"), &err, &id), qPrintable(err));
        const FixtureMode *m = s.library().find(id)->mode("26ch");
        QCOMPARE(m->cellCount, 12);
        QCOMPARE(m->footprint, 26);
        QCOMPARE(m->partPath(1), (QPair<int, int>(1, 0)));
        QCOMPARE(m->partPath(3), (QPair<int, int>(1, 2)));
        QVERIFY2(s.addFixture({301, "Quad", id, "26ch", 4, 1}, &err), qPrintable(err));
        const int head2 = s.parseId("301.2"), ring2 = s.parseId("301.2.1"), main2 = s.parseId("301.2.2"), main4 = s.parseId("301.4.2");
        QVERIFY(head2 && ring2 && main2 && main4);
        QCOMPARE(s.idText(ring2), QString("301.2.1"));
        QCOMPARE(s.idText(head2), QString("301.2"));
        QCOMPARE(s.idText(s.fixtureIds()[s.fixtureIds().indexOf(301) + 1]), QString("301.1"));
        QCOMPARE(s.fixture(ring2)->parent, head2);
        QCOMPARE(s.childrenOf(301).size(), 4);
        QCOMPARE(s.childrenOf(head2), (QVector<int>{ring2, main2}));
        QCOMPARE(s.fixtureAttributes(301), (QStringList{"Dimmer", "Shutter"}));
        QCOMPARE(s.defaultValue(301, "Dimmer"), 1.0);
        QCOMPARE(s.fixtureAttributes(head2), QStringList{"Tilt"});
        QVERIFY(s.fixtureHas(ring2, "ColorAdd_R"));
        QVERIFY(s.fixtureHas(ring2, "Dimmer"));   // virtual, its own
        QCOMPARE(s.fixtureMode(ring2)->channel("ColorAdd_R")->coarse, 9);   // head 2 starts at 9
        QCOMPARE(s.fixtureMode(main4)->channel("Zoom")->coarse, 25);

        // Selecting at either level.
        QVERIFY(cl.execute("Fixture 301.1.1 Thru 301.4.1").ok);   // every ring
        QCOMPARE(s.selection().size(), 4);
        QCOMPARE(s.selection()[1], ring2);
        cl.execute("ClearAll");
        QVERIFY(cl.execute("Fixture 301.1 Thru 4").ok);   // every head
        QCOMPARE(s.selection().size(), 4);
        QCOMPARE(s.selection()[1], head2);
        cl.execute("ClearAll");
        QVERIFY(cl.execute("Fixture 301.2.1 Thru 2").ok);
        QCOMPARE(s.selection(), (QVector<int>{ring2, main2}));
        cl.execute("ClearAll");
        QVERIFY(cl.execute("Fixture 301 Cell 2.1").ok);
        QCOMPARE(s.selection(), QVector<int>{ring2});
        cl.execute("ClearAll");
        QVERIFY(cl.execute("Fixture 301 Cell 1 Thru 4 At 50").ok);   // heads have Tilt, not Dimmer
        QVERIFY(cl.execute("Attribute Tilt At 30").ok);
        QCOMPARE(s.programmerValues().value(head2).value("Tilt").value, 30.0);   // degrees

        // Output: ring 2 red, through its own virtual dimmer.
        cl.execute("ClearAll");
        cl.execute("Fixture 301.2.1 At 100");
        s.setAttribute("ColorAdd_G", 0.0);
        s.setAttribute("ColorAdd_B", 0.0);
        Engine engine(&s);
        engine.tick(0.01);
        const QByteArray u = engine.universes().value(4);
        QCOMPARE(quint8(u[9]), quint8(255));
        QCOMPARE(quint8(u[10]), quint8(0));
        QCOMPARE(quint8(u[3]), quint8(0));   // ring 1 stays dark

        // Saved and loaded.
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        QCOMPARE(t.idText(ring2), QString("301.2.1"));
    }

    void degreesAndFlip()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        cl.execute("Fixture 5");
        QVERIFY(cl.execute("Attribute Pan At -90").ok);
        QVERIFY(cl.execute("Attribute Tilt At 45").ok);
        QCOMPARE(s.programmerValues()[5]["Pan"].value, -90.0);
        // Flip: half a turn round and tilt mirrored points the same way.
        QVERIFY(cl.execute("Flip").ok);
        QCOMPARE(s.programmerValues()[5]["Pan"].value, 90.0);
        QCOMPARE(s.programmerValues()[5]["Tilt"].value, -45.0);
        QVERIFY(cl.execute("Flip").ok);   // and back: of 270 and -90, the one nearer the middle
        QCOMPARE(s.programmerValues()[5]["Pan"].value, -90.0);
        // Encoders move by a share of the range: 1% of 540°.
        s.nudgeAttribute("Pan", 0.01);
        QCOMPARE(s.programmerValues()[5]["Pan"].value, -84.6);

        // Shows saved before degrees: pan and tilt values convert.
        QJsonObject old = s.toJson();
        old["version"] = 1;
        QJsonObject defaults;
        defaults["5"] = QJsonObject{{"Pan", 0.75}};
        old["defaults"] = defaults;
        Show t;
        QVERIFY(t.fromJson(old));
        QCOMPARE(t.defaultValue(5, "Pan"), 135.0);   // 0.75 of -270..270
    }

    void offTimeAndInvert()
    {
        Show s;
        setupShow(s);
        Engine engine(&s);
        s.select({1});
        s.setAttribute("Dimmer", 1.0);
        QVERIFY(s.storeCue(1, 1));
        s.clearAll();
        Sequence seq = *s.sequence(1);
        seq.cues[0].fade = 0.0;
        seq.offFade = 2.0;   // the Off line
        s.setSequence(seq);
        s.assignSequence(1, 1, 201);
        Executor &e = *s.executor(1, 201);
        s.go(e);
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 1, 1), 255);
        s.off(e);
        engine.tick(1.0);
        QCOMPARE(dmx(engine, 1, 1), 128);   // halfway out
        QVERIFY(e.pb().isOn());
        engine.tick(1.01);
        QCOMPARE(dmx(engine, 1, 1), 0);
        QVERIFY(!e.pb().isOn());               // off once the fade is done
        QCOMPARE(Sequence::fromJson(s.sequence(1)->toJson()).offFade, 2.0);

        // Pan inverted in the patch: the same degrees, the DMX the other way round.
        Fixture spot = *s.fixture(5);
        spot.invertPan = true;
        QVERIFY(s.updateFixture(spot));
        s.select({5});
        s.setAttribute("Pan", 90.0);
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 1, 101), 0x55);   // 1 - 2/3 of 65535 = 21845 → 0x55 0x55
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        QVERIFY(t.fixture(5)->invertPan && !t.fixture(5)->invertTilt);
    }

    void spread()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        cl.execute("Fixture 1 Thru 4");
        // Interleave: values go to every other fixture.
        QVERIFY(cl.execute("spread Every 2").ok);
        QCOMPARE(s.workingSelection(), (QVector<int>{1, 3}));
        cl.execute("At 100");
        QVERIFY(s.programmerValues().contains(1) && !s.programmerValues().contains(2));
        QVERIFY(cl.execute("spread Every 2 Offset 2 Wings 2 Phase 0 Thru 180").ok);
        QCOMPARE(s.workingSelection(), (QVector<int>{2, 4}));
        QVERIFY(cl.execute("Store spread 3 \"Evens\"").ok);
        QCOMPARE(s.spread(3)->name, QString("Evens"));
        QCOMPARE(s.spread(3)->offset, 1);
        QVERIFY(cl.execute("spread Off").ok);
        QCOMPARE(s.workingSelection().size(), 4);
        QVERIFY(cl.execute("spread 3").ok);
        QCOMPARE(s.workingSelection(), (QVector<int>{2, 4}));
        s.clearSelection();   // spread go with the selection
        QVERIFY(s.activeSpread().isDefault());

        // Layout: blocks share a phase, wings mirror, the phase range spreads it.
        Spread m;
        m.blocks = 2;
        QCOMPARE(m.phases(4), (QVector<double>{0.0, 0.0, 0.5, 0.5}));
        m = Spread();
        m.wings = 2;
        QCOMPARE(m.phases(4), (QVector<double>{0.0, 0.5, 0.5, 0.0}));
        m = Spread();
        m.phaseTo = 0.0;   // together
        QCOMPARE(m.phases(3), (QVector<double>{0.0, 0.0, 0.0}));
        m.phaseFrom = 0.0;
        m.phaseTo = -360.0;   // the other way round
        QCOMPARE(m.phases(4), (QVector<double>{0.0, -0.25, -0.5, -0.75}));

        // Called Spreads on the command line (spread still works too).
        cl.execute("Fixture 1 Thru 4");
        QVERIFY(cl.execute("Spread Phase -360 Thru 0").ok);
        QCOMPARE(s.activeSpread().phaseFrom, -360.0);
        QCOMPARE(s.activeSpread().phaseTo, 0.0);
        QVERIFY(cl.execute("Spread Phase 0 Thru 900").ok);   // held at 720
        QCOMPARE(s.activeSpread().phaseTo, 720.0);
        QVERIFY(cl.execute("Store Spread 8").ok);
        QVERIFY(cl.execute("Spread Off").ok);
        QVERIFY(cl.execute("Spread 8").ok);
        QCOMPARE(s.activeSpread().phaseTo, 720.0);
        cl.execute("ClearAll");

        // An effect run with the spread takes its layout; a blueprint line can name one.
        Effect e;
        e.id = 1;
        e.layers << EffectLayer();
        s.setEffect(e);
        cl.execute("Fixture 1 Thru 4");
        cl.execute("spread Blocks 2");
        QVERIFY(s.applyEffect(1));
        QVERIFY(s.programmerEffects().last().hasLayout);
        QCOMPARE(s.programmerEffects().last().layout.blocks, 2);
        cl.execute("ClearAll");
        cl.execute("Fixture 1 Thru 4");
        QVERIFY(cl.execute("Store Group 7").ok);
        cl.execute("ClearAll");
        QVERIFY(cl.execute("Store Sequence 5 Cue 1").ok);
        QVERIFY(cl.execute("Blueprint Sequence 5 Cue 1 Group 7 Effect 1 spread 3").ok);
        const Cue &cue = s.sequence(5)->cues[0];
        QCOMPARE(cue.blueprints[0].spreadId, 3);
        QCOMPARE(s.blueprintFixtures(cue.blueprints[0]), (QVector<int>{2, 4}));
        QVERIFY(s.cueEffects(cue)[0].hasLayout);
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        QCOMPARE(t.spread(3)->wings, 2);
        QCOMPARE(t.sequence(5)->cues[0].blueprints[0].spreadId, 3);
    }

    void patchWrapsUniverses()
    {
        Show s;
        setupShow(s);
        // 26-channel bars from 3.480: the second can't fit in what's left, so it starts universe 4.
        QVector<Fixture> list;
        for (int i = 0; i < 3; ++i)
            list << Fixture{300 + i, QString(), "generic/pixel-bar", "26-channel", 3, 0};
        QVERIFY(s.layOut(list, 3, 480));
        QCOMPARE(list[0].universe, 3);
        QCOMPARE(list[0].address, 480);
        QCOMPARE(list[1].universe, 4);
        QCOMPARE(list[1].address, 1);
        QCOMPARE(list[2].address, 27);
        // It skips what's patched: universe 1 has pars at 1–24 and spots from 101.
        QVector<Fixture> pars{Fixture{40, QString(), "generic/rgbw-par", "6-channel", 1, 0},
                              Fixture{41, QString(), "generic/rgbw-par", "6-channel", 1, 0}};
        QVERIFY(s.layOut(pars, 1, 1));
        QCOMPARE(pars[0].address, 25);
        QCOMPARE(pars[1].address, 31);
        // Repatching fixtures that are already patched: their old addresses don't count.
        QVector<Fixture> again{*s.fixture(1), *s.fixture(2)};
        QVERIFY(s.layOut(again, 1, 1));
        QCOMPARE(again[0].address, 1);
        QCOMPARE(again[1].address, 7);
    }

    void namedPresetsAndEmptyCues()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        cl.execute("Fixture 1 Thru 4");
        s.setAttribute("ColorAdd_R", 1.0);
        QVERIFY(cl.execute("Store Color.3 \"Red\"").ok);
        QVERIFY(s.preset(4, 3));
        cl.execute("ClearAll");
        cl.execute("Fixture 1");
        QVERIFY(cl.execute("At Color.3").ok);
        QCOMPARE(s.programmerValues()[1]["ColorAdd_R"].presetId, 3);
        cl.execute("ClearAll");
        QVERIFY(cl.execute("Fixture 1 Thru 4 At colour.3").ok);   // spelling and case don't matter
        QVERIFY(cl.execute("Preset Color.3").ok);         // "Preset" in front is fine too
        QVERIFY(cl.execute("Move Color.3 At Color.7").ok);
        QVERIFY(s.preset(4, 7) && !s.preset(4, 3));
        CommandResult r = cl.execute("Label Color.7 \"Deep red\"");
        QVERIFY(r.ok);
        QCOMPARE(s.preset(4, 7)->name, QString("Deep red"));
        QVERIFY(cl.execute("Store Position.2").ok == false);   // no position values: refused
        QVERIFY(cl.execute("Store Preset 4.8").ok);          // numbers still work
        QCOMPARE(presetRef(2, 12), QString("Position.12"));

        // Empty cues for blueprint-only programming, and runs of cues.
        cl.execute("ClearAll");
        QVERIFY(cl.execute("Store Sequence 9 Cue 1").ok);
        QCOMPARE(s.sequence(9)->cues.size(), 1);
        QVERIFY(s.sequence(9)->cues[0].values.isEmpty());
        QVERIFY(cl.execute("Store Sequence 9 Cue 1 Thru 40").ok);
        QCOMPARE(s.sequence(9)->cues.size(), 40);
        cl.execute("Fixture 1 Thru 4");
        QVERIFY(cl.execute("Store Group 1").ok);
        QVERIFY(cl.execute("Blueprint Sequence 9 Cue 5 Group 1 Color.7 At 80").ok);
        QCOMPARE(s.sequence(9)->cues[4].blueprints[0].presets[0], PresetRef(4, 7));
        double n = 0;
        QVERIFY(s.addEmptyCue(9, -1, &n));
        QCOMPARE(n, 41.0);
        QVERIFY(!s.addEmptyCue(9, 41));
    }

    void brokenReferences()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        cl.execute("Fixture 1 Thru 4");
        QVERIFY(cl.execute("Store Group 1").ok);
        s.setAttribute("ColorAdd_R", 1.0);
        QVERIFY(cl.execute("Store Color.1").ok);
        cl.execute("ClearAll");
        QVERIFY(cl.execute("Store Cue 1").ok);
        QVERIFY(cl.execute("Blueprint Cue 1 Group 1 Color.1 At 50").ok);
        const auto cue = [&]() { return s.sequence(s.selectedSequence())->cues[0]; };
        QVERIFY(s.cueProblems(cue()).isEmpty());
        s.deletePreset(4, 1);
        QCOMPARE(s.blueprintProblems(cue().blueprints[0]).size(), 1);
        QVERIFY(s.blueprintProblems(cue().blueprints[0])[0].contains("Color.1"));
        s.deleteGroup(1);
        QCOMPARE(s.blueprintProblems(cue().blueprints[0]).size(), 2);
        QCOMPARE(s.cueProblems(cue()).size(), 2);
        // Bringing it back clears the marker: the line still points at Group 1.
        cl.execute("Fixture 1 Thru 4");
        cl.execute("Store Group 1");
        QCOMPARE(s.blueprintProblems(cue().blueprints[0]).size(), 1);
    }

    void qxfImport()
    {
        const QString dir = QStringLiteral(LUMORA_SOURCE_DIR "/tests/data/");
        FixtureLibrary lib;
        QString err, id;
        QVERIFY2(lib.loadQxfFile(dir + "Generic-Generic-RGB.qxf", &err, &id), qPrintable(err));
        QCOMPARE(id, QString("generic/generic-rgb"));
        const FixtureType *rgb = lib.find(id);
        QCOMPARE(rgb->source, QString("QLC+"));
        QCOMPARE(rgb->modes.size(), 5);
        QVERIFY(rgb->mode("RGB")->virtualDimmer);
        QCOMPARE(rgb->mode("GRB")->channel("ColorAdd_R")->coarse, 1);
        QCOMPARE(rgb->mode("Dimmer RGB")->channel("Dimmer")->coarse, 0);
        QVERIFY(!rgb->mode("Dimmer RGB")->virtualDimmer);

        // A moving head: fine channels pair up, wheels get their slots and gobo images.
        QFile f(dir + "Robe-Robin-600E-Spot.qxf");
        QVERIFY(f.open(QIODevice::ReadOnly));
        QStringList asked;
        FixtureType robe;
        QVERIFY2(FixtureLibrary::parseQxf(f.readAll(), &robe, &err, [&](const QString &path) {
            asked << path;
            return QByteArray("img:") + path.toUtf8();
        }), qPrintable(err));
        QCOMPARE(robe.manufacturer, QString("Robe"));
        QCOMPARE(robe.modes.size(), 3);
        const FixtureMode *m1 = robe.mode("Mode 1");
        QCOMPARE(m1->footprint, 32);
        QCOMPARE(m1->channel("Pan")->fine, 1);
        QCOMPARE(m1->channel("Tilt")->coarse, 2);
        QCOMPARE(m1->channel("Dimmer")->fine, 31);
        QCOMPARE(m1->channel("Color1")->fine, 7);
        QCOMPARE(m1->channel("ColorSub_C")->coarse, 8);
        QCOMPARE(m1->channel("CTO")->coarse, 11);
        QCOMPARE(m1->channel("Gobo1")->coarse, 14);
        QCOMPARE(m1->channel("Gobo1")->fine, 15);
        QCOMPARE(m1->channel("Gobo2")->coarse, 16);
        QCOMPARE(m1->channel("Zoom")->fine, 25);
        QCOMPARE(m1->channel("Focus1")->fine, 27);
        QCOMPARE(m1->channel("Iris")->fine, 23);
        QCOMPARE(m1->channel("Shutter")->coarse, 29);
        QCOMPARE(m1->channel("Prism1Pos")->coarse, 20);
        const ChannelDef *gobo = m1->channel("Gobo1");
        QCOMPARE(gobo->setNameFor(7 / 255.0), QString("Gobo 1"));
        QCOMPARE(gobo->setFor(7 / 255.0)->media, QString("Robe/gobo00024.svg"));
        QVERIFY(robe.media.value("Robe/gobo00010.png").startsWith("img:"));
        QVERIFY(asked.contains("Robe/gobo00010.png"));
        // Shutter rests on an open range.
        QCOMPARE(m1->channel("Shutter")->setNameFor(m1->channel("Shutter")->defaultValue), QString("Open"));

        // A pixel bar with ten heads: each is a cell with its own dimmer.
        QVERIFY2(lib.loadFile(dir + "Showtec-Sunstrip-Active.qxf", &err, &id), qPrintable(err));
        const FixtureMode *ten = lib.find(id)->mode("10 Channels Mode");
        QCOMPARE(ten->channels.size(), 10);
        QCOMPARE(ten->cellCount, 10);
        QCOMPARE(ten->part(10)->channel("Dimmer")->coarse, 9);
        QVERIFY(ten->part(0)->channels.isEmpty());
    }

    // Parses every fixture of a full Open Fixture Library checkout when LUMORA_OFL_DIR is set.
    void oflBulk()
    {
        const QString dir = qEnvironmentVariable("LUMORA_OFL_DIR");
        if (dir.isEmpty())
            QSKIP("Set LUMORA_OFL_DIR to an open-fixture-library/fixtures folder");
        QDirIterator it(dir, {"*.json"}, QDir::Files, QDirIterator::Subdirectories);
        int total = 0, okCount = 0, redirects = 0;
        QStringList failures;
        while (it.hasNext()) {
            const QString path = it.next();
            if (path.endsWith("manufacturers.json") || path.endsWith("register.json"))
                continue;
            ++total;
            FixtureLibrary lib;
            QString err;
            if (lib.loadOflFile(path, &err))
                ++okCount;
            else if (err.contains("renamed"))
                ++redirects;
            else
                failures << QFileInfo(path).dir().dirName() + "/" + QFileInfo(path).fileName() + ": " + err;
        }
        qInfo("OFL: %d of %d parsed, %d redirects, %d unsupported", okCount, total, redirects, int(failures.size()));
        for (const QString &f : failures.mid(0, 15))
            qInfo("  %s", qPrintable(f));
        QVERIFY(okCount > total * 0.95);
    }

    void pagesAndPoolMoves()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        QVERIFY(cl.execute("Label Page 2 \"Verse\"").ok);
        QCOMPARE(s.pageLabel(2), QString("Verse"));
        QCOMPARE(s.pageLabel(3), QString("Page 3"));
        QCOMPARE(s.pages(), QList<int>({1, 2}));
        s.setPage(4);
        QCOMPARE(s.pages(), QList<int>({1, 2, 4}));

        // Moving a preset keeps the cue that references it pointing at it.
        cl.execute("Fixture 1 Thru 4 At 50");
        s.setAttribute("ColorAdd_B", 1.0);
        QVERIFY(cl.execute("Store Preset 4.1").ok);
        cl.execute("ClearAll");
        cl.execute("Fixture 1 Thru 4 At Preset 4.1");
        QVERIFY(cl.execute("Store Cue 1").ok);
        QVERIFY(cl.execute("Move Preset 4.1 At 4.9").ok);
        QVERIFY(!s.preset(4, 1));
        QCOMPARE(s.sequence(s.selectedSequence())->cues[0].values[1]["ColorAdd_B"].presetId, 9);

        // Effects: label, copy, move (keeps references), delete.
        Effect e;
        e.id = 1;
        e.name = "Chase";
        s.setEffect(e);
        Cue &cue = s.sequenceMutable(s.selectedSequence())->cues[0];
        EffectInstance inst;
        inst.effectId = 1;
        cue.effects.append(inst);
        QVERIFY(cl.execute("Label Effect 1 \"Sweep\"").ok);
        QCOMPARE(s.effect(1)->name, QString("Sweep"));
        QVERIFY(cl.execute("Copy Effect 1 At 2").ok);
        QCOMPARE(s.effect(2)->name, QString("Sweep"));
        QVERIFY(!cl.execute("Move Effect 1 At 2").ok);   // occupied
        QVERIFY(cl.execute("Move Effect 1 At 5").ok);
        QCOMPARE(s.sequence(s.selectedSequence())->cues[0].effects[0].effectId, 5);
        QVERIFY(cl.execute("Delete Effect 2").ok);
        QVERIFY(!s.effect(2));

        // Page names survive a save.
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        QCOMPARE(t.pageName(2), QString("Verse"));
    }
    void oflImport()
    {
        FixtureLibrary lib;
        // 8 generic OFL + 2 GDTF + Pangolin and Resolume, and the Chauvet DJ and Chauvet Professional ranges.
        QCOMPARE(lib.loadDirectory(LUMORA_SOURCE_FIXTURES), 14 + 134 + 126);
        // Every Chauvet profile loads, with at least one mode, under its own maker.
        for (const char *maker : {"chauvet-dj", "chauvet-professional"}) {
            QDirIterator it(QStringLiteral(LUMORA_SOURCE_FIXTURES "/") + maker, {"*.json"});
            int n = 0;
            while (it.hasNext()) {
                const QString path = it.next();
                FixtureLibrary one;
                QString err, id;
                QVERIFY2(one.loadOflFile(path, &err, &id), qPrintable(path + ": " + err));
                QVERIFY2(!one.find(id)->modes.isEmpty(), qPrintable(path));
                QVERIFY(one.find(id)->manufacturer.startsWith("Chauvet"));
                ++n;
            }
            QVERIFY(n > 100);
        }
        // Gobo pictures from the manufacturers' GDTF files, on the wheel slots (shake ranges too).
        FixtureLibrary chauvet;
        QString err, id;
        QVERIFY(chauvet.loadOflFile(QStringLiteral(LUMORA_SOURCE_FIXTURES "/chauvet-dj/intimidator-spot-360x.json"), &err, &id));
        const FixtureType *spot360 = chauvet.find(id);
        QVERIFY(spot360->media.size() >= 7);
        int pictured = 0;
        for (const ChannelDef &c : spot360->modes.first().channels)
            if (c.attribute.startsWith("Gobo") && !c.attribute.endsWith("Pos"))
                for (const ChannelSet &set : c.sets)
                    if (!set.media.isEmpty()) {
                        QVERIFY(spot360->media.contains(set.media));
                        QVERIFY(spot360->media.value(set.media).startsWith("\x89PNG"));
                        ++pictured;
                    }
        QVERIFY(pictured >= 7);
        // The laser and media servers.
        const FixtureType *beyond = lib.find("pangolin/beyond-dmx-server");
        QVERIFY(beyond);
        QCOMPARE(beyond->mode("Mid")->footprint, 32);
        QCOMPARE(beyond->mode("Max")->footprint, 50);
        const FixtureType *fb3 = lib.find("pangolin/beyond-dmx-server-fb3");
        QVERIFY(fb3);
        QCOMPARE(fb3->mode("Basic")->footprint, 4);
        QCOMPARE(fb3->mode("Full")->footprint, 16);
        QVERIFY(lib.find("resolume/arena-composition"));
        QCOMPARE(lib.find("resolume/arena-layer")->modes.first().footprint, 38);
        const FixtureType *spot = lib.find("generic/moving-spot");
        QVERIFY(spot);
        const FixtureMode *m = spot->mode("16-channel");
        QVERIFY(m);
        QCOMPARE(m->footprint, 16);
        const ChannelDef *pan = m->channel("Pan");
        QVERIFY(pan);
        QCOMPARE(pan->coarse, 0);
        QCOMPARE(pan->fine, 1);
        // Pan and tilt are in degrees, centered: 0 is the middle of a 540° pan.
        QCOMPARE(pan->defaultValue, 0.0);
        QCOMPARE(pan->minValue(), -270.0);
        QCOMPARE(pan->maxValue(), 270.0);
        QCOMPARE(m->channel("Tilt")->maxValue(), 135.0);
        QCOMPARE(pan->toDmx(90.0), 2.0 / 3.0);
        QCOMPARE(pan->fromDmx(0.0), -270.0);
        const ChannelDef *dim = m->channel("Dimmer");
        QCOMPARE(dim->coarse, 5);
        QCOMPARE(dim->fine, 6);
        QVERIFY(m->channel("Color1"));
        QCOMPARE(m->channel("Color1")->setNameFor(20 / 255.0), QString("Red"));
        QVERIFY(m->channel("Gobo1"));
        QVERIFY(m->channel("Gobo1Pos"));
        QVERIFY(m->channel("Prism1"));
        QVERIFY(m->channel("PTSpeed"));
        QVERIFY(!m->virtualDimmer);
        QCOMPARE(featureOf("Gobo1Pos"), Feature::Gobo);

        const FixtureMode *rgb = lib.find("generic/rgb-par")->mode("3-channel");
        QVERIFY(rgb->virtualDimmer);
        QCOMPARE(rgb->attributes().first(), QString("Dimmer"));

        // Round-trip through the show-file representation
        FixtureType copy = FixtureType::fromJson(spot->toJson());
        QCOMPARE(copy.mode("16-channel")->channel("Tilt")->fine, 3);
        QCOMPARE(copy.mode("16-channel")->channel("Color1")->sets.size(), 7);
    }

    void gdtfImport()
    {
        FixtureLibrary lib;
        QString err, id;
        QVERIFY2(lib.loadGdtfFile(QStringLiteral(LUMORA_SOURCE_FIXTURES) + "/gdtf/Generic@GDTF_Sample_Spot@v1.gdtf", &err, &id), qPrintable(err));
        QCOMPARE(id, QString("generic/gdtf-sample-spot"));
        const FixtureType *t = lib.find(id);
        QCOMPARE(t->source, QString("GDTF"));
        QCOMPARE(t->manufacturer, QString("Generic"));
        QCOMPARE(t->modes.size(), 2);

        const FixtureMode *ext = t->mode("Extended");
        QVERIFY(ext);
        QCOMPARE(ext->footprint, 14);
        const ChannelDef *pan = ext->channel("Pan");
        QCOMPARE(pan->coarse, 0);
        QCOMPARE(pan->fine, 1);
        QVERIFY(std::abs(pan->defaultValue) < 0.1);    // "32768/2": the middle, 0°
        QCOMPARE(ext->channel("Dimmer")->coarse, 4);
        QCOMPARE(ext->channel("Dimmer")->highlightValue, 1.0);
        QVERIFY(ext->channel("Shutter"));                       // Shutter1 maps to Shutter
        QVERIFY(std::abs(ext->channel("Shutter")->defaultValue - 32 / 255.0) < 1e-6);
        // A value exactly on a boundary belongs to the range that starts there
        QCOMPARE(ext->channel("Shutter")->setNameFor(ext->channel("Shutter")->defaultValue), QString("Open"));

        // Gobo wheel: names and images come from the wheel slots
        const ChannelDef *gobo = ext->channel("Gobo1");
        QVERIFY(gobo);
        QCOMPARE(gobo->setNameFor(0.0), QString("Open"));
        QCOMPARE(gobo->setNameFor(30 / 255.0), QString("Dots"));
        QCOMPARE(gobo->setNameFor(70 / 255.0), QString("Ring"));
        QCOMPARE(gobo->setNameFor(90 / 255.0), QString("Star"));
        bool foundMedia = false;
        for (const ChannelSet &s : gobo->sets)
            if (s.name == "Dots") {
                QCOMPARE(s.media, QString("gobo_dots"));
                foundMedia = true;
            }
        QVERIFY(foundMedia);
        QCOMPARE(t->media.size(), 5);
        QVERIFY(t->media.value("gobo_star").startsWith("\x89PNG"));
        QCOMPARE(ext->channel("Color1")->setNameFor(20 / 255.0), QString("Red"));

        const FixtureMode *basic = t->mode("Basic");
        QCOMPARE(basic->footprint, 11);
        QCOMPARE(basic->channel("Pan")->fine, -1);

        // Media survives the show file round trip
        FixtureType copy = FixtureType::fromJson(t->toJson());
        QCOMPARE(copy.media.value("gobo_ring"), t->media.value("gobo_ring"));
        QCOMPARE(copy.mode("Extended")->channel("Gobo1")->sets[1].media, QString("gobo_dots"));

        // A pixel batten: the geometry referenced four times becomes four cells at their offsets.
        QVERIFY2(lib.loadGdtfFile(QStringLiteral(LUMORA_SOURCE_DIR "/tests/data/pixel-batten.gdtf"), &err, &id), qPrintable(err));
        const FixtureMode *batten = lib.find(id)->mode("14ch");
        QCOMPARE(batten->cellCount, 4);
        QCOMPARE(batten->footprint, 14);
        QCOMPARE(batten->part(0)->attributes(), (QStringList{"Dimmer", "Shutter"}));
        QCOMPARE(batten->part(1)->channel("ColorAdd_R")->coarse, 2);
        QCOMPARE(batten->part(4)->channel("ColorAdd_B")->coarse, 13);

        // Heads written out one by one (like an Acme Tornado or a Chauvet PXL Curve): each yoke is a
        // head with its own pan and tilt, and its two beams are its parts.
        QVERIFY2(lib.loadGdtfFile(QStringLiteral(LUMORA_SOURCE_DIR "/tests/data/explicit-heads.gdtf"), &err, &id), qPrintable(err));
        const FixtureMode *heads = lib.find(id)->mode("Std");
        QCOMPARE(heads->cellCount, 9);   // 3 heads, each with 2 parts
        QCOMPARE(heads->part(0)->attributes(), QStringList{"Dimmer"});
        QCOMPARE(heads->partPath(4), (QPair<int, int>(2, 0)));
        QCOMPARE(heads->part(4)->attributes(), (QStringList{"Pan", "Tilt"}));
        QCOMPARE(heads->part(4)->channel("Pan")->coarse, 10);
        QCOMPARE(heads->part(5)->channel("Zoom")->coarse, 12);
        QCOMPARE(heads->part(6)->channel("ColorAdd_B")->coarse, 18);

        // Garbage is rejected with a message
        FixtureType bad;
        QVERIFY(!FixtureLibrary::parseGdtf("not a zip", &bad, &err));
        QVERIFY(!err.isEmpty());
    }

    void patchConflicts()
    {
        Show s;
        setupShow(s);
        QString err;
        QVERIFY(!s.addFixture({8, QString(), "generic/rgbw-par", "6-channel", 1, 3}, &err));
        QVERIFY(err.contains("Overlaps"));
        QVERIFY(!s.addFixture({1, QString(), "generic/dimmer", "8-bit", 3, 1}, &err));
        QVERIFY(!s.addFixture({9, QString(), "generic/moving-spot", "16-channel", 1, 500}, &err));
        QCOMPARE(s.nextFreeAddress(1, 6), 25);
        QCOMPARE(s.nextFreeAddress(1, 20), 25);
    }

    void presetsAreLiveReferences()
    {
        Show s;
        setupShow(s);
        s.select({1, 2});
        s.setAttribute("ColorAdd_R", 1.0);
        s.setAttribute("ColorAdd_B", 0.0);
        QVERIFY(s.storePreset(4, 1, Show::StoreMode::Merge, "Red", PresetScope::Fixture));
        QCOMPARE(s.programmerValues()[1]["ColorAdd_R"].presetId, 1);   // programmer now references it
        s.select({1, 2, 3, 4});
        s.setAttribute("Dimmer", 1.0);
        QVERIFY(s.storeCue(1, 1));
        s.clearAll();

        // Edit the preset; the cue follows.
        Preset p = *s.preset(4, 1);
        p.perFixture[1]["ColorAdd_R"] = 0.25;
        s.setPreset(p);
        RawTable st = s.trackedState(*s.sequence(1), 0);
        QCOMPARE(st[1]["ColorAdd_R"], 0.25);
        QCOMPARE(st[2]["ColorAdd_R"], 1.0);
    }

    void defaultsAndHome()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        // Open defaults: white from RGB, centered position.
        QCOMPARE(s.defaultValue(1, "ColorAdd_R"), 1.0);
        QCOMPARE(s.defaultValue(1, "ColorAdd_W"), 0.0);
        QCOMPARE(s.defaultValue(5, "Pan"), 0.0);   // degrees: the middle

        // A color preset as the pars' default: they rest in it, by reference.
        cl.execute("Fixture 1 Thru 4");
        s.setAttribute("ColorAdd_R", 0.0);
        s.setAttribute("ColorAdd_B", 1.0);
        QVERIFY(cl.execute("Store Color.5 \"Blue\"").ok);
        QVERIFY(cl.execute("Store Default").ok);
        QCOMPARE(s.storedDefaults()[1]["ColorAdd_B"].presetId, 5);
        cl.execute("ClearAll");
        Engine e(&s);
        e.tick(0.025);
        QCOMPARE(e.merged()[2]["ColorAdd_R"], 0.0);
        QCOMPARE(e.merged()[2]["ColorAdd_B"], 1.0);
        // Edit the preset: the default follows.
        Preset p = *s.preset(4, 5);
        p.universal["ColorAdd_B"] = 0.5;
        s.setPreset(p);
        e.tick(0.025);
        QCOMPARE(e.merged()[2]["ColorAdd_B"], 0.5);

        // Default Default puts the selection's defaults in the programmer (keeping the reference).
        cl.execute("Fixture 1");
        s.setAttribute("ColorAdd_R", 0.7);
        QVERIFY(cl.execute("Default Default").ok);
        QCOMPARE(s.programmerValues()[1]["ColorAdd_B"].presetId, 5);
        QCOMPARE(s.programmerValues()[1]["ColorAdd_R"].presetId, 5);
        QVERIFY(!s.programmerValues().contains(5));
        // Default All: every fixture. (Home still works, as Default.)
        QVERIFY(cl.execute("Default All").ok);
        QCOMPARE(s.programmerValues()[5]["Pan"].value, 0.0);
        cl.execute("ClearAll");
        QVERIFY(cl.execute("Home Home").ok);   // nothing selected: every fixture
        QCOMPARE(s.programmerValues()[5]["Pan"].value, 0.0);

        // Saved with the show; Delete Default forgets them.
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        QCOMPARE(t.storedDefaults()[3]["ColorAdd_B"].presetId, 5);
        cl.execute("ClearAll");
        QVERIFY(cl.execute("Delete Default").ok);
        QVERIFY(s.storedDefaults().isEmpty());
        QCOMPARE(s.defaultValue(1, "ColorAdd_B"), 1.0);
    }

    void handBuiltFixtureTypes()
    {
        // What the fixture editor saves: a .lft file with exactly the defaults it was given.
        FixtureType t;
        t.id = "me/strobe";
        t.manufacturer = "Me";
        t.name = "Strobe";
        t.source = "Lumora";
        t.openDefaults = true;
        FixtureMode m;
        m.name = "2ch";
        ChannelDef d;
        d.attribute = "Dimmer";
        d.coarse = 0;
        ChannelDef r;
        r.attribute = "ColorAdd_R";
        r.coarse = 1;
        r.defaultValue = 0.0;   // a hand-built type keeps a dark red default
        r.sets = {{0, 0.5, "Low", {}}, {0.5, 1, "High", {}}};
        m.channels = {d, r};
        t.modes = {m};
        t.finalize();
        const QString dir = QDir::temp().filePath("lumora-lft-test");
        QDir().mkpath(dir);
        QFile f(dir + "/me__strobe.lft");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(t.toJson()).toJson());
        f.close();
        FixtureLibrary lib;
        QString err, id;
        QVERIFY2(lib.loadFile(f.fileName(), &err, &id), qPrintable(err));
        QCOMPARE(id, QString("me/strobe"));
        QCOMPARE(lib.find(id)->mode("2ch")->channel("ColorAdd_R")->defaultValue, 0.0);
        QCOMPARE(lib.find(id)->mode("2ch")->channel("ColorAdd_R")->sets.size(), 2);
        QCOMPARE(lib.find(id)->mode("2ch")->footprint, 2);

        // Replacing a patched type takes effect at once; a vanished mode falls back to the first.
        Show s;
        setupShow(s);
        FixtureType par = *s.library().find("generic/rgbw-par");
        par.modes[0].name = "Renamed";
        par.modes = {par.modes[0]};
        s.updateFixtureType(par);
        QCOMPARE(s.fixture(1)->mode, QString("Renamed"));
        QVERIFY(s.fixtureMode(1));
        QDir(dir).removeRecursively();
    }

    void unpatchedFixtures()
    {
        Show s;
        setupShow(s);
        QString err;
        // Address 0: in the show, programmable, but no DMX and no address conflicts.
        QVERIFY2(s.addFixture({20, QString(), "generic/rgbw-par", "6-channel", 1, 0}, &err), qPrintable(err));
        QVERIFY(s.addFixture({21, QString(), "generic/rgbw-par", "6-channel", 1, 0}, &err));
        QVERIFY(!s.fixture(20)->patched());
        QCOMPARE(s.nextFreeAddress(1, 6), 25);
        Engine e(&s);
        s.select({20});
        s.setAttribute("Dimmer", 1.0);
        e.tick(0.025);
        QCOMPARE(e.merged()[20]["Dimmer"], 1.0);
        QCOMPARE(e.universes()[1].at(0), char(0));   // nothing written at address 1 by fixture 20
    }

    void selectionAddsUp()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        for (const char *c : {"1", "3", "Fixture 2"})
            QVERIFY(cl.execute(c).ok);
        QCOMPARE(s.selection(), (QVector<int>{1, 3, 2}));
        QVERIFY(cl.execute("Fixture 3 Thru 4 - 3").ok);
        QCOMPARE(s.selection(), (QVector<int>{1, 2, 4}));   // "- 3" takes it out
        QVERIFY(cl.execute("At At").ok);                        // At At: full
        QCOMPARE(s.programmerValues()[4]["Dimmer"].value, 1.0);
        s.clearSelection();
        QVERIFY(cl.execute("Fixture 2 At At").ok);
        QCOMPARE(s.selection(), QVector<int>{2});
    }

    void outputDefaults()
    {
        OutputManager out;
        OutputDefaults d;
        d.kind = OutputKind::ArtNet;
        d.artnetBase = 0;
        d.interfaceAddress = "10.0.0.5";
        out.setDefaults(d);
        QCOMPARE(out.ensureUniverses({1, 3}), 2);
        QCOMPARE(out.configs().size(), 2);
        QVERIFY(out.configs()[0].enabled);                 // on by default
        QCOMPARE(out.configs()[1].protocolUniverse, 2);    // console 3 → Art-Net 2
        QCOMPARE(out.configs()[0].interfaceAddress, QString("10.0.0.5"));
        QCOMPARE(out.ensureUniverses({1}), 0);             // already sent
        // Double patch: universe 1 on sACN too, and Art-Net 8 instead of 0.
        out.addConfig(out.defaultFor(1, OutputKind::Sacn));
        OutputConfig c = out.configs()[0];
        c.protocolUniverse = 8;
        out.updateConfig(0, c);
        QCOMPARE(out.configs()[2].kind, OutputKind::Sacn);
        QCOMPARE(out.configs()[2].protocolUniverse, 1);
        // Saved and read back; an old route over 3 universes becomes three outputs.
        OutputManager back;
        back.fromJson(out.toJson());
        QCOMPARE(back.configs().size(), 3);
        QCOMPARE(back.defaults().interfaceAddress, QString("10.0.0.5"));
        QJsonObject old{{"routes", QJsonArray{QJsonObject{{"kind", 0}, {"firstUniverse", 2}, {"count", 3}, {"protocolUniverse", 5}}}}};
        back.fromJson(old);
        QCOMPARE(back.configs().size(), 3);
        QCOMPARE(back.configs()[2].firstUniverse, 4);
        QCOMPARE(back.configs()[2].protocolUniverse, 7);
    }

    void fixtureTypesLiveInTheShow()
    {
        Show s;
        setupShow(s);   // patches pars and spots from the shipped library
        QVERIFY(s.library().isShowType("generic/rgbw-par"));      // patching copies the type in
        // An edited type changes this show only, and is saved with it.
        FixtureType edited = *s.library().find("generic/rgbw-par");
        edited.name = "Edited par";
        s.updateFixtureType(edited);
        QCOMPARE(s.library().find("generic/rgbw-par")->name, QString("Edited par"));
        const QJsonObject json = s.toJson();
        // A new show goes back to the shipped type; opening the file brings the edit back.
        s.clearShow();
        QVERIFY(!s.library().isShowType("generic/rgbw-par"));
        QVERIFY(s.library().find("generic/rgbw-par")->name != QString("Edited par"));
        QVERIFY(s.fromJson(json));
        QCOMPARE(s.library().find("generic/rgbw-par")->name, QString("Edited par"));
        // Imported files go into the show too.
        const QString dir = QStringLiteral(LUMORA_SOURCE_DIR) + "/tests/data/";
        QString id;
        QVERIFY(s.importFixtureFile(dir + "Showtec-Sunstrip-Active.qxf", nullptr, &id));
        QVERIFY(s.library().isShowType(id));
        QVERIFY(s.toJson().value("fixtureTypes").toArray().size() >= 3);
        s.clearShow();
        QVERIFY(!s.library().find(id));
    }

    void fixtureNames()
    {
        QCOMPARE(numberedNames("Spot 5", 3), (QStringList{"Spot 5", "Spot 6", "Spot 7"}));
        QCOMPARE(numberedNames("Par12", 2), (QStringList{"Par12", "Par13"}));
        QCOMPARE(numberedNames("Wash", 2), (QStringList{"Wash 1", "Wash 2"}));
        QCOMPARE(numberedNames("Truss 2 Spot", 2), (QStringList{"Truss 2 Spot 1", "Truss 2 Spot 2"}));
    }

    void renumberFixtures()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        cl.execute("Fixture 1 Thru 2");
        cl.execute("Store Group 1");
        cl.execute("At 50");
        cl.execute("Store Cue 1");
        QString err;
        QVERIFY(!s.renumberFixtures({{1, 3}}, &err));   // 3 is taken
        QVERIFY(s.renumberFixtures({{1, 101}, {2, 102}}, &err));
        QVERIFY(!s.fixture(1) && s.fixture(101) && s.fixture(102));
        QCOMPARE(s.group(1)->fixtures, (QVector<int>{101, 102}));
        QVERIFY(s.sequence(1)->cues[0].values.contains(101));
        QCOMPARE(s.selection(), (QVector<int>{101, 102}));
        QVERIFY(s.renumberFixtures({{101, 102}, {102, 101}}, &err));   // swapping is fine
        QCOMPARE(s.group(1)->fixtures, (QVector<int>{102, 101}));
    }

    void assignFunctionsAndTop()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        QVERIFY(cl.execute("Assign Flash At Executor 1.201 Button 1").ok);
        QCOMPARE(s.executor(1, 201)->buttons[0], ButtonFn::Flash);
        QVERIFY(!cl.execute("Assign Flash At Executor 1.201 Button 3").ok);   // one button each
        QVERIFY(cl.execute("Assign Toggle At Executor 201").ok);   // button 1 by default
        QCOMPARE(s.executor(1, 201)->buttons[0], ButtonFn::Toggle);
        QVERIFY(cl.execute("Assign XFade At Executor 201").ok);
        QCOMPARE(s.executor(1, 201)->faderFn, FaderFn::XFade);
        QVERIFY(cl.execute("Assign Temp At Executor 201 Fader").ok);
        QCOMPARE(s.executor(1, 201)->faderFn, FaderFn::Temp);
        QVERIFY(cl.execute("Assign Off At Executor 201").ok);
        QCOMPARE(s.executor(1, 201)->buttons[0], ButtonFn::Off);
        QVERIFY(!cl.execute("Assign Flash At Executor 101 Button 2").ok);   // keys have one button
        QVERIFY(!cl.execute("Assign Master At Executor 101").ok);           // and no fader
        QVERIFY(!cl.execute("Assign Banana At Executor 201").ok);
        QVERIFY(cl.execute("Assign Sequence 1 At Executor 201").ok);         // still works

        // Top: back to the first cue
        cl.execute("Fixture 1 At 50");
        cl.execute("Store Cue 1");
        cl.execute("Fixture 1 At 80");
        cl.execute("Store Cue 2");
        Executor *e = s.executor(1, 201);
        s.go(*e);
        s.go(*e);
        QCOMPARE(e->pb().cueIndex, 1);
        QVERIFY(cl.execute("Top Executor 201").ok);
        QCOMPARE(e->pb().cueIndex, 0);
        QVERIFY(cl.execute("Assign Top At Executor 201 Button 1").ok);
        s.go(*e);
        s.pressButton(*e, 0, true);
        QCOMPARE(e->pb().cueIndex, 0);
    }

    void recipeDimmerBecomesPreset()
    {
        // A show from before: a blueprint line with its own dimmer level.
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        cl.execute("Fixture 1 Thru 4");
        cl.execute("Store Group 1");
        BlueprintLine l;
        l.groupId = 1;
        l.dimmer = 1.0;
        s.addEmptyCue(1, 1);
        s.addBlueprintLine(1, 1, l);
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        const BlueprintLine &m = t.sequence(1)->cues[0].blueprints[0];
        QCOMPARE(m.dimmer, -1.0);
        QCOMPARE(m.presets.size(), 1);
        QCOMPARE(t.preset(1, m.presets[0].second)->name, QString("Full"));
        QCOMPARE(t.evaluateCue(t.sequence(1)->cues[0])[1]["Dimmer"], 1.0);
        QCOMPARE(t.dimmerPresetFor(1.0, false), m.presets[0].second);   // reused, not made twice
    }

    void executorKinds()
    {
        Show s;
        QCOMPARE(executorKind(101), ExecKind::Button);
        QCOMPARE(executorKind(215), ExecKind::Fader);
        QCOMPARE(executorKind(301), ExecKind::Encoder);
        QCOMPARE(executorKind(401), ExecKind::Fader);
        QCOMPARE(s.ensureExecutor(1, 105).buttons[0], ButtonFn::Go);   // keys and faders: Go
        QVERIFY(!s.setExecutorKind(1, 201, ExecKind::Button));             // only 401–499
        Executor &e = s.ensureExecutor(1, 402);
        s.setFader(e, 0.0);
        QVERIFY(s.setExecutorKind(1, 402, ExecKind::Button));
        QCOMPARE(executorKind(402, s.executor(1, 402)), ExecKind::Button);
        QCOMPARE(s.executor(1, 402)->fader, 1.0);   // a key plays at full
        QCOMPARE(s.executor(1, 402)->buttons[0], ButtonFn::Go);
        CommandLine cl(&s);
        QVERIFY(cl.execute("Executor 403 Encoder").ok);
        QVERIFY(!cl.execute("Executor 203 Button").ok);
        Show t;
        QVERIFY(t.fromJson(s.toJson()));   // kept even with nothing assigned
        QCOMPARE(executorKind(402, t.executor(1, 402)), ExecKind::Button);
        QCOMPARE(executorKind(403, t.executor(1, 403)), ExecKind::Encoder);
    }

    void storedViews()
    {
        Show s;
        CommandLine cl(&s);
        QVERIFY(!cl.execute("Store View 1").ok);   // no screen to take the windows from
        QJsonArray live{QJsonObject{{"x", 0}, {"y", 0}, {"w", 24}, {"h", 16}, {"tabs", QJsonArray{"cues"}}}};
        s.setViewLayoutProvider([&live]() -> std::optional<QJsonArray> { return live; });
        QVERIFY(cl.execute("Store View 1 \"Cues\"").ok);
        QCOMPARE(s.view(1)->name, QString("Cues"));
        QCOMPARE(s.view(1)->layout, live);
        live = QJsonArray{};
        QVERIFY(cl.execute("Store View 1").ok);   // stored again: keeps its name
        QCOMPARE(s.view(1)->name, QString("Cues"));
        QVERIFY(s.view(1)->layout.isEmpty());
        QVERIFY(cl.execute("Label View 1 \"Busk\"").ok);
        QVERIFY(cl.execute("Copy View 1 At 3").ok);
        QVERIFY(cl.execute("Move View 1 At 2").ok);
        QVERIFY(!s.view(1) && s.view(2) && s.view(3));
        QCOMPARE(s.view(2)->name, QString("Busk"));
        int recalled = 0;
        QObject::connect(&s, &Show::viewRecallRequested, [&recalled](int id) { recalled = id; });
        QVERIFY(cl.execute("View 2").ok);
        QCOMPARE(recalled, 2);
        QVERIFY(!cl.execute("View 9").ok);
        // Saved with the show
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        QCOMPARE(t.views().size(), 2);
        QCOMPARE(t.view(2)->name, QString("Busk"));
        QVERIFY(cl.execute("Delete View 3").ok);
        QCOMPARE(s.views().size(), 1);
    }

    void presetLayers()
    {
        Show s;
        setupShow(s);
        QCOMPARE(defaultPresetLayer(4), PresetScope::Universal);   // color
        QCOMPARE(defaultPresetLayer(1), PresetScope::Universal);   // dimmer
        QCOMPARE(defaultPresetLayer(2), PresetScope::Fixture);     // position: selective
        QCOMPARE(defaultPresetLayer(3), PresetScope::Fixture);

        // A color preset stores Universal by default: par 2 is first selected, so it sets the value.
        s.select({2, 1});
        s.setAttribute(2, "ColorAdd_R", 1.0);
        s.setAttribute(2, "ColorAdd_G", 0.0);
        s.setAttribute(1, "ColorAdd_R", 1.0);
        s.setAttribute(1, "ColorAdd_G", 0.5);
        QVERIFY(s.storePreset(4, 1));
        const Preset *p = s.preset(4, 1);
        QCOMPARE(p->layerBadge(), QString("U"));
        QCOMPARE(p->universal["ColorAdd_G"], 0.0);
        s.clearAll();
        double v = -1;
        QVERIFY(s.resolvePreset(4, 1, 7, "ColorAdd_R", &v));   // any fixture with the attribute
        QCOMPARE(v, 1.0);

        // Add a Global layer for the RGBW pars: it overrides Universal for that type only.
        s.select({3});
        s.setAttribute("ColorAdd_G", 0.3);
        QVERIFY(s.storePreset(4, 1, Show::StoreMode::Merge, QString(), PresetScope::Type));
        QCOMPARE(s.preset(4, 1)->layerBadge(), QString("U·G"));
        s.clearAll();
        QVERIFY(s.resolvePreset(4, 1, 4, "ColorAdd_G", &v));   // same type as par 3
        QCOMPARE(v, 0.3);
        QVERIFY(s.resolvePreset(4, 1, 4, "ColorAdd_R", &v));   // not in Global: falls back to Universal
        QCOMPARE(v, 1.0);
        QVERIFY(!s.resolvePreset(4, 1, 7, "ColorAdd_G", &v) || v == 0.0);   // RGB par: Universal

        // And a Selective layer for par 1 overrides both.
        s.select({1});
        s.setAttribute("ColorAdd_G", 0.9);
        CommandLine cl(&s);
        QVERIFY(cl.execute("Store Color.1 /selective").ok);
        QCOMPARE(s.preset(4, 1)->layerBadge(), QString("U·G·S"));
        s.clearAll();
        QVERIFY(s.resolvePreset(4, 1, 1, "ColorAdd_G", &v));
        QCOMPARE(v, 0.9);
        QVERIFY(s.resolvePreset(4, 1, 2, "ColorAdd_G", &v));
        QCOMPARE(v, 0.3);

        // Position presets are Selective by default: only the stored fixtures.
        s.select({11 - 6});   // fixture 5 is a spot
        s.setAttribute("Pan", 0.2);
        QVERIFY(s.storePreset(2, 1));
        QCOMPARE(s.preset(2, 1)->layerBadge(), QString("S"));
        s.clearAll();
        QVERIFY(!s.resolvePreset(2, 1, 6, "Pan", &v));
        QVERIFY(cl.execute("Fixture 5 Thru 6").ok);
        s.setAttribute("Pan", 0.7);
        QVERIFY(cl.execute("Store Position.1 /universal").ok);   // merge: adds a Universal layer
        s.clearAll();
        QVERIFY(s.resolvePreset(2, 1, 6, "Pan", &v));
        QCOMPARE(v, 0.7);
        QVERIFY(s.resolvePreset(2, 1, 5, "Pan", &v));   // Selective still wins for fixture 5
        QCOMPARE(v, 0.2);
        // Overwrite replaces every layer.
        cl.execute("Fixture 6");
        s.setAttribute("Pan", 0.4);
        QVERIFY(cl.execute("Store Position.1 /overwrite").ok);
        QCOMPARE(s.preset(2, 1)->layerBadge(), QString("S"));

        // Layers survive a save and load.
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        QCOMPARE(t.preset(4, 1)->layerBadge(), QString("U·G·S"));

        // Old shows: only the layer their scope used is kept.
        QJsonObject old = s.preset(4, 1)->toJson();
        old.remove("layered");
        old["scope"] = "Type";
        QCOMPARE(Preset::fromJson(old).layerBadge(), QString("G"));
    }

    void blueprints()
    {
        Show s;
        setupShow(s);
        s.select({1, 2, 3, 4});
        QVERIFY(s.storeGroup(1, "Pars"));
        s.setAttribute("ColorAdd_B", 1.0);
        QVERIFY(s.storePreset(4, 2, Show::StoreMode::Merge, "Blue"));
        s.clearAll();

        BlueprintLine line;
        line.groupId = 1;
        line.presets = {{4, 2}};
        line.dimmer = 0.8;
        line.every = 2;   // fixtures 1 and 3
        QVERIFY(s.addBlueprintLine(1, 1, line));
        RawTable st = s.trackedState(*s.sequence(1), 0);
        QCOMPARE(st.keys(), (QList<int>{1, 3}));
        QCOMPARE(st[3]["Dimmer"], 0.8);
        QCOMPARE(st[3]["ColorAdd_B"], 1.0);

        // Change the group: the cue changes with it, no re-store needed.
        Group g = *s.group(1);
        g.fixtures = {4, 3, 2, 1};
        s.setGroup(g);
        st = s.trackedState(*s.sequence(1), 0);
        QCOMPARE(st.keys(), (QList<int>{2, 4}));

        // Where the values came from: the preset for color, a plain level for the dimmer.
        const auto refs = s.trackedPresets(*s.sequence(1), 0);
        QCOMPARE(refs.keys(), (QList<int>{2, 4}));
        QCOMPARE(refs[4]["ColorAdd_B"], PresetRef(4, 2));
        QCOMPARE(refs[4]["Dimmer"], PresetRef(0, 0));

        // Build: blueprint becomes hard values that still reference the preset.
        QVERIFY(s.buildCue(1, 1));
        const Cue &cue = s.sequence(1)->cues[0];
        QVERIFY(cue.blueprints.isEmpty());
        QCOMPARE(cue.values[4]["ColorAdd_B"].presetId, 2);
        QCOMPARE(cue.values[4]["Dimmer"].value, 0.8);
    }

    void fadesAndTracking()
    {
        Show s;
        setupShow(s);
        Engine engine(&s);
        s.select({1});
        s.setAttribute("Dimmer", 1.0);
        s.setAttribute("ColorAdd_R", 1.0);
        QVERIFY(s.storeCue(1, 1));
        s.clearAll();
        s.select({1});
        s.setAttribute("Dimmer", 0.5);
        QVERIFY(s.storeCue(1, 2));
        s.clearAll();
        Sequence seq = *s.sequence(1);
        seq.cues[0].fade = 2.0;
        seq.cues[1].fade = 0.0;
        s.setSequence(seq);
        s.assignSequence(1, 1, 201);
        Executor &e = *s.executor(1, 201);

        s.go(e);
        engine.tick(1.0);
        QCOMPARE(dmx(engine, 1, 1), 128);   // halfway through a 2 s fade from 0
        engine.tick(1.0);
        QCOMPARE(dmx(engine, 1, 1), 255);
        QCOMPARE(dmx(engine, 1, 2), 255);   // red
        s.go(e);
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 1, 1), 128);   // snap cue at 50%
        QCOMPARE(dmx(engine, 1, 2), 255);   // red tracked from cue 1

        s.setFader(e, 0.5);
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 1, 1), 64);    // master scales intensity only
        QCOMPARE(dmx(engine, 1, 2), 255);

        s.setGrandMaster(0.5);
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 1, 1), 32);

        s.setFader(e, 0.0);                 // auto stop
        QVERIFY(!e.pb().isOn());
    }

    void htpAndLtpMerge()
    {
        Show s;
        setupShow(s);
        Engine engine(&s);
        s.select({1});
        s.setAttribute("Dimmer", 0.4);
        s.setAttribute("ColorAdd_G", 1.0);
        s.storeCue(1, 1);
        s.clearAll();
        s.select({1});
        s.setAttribute("Dimmer", 0.8);
        s.setAttribute("ColorAdd_G", 0.0);
        s.storeCue(2, 1);
        s.clearAll();
        for (int id : {1, 2}) {
            Sequence q = *s.sequence(id);
            q.cues[0].fade = 0;
            s.setSequence(q);
        }
        s.assignSequence(1, 1, 201);
        s.assignSequence(2, 1, 202);
        Executor &x1 = *s.executor(1, 201), &x2 = *s.executor(1, 202);
        // Everything LTP by default, intensity too. 201 went last, so it wins all of
        // fixture 1, and 202, overridden everywhere, turns itself off.
        s.go(x2);
        s.go(x1);
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 1, 1), 102);   // 0.4 from 201
        QCOMPARE(dmx(engine, 1, 3), 255);   // green from 201
        engine.tick(0.01);
        QVERIFY(!x2.pb().isOn());
        QVERIFY(x1.pb().isOn());

        // Off When Overridden off: 202 stays on underneath.
        for (int id : {1, 2}) {
            Sequence q = *s.sequence(id);
            q.offWhenOverridden = false;
            q.priority = SeqPriority::HTP;
            s.setSequence(q);
        }
        s.off(x1, true);
        s.go(x2);
        s.go(x1);
        engine.tick(0.01);
        engine.tick(0.01);
        QVERIFY(x2.pb().isOn());
        QCOMPARE(dmx(engine, 1, 1), 204);   // HTP: max(0.4, 0.8)
        QCOMPARE(dmx(engine, 1, 3), 255);   // the rest still LTP: 201 went last

        // A higher priority wins whatever went last; Ultra wins over the programmer too.
        {
            Sequence q = *s.sequence(2);
            q.priority = SeqPriority::Ultra;
            s.setSequence(q);
        }
        s.select({1});
        s.setAttribute("ColorAdd_G", 0.5);
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 1, 3), 0);     // 202's green, Ultra
        QCOMPARE(dmx(engine, 1, 1), 204);   // 202's 0.8
        s.clearAll();

        // Low data never turns anything off.
        for (int id : {1, 2}) {
            Sequence q = *s.sequence(id);
            q.offWhenOverridden = true;
            q.priority = id == 1 ? SeqPriority::Low : SeqPriority::LTP;
            s.setSequence(q);
        }
        s.off(x1, true);
        s.off(x2, true);
        s.go(x2);
        s.go(x1);   // Low, latest: still under 202
        engine.tick(0.01);
        engine.tick(0.01);
        QVERIFY(x2.pb().isOn());
        QCOMPARE(dmx(engine, 1, 1), 204);
        QVERIFY(!x1.pb().isOn());   // but it was overridden itself, by LTP data

        // Programmer overrides playbacks
        s.select({1});
        s.setAttribute("Dimmer", 0.1);
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 1, 1), 26);
    }

    // Effects sit in a layer of their own above everyone's values, keeping relative
    // values apart from absolute ones.
    void effectPriorityLayers()
    {
        Show s;
        setupShow(s);
        Engine engine(&s);
        const QVector<Effect> lib = effectTemplates();
        for (int i = 0; i < lib.size(); ++i) {
            Effect e = lib[i];
            e.id = i + 1;
            s.setEffect(e);
        }
        const int circle = 7, rainbow = 12, police = 15;
        QCOMPARE(s.effect(circle)->name, QString("Circle"));
        QCOMPARE(s.effect(rainbow)->name, QString("Rainbow"));
        QCOMPARE(s.effect(police)->name, QString("Police"));
        auto store = [&](int seq, const QVector<int> &fids, const QHash<QString, double> &values, int fx,
                         SeqPriority priority) {
            s.select(fids);
            for (auto v = values.begin(); v != values.end(); ++v)
                s.setAttribute(v.key(), v.value());
            if (fx)
                s.applyEffect(fx);
            s.storeCue(seq, 1);
            s.clearAll();
            Sequence q = *s.sequence(seq);
            q.cues[0].fade = 0;
            q.priority = priority;
            s.setSequence(q);
            s.assignSequence(seq, 1, 200 + seq);
            return s.executor(1, 200 + seq);
        };
        Executor *circleOnly = store(1, {5}, {}, circle, SeqPriority::LTP);
        Executor *position = store(2, {5}, {{"Pan", 0.3}, {"Tilt", 0.6}}, 0, SeqPriority::LTP);
        Executor *positionCircle = store(3, {5}, {{"Pan", 0.7}, {"Tilt", 0.2}}, circle, SeqPriority::LTP);
        Executor *lowRainbow = store(4, {1}, {}, rainbow, SeqPriority::Low);
        Executor *red = store(5, {1}, {{"ColorAdd_R", 1.0}, {"ColorAdd_G", 0.0}, {"ColorAdd_B", 0.0}}, 0, SeqPriority::LTP);
        Executor *lowPolice = store(6, {1}, {}, police, SeqPriority::Low);
        auto pan = [&]() { return s.lastOutput()[5]["Pan"]; };

        // A circle on its own, then a position from another executor: neither turns the other
        // off, the circle goes round the new position.
        s.go(*circleOnly);
        engine.tick(0.01);
        s.go(*position);
        double lo = 1, hi = 0;
        for (int i = 0; i < 40; ++i) {
            engine.tick(0.1);
            lo = std::min(lo, pan());
            hi = std::max(hi, pan());
        }
        QVERIFY(circleOnly->pb().isOn());
        QVERIFY(position->pb().isOn());
        QVERIFY2(hi - lo > 0.05 && lo < 0.3 && hi > 0.3, qPrintable(QStringLiteral("%1..%2").arg(lo).arg(hi)));

        // An executor with a position and a circle is overridden by a later position, and goes.
        s.off(*circleOnly, true);
        s.off(*position, true);
        engine.tick(0.01);
        s.go(*positionCircle);
        engine.tick(0.01);
        s.go(*position);
        engine.tick(0.01);
        engine.tick(0.01);
        QVERIFY(!positionCircle->pb().isOn());
        QVERIFY(position->pb().isOn());
        engine.tick(0.3);
        QCOMPARE(pan(), 0.3);   // nothing moving it now
        s.off(*position, true);

        // A Low color effect still runs under an LTP color preset.
        auto sample = [&](double *maxG, double *maxB) {
            *maxG = 0;
            *maxB = 0;
            for (int i = 0; i < 60; ++i) {
                engine.tick(0.1);
                *maxG = std::max(*maxG, s.lastOutput()[1]["ColorAdd_G"]);
                *maxB = std::max(*maxB, s.lastOutput()[1]["ColorAdd_B"]);
            }
        };
        s.go(*lowRainbow);
        engine.tick(0.01);
        s.go(*red);
        double g, b;
        sample(&g, &b);
        QVERIFY(lowRainbow->pb().isOn());
        QVERIFY(red->pb().isOn());
        QVERIFY2(g > 0.3, qPrintable(QString::number(g)));   // the rainbow, not plain red

        // A second Low color effect takes over from the first (police: red and blue, no green).
        s.go(*lowPolice);
        engine.tick(0.01);
        sample(&g, &b);
        QVERIFY(lowRainbow->pb().isOn());   // Low: underneath, still on
        QVERIFY(lowPolice->pb().isOn());
        QVERIFY2(g < 0.01 && b > 0.9, qPrintable(QStringLiteral("%1 %2").arg(g).arg(b)));

        // Stomp: a preset onto fixtures running a programmer effect stops it on that feature.
        s.clearAll();
        s.select({1, 2});
        s.setAttribute("ColorAdd_R", 1.0);
        s.storePreset(int(Feature::Color), 1);
        s.clearAll();
        s.select({1, 2});
        s.applyEffect(rainbow);
        s.select({1});
        QVERIFY(s.applyPreset(int(Feature::Color), 1));
        QCOMPARE(s.programmerEffects().size(), 1);
        QCOMPARE(s.programmerEffects()[0].fixtures, QVector<int>({2}));
    }

    void presetEffectsWithSpreads()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        const QVector<Effect> lib = effectTemplates();
        for (int i = 0; i < lib.size(); ++i) {
            Effect e = lib[i];
            e.id = i + 1;
            s.setEffect(e);
        }
        const int strobe = lib.size() - 2;   // "Strobe"
        const int circle = 7;
        QCOMPARE(s.effect(strobe)->name, QString("Strobe"));
        // A strobe with a random spread, stored universally in an All preset.
        cl.execute("Fixture 1 Thru 4");
        QVERIFY(cl.execute("Spread Direction Random").ok);
        QVERIFY(cl.execute(QStringLiteral("Effect %1").arg(strobe)).ok);
        QVERIFY(cl.execute("Store All.5 /universal").ok);
        const Preset *all5 = s.preset(kPresetTypeAll, 5);
        QVERIFY(all5 && all5->effects.size() == 1);
        QVERIFY(all5->effects[0].hasLayout);
        QCOMPARE(all5->effects[0].layout.direction, SpreadDirection::Random);
        QVERIFY(all5->effects[0].fixtures.isEmpty());
        cl.execute("ClearAll");
        cl.execute("Fixture 5 + 6 + 7");   // anything: it's universal
        QVERIFY(cl.execute("All.5").ok);
        QCOMPARE(s.programmerEffects().size(), 1);
        QCOMPARE(s.programmerEffects()[0].fixtures.size(), 3);
        QCOMPARE(s.programmerEffects()[0].layout.direction, SpreadDirection::Random);   // the spread came along
        cl.execute("ClearAll");

        // A circle on fixture 5, Selective: only 5 plays it. Global: every spot of that type.
        cl.execute("Fixture 5");
        cl.execute(QStringLiteral("Effect %1").arg(circle));
        QVERIFY(cl.execute("Store Position.7 /selective").ok);
        QVERIFY(cl.execute("Store Position.8 /global").ok);
        cl.execute("ClearAll");
        cl.execute("Fixture 1 + 5 + 6");
        cl.execute("Position.7");
        QCOMPARE(s.programmerEffects().size(), 1);
        QCOMPARE(s.programmerEffects()[0].fixtures, QVector<int>{5});
        cl.execute("ClearAll");
        cl.execute("Fixture 1 + 5 + 6");
        cl.execute("Position.8");
        QCOMPARE(s.programmerEffects()[0].fixtures, (QVector<int>{5, 6}));   // spots, not the par
        cl.execute("ClearAll");

        // Blueprints bring a preset's effects onto their group.
        s.select({1, 2, 3, 4});
        s.storeGroup(9);
        s.clearAll();
        s.select({1});
        s.setAttribute("Dimmer", 0.5);
        s.storeCue(3, 1);
        s.clearAll();
        QVERIFY(cl.execute("Blueprint Sequence 3 Cue 1 Group 9 All.5").ok);
        const QVector<EffectInstance> fx = s.cueEffects(s.sequence(3)->cues[0]);
        QCOMPARE(fx.size(), 1);
        QCOMPARE(fx[0].effectId, strobe);
        QCOMPARE(fx[0].fixtures.size(), 4);
        QCOMPARE(fx[0].layout.direction, SpreadDirection::Random);

        // Saved and loaded with their layers.
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        QCOMPARE(t.preset(2, 8)->effects[0].scope, int(EffectInstance::Global));
        QCOMPARE(t.preset(2, 7)->effects[0].fixtures, QVector<int>{5});
    }

    void effectsFollowFades()
    {
        Show s;
        setupShow(s);
        Engine engine(&s);
        const QVector<Effect> lib = effectTemplates();
        Effect wave = lib[0];   // Dimmer wave
        wave.id = 1;
        s.setEffect(wave);
        // Cue 1: pars at 50 with a dimmer wave; cue 2: pars at 50, no effect, 2 s fade.
        for (int c = 1; c <= 2; ++c) {
            s.select({1, 2, 3, 4});
            s.setAttribute("Dimmer", 0.5);
            if (c == 1)
                s.applyEffect(1);
            s.storeCue(1, c);
            s.clearAll();
        }
        Sequence q = *s.sequence(1);
        q.cues[0].fade = 0;
        q.cues[1].fade = 2;
        s.setSequence(q);
        s.assignSequence(1, 1, 201);
        Executor &x = *s.executor(1, 201);
        s.go(x);
        engine.tick(0.01);
        QVERIFY(!s.cueEffects(s.sequence(1)->cues[0]).isEmpty());
        // Spread of the wave across the four pars (all different) then, half way into cue 2,
        // the spread is roughly halved: the wave fades out with the cue instead of snapping.
        auto spreadNow = [&]() {
            double lo = 1, hi = 0;
            for (int f = 1; f <= 4; ++f) {
                lo = std::min(lo, s.lastOutput()[f]["Dimmer"]);
                hi = std::max(hi, s.lastOutput()[f]["Dimmer"]);
            }
            return hi - lo;
        };
        const double full = spreadNow();
        QVERIFY(full > 0.2);
        s.go(x);
        engine.tick(1.0);
        const double half = spreadNow();
        QVERIFY2(half > 0.05 && half < full, qPrintable(QStringLiteral("%1 of %2").arg(half).arg(full)));
        engine.tick(1.1);
        QVERIFY(spreadNow() < 1e-6);   // gone once the fade is done
    }

    void cueTriggers()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        Engine engine(&s);
        for (int c = 1; c <= 3; ++c) {
            s.select({1});
            s.setAttribute("Dimmer", c * 0.2);
            s.storeCue(1, c);
            s.clearAll();
        }
        QVERIFY(cl.execute("Cue 1 Fade 0.5").ok);
        QVERIFY(cl.execute("Cue 2 Follow Fade 0").ok);
        QVERIFY(cl.execute("Cue 3 Wait 1 Fade 0").ok);
        QCOMPARE(s.sequence(1)->cues[2].trigger, CueTrigger::Wait);
        Sequence q = *s.sequence(1);
        q.wrap = false;
        s.setSequence(q);
        s.assignSequence(1, 1, 201);
        Executor &x = *s.executor(1, 201);
        s.go(x);
        engine.tick(0.3);
        QCOMPARE(x.pb().cueIndex, 0);   // still fading
        engine.tick(0.3);
        engine.tick(0.01);
        QCOMPARE(x.pb().cueIndex, 1);   // followed once cue 1 was done
        engine.tick(0.5);
        QCOMPARE(x.pb().cueIndex, 1);   // waiting
        engine.tick(0.6);
        QCOMPARE(x.pb().cueIndex, 2);   // a second after cue 2 started
        engine.tick(5);
        QCOMPARE(x.pb().cueIndex, 2);   // the last one: nothing after it
        // The Off line can follow too: off once the last cue is done.
        QVERIFY(cl.execute("Cue Off Wait 1").ok);
        QCOMPARE(s.sequence(1)->offTrigger, CueTrigger::Wait);
        engine.tick(0.01);
        QVERIFY(!x.pb().isOn());   // the last cue started well over a second ago
        QVERIFY(cl.execute("Cue Off Go").ok);
        // Saved and loaded.
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        QCOMPARE(t.sequence(1)->cues[1].trigger, CueTrigger::Follow);
        QCOMPARE(t.sequence(1)->cues[2].wait, 1.0);
        QVERIFY(cl.execute("Cue 2 Go").ok);
        QCOMPARE(s.sequence(1)->cues[1].trigger, CueTrigger::Go);
        // Off line on Go and no wrap: Go on the last cue turns the sequence off.
        s.gotoCue(x, 2, false);
        QCOMPARE(x.pb().cueIndex, 2);
        s.go(x);
        QVERIFY(!x.pb().isOn());
        s.go(x);   // and the next Go starts it again from the top
        QCOMPARE(x.pb().cueIndex, 0);
    }

    void executorsInTandem()
    {
        Show s;
        setupShow(s);
        Engine engine(&s);
        for (int c = 1; c <= 2; ++c) {
            s.select({1});
            s.setAttribute("Dimmer", c * 0.3);
            s.storeCue(1, c);
            s.clearAll();
        }
        Sequence q = *s.sequence(1);
        for (Cue &c : q.cues)
            c.fade = 10;
        s.setSequence(q);
        // Sequence 1 on a key (101), a fader (201), an encoder (301) and 401.
        for (int n : {101, 201, 301, 401})
            s.assignSequence(1, 1, n);
        Executor &go = *s.executor(1, 101), &fader = *s.executor(1, 201), &push = *s.executor(1, 301), &off = *s.executor(1, 401);
        QCOMPARE(go.buttons[0], ButtonFn::Go);
        QCOMPARE(fader.buttons[0], ButtonFn::Go);
        push.buttons[0] = ButtonFn::Pause;
        off.buttons[0] = ButtonFn::Off;
        s.pressButton(go, 0, true);
        QCOMPARE(fader.pb().cueIndex, 0);   // one playback
        s.pressButton(go, 0, true);         // cue 2, fading
        engine.tick(1.0);
        s.pressButton(push, 0, true);       // paused from 301
        QVERIFY(go.pb().paused && fader.pb().paused);
        const double held = go.pb().elapsed;
        engine.tick(1.0);
        QCOMPARE(go.pb().elapsed, held);
        s.pressButton(off, 0, true);        // off from 401
        QVERIFY(!go.pb().isOn() && !fader.pb().isOn());
        // Master faders of the sequence move together.
        Executor &fader2 = s.ensureExecutor(1, 202);
        s.assignSequence(1, 1, 202);
        s.setFader(fader, 0.5);
        QCOMPARE(fader2.fader, 0.5);
        // Saved and loaded: still one playback.
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        t.pressButton(*t.executor(1, 101), 0, true);
        QVERIFY(t.executor(1, 201)->pb().isOn());
    }

    void virtualChannels()
    {
        // RGB with no dimmer: a virtual Dimmer channel appears, with no DMX slot.
        FixtureType t;
        t.id = "x/rgb";
        FixtureMode m;
        m.name = "RGB";
        for (int i = 0; i < 3; ++i) {
            ChannelDef c;
            c.attribute = QStringList{"ColorAdd_R", "ColorAdd_G", "ColorAdd_B"}[i];
            c.coarse = i;
            m.channels << c;
        }
        // Plus a virtual channel of its own: programmable, never output.
        ChannelDef zoom;
        zoom.attribute = "Zoom";
        zoom.coarse = -1;
        m.channels << zoom;
        t.modes << m;
        t.finalize();
        const FixtureMode &f = t.modes[0];
        QCOMPARE(f.channels.size(), 5);
        QCOMPARE(f.channels[0].attribute, QString("Dimmer"));
        QVERIFY(f.channels[0].isVirtual());
        QVERIFY(f.virtualDimmer);
        QCOMPARE(f.footprint, 3);
        // Saved and read back the same; finalizing again adds nothing.
        FixtureType back = FixtureType::fromJson(t.toJson());
        QCOMPARE(back.modes[0].channels.size(), 5);
        QVERIFY(back.modes[0].channel("Zoom")->isVirtual());
        // Removed by hand, it stays removed, also after saving.
        FixtureType none = t;
        none.modes[0].channels.removeFirst();
        none.modes[0].noAutoDimmer = true;
        none.finalize();
        QVERIFY(!none.modes[0].channel("Dimmer"));
        QVERIFY(!FixtureType::fromJson(none.toJson()).modes[0].channel("Dimmer"));
        // A real Dimmer replaces the virtual one.
        FixtureMode real = f;
        ChannelDef d;
        d.attribute = "Dimmer";
        d.coarse = 3;
        real.channels << d;
        QVERIFY(real.ensureVirtualDimmer());
        QCOMPARE(real.channels.size(), 5);
        QVERIFY(!real.channel("Dimmer")->isVirtual());

        // Output: the virtual Zoom writes nothing; the virtual Dimmer scales the colors.
        Show s;
        s.library().add(t);
        QVERIFY(s.addFixture({1, QString(), "x/rgb", "RGB", 1, 1}));
        s.select({1});
        s.setAttribute("Dimmer", 0.5);
        s.setAttribute("ColorAdd_R", 1.0);
        s.setAttribute("Zoom", 1.0);
        Engine e(&s);
        e.tick(0.025);
        QCOMPARE(int(uchar(e.universes()[1].at(0))), 128);
        QCOMPARE(int(uchar(e.universes()[1].at(3))), 0);   // nothing after the 3 RGB slots
    }

    void virtualDimmer()
    {
        Show s;
        setupShow(s);
        Engine engine(&s);
        s.select({7});
        s.setAttribute("ColorAdd_R", 1.0);
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 2, 1), 0);     // dimmer defaults to 0
        s.setAttribute("Dimmer", 0.5);
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 2, 1), 128);
        s.setHighlight(true);
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 2, 1), 255);
        QCOMPARE(dmx(engine, 2, 3), 255);   // highlight = white
    }

    void sixteenBit()
    {
        Show s;
        setupShow(s);
        Engine engine(&s);
        s.select({5});
        s.setAttribute("Pan", 0.0);
        engine.tick(0.01);
        // Pan 0° is the middle of -270..270: 0.5 * 65535 = 32768 → 0x80 0x00 at 1.101 / 1.102
        QCOMPARE(dmx(engine, 1, 101), 0x80);
        QCOMPARE(dmx(engine, 1, 102), 0x00);
        s.setAttribute("Pan", 90.0);   // 360 of 540: 43690 → 0xAA 0xAA
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 1, 101), 0xAA);
        QCOMPARE(dmx(engine, 1, 102), 0xAA);
        s.setAttribute("Pan", 400.0);   // past the end: held at 270
        QCOMPARE(s.programmerValues()[5]["Pan"].value, 270.0);
    }

    void flashAndSwop()
    {
        Show s;
        setupShow(s);
        Engine engine(&s);
        s.select({1});
        s.setAttribute("Dimmer", 1.0);
        s.storeCue(1, 1);
        s.clearAll();
        s.select({2});
        s.setAttribute("Dimmer", 1.0);
        s.storeCue(2, 1);
        s.clearAll();
        s.assignSequence(1, 1, 201);
        s.assignSequence(2, 1, 202);
        Executor &a = *s.executor(1, 201);
        Executor &b = *s.executor(1, 202);
        a.fader = 0.0;
        s.setFader(a, 0.2);   // auto start when the fader leaves zero
        QVERIFY(a.pb().isOn());
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 1, 1), 51);

        // Flash (on button 3 here: a fader's keys are Go, GoBack, Off by default) goes to full while held
        QCOMPARE(a.buttons[2], ButtonFn::Off);
        a.buttons[2] = ButtonFn::Flash;
        s.pressButton(a, 2, true);
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 1, 1), 255);
        s.pressButton(a, 2, false);
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 1, 1), 51);
        QVERIFY(a.pb().isOn());   // it was already running, so release keeps it on

        // Swop on B blacks out A's intensity
        b.buttons[0] = ButtonFn::Swop;
        s.pressButton(b, 0, true);
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 1, 1), 0);
        QCOMPARE(dmx(engine, 1, 7), 255);
        s.pressButton(b, 0, false);
        QVERIFY(!b.pb().isOn());  // swop started it, release turns it off
    }

    void xfadeFader()
    {
        Show s;
        setupShow(s);
        Engine engine(&s);
        s.select({1});
        s.setAttribute("Dimmer", 0.0);
        s.storeCue(1, 1);
        s.setAttribute("Dimmer", 1.0);
        s.storeCue(1, 2);
        s.clearAll();
        s.assignSequence(1, 1, 201);
        Executor &e = *s.executor(1, 201);
        e.faderFn = FaderFn::XFade;
        e.fader = 0.0;
        s.setFader(e, 0.0);
        s.setFader(e, 0.001);   // starts at cue 1
        engine.tick(0.01);
        s.setFader(e, 0.5);
        engine.tick(0.01);
        QCOMPARE(e.pb().cueIndex, 1);
        QVERIFY(std::abs(dmx(engine, 1, 1) - 128) <= 1);
        s.setFader(e, 1.0);
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 1, 1), 255);
        QCOMPARE(e.pb().xfadeTarget, -1);
    }

    void commandLine()
    {
        Show s;
        setupShow(s);
        Engine engine(&s);
        CommandLine cl(&s);
        CommandResult r = cl.execute("Fixture 1 Thru 4 - 2 At 50");
        QVERIFY2(r.ok, qPrintable(r.message));
        QCOMPARE(s.selection(), (QVector<int>{1, 3, 4}));
        QCOMPARE(s.programmerOutput()[3]["Dimmer"], 0.5);

        QVERIFY(cl.execute("Store Group 1 \"Odd Pars\"").ok);
        QCOMPARE(s.group(1)->name, QString("Odd Pars"));
        QVERIFY(cl.execute("Attribute ColorAdd_R At Full").ok);
        QVERIFY(cl.execute("Store Preset 4.1 \"Red\"").ok);
        QVERIFY(cl.execute("Store Cue 1").ok);
        QVERIFY(cl.execute("Cue 1 Fade 0").ok);
        QVERIFY(cl.execute("ClearAll").ok);
        QVERIFY(s.programmerValues().isEmpty());
        QVERIFY(cl.execute("Assign Sequence 1 At Executor 201").ok);
        QVERIFY(cl.execute("Go Executor 201").ok);
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 1, 13), 128);
        QCOMPARE(dmx(engine, 1, 14), 255);

        QVERIFY(cl.execute("g 1 @ full").ok);   // abbreviations
        QCOMPARE(s.programmerOutput()[1]["Dimmer"], 1.0);
        QVERIFY(cl.execute("Clear").ok);
        QVERIFY(s.selection().isEmpty());
        QVERIFY(!s.programmerValues().isEmpty());
        QVERIFY(cl.execute("Clear").ok);
        QVERIFY(s.programmerValues().isEmpty());

        QVERIFY(cl.execute("Blueprint Cue 2 Group 1 Preset 4.1 At 70 Every 2").ok);
        QCOMPARE(s.sequence(1)->cues[1].blueprints.size(), 1);
        QVERIFY(cl.execute("Executor 201 At 40").ok);
        QCOMPARE(s.executor(1, 201)->fader, 0.4);
        QVERIFY(cl.execute("Goto Cue 2 Executor 201").ok);
        QCOMPARE(s.executor(1, 201)->pb().cueIndex, 1);
        // A fade of its own, for every value.
        QVERIFY(cl.execute("Goto Cue 1 Executor 201 Fade 7").ok);
        QCOMPARE(s.executor(1, 201)->pb().cueIndex, 0);
        QCOMPARE(s.executor(1, 201)->pb().fadeTime, 7.0);
        QVERIFY(s.executor(1, 201)->pb().timing.isEmpty());
        // Goto Executor alone, and a Goto button: the screens show the cue list.
        {
            QSignalSpy asked(&s, &Show::gotoRequested);
            QVERIFY(cl.execute("Goto Executor 201").ok);
            QCOMPARE(asked.count(), 1);
            QCOMPARE(asked[0][1].toInt(), 201);
            Executor &ge = *s.executor(1, 201);
            ge.buttons[1] = ButtonFn::Goto;
            s.pressButton(ge, 1, true);
            QCOMPARE(asked.count(), 2);
            QCOMPARE(buttonFnFromName("Goto"), ButtonFn::Goto);
            QVERIFY(cl.execute("Assign Goto At Executor 201").ok);
            QCOMPARE(s.executor(1, 201)->buttons[0], ButtonFn::Goto);
            s.executor(1, 201)->buttons[0] = ButtonFn::Go;
        }
        QVERIFY(cl.execute("Off Executor 201").ok);
        QVERIFY(!s.executor(1, 201)->pb().isOn());
        QVERIFY(cl.execute("Page +").ok);
        QCOMPARE(s.currentPage(), 2);

        QVERIFY(!cl.execute("Frobnicate 3").ok);
        QVERIFY(!cl.execute("Store Group 2").ok);   // nothing selected
        QVERIFY(!cl.execute("Preset 4.9").ok);
    }

    void midiLearnAndPickup()
    {
        Show s;
        setupShow(s);
        MidiManager midi(&s);
        s.select({1});
        s.setAttribute("Dimmer", 1.0);
        s.storeCue(1, 1);
        s.clearAll();
        s.assignSequence(1, 1, 201);
        Executor &e = *s.executor(1, 201);
        e.fader = 1.0;

        // Learn the fader
        MidiTarget fader;
        fader.kind = MidiTarget::ExecutorFader;
        fader.executor = 201;
        QSignalSpy learned(&midi, &MidiManager::learned);
        midi.arm(fader);
        midi.handleMessage(cc(7, 10));
        QCOMPARE(learned.count(), 1);
        QVERIFY(!midi.isArmed());
        QCOMPARE(midi.bindings().size(), 1);
        QCOMPARE(midi.bindings()[0].source.number, 7);
        QVERIFY(midi.bindings()[0].pickup);   // no feedback output → soft takeover
        QCOMPARE(e.fader, 1.0);               // the learn message itself didn't move anything

        // Pickup: physical fader at 0.2 must not jump the 100% software fader
        midi.handleMessage(cc(7, 25));
        QCOMPARE(e.fader, 1.0);
        midi.handleMessage(cc(7, 126));        // within 3% → picked up
        QVERIFY(std::abs(e.fader - 126 / 127.0) < 1e-9);
        midi.handleMessage(cc(7, 64));
        QVERIFY(std::abs(e.fader - 64 / 127.0) < 1e-9);

        // Learn a button: Note 36 → Go
        MidiTarget go;
        go.kind = MidiTarget::ExecutorButton;
        go.executor = 201;
        go.button = 0;
        midi.arm(go);
        midi.handleMessage(note(36, false));   // releases are ignored while learning
        QVERIFY(midi.isArmed());
        midi.handleMessage(note(36, true));
        QCOMPARE(midi.bindings().size(), 2);
        e.pb() = PlaybackState();
        midi.handleMessage(note(36, true));
        QVERIFY(e.pb().isOn());

        // Re-learning a control moves it: CC 7 now drives the grand master instead
        MidiTarget gm;
        gm.kind = MidiTarget::GrandMaster;
        midi.arm(gm);
        midi.handleMessage(cc(7, 127));
        QCOMPARE(midi.bindings().size(), 2);
        QCOMPARE(midi.bindingsFor(fader).size(), 0);

        // Bindings persist
        MidiManager other(&s);
        other.fromJson(midi.toJson());
        QCOMPARE(other.bindings().size(), 2);
        QCOMPARE(other.bindings()[0].target.kind, MidiTarget::ExecutorButton);

        // Encoders: by place on the bar. An endless encoder is spotted from its first step.
        QCOMPARE(MidiBinding::relativeSteps(65), 1);
        QCOMPARE(MidiBinding::relativeSteps(63), -1);
        QCOMPARE(MidiBinding::relativeSteps(1), 1);
        QCOMPARE(MidiBinding::relativeSteps(127), -1);
        QCOMPARE(MidiBinding::relativeSteps(125), -3);
        MidiTarget enc;
        enc.kind = MidiTarget::Encoder;
        enc.encoder = 2;
        QSignalSpy turned(&midi, &MidiManager::encoderTurned);
        midi.arm(enc);
        midi.handleMessage(cc(20, 65));
        QVERIFY(midi.bindings().last().relative);
        midi.handleMessage(cc(20, 63));
        QCOMPARE(turned.count(), 1);
        QCOMPARE(turned[0][0].toInt(), 2);
        QVERIFY(std::abs(turned[0][1].toDouble() + 0.01) < 1e-9);
        // A knob: how far it moved.
        enc.encoder = 3;
        midi.arm(enc);
        midi.handleMessage(cc(21, 40));
        QVERIFY(!midi.bindings().last().relative);
        midi.handleMessage(cc(21, 40));   // where it is: nothing yet
        midi.handleMessage(cc(21, 50));
        QCOMPARE(turned.count(), 2);
        QCOMPARE(turned[1][0].toInt(), 3);
        QVERIFY(std::abs(turned[1][1].toDouble() - 10 / 127.0) < 1e-9);
        // Push
        MidiTarget push;
        push.kind = MidiTarget::EncoderPush;
        push.encoder = 2;
        QSignalSpy pushed(&midi, &MidiManager::encoderPushed);
        midi.arm(push);
        midi.handleMessage(note(40, true));
        midi.handleMessage(note(40, true));
        QCOMPARE(pushed.count(), 1);
        MidiManager third(&s);
        third.fromJson(midi.toJson());
        QCOMPARE(third.bindings().last().target.kind, MidiTarget::EncoderPush);
        QCOMPARE(third.bindings().last().target.encoder, 2);

        // Rate masters: the fader (with pickup) and the Learn key.
        MidiTarget rmFader;
        rmFader.kind = MidiTarget::RateMasterFader;
        rmFader.master = 3;
        midi.arm(rmFader);
        midi.handleMessage(cc(30, 64));
        QVERIFY(midi.bindings().last().pickup);
        const double before = s.rateMaster(3)->value;
        midi.handleMessage(cc(30, 0));            // far from the fader: not picked up yet
        QCOMPARE(s.rateMaster(3)->value, before);
        midi.handleMessage(cc(30, int(std::lround(s.rateMaster(3)->faderPosition() * 127))));
        midi.handleMessage(cc(30, 127));
        QVERIFY(s.rateMaster(3)->value > before);
        MidiTarget rmKey;
        rmKey.kind = MidiTarget::RateMasterKey;
        rmKey.master = 3;
        midi.arm(rmKey);
        midi.handleMessage(note(50, true));
        QCOMPARE(midi.bindings().last().target.kind, MidiTarget::RateMasterKey);
        MidiManager fourth(&s);
        fourth.fromJson(midi.toJson());
        QCOMPARE(fourth.bindings().last().target.master, 3);
    }

    void selectionMatrix()
    {
        Show s;
        setupShow(s);
        QVERIFY(s.addFixture({8, QString(), "generic/rgb-par", "3-channel", 2, 4}));
        CommandLine cl(&s);
        // Selecting fills along X; Next Y starts the next ones one step down.
        QVERIFY(cl.execute("Fixture 1 Thru 4").ok);
        QVERIFY(cl.execute("NextY").ok);
        QVERIFY(cl.execute("Fixture 5 Thru 8").ok);
        QCOMPARE(s.gridPosition(1), QPoint(0, 0));
        QCOMPARE(s.gridPosition(4), QPoint(3, 0));
        QCOMPARE(s.gridPosition(5), QPoint(0, 1));
        QCOMPARE(s.gridPosition(8), QPoint(3, 1));
        // Grid: coordinates, as x/y or x y.
        QVERIFY(cl.execute("Fixture 8 Grid 6/2").ok);
        QCOMPARE(s.gridPosition(8), QPoint(6, 2));
        QVERIFY(cl.execute("Grid 0 3").ok);
        QCOMPARE(s.gridCursor(), QPoint(0, 3));
        QVERIFY(cl.execute("Grid 0/0 Thru 1/0").ok);   // two wide: 1 2 / 3 4 / 5 6 / 7 8
        QCOMPARE(s.gridPosition(3), QPoint(0, 1));
        QCOMPARE(s.gridPosition(8), QPoint(1, 3));
        QVERIFY(!cl.execute("Grid 2").ok);
        s.resetGrid();
        QVERIFY(cl.execute("Clear").ok);
        QVERIFY(cl.execute("Fixture 1 Thru 4").ok);
        QVERIFY(cl.execute("NextY").ok);
        QVERIFY(cl.execute("Fixture 5 Thru 8").ok);
        // A group keeps its layout.
        QVERIFY(cl.execute("Store Group 9").ok);
        QCOMPARE(s.group(9)->grid.size(), 8);
        QVERIFY(cl.execute("Clear").ok);
        QCOMPARE(s.gridPosition(5), QPoint());
        QVERIFY(cl.execute("Group 9").ok);
        QCOMPARE(s.gridPosition(6), QPoint(1, 1));
        // Spreads stack: X keeps columns, Y keeps rows.
        QVERIFY(cl.execute("Spread Every 2").ok);
        QCOMPARE(s.workingSelection(), (QVector<int>{1, 3, 5, 7}));
        QVERIFY(cl.execute("Spread Y Every 2").ok);
        QCOMPARE(s.activeSpreadY().every, 2);
        QCOMPARE(s.workingSelection(), (QVector<int>{1, 3}));
        s.setSpreadAxisOn(Spread::Axis::X, false);
        QCOMPARE(s.workingSelection(), (QVector<int>{1, 2, 3, 4}));
        s.setSpreadAxisOn(Spread::Axis::X, true);
        // Stored on its axis, recalled onto it.
        QVERIFY(s.storeSpread(20, "Down", 1));
        QVERIFY(s.spread(20)->onY && !s.spread(20)->onX);
        // One spread can work both ways: it fills both slots.
        QVERIFY(s.storeSpread(21, "Both", 2));
        QVERIFY(s.spread(21)->onX && s.spread(21)->onY);
        // An effect run now keeps where its fixtures sit and both layouts.
        Effect fx;
        fx.id = 30;
        fx.name = "Diag";
        fx.axisY = true;
        s.setEffect(fx);
        QVERIFY(s.effect(30)->axisX && s.effect(30)->axisY);
        // Clearing the selection resets the spreads and the matrix.
        QVERIFY(cl.execute("Clear").ok);
        QVERIFY(s.activeSpread().isDefault());
        QVERIFY(s.activeSpreadY().isDefault());
        QVERIFY(cl.execute("Spread 20").ok);
        QCOMPARE(s.activeSpreadY().every, 2);
        QVERIFY(s.activeSpread().isDefault());
        // Round trips.
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        QCOMPARE(t.group(9)->grid, s.group(9)->grid);
        QVERIFY(t.spread(20)->onY && !t.spread(20)->onX);
        QVERIFY(t.spread(21)->onX && t.spread(21)->onY);
        QVERIFY(cl.execute("Clear").ok);
        QVERIFY(cl.execute("Group 9").ok);
        QVERIFY(cl.execute("Spread 21").ok);   // Every 2 both ways
        QCOMPARE(s.workingSelection(), (QVector<int>{1, 3}));
        QVERIFY(t.effect(30)->axisY);
        // The grid as indices: gaps don't count.
        const GridIndex gi = gridIndex({1, 2, 3}, {QPoint(0, 0), QPoint(5, 0), QPoint(5, 3)});
        QCOMPARE(gi.cols, 2);
        QCOMPARE(gi.rows, 2);
        QCOMPARE(gi.col[1], 1);
        QCOMPARE(gi.row[2], 1);
    }

    void matrixMoveAndSpreadStore()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        QVERIFY(cl.execute("Fixture 1 Thru 4").ok);   // X 0..3, Y 0
        // Move 1 and 2 down one: nobody in the way.
        s.moveInGrid({1, 2}, QPoint(0, 1));
        QCOMPARE(s.gridPosition(1), QPoint(0, 1));
        QCOMPARE(s.gridPosition(2), QPoint(1, 1));
        // Move 3 and 4 onto them: 1 and 2 take the cells 3 and 4 left.
        s.moveInGrid({3, 4}, QPoint(-2, 1));
        QCOMPARE(s.gridPosition(3), QPoint(0, 1));
        QCOMPARE(s.gridPosition(4), QPoint(1, 1));
        QCOMPARE(s.gridPosition(1), QPoint(2, 0));
        QCOMPARE(s.gridPosition(2), QPoint(3, 0));
        // Not past the edge.
        s.moveInGrid({3}, QPoint(-5, -5));
        QCOMPARE(s.gridPosition(3), QPoint(0, 0));
        // Store Spread with X and Y different: X in 30, Y in the next free slot.
        QVERIFY(cl.execute("Spread X Every 2").ok);
        QVERIFY(cl.execute("Spread Y Every 3").ok);
        QVERIFY(cl.execute("Store Spread 30").ok);
        QVERIFY(s.spread(30) && s.spread(30)->onX && !s.spread(30)->onY && s.spread(30)->every == 2);
        QVERIFY(s.spread(31) && s.spread(31)->onY && !s.spread(31)->onX && s.spread(31)->every == 3);
        QVERIFY(cl.execute("Store Spread 40 /y").ok);
        QVERIFY(s.spread(40)->onY && !s.spread(40)->onX);
        QVERIFY(cl.execute("Spread Y Every 2").ok);
        QVERIFY(cl.execute("Store Spread 50").ok);   // the same both ways: one spread for both
        QVERIFY(s.spread(50)->onX && s.spread(50)->onY && !s.spread(51));
    }

    void undoSteps()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        UndoManager undo(&s);
        auto run = [&](const QString &c) {
            undo.beginAction(c);
            QVERIFY(cl.execute(c).ok);
            QCoreApplication::processEvents();   // the command's step ends
        };
        run("Fixture 1 Thru 2 At 50");
        run("Store Group 5 \"Pair\"");
        run("Store Cue 1 Sequence 3");
        QVERIFY(s.group(5));
        QVERIFY(s.sequence(3) && s.sequence(3)->cues.size() == 1);
        QCOMPARE(undo.undo(), QStringLiteral("Store Cue 1 Sequence 3"));
        QVERIFY(!s.sequence(3) || s.sequence(3)->cues.isEmpty());
        QCOMPARE(undo.undo(), QStringLiteral("Store Group 5 \"Pair\""));
        QVERIFY(!s.group(5));
        QVERIFY(!s.programmerValues().isEmpty());   // the programmer is as the first command left it
        QCOMPARE(undo.redo(), QStringLiteral("Store Group 5 \"Pair\""));
        QVERIFY(s.group(5));
        run("Delete Group 5");
        QVERIFY(!s.group(5));
        QVERIFY(!undo.canRedo());   // a new step drops what could be redone
        undo.undo();
        QVERIFY(s.group(5));
        QCOMPARE(undo.undo(), QStringLiteral("Store Group 5 \"Pair\""));
        QCOMPARE(undo.undo(), QStringLiteral("Fixture 1 Thru 2 At 50"));
        QVERIFY(s.programmerValues().isEmpty());
        // Running playbacks aren't undone: undoing a label leaves the cue list running.
        run("Store Cue 1 Sequence 4");
        run("Assign Sequence 4 At Executor 201");
        run("Go Executor 201");
        run("Label Sequence 4 \"Pars\"");
        QCOMPARE(undo.undo(), QStringLiteral("Label Sequence 4 \"Pars\""));
        QVERIFY(s.executor(1, 201) && s.executor(1, 201)->pb().isOn());
        // Plots keep their own steps.
        run("Store Plot 2 \"Rig\"");
        QVERIFY(s.plot(2));
        QVERIFY(undo.canUndo(true));
        undo.undo(true);
        QVERIFY(!s.plot(2));
    }

    void programmerFade()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        Engine engine(&s);
        // Switching it on leaves what's programmed where it is.
        QVERIFY(cl.execute("Fixture 2 At 60").ok);
        engine.tick(0.05);
        QVERIFY(cl.execute("ProgFade 2").ok);
        QVERIFY(s.progFadeOn());
        QCOMPARE(s.progFadeTime(), 2.0);
        engine.tick(0.05);
        QVERIFY(std::abs(engine.merged()[2]["Dimmer"] - 0.6) < 1e-6);
        QVERIFY(cl.execute("Clear").ok);
        QVERIFY(cl.execute("Fixture 1 At 100").ok);
        engine.tick(1.0);
        QVERIFY(std::abs(engine.merged()[1]["Dimmer"] - 0.5) < 1e-6);   // halfway after 1 of 2 s
        engine.tick(1.0);
        QVERIFY(std::abs(engine.merged()[1]["Dimmer"] - 1.0) < 1e-6);
        QVERIFY(cl.execute("ClearAll").ok);
        engine.tick(1.0);
        QVERIFY(std::abs(engine.merged()[1]["Dimmer"] - 0.5) < 1e-6);   // Clear fades out too
        engine.tick(1.0);
        QVERIFY(engine.merged()[1]["Dimmer"] < 1e-6);
        QVERIFY(cl.execute("ProgFade Off").ok);
        QVERIFY(cl.execute("Fixture 1 At 100").ok);
        engine.tick(0.05);
        QVERIFY(std::abs(engine.merged()[1]["Dimmer"] - 1.0) < 1e-6);   // off: at once
        Show t;
        QVERIFY(cl.execute("ProgFade 3").ok);
        QVERIFY(t.fromJson(s.toJson()));
        QVERIFY(t.progFadeOn());
        QCOMPARE(t.progFadeTime(), 3.0);
    }

    void emitterGamut()
    {
        // What each fixture can make: its emitters, or the primaries for CMY, or white only.
        FixtureMode rgbw, whites, dimmer;
        auto ch = [](const char *a) { ChannelDef c; c.attribute = a; return c; };
        rgbw.channels = {ch("Dimmer"), ch("ColorAdd_R"), ch("ColorAdd_G"), ch("ColorAdd_B"), ch("ColorAdd_W")};
        whites.channels = {ch("Dimmer"), ch("ColorAdd_WW"), ch("ColorAdd_CW")};
        dimmer.channels = {ch("Dimmer")};
        QCOMPARE(emitterColors(&rgbw).size(), 4);
        QCOMPARE(emitterColors(&whites).size(), 2);
        QCOMPARE(emitterColors(&dimmer).size(), 1);
    }

    void oscMessages()
    {
        // Encoding and decoding, bundles, and TCP framing.
        OscMessage m{QStringLiteral("/exec/1/201/fader"), {QVariant(0.5), QVariant(7), QVariant(QStringLiteral("hi")), QVariant(true)}};
        const QVector<OscMessage> back = OscMessage::decode(m.encode());
        QCOMPARE(back.size(), 1);
        QCOMPARE(back[0].address, m.address);
        QCOMPARE(back[0].args[0].toDouble(), 0.5);
        QCOMPARE(back[0].args[1].toInt(), 7);
        QCOMPARE(back[0].args[2].toString(), QString("hi"));
        QVERIFY(back[0].args[3].toBool());
        QByteArray bundle("#bundle\0", 8);
        bundle += QByteArray(8, '\0');
        const QByteArray one = m.encode();
        char size[4];
        qToBigEndian<qint32>(qint32(one.size()), size);
        bundle += QByteArray(size, 4) + one;
        bundle += QByteArray(size, 4) + one;
        QCOMPARE(OscMessage::decode(bundle).size(), 2);
        QByteArray tcp = slipEncode(one) + slipEncode(one).left(5);
        QCOMPARE(takeTcpPackets(tcp).size(), 1);   // the second isn't all there yet
        QByteArray textForm;
        OscMessage parsed;
        QVERIFY(OscManager::parseText(QStringLiteral("/Page1/Fader201,i,50"), &parsed));
        QCOMPARE(parsed.args[0].toInt(), 50);
        QVERIFY(!OscManager::parseText(QStringLiteral("Page1"), &parsed));
    }

    void oscControl()
    {
        Show s;
        setupShow(s);
        s.select({1});
        s.setAttribute("Dimmer", 1.0);
        s.storeCue(1, 1);
        s.clearAll();
        s.assignSequence(1, 1, 201);
        OscManager osc(&s);
        OscLine line;
        line.name = "Tablet";
        line.inPort = 0;   // not listening in a test: messages go straight to handle()
        line.receive = true;
        line.receiveCommands = true;
        OscLine prefixed = line;
        prefixed.prefix = "lumora";
        osc.setLines({line, prefixed});
        auto msg = [](const QString &a, QVariantList args = {}) { return OscMessage{a, args}; };
        osc.handle(msg("/exec/1/201/fader", {0.25}), 0);
        QCOMPARE(s.executor(1, 201)->fader, 0.25);
        osc.handle(msg("/Page1/Fader201", {QVariant(80)}), 0);   // an integer: percent
        QCOMPARE(s.executor(1, 201)->fader, 0.8);
        osc.handle(msg("/exec/201/go"), 0);
        QVERIFY(s.executor(1, 201)->pb().isOn());
        // A prefix: only messages under it.
        osc.handle(msg("/exec/1/201/fader", {0.1}), 1);
        QCOMPARE(s.executor(1, 201)->fader, 0.8);
        osc.handle(msg("/lumora/exec/1/201/fader", {0.1}), 1);
        QCOMPARE(s.executor(1, 201)->fader, 0.1);
        // Command lines, groups and presets come out as commands for the program to run.
        QSignalSpy commands(&osc, &OscManager::commandRequested);
        osc.handle(msg("/cmd", {QStringLiteral("Fixture %1 At %2"), 3, 40}), 0);
        QCOMPARE(commands.last()[0].toString(), QString("Fixture 3 At 40"));
        osc.handle(msg("/preset/4/2/fire"), 0);
        QCOMPARE(commands.last()[0].toString(), QString("Preset 4.2"));
        // Masters and the grand master.
        osc.handle(msg("/master/3", {1.0}), 0);
        QCOMPARE(s.rateMaster(3)->value, 2.0);
        osc.handle(msg("/master/3/key"), 0);   // Reset on a rate master
        QCOMPARE(s.rateMaster(3)->value, 1.0);
        osc.handle(msg("/gm", {0.5}), 0);
        QCOMPARE(s.grandMaster(), 0.5);
        osc.handle(msg("/fixture/2", {50.0}), 0);
        QCOMPARE(s.programmerValues()[2]["Dimmer"].value, 0.5);
        // Commands need Receive commands.
        OscLine noCmd = line;
        noCmd.receiveCommands = false;
        osc.setLines({noCmd});
        const int before = int(commands.size());
        osc.handle(msg("/cmd", {QStringLiteral("Fixture 1")}), 0);
        QCOMPARE(int(commands.size()), before);
        // SendOSC asks the manager to send.
        CommandLine cl(&s);
        QSignalSpy sends(&s, &Show::oscSendRequested);
        QVERIFY(cl.execute("SendOSC 1 \"/Page1/Fader201,i,50\"").ok);
        QCOMPARE(sends.size(), 1);
        QVERIFY(!cl.execute("SendOSC 1").ok);
        // Kept with its settings.
        OscManager other(&s);
        other.fromJson(osc.toJson());
        QCOMPARE(other.lines().size(), 1);
        QCOMPARE(other.lines()[0].name, QString("Tablet"));
    }

    void rateMasterAppearance()
    {
        Show s;
        CommandLine cl(&s);
        QVERIFY(cl.execute("Appearance RateMaster 2 \"#ff8800\"").ok);
        QCOMPARE(s.rateMaster(2)->color, QString("#ff8800"));
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        QCOMPARE(t.rateMaster(2)->color, QString("#ff8800"));
        QVERIFY(cl.execute("Appearance RateMaster 2 Off").ok);
        QVERIFY(s.rateMaster(2)->color.isEmpty());
    }

    void artNetPacket()
    {
        QByteArray dmxData(512, 0);
        dmxData[0] = char(255);
        dmxData[511] = char(7);
        QByteArray p = buildArtDmx(0x123, 9, dmxData);
        QCOMPARE(p.size(), 530);
        QCOMPARE(p.left(8), QByteArray("Art-Net\0", 8));
        QCOMPARE(quint8(p[8]), quint8(0x00));
        QCOMPARE(quint8(p[9]), quint8(0x50));
        QCOMPARE(quint8(p[11]), quint8(14));
        QCOMPARE(quint8(p[12]), quint8(9));
        QCOMPARE(quint8(p[14]), quint8(0x23));   // SubUni
        QCOMPARE(quint8(p[15]), quint8(0x01));   // Net
        QCOMPARE(quint8(p[16]), quint8(0x02));
        QCOMPARE(quint8(p[17]), quint8(0x00));
        QCOMPARE(quint8(p[18]), quint8(255));
        QCOMPARE(quint8(p[529]), quint8(7));
    }

    void sacnPacket()
    {
        QByteArray dmxData(512, 0);
        dmxData[0] = char(200);
        QUuid cid = QUuid::createUuid();
        QByteArray p = buildSacn(258, 3, dmxData, cid, "Lumora", 100);
        QCOMPARE(p.size(), 638);
        QCOMPARE(p.mid(4, 9), QByteArray("ASC-E1.17"));
        QCOMPARE(quint8(p[16]), quint8(0x72));   // flags+length 0x726e
        QCOMPARE(quint8(p[17]), quint8(0x6e));
        QCOMPARE(p.mid(22, 16), cid.toRfc4122());
        QCOMPARE(quint8(p[38]), quint8(0x72));   // 0x7258
        QCOMPARE(quint8(p[39]), quint8(0x58));
        QCOMPARE(p.mid(44, 6), QByteArray("Lumora"));
        QCOMPARE(quint8(p[108]), quint8(100));
        QCOMPARE(quint8(p[111]), quint8(3));
        QCOMPARE(quint8(p[113]), quint8(0x01));  // universe 258
        QCOMPARE(quint8(p[114]), quint8(0x02));
        QCOMPARE(quint8(p[115]), quint8(0x72));  // 0x720b
        QCOMPARE(quint8(p[116]), quint8(0x0b));
        QCOMPARE(quint8(p[118]), quint8(0xa1));
        QCOMPARE(quint8(p[123]), quint8(0x02));  // 513 values
        QCOMPARE(quint8(p[124]), quint8(0x01));
        QCOMPARE(quint8(p[126]), quint8(200));
        QCOMPARE(sacnMulticastAddress(258).toString(), QString("239.255.1.2"));
    }

    void enttecPacket()
    {
        QByteArray dmxData(512, 0);
        dmxData[0] = char(42);
        QByteArray p = buildEnttecProDmx(dmxData);
        QCOMPARE(p.size(), 518);
        QCOMPARE(quint8(p[0]), quint8(0x7e));
        QCOMPARE(quint8(p[1]), quint8(6));
        QCOMPARE(quint8(p[2]), quint8(0x01));    // 513 LSB
        QCOMPARE(quint8(p[3]), quint8(0x02));
        QCOMPARE(quint8(p[4]), quint8(0));       // start code
        QCOMPARE(quint8(p[5]), quint8(42));
        QCOMPARE(quint8(p[517]), quint8(0xe7));
    }

    void networkOutputsOverLoopback()
    {
        QUdpSocket artIn, sacnIn;
        QVERIFY(artIn.bind(QHostAddress::LocalHost, 6454));
        QVERIFY(sacnIn.bind(QHostAddress::LocalHost, 5568));

        Show s;
        setupShow(s);
        OutputManager outputs;
        OutputConfig art;
        art.kind = OutputKind::ArtNet;
        art.host = "127.0.0.1";
        art.protocolUniverse = 3;
        OutputConfig sacn;
        sacn.kind = OutputKind::Sacn;
        sacn.host = "127.0.0.1";
        sacn.protocolUniverse = 7;
        outputs.setConfigs({art, sacn});
        Engine engine(&s);
        engine.setOutputs(&outputs);

        s.select({1});
        s.setAttribute("Dimmer", 1.0);
        engine.tick(0.01);

        QTRY_VERIFY_WITH_TIMEOUT(artIn.hasPendingDatagrams(), 2000);
        QNetworkDatagram a = artIn.receiveDatagram();
        QCOMPARE(a.data().size(), 530);
        QCOMPARE(quint8(a.data()[14]), quint8(3));     // port-address
        QCOMPARE(quint8(a.data()[18]), quint8(255));   // fixture 1 dimmer at 1.001

        QTRY_VERIFY_WITH_TIMEOUT(sacnIn.hasPendingDatagrams(), 2000);
        QNetworkDatagram d = sacnIn.receiveDatagram();
        QCOMPARE(d.data().size(), 638);
        QCOMPARE(quint8(d.data()[114]), quint8(7));    // universe
        QCOMPARE(quint8(d.data()[126]), quint8(255));
        QCOMPARE(outputs.status(0), QString("Sending"));
    }

    void repatchTypeAndMode()
    {
        Show s;
        setupShow(s);
        s.select({1, 2});
        s.setAttribute("ColorAdd_W", 1.0);
        // Pars 1-4 from 6-channel to 4-channel mode, keeping addresses: fits (footprint shrinks).
        QVector<Fixture> list;
        for (int fid = 1; fid <= 4; ++fid) {
            Fixture f = *s.fixture(fid);
            f.mode = "4-channel";
            list << f;
        }
        QString err;
        QVERIFY2(s.updateFixtures(list, &err), qPrintable(err));
        QCOMPARE(s.fixtureMode(1)->footprint, 4);
        QVERIFY(s.fixtureMode(1)->virtualDimmer);   // 4-channel mode has no dimmer
        QCOMPARE(s.programmerValues()[1]["ColorAdd_W"].value, 1.0);   // values for shared attributes survive

        // Par 1 becomes a 16-channel spot at its old address: would overlap par 2 → rejected, nothing changes.
        Fixture spot = *s.fixture(1);
        spot.typeId = "generic/moving-spot";
        spot.mode = "16-channel";
        QVERIFY(!s.updateFixtures({spot}, &err));
        QVERIFY(err.contains("Overlaps"));
        QCOMPARE(s.fixture(1)->typeId, QString("generic/rgbw-par"));

        // Shifting several fixtures together is fine even if they'd collide one at a time.
        Fixture a = *s.fixture(1), b = *s.fixture(2);
        a.address = 7;
        b.address = 1;
        QVERIFY2(s.updateFixtures({a, b}, &err), qPrintable(err));
        QCOMPARE(s.fixture(2)->address, 1);

        // Unknown mode falls back to the type's first mode
        Fixture c = *s.fixture(3);
        c.typeId = "generic/dimmer";
        c.mode = "nonsense";
        QVERIFY2(s.updateFixtures({c}, &err), qPrintable(err));
        QCOMPARE(s.fixture(3)->mode, QString("8-bit"));
    }

    void editCommands()
    {
        Show s;
        setupShow(s);
        Engine engine(&s);
        CommandLine cl(&s);
        QVERIFY(cl.execute("Fixture 1 Thru 4 At 50").ok);
        QVERIFY(cl.execute("Store Group 1").ok);
        QVERIFY(cl.execute("Store Cue 1").ok);
        QVERIFY(cl.execute("Assign Sequence 1 At Executor 201").ok);
        QVERIFY(cl.execute("Go Executor 201").ok);

        // Update (no cue) merges into the live cue
        QVERIFY(cl.execute("Fixture 5 At 80").ok);
        CommandResult r = cl.execute("Update");
        QVERIFY2(r.ok, qPrintable(r.message));
        QVERIFY(s.sequence(1)->cues[0].values.contains(5));

        // Remove from the programmer, then from a cue
        QVERIFY(cl.execute("Remove").ok);
        QVERIFY(!s.programmerValues().contains(5));
        r = cl.execute("Remove Fixture 3 + 4 Cue 1");
        QVERIFY2(r.ok, qPrintable(r.message));
        QCOMPARE(s.sequence(1)->cues[0].values.keys(), (QList<int>{1, 2, 5}));
        QVERIFY(!cl.execute("Remove Fixture 3 Cue 1").ok);   // already gone

        // Copy / Move
        QVERIFY(cl.execute("Copy Group 1 At 5").ok);
        QCOMPARE(s.group(5)->fixtures, s.group(1)->fixtures);
        QVERIFY(!cl.execute("Copy Group 1 At 5").ok);          // occupied
        QVERIFY(cl.execute("Copy Group 1 At 5 /overwrite").ok);
        QVERIFY(cl.execute("Move Group 5 At 6").ok);
        QVERIFY(!s.group(5));
        QVERIFY(s.group(6));
        s.select({1});
        s.setAttribute("ColorAdd_R", 1.0);
        QVERIFY(cl.execute("Store Preset 4.1").ok);
        QVERIFY(cl.execute("Move Preset 4.1 At 4.9").ok);
        QVERIFY(!s.preset(4, 1));
        QVERIFY(s.preset(4, 9));
        QVERIFY(cl.execute("Copy Cue 1 At 5").ok);
        QCOMPARE(s.sequence(1)->cues.size(), 2);
        QVERIFY(cl.execute("Move Cue 5 At 1.5").ok);
        QCOMPARE(s.sequence(1)->cues[1].number, 1.5);
        QVERIFY(cl.execute("Move Sequence 1 At 7").ok);
        QVERIFY(!s.sequence(1));
        QCOMPARE(s.executor(1, 201)->sequenceId, 7);           // executor follows
        QVERIFY(cl.execute("Copy Sequence 7 At 8").ok);
        QCOMPARE(s.sequence(8)->cues.size(), 2);
    }

    void twoThousandUniverses()
    {
        Show s;
        setupShow(s);
        QString err;
        QVERIFY2(s.addFixture({100, QString(), "generic/dimmer", "8-bit", 2048, 512}, &err), qPrintable(err));
        QVERIFY(!s.addFixture({101, QString(), "generic/dimmer", "8-bit", 2049, 1}, &err));
        QVERIFY(err.contains("2048"));

        QUdpSocket artIn;
        QVERIFY(artIn.bind(QHostAddress::LocalHost, 6454));
        OutputManager outputs;
        OutputConfig art;
        art.kind = OutputKind::ArtNet;
        art.host = "127.0.0.1";
        art.firstUniverse = 1;
        art.count = 2048;
        outputs.setConfigs({art});
        Engine engine(&s);
        engine.setOutputs(&outputs);
        s.select({100});
        s.setAttribute("Dimmer", 1.0);
        engine.tick(0.01);

        // Universes 1, 2 and 2048 have fixtures; only those three packets are sent.
        QSet<int> ports;
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 1000 && ports.size() < 3) {
            if (!artIn.waitForReadyRead(200))
                continue;
            while (artIn.hasPendingDatagrams()) {
                const QByteArray d = artIn.receiveDatagram().data();
                const int port = quint8(d[14]) | (quint8(d[15]) << 8);
                ports.insert(port);
                if (port == 2047)
                    QCOMPARE(quint8(d[18 + 511]), quint8(255));
            }
        }
        QCOMPARE(ports, (QSet<int>{0, 1, 2047}));
    }

    void effectShapesAndSpread()
    {
        const Shape *sine = builtinShape(ShapeSine);
        QCOMPARE(sine->value(0.0), 0.0);
        QVERIFY(std::abs(sine->value(0.5) - 1.0) < 1e-9);
        QVERIFY(std::abs(sine->value(0.25) - 0.5) < 1e-9);
        QCOMPARE(builtinShape(ShapeSquare)->value(0.2), 1.0);
        QCOMPARE(builtinShape(ShapeSquare)->value(0.7), 0.0);
        // Random is stable for the same cycle and seed, and differs between seeds
        const Shape *rnd = builtinShape(ShapeRandom);
        QCOMPARE(rnd->value(0.3, 5, 1), rnd->value(0.9, 5, 1));
        QVERIFY(rnd->value(0.3, 5, 1) != rnd->value(0.3, 5, 2));

        // Custom shape: a triangle drawn with three points
        Shape custom;
        custom.id = 1;
        custom.smooth = false;
        custom.points = {{0.0, 0.0}, {0.5, 1.0}, {1.0, 0.0}};
        QVERIFY(std::abs(custom.value(0.25) - 0.5) < 1e-9);
        QCOMPARE(Shape::fromJson(custom.toJson()).points.size(), 3);

        Effect e;
        e.spread = 360;
        QCOMPARE(spreadPosition(e, 1, 4), 0.25);
        QCOMPARE(spreadPosition(e, 3, 4), 0.75);
        e.direction = SpreadDirection::Backward;
        QCOMPARE(spreadPosition(e, 0, 4), 0.75);
        e.direction = SpreadDirection::Forward;
        e.wings = 2;   // 4 fixtures, 2 wings: mirror from the middle
        QCOMPARE(spreadPosition(e, 0, 4), 0.0);
        QCOMPARE(spreadPosition(e, 1, 4), 0.5);
        QCOMPARE(spreadPosition(e, 2, 4), 0.5);
        QCOMPARE(spreadPosition(e, 3, 4), 0.0);
        e.wings = 1;
        e.blocks = 2;  // pairs share a phase
        QCOMPARE(spreadPosition(e, 0, 4), spreadPosition(e, 1, 4));
        QCOMPARE(spreadPosition(e, 2, 4), 0.5);
        e.blocks = 1;
        e.groups = 2;  // phases repeat every 2 fixtures
        QCOMPARE(spreadPosition(e, 2, 4), spreadPosition(e, 0, 4));
    }

    void effectLayers()
    {
        Show s;
        setupShow(s);
        auto base = [&s](int fid, const QString &a) { return s.defaultValue(fid, a); };

        // Dimmer wave, sine, no spread
        Effect dim;
        dim.id = 1;
        EffectLayer d;
        d.kind = LayerKind::Dimmer;
        d.points = {EffectPoint::level(0.0), EffectPoint::level(1.0)};
        dim.layers << d;
        EffectInstance inst;
        inst.effectId = 1;
        inst.fixtures = {1, 2};
        RawTable out;
        evaluateEffect(s, dim, inst, 0.25, base, out);
        QVERIFY(std::abs(out[1]["Dimmer"] - 0.5) < 1e-9);
        inst.size = 0.5;   // size scales amplitude
        out.clear();
        evaluateEffect(s, dim, inst, 0.5, base, out);
        QVERIFY(std::abs(out[1]["Dimmer"] - 0.5) < 1e-9);

        // Color steps between two presets: red and blue, crossfade 0 (snap)
        s.select({1, 2, 3, 4});
        s.setAttribute("ColorAdd_R", 1.0);
        s.setAttribute("ColorAdd_B", 0.0);
        QVERIFY(s.storePreset(4, 1, Show::StoreMode::Merge, "Red", PresetScope::Universal));
        s.setAttribute("ColorAdd_R", 0.0);
        s.setAttribute("ColorAdd_B", 1.0);
        QVERIFY(s.storePreset(4, 2, Show::StoreMode::Merge, "Blue", PresetScope::Universal));
        s.clearAll();
        Effect col;
        col.id = 2;
        EffectLayer c;
        c.kind = LayerKind::Color;
        c.mode = LayerMode::Steps;
        c.crossfade = 0.0;
        c.points = {EffectPoint::preset(4, 1), EffectPoint::preset(4, 2)};
        col.layers << c;
        EffectInstance ci;
        ci.effectId = 2;
        ci.fixtures = {1, 7};   // RGBW par and an RGB par (other type): Universal presets apply to both
        out.clear();
        evaluateEffect(s, col, ci, 0.1, base, out);
        QCOMPARE(out[1]["ColorAdd_R"], 1.0);
        QCOMPARE(out[7]["ColorAdd_R"], 1.0);
        out.clear();
        evaluateEffect(s, col, ci, 0.6, base, out);
        QCOMPARE(out[1]["ColorAdd_B"], 1.0);
        QCOMPARE(out[1]["ColorAdd_R"], 0.0);

        // Color points as RGB: white comes from the W emitter. Switch white off and W is left alone.
        c.points = {EffectPoint::color(1, 1, 1), EffectPoint::color(1, 0, 0)};
        col.layers = {c};
        out.clear();
        evaluateEffect(s, col, ci, 0.1, base, out);
        QCOMPARE(out[1]["ColorAdd_W"], 1.0);
        QCOMPARE(out[1]["ColorAdd_R"], 0.0);
        col.layers[0].excludedFamilies = {"white"};
        out.clear();
        evaluateEffect(s, col, ci, 0.1, base, out);
        QVERIFY(!out[1].contains("ColorAdd_W"));
        QVERIFY(out[1].contains("ColorAdd_R"));

        // Color wheel fixtures pick the nearest slot: red point -> "Red" slot on the spot's wheel
        c.points = {EffectPoint::color(1, 0, 0), EffectPoint::color(0, 0, 1)};
        col.layers = {c};
        ci.fixtures = {5};
        out.clear();
        evaluateEffect(s, col, ci, 0.1, base, out);
        QCOMPARE(s.fixtureMode(5)->channel("Color1")->setNameFor(out[5]["Color1"]), QString("Red"));

        // Form: circle around the current position
        Effect circ;
        circ.id = 3;
        EffectLayer f;
        f.kind = LayerKind::Position;
        f.mode = LayerMode::Form;
        f.form = FormType::Circle;
        f.sizePan = 0.1;
        f.sizeTilt = 0.1;
        circ.layers << f;
        EffectInstance fi;
        fi.effectId = 3;
        fi.fixtures = {5};
        out.clear();
        evaluateEffect(s, circ, fi, 0.0, base, out);   // cos(0)=1 → pan +10% of 540° around 0°
        QVERIFY(std::abs(out[5]["Pan"] - 54.0) < 1e-9);
        QVERIFY(std::abs(out[5]["Tilt"]) < 1e-9);

        // Every template evaluates without trouble on the whole rig
        for (Effect t : effectTemplates()) {
            t.id = 99;
            EffectInstance ti;
            ti.effectId = 99;
            ti.fixtures = {1, 2, 3, 4, 5, 6, 7};
            RawTable o;
            evaluateEffect(s, t, ti, 0.37, base, o);
            QVERIFY2(!o.isEmpty(), qPrintable(t.name));
        }
    }

    void effectsInProgrammerPresetsAndCues()
    {
        Show s;
        setupShow(s);
        Engine engine(&s);
        Effect chase = effectTemplates()[2];   // Hard chase: square, 360 spread
        chase.id = 1;
        s.setEffect(chase);
        s.select({1, 2, 3, 4});
        QVERIFY(s.applyEffect(1));
        QCOMPARE(s.programmerEffects().size(), 1);
        engine.tick(0.01);
        // Square at width 0.5 with a 360° spread over 4: fixture 1 on, the others off at the start
        QCOMPARE(dmx(engine, 1, 1), 255);
        QCOMPARE(dmx(engine, 1, 7), 0);
        engine.tick(chase.cycle * 0.25);   // a quarter cycle later the chase moved on
        QCOMPARE(dmx(engine, 1, 1), 0);

        // Store as a Dimmer preset: the effect goes in without fixtures, and runs on any selection
        QVERIFY(s.storePreset(1, 5));
        QCOMPARE(s.preset(1, 5)->effects.size(), 1);
        QVERIFY(s.preset(1, 5)->effects[0].fixtures.isEmpty());
        s.clearAll();
        QVERIFY(s.programmerEffects().isEmpty());
        s.select({5, 6});
        QVERIFY(s.applyPreset(1, 5));
        QCOMPARE(s.programmerEffects()[0].fixtures, (QVector<int>{5, 6}));

        // A newer dimmer effect takes fixtures from the older one
        Effect wave = effectTemplates()[0];
        wave.id = 2;
        s.setEffect(wave);
        s.select({6});
        QVERIFY(s.applyEffect(2));
        QCOMPARE(s.programmerEffects()[0].fixtures, (QVector<int>{5}));

        // Store in a cue and play it back: the executor master scales the effect
        s.clearAll();
        s.select({1, 2, 3, 4});
        s.applyEffect(1);
        QVERIFY(s.storeCue(1, 1));
        QCOMPARE(s.sequence(1)->cues[0].effects.size(), 1);
        s.clearAll();
        {
            Sequence q = *s.sequence(1);
            q.cues[0].fade = 0;   // effects fade in with the cue: none here
            s.setSequence(q);
        }
        s.assignSequence(1, 1, 201);
        Executor &e = *s.executor(1, 201);
        s.go(e);
        s.setFader(e, 0.5);
        engine.tick(0.01);
        engine.tick(0.01);
        QCOMPARE(dmx(engine, 1, 1), 128);

        // Blueprint lines can carry an effect on a group
        s.select({1, 2, 3, 4});
        s.storeGroup(1);
        s.clearAll();
        CommandLine cl(&s);
        QVERIFY(cl.execute("Blueprint Cue 2 Group 1 Effect 2").ok);
        QCOMPARE(s.cueEffects(s.sequence(1)->cues[1]).size(), 1);
        QCOMPARE(s.cueEffects(s.sequence(1)->cues[1])[0].fixtures.size(), 4);

        // Command line
        QVERIFY(cl.execute("Fixture 1 Thru 4").ok);
        QVERIFY(cl.execute("Effect 2").ok);
        QVERIFY(cl.execute("Effect 2 Rate 200 Size 50").ok);
        QCOMPARE(s.programmerEffects().last().rate, 2.0);
        QCOMPARE(s.programmerEffects().last().size, 0.5);
        QVERIFY(cl.execute("Effect 2 Off").ok);
        QVERIFY(s.programmerEffects().isEmpty());
        QVERIFY(!cl.execute("Effect 42").ok);

        // Effects, shapes and stored instances survive the show file
        Shape sh;
        sh.id = 1;
        sh.name = "Kick";
        sh.points = {{0, 1}, {0.2, 0}};
        s.setShape(sh);
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        QCOMPARE(t.effects().size(), 2);
        QCOMPARE(t.effect(1)->layers.size(), 1);
        QCOMPARE(t.shape(1)->name, QString("Kick"));
        QCOMPARE(t.preset(1, 5)->effects.size(), 1);
        QCOMPARE(t.sequence(1)->cues[0].effects[0].fixtures.size(), 4);
        QCOMPARE(t.sequence(1)->cues[1].blueprints[0].effects, (QVector<int>{2}));
    }

    void showRoundTrip()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        cl.execute("Fixture 1 Thru 4 At 60");
        cl.execute("Store Group 1 \"Pars\"");
        cl.execute("Attribute ColorAdd_B At Full");
        cl.execute("Store Preset 4.3 \"Blue\"");
        cl.execute("Store Cue 1");
        cl.execute("Blueprint Cue 2 Group 1 Preset 4.3 At 30");
        cl.execute("Assign Sequence 1 At Executor 205");
        s.setExtra("midi", QJsonObject{{"bindings", QJsonArray()}});
        const QByteArray json = QJsonDocument(s.toJson()).toJson();

        Show t;   // fresh library: fixture types come from the show file
        QString err;
        QVERIFY2(t.fromJson(QJsonDocument::fromJson(json).object(), &err), qPrintable(err));
        QCOMPARE(t.fixtures().size(), 7);
        QVERIFY(t.fixtureMode(5));
        QCOMPARE(t.group(1)->fixtures.size(), 4);
        QCOMPARE(t.preset(4, 3)->name, QString("Blue"));
        QCOMPARE(t.sequence(1)->cues.size(), 2);
        QCOMPARE(t.sequence(1)->cues[0].values[2]["ColorAdd_B"].presetId, 3);
        // "At 30" in a blueprint is a universal Dimmer preset now.
        const BlueprintLine &rl = t.sequence(1)->cues[1].blueprints[0];
        QCOMPARE(rl.dimmer, -1.0);
        QCOMPARE(rl.presets.last().first, 1);
        QCOMPARE(t.preset(1, rl.presets.last().second)->universal.value("Dimmer"), 0.3);
        QCOMPARE(t.preset(1, rl.presets.last().second)->name, QString("30%"));
        QCOMPARE(t.executor(1, 205)->sequenceId, 1);
        QVERIFY(t.extra("midi").contains("bindings"));
        RawTable a = s.trackedState(*s.sequence(1), 1);
        RawTable b = t.trackedState(*t.sequence(1), 1);
        QCOMPARE(a, b);
    }
    void rateMasters()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        QCOMPARE(s.rateMasters().size(), RateMaster::kCount);
        // A new show: 1 is BPM, 2 a ratio, the rest rates; the key learns on BPM, resets on the rest.
        QCOMPARE(s.rateMaster(1)->mode, RateMode::BPM);
        QCOMPARE(s.rateMaster(2)->mode, RateMode::Ratio);
        QCOMPARE(s.rateMaster(3)->mode, RateMode::Rate);
        QVERIFY(s.rateMaster(1)->keyLearns());
        QVERIFY(!s.rateMaster(3)->keyLearns());
        QVERIFY(cl.execute("Assign RateMaster 3 At Executor 205").ok);
        Executor *e = s.executor(1, 205);
        QVERIFY(e);
        QCOMPARE(e->rateMaster, 3);
        QVERIFY(!e->isEmpty());
        s.setFader(*e, 1.0);   // top of a Rate fader is 200%
        QCOMPARE(s.rateMaster(3)->value, 2.0);
        QCOMPARE(s.rateMaster(3)->cyclesPerSecond(2.0, 1.0), 1.0);
        s.pressRateMasterKey(3);   // Reset
        QCOMPARE(s.rateMaster(3)->value, 1.0);
        {
            RateMaster k = *s.rateMaster(3);
            k.key = RateMaster::Key::Learn;
            s.setRateMaster(k);
            Show kept;
            QVERIFY(kept.fromJson(s.toJson()));
            QVERIFY(kept.rateMaster(3)->keyLearns());
            QCOMPARE(kept.rateMaster(1)->mode, RateMode::BPM);   // defaults aren't written, and come back
        }
        // BPM mode: one cycle per beat.
        RateMaster m = *s.rateMaster(2);
        m.mode = RateMode::BPM;
        m.value = 120;
        s.setRateMaster(m);
        QCOMPARE(s.rateMaster(2)->cyclesPerSecond(2.0, 1.0), 2.0);
        QCOMPARE(s.rateMaster(2)->valueText(), QString("120 BPM"));
        QVERIFY(cl.execute("RateMaster 2 At 90").ok);
        QCOMPARE(s.rateMaster(2)->value, 90.0);
        s.toggleRateMasterFreeze(2);
        QCOMPARE(s.rateMaster(2)->cyclesPerSecond(2.0, 1.0), 0.0);
        QVERIFY(cl.execute("Label RateMaster 2 \"Chase\"").ok);
        QCOMPARE(s.rateMaster(2)->name, QString("Chase"));
        // Effects follow a master.
        Effect fx;
        fx.id = 1;
        fx.name = "Fx";
        s.setEffect(fx);
        QVERIFY(cl.execute("Assign Effect 1 At RateMaster 2").ok);
        QCOMPARE(s.effect(1)->rateMaster, 2);
        // Round trip.
        const QByteArray json = QJsonDocument(s.toJson()).toJson();
        Show t;
        QString err;
        QVERIFY2(t.fromJson(QJsonDocument::fromJson(json).object(), &err), qPrintable(err));
        QCOMPARE(t.rateMaster(2)->name, QString("Chase"));
        QCOMPARE(t.rateMaster(2)->mode, RateMode::BPM);
        QVERIFY(t.rateMaster(2)->frozen);
        QCOMPARE(t.effect(1)->rateMaster, 2);
        // Shown as a button in the Rate masters window: kept with the show.
        QVERIFY(cl.execute("RateMaster 4 Button").ok);
        QVERIFY(s.rateMaster(4)->button);
        Show u;
        QVERIFY(u.fromJson(s.toJson()));
        QVERIFY(u.rateMaster(4)->button);
        QVERIFY(cl.execute("RateMaster 4 Fader").ok);
        QVERIFY(!s.rateMaster(4)->button);
    }

    void newParameters()
    {
        QVERIFY(parameterInfo("Prism2"));
        QVERIFY(parameterInfo("PanRotate"));
        QCOMPARE(parameterInfo("PanRotate")->label, QString("Infinite pan"));
        QVERIFY(parameterInfo("ColorAdd_Mint"));
        QCOMPARE(featureOf("Blade1A"), Feature::Beam);
        QCOMPARE(featureOf("HSB_Hue"), Feature::Color);
        QCOMPARE(featureOf("PositionMacro"), Feature::Position);
        QCOMPARE(featureOf("Prism2Pos"), Feature::Beam);
    }

    void individualTiming()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        Engine engine(&s);
        // "At 100 Fade 0 Thru 3" spreads the fade across the selection, in its order.
        QVERIFY(cl.execute("Fixture 1 Thru 4 At 100 Fade 0 Thru 3").ok);
        QCOMPARE(s.programmerValues()[1]["Dimmer"].fade, 0.0);
        QCOMPARE(s.programmerValues()[3]["Dimmer"].fade, 2.0);
        QCOMPARE(s.programmerValues()[4]["Dimmer"].fade, 3.0);
        // Selection order counts: 4 Thru 1 runs the other way.
        s.clearSelection();
        QVERIFY(cl.execute("Fixture 4 Thru 1 Delay 0 Thru 1.5").ok);
        QCOMPARE(s.programmerValues()[4]["Dimmer"].delay, 0.0);
        QCOMPARE(s.programmerValues()[1]["Dimmer"].delay, 1.5);
        // A new value keeps the times.
        s.clearSelection();
        QVERIFY(cl.execute("Fixture 4 At 80").ok);
        QCOMPARE(s.programmerValues()[4]["Dimmer"].fade, 3.0);
        s.clearSelection();
        QVERIFY(cl.execute("Fixture 1 Thru 4 Delay Off").ok);
        QCOMPARE(s.programmerValues()[1]["Dimmer"].delay, -1.0);
        s.clearSelection();
        QVERIFY(cl.execute("Fixture 4 At 100").ok);
        QVERIFY(cl.execute("Store Cue 1").ok);
        QCOMPARE(s.sequence(1)->cues[0].values[3]["Dimmer"].fade, 2.0);
        s.clearAll();
        s.assignSequence(1, 1, 201);
        Executor &x = *s.executor(1, 201);
        s.go(x);
        QCOMPARE(x.pb().duration, 3.0);
        engine.tick(0.01);
        engine.tick(1.49);
        QCOMPARE(s.lastOutput()[1]["Dimmer"], 1.0);              // fade 0
        QVERIFY(std::fabs(s.lastOutput()[2]["Dimmer"] - 1.0) < 1e-6);   // fade 1: done
        QVERIFY(std::fabs(s.lastOutput()[4]["Dimmer"] - 0.5) < 0.01);   // fade 3: halfway
        QVERIFY(x.pb().fading);
        engine.tick(1.6);
        QVERIFY(!x.pb().fading);
        QCOMPARE(s.lastOutput()[4]["Dimmer"], 1.0);
        // "Time" and "At Fade": the same thing.
        s.clearSelection();
        QVERIFY(cl.execute("Fixture 1 Thru 2 At 50 Time 4").ok);
        QCOMPARE(s.programmerValues()[2]["Dimmer"].fade, 4.0);
        s.clearSelection();
        QVERIFY(cl.execute("Fixture 1 Thru 3 At Fade 0 Thru 2 Thru 0").ok);
        QCOMPARE(s.programmerValues()[2]["Dimmer"].fade, 2.0);
        QCOMPARE(s.programmerValues()[1]["Dimmer"].fade, 0.0);
        // Individual times on a stored cue, across the cue's own fixtures.
        s.clearSelection();
        QVERIFY(cl.execute("Cue 1 Fade 4 Thru 1").ok);
        QCOMPARE(s.sequence(1)->cues[0].values[1]["Dimmer"].fade, 4.0);
        QCOMPARE(s.sequence(1)->cues[0].values[4]["Dimmer"].fade, 1.0);
        QCOMPARE(s.sequence(1)->cues[0].fade, 2.0);   // the cue's own fade is untouched
        s.select({3, 4});
        QVERIFY(cl.execute("Cue 1 Delay 0 Thru 2").ok);
        QCOMPARE(s.sequence(1)->cues[0].values[4]["Dimmer"].delay, 2.0);
        QCOMPARE(s.sequence(1)->cues[0].values[1]["Dimmer"].delay, -1.0);
        QVERIFY(cl.execute("Cue 1 Delay Off").ok);
        QCOMPARE(s.sequence(1)->cues[0].values[4]["Dimmer"].delay, -1.0);
        QVERIFY(cl.execute("Cue 1 Fade 3").ok);
        QCOMPARE(s.sequence(1)->cues[0].fade, 3.0);
        s.clearSelection();
        QVERIFY(cl.execute("Cue 1 Fade 0 Thru 3").ok);
        // Saved and loaded with the cue.
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        QCOMPARE(t.sequence(1)->cues[0].values[4]["Dimmer"].fade, 3.0);
        QCOMPARE(t.sequence(1)->cues[0].values[4]["Dimmer"].delay, -1.0);
    }

    void spreadTimingInBlueprints()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        Engine engine(&s);
        QVERIFY(cl.execute("Fixture 1 Thru 4").ok);
        QVERIFY(cl.execute("Store Group 1").ok);
        s.clearAll();
        s.clearSelection();
        // The line's own times, across its fixtures in the group's order.
        QVERIFY(cl.execute("Blueprint Cue 1 Group 1 At 100 Fade 0 Thru 3 Delay 1").ok);
        QCOMPARE(s.sequence(1)->cues[0].blueprints[0].fadeTimes, QVector<double>({0.0, 3.0}));
        Sequence q = *s.sequence(1);
        q.cues[0].fade = 0;
        s.setSequence(q);
        const TimingTable t = s.cueTiming(s.sequence(1)->cues[0]);
        QCOMPARE(t[1]["Dimmer"].fade, 0.0);
        QCOMPARE(t[4]["Dimmer"].fade, 3.0);
        QCOMPARE(t[4]["Dimmer"].delay, 1.0);
        QVERIFY(t[2].contains("*"));   // the line's effects take the times too
        s.assignSequence(1, 1, 201);
        Executor &x = *s.executor(1, 201);
        s.go(x);
        QCOMPARE(x.pb().duration, 4.0);
        engine.tick(0.5);
        QCOMPARE(s.lastOutput()[1]["Dimmer"], 0.0);   // still in its delay
        engine.tick(1.0);
        QCOMPARE(s.lastOutput()[1]["Dimmer"], 1.0);   // fade 0 after the delay
        QVERIFY(s.lastOutput()[4]["Dimmer"] < 0.5);
        // A spread on the line lays the times out: Wings 2 runs from both ends.
        QVERIFY(cl.execute("Spread Wings 2").ok);
        QVERIFY(cl.execute("Store Spread 2").ok);
        QVERIFY(cl.execute("Spread Off").ok);
        q = *s.sequence(1);
        q.cues[0].blueprints[0].spreadId = 2;
        s.setSequence(q);
        const TimingTable w = s.cueTiming(s.sequence(1)->cues[0]);
        QCOMPARE(w[1]["Dimmer"].fade, w[4]["Dimmer"].fade);
        // Saved with the line.
        Show u;
        QVERIFY(u.fromJson(s.toJson()));
        QCOMPARE(u.sequence(1)->cues[0].blueprints[0].delayTimes, QVector<double>({1.0}));
        // Built into values, the times stay with them.
        q.cues[0].blueprints[0].spreadId = 0;
        s.setSequence(q);
        QVERIFY(cl.execute("Build Cue 1").ok);
        QCOMPARE(s.sequence(1)->cues[0].values[3]["Dimmer"].fade, 2.0);
        // Shows from a.7.1.10 kept the times in the spread: they move to the line.
        QJsonObject old = s.toJson();
        QJsonArray spreads = old["spreads"].toArray();
        spreads.append(QJsonObject{{"id", 9}, {"name", "Old"}, {"fadeFrom", 0}, {"fadeTo", 5}});
        old["spreads"] = spreads;
        QJsonArray seqs = old["sequences"].toArray();
        QJsonObject seq0 = seqs[0].toObject();
        QJsonArray cues = seq0["cues"].toArray();
        QJsonObject cue0 = cues[0].toObject();
        cue0["blueprints"] = QJsonArray{BlueprintLine{"L", 1, {}, -1.0, 1, 0, 9, {}, {}, {}}.toJson()};
        cues[0] = cue0;
        seq0["cues"] = cues;
        seqs[0] = seq0;
        old["sequences"] = seqs;
        Show v;
        QVERIFY(v.fromJson(old));
        QCOMPARE(v.sequence(1)->cues[0].blueprints[0].fadeTimes, QVector<double>({0.0, 5.0}));
    }

    void tempUsesOffTime()
    {
        Show s;
        setupShow(s);
        Engine engine(&s);
        s.select({1});
        s.setAttribute("Dimmer", 1.0);
        s.storeCue(1, 1);
        s.clearAll();
        Sequence q = *s.sequence(1);
        q.cues[0].fade = 1.0;
        q.offFade = 2.0;
        s.setSequence(q);
        s.assignSequence(1, 1, 201);
        Executor &x = *s.executor(1, 201);
        x.buttons[0] = ButtonFn::Temp;
        s.pressButton(x, 0, true);
        QVERIFY(x.pb().isOn());
        QVERIFY(x.pb().fading);          // in with the cue's fade
        engine.tick(1.2);
        QCOMPARE(s.lastOutput()[1]["Dimmer"], 1.0);
        s.pressButton(x, 0, false);
        QVERIFY(x.pb().isOn());          // going out over the Off line's 2 s
        QVERIFY(x.pb().goingOff);
        engine.tick(1.0);
        QVERIFY(s.lastOutput()[1]["Dimmer"] > 0.3 && s.lastOutput()[1]["Dimmer"] < 0.7);
        engine.tick(1.1);
        QVERIFY(!x.pb().isOn());
        // Flash still lets go at once.
        x.buttons[0] = ButtonFn::Flash;
        s.pressButton(x, 0, true);
        s.pressButton(x, 0, false);
        QVERIFY(!x.pb().isOn());
    }

    // Developer sweep: every fixture file under LUMORA_SWEEP_DIRS (folders, separated by ';')
    // must load. Point it at a checkout of the QLC+ fixtures and the Open Fixture Library.
    void fixtureSweep()
    {
        const QString dirs = qEnvironmentVariable("LUMORA_SWEEP_DIRS");
        if (dirs.isEmpty())
            QSKIP("Set LUMORA_SWEEP_DIRS to sweep fixture files");
        QStringList failures;
        int tried = 0;
        for (const QString &dir : dirs.split(';', Qt::SkipEmptyParts)) {
            QDirIterator it(dir, {"*.qxf", "*.json", "*.gdtf"}, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString path = it.next();
                if (path.endsWith(QLatin1String("manufacturers.json")) || path.contains(QLatin1String("/schema")))
                    continue;
                FixtureLibrary lib;
                QString error;
                ++tried;
                if (!lib.loadFile(path, &error))
                    failures << QStringLiteral("%1: %2").arg(path, error);
            }
        }
        qInfo().noquote() << tried << "files," << failures.size() << "failed";
        for (const QString &f : failures)
            qInfo().noquote() << f;
        QVERIFY(failures.isEmpty());
    }

    // QLC+ capabilities can hold elements (<Alias> on a mode channel): the text around them is
    // the capability's name. 63 QLC+ fixtures failed with "Expected character data" before.
    void qxfCapabilityWithAlias()
    {
        const QByteArray xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<FixtureDefinition xmlns=\"http://www.qlcplus.org/FixtureDefinition\">\n"
            " <Manufacturer>Test</Manufacturer>\n"
            " <Model>Alias Par</Model>\n"
            " <Type>Color Changer</Type>\n"
            " <Channel Name=\"Dimmer\" Preset=\"IntensityMasterDimmer\"/>\n"
            " <Channel Name=\"Color mode\">\n"
            "  <Group Byte=\"0\">Effect</Group>\n"
            "  <Capability Min=\"0\" Max=\"127\" Preset=\"Alias\">Dimmer mode</Capability>\n"
            "  <Capability Min=\"128\" Max=\"255\" Preset=\"Alias\">Color macro mode\n"
            "   <Alias Mode=\"2 Channel\" Channel=\"Dimmer\" With=\"Color mode\"/>\n"
            "  </Capability>\n"
            " </Channel>\n"
            " <Mode Name=\"2 Channel\">\n"
            "  <Channel Number=\"0\">Dimmer</Channel>\n"
            "  <Channel Number=\"1\">Color mode</Channel>\n"
            " </Mode>\n"
            "</FixtureDefinition>\n";
        FixtureType t;
        QString error;
        QVERIFY2(FixtureLibrary::parseQxf(xml, &t, &error), qPrintable(error));
        QCOMPARE(t.modes.size(), 1);
        QCOMPARE(t.modes.first().footprint, 2);
    }

    void cueMacros()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        QVERIFY(cl.execute("Store Macro 4 \"Haze\"").ok);
        Macro m = *s.macro(4);
        m.lines = {{"Fixture 5 At 40"}};
        s.setMacro(m);
        QVERIFY(cl.execute("Fixture 1 At 50").ok);
        QVERIFY(cl.execute("Store Cue 1 Sequence 7").ok);
        QVERIFY(cl.execute("Store Cue 2 Sequence 7").ok);
        QVERIFY(cl.execute("ClearAll").ok);
        s.setSelectedSequence(7);
        QVERIFY(cl.execute("Cue 2 Macro 4").ok);
        QCOMPARE(s.sequence(7)->cues[1].macro, 4);
        QVERIFY(cl.execute("Assign Sequence 7 At Executor 205").ok);
        QVERIFY(cl.execute("Go Executor 205").ok);
        QCoreApplication::processEvents();
        QVERIFY(!s.programmerValues().contains(5));   // cue 1 has no macro
        QVERIFY(cl.execute("Go Executor 205").ok);
        QCoreApplication::processEvents();
        QCOMPARE(s.programmerValues()[5]["Dimmer"].value, 0.4);   // cue 2 ran it
        // Saved with the show.
        const Cue back = Cue::fromJson(s.sequence(7)->cues[1].toJson());
        QCOMPARE(back.macro, 4);
    }

    void macros()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        Engine engine(&s);
        QVERIFY(cl.execute("Store Macro 1 \"Busk\"").ok);
        Macro m = *s.macro(1);
        QCOMPARE(m.name, QString("Busk"));
        m.lines = {{"Fixture 1 At 50"},
                   {"Fixture 2 At 60", MacroWait::Follow, 1.0},
                   {"Fixture 3 At 70", MacroWait::Wait},
                   {"Fixture 4 At 80"},
                   {"Fixture 5 At 90"}};
        m.lines[3].enabled = false;   // skipped
        s.setMacro(m);
        QVERIFY(!cl.execute("Macro 9").ok);
        QVERIFY(cl.execute("Macro 1").ok);
        // Line 1 at once, line 2 at once (its 1 s comes after it), then the Follow time.
        QVERIFY(s.programmerValues()[1].contains("Dimmer"));   // (selections add up: now 60 too)
        QCOMPARE(s.programmerValues()[2]["Dimmer"].value, 0.6);
        QVERIFY(!s.programmerValues().contains(3));
        QVERIFY(s.macroRun(1));
        engine.tick(0.6);
        QVERIFY(!s.programmerValues().contains(3));
        engine.tick(0.5);
        QCOMPARE(s.programmerValues()[3]["Dimmer"].value, 0.7);
        QVERIFY(s.macroRun(1)->waiting);   // Wait: until it's tapped again
        engine.tick(5);
        QVERIFY(!s.programmerValues().contains(5));
        QVERIFY(cl.execute("Go Macro 1").ok);
        QVERIFY(!s.programmerValues().contains(4));   // disabled
        QCOMPARE(s.programmerValues()[5]["Dimmer"].value, 0.9);
        QVERIFY(!s.macroRun(1));   // done
        // Off Macro stops it.
        s.clearAll();
        QVERIFY(cl.execute("Macro 1").ok);
        QVERIFY(cl.execute("Off Macro 1").ok);
        engine.tick(2);
        QVERIFY(!s.programmerValues().contains(3));
        // A loop: Go Macro 2 as its own last line, with a time so it doesn't spin.
        Macro loop;
        loop.id = 2;
        loop.lines = {{"SetVar $n = 1"}, {"Go Macro 2", MacroWait::Follow, 0.5}};
        s.setMacro(loop);
        QVERIFY(cl.execute("Macro 2").ok);
        engine.tick(0.6);
        engine.tick(0.6);
        QVERIFY(s.macroRun(2));   // still going round
        QVERIFY(cl.execute("Off Macro").ok);
        QVERIFY(!s.macroRun(2));
        // Variables, in any command.
        QVERIFY(cl.execute("SetVar $level = 40").ok);
        s.clearAll();
        s.clearSelection();
        QVERIFY(cl.execute("Fixture 6 At $level").ok);
        QCOMPARE(s.programmerValues()[6]["Dimmer"].value, 0.4);
        // Appearance: macros can recolor sequences and macros.
        s.select({1});
        s.setAttribute("Dimmer", 1.0);
        s.storeCue(1, 1);
        QVERIFY(cl.execute("Appearance Sequence 1 \"Red\"").ok);
        QCOMPARE(s.sequence(1)->color, QString("#ff0000"));
        QVERIFY(cl.execute("Appearance Macro 1 \"#00ff88\"").ok);
        QCOMPARE(s.macro(1)->color, QString("#00ff88"));
        QVERIFY(cl.execute("Appearance Macro 1 Off").ok);
        QVERIFY(s.macro(1)->color.isEmpty());
        QVERIFY(!cl.execute("Appearance Macro 1 \"Plaid\"").ok);
        // Copy, label, save and load.
        QVERIFY(cl.execute("Copy Macro 1 At 5").ok);
        QCOMPARE(s.macro(5)->lines.size(), 5);
        QVERIFY(cl.execute("Label Macro 5 \"Copy\"").ok);
        Macro bg = *s.macro(5);
        bg.background = true;
        s.setMacro(bg);
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        QCOMPARE(t.macro(5)->name, QString("Copy"));
        QVERIFY(t.macro(5)->background);
        QCOMPARE(t.macro(1)->lines[2].wait, MacroWait::Wait);
        QCOMPARE(t.macro(1)->lines[1].time, 1.0);
        QVERIFY(!t.macro(1)->lines[3].enabled);
        QVERIFY(cl.execute("Delete Macro 5").ok);
        QVERIFY(!s.macro(5));
    }

    void effectsReferenceSpreads()
    {
        Show s;
        setupShow(s);
        Effect e;
        e.id = 1;
        e.name = "Chase";
        e.spread = 360;
        EffectLayer l;
        l.kind = LayerKind::Dimmer;
        l.points = {EffectPoint::level(0), EffectPoint::level(1)};
        e.layers << l;
        s.setEffect(e);
        const int sid = s.effect(1)->spreadId;
        QVERIFY(sid > 0);                       // the matching pool spread, made for it
        QCOMPARE(s.spread(sid)->phaseTo, 360.0);
        Effect e2 = e;
        e2.id = 2;
        s.setEffect(e2);
        QCOMPARE(s.effect(2)->spreadId, sid);   // shared, not made twice
        // The pool spread lays it out: Together puts every fixture on one phase.
        EffectInstance inst;
        inst.effectId = 1;
        inst.fixtures = {1, 2, 3, 4};
        auto run = [&]() {
            RawTable out;
            evaluateEffect(s, *s.effect(1), inst, 0.3, [](int, const QString &) { return 0.0; }, out);
            return out;
        };
        RawTable a = run();
        QVERIFY(std::fabs(a[1]["Dimmer"] - a[3]["Dimmer"]) > 0.05);
        Spread m = *s.spread(sid);
        m.phaseTo = 0;
        s.setSpread(m);
        RawTable b = run();
        QVERIFY(std::fabs(b[1]["Dimmer"] - b[3]["Dimmer"]) < 1e-6);
        // None: back to its own layout.
        Effect own = *s.effect(1);
        own.spreadId = 0;
        s.setEffect(own);
        QCOMPARE(s.effect(1)->spreadId, 0);
        // Older shows: effects without a reference get one when they load.
        QJsonObject o = s.toJson();
        QJsonArray fx = o["effects"].toArray();
        QJsonObject f0 = fx[1].toObject();
        f0.remove("spreadRef");
        f0["spread"] = 90;
        fx[1] = f0;
        o["effects"] = fx;
        Show t;
        QVERIFY(t.fromJson(o));
        QVERIFY(t.effect(2)->spreadId > 0);
        QCOMPARE(t.spread(t.effect(2)->spreadId)->phaseTo, 90.0);
        QCOMPARE(t.effect(1)->spreadId, 0);
    }

    void colorEffectsDriveEveryEmitter()
    {
        // An RGBAL + UV par and a virtual-color (hue/saturation) fixture.
        auto ofl = [](const QStringList &colors) {
            QJsonObject chans;
            QJsonArray order;
            for (const QString &c : colors) {
                chans[c] = QJsonObject{{"capability", QJsonObject{{"type", "ColorIntensity"}, {"color", c}}}};
                order.append(c);
            }
            return QJsonObject{{"name", "Par"}, {"availableChannels", chans},
                               {"modes", QJsonArray{QJsonObject{{"name", "m"}, {"channels", order}}}}};
        };
        FixtureType t;
        QString err;
        QVERIFY2(FixtureLibrary::parseOfl(ofl({"Red", "Green", "Blue", "Amber", "Lime", "UV"}), "test", "rgbal", &t, &err), qPrintable(err));
        const FixtureMode &m = t.modes.first();
        // Orange: amber does most of it.
        RawAttr o = emitterMix(&m, 1.0, 0.47, 0.0);
        QVERIFY(o.value("ColorAdd_A") > 0.8);
        QVERIFY(o.value("ColorAdd_R") < 0.2);
        QCOMPARE(o.value("ColorAdd_UV"), 0.0);
        // Pure blue: no amber or lime at all.
        RawAttr b = emitterMix(&m, 0.0, 0.0, 1.0);
        QCOMPARE(b.value("ColorAdd_A"), 0.0);
        QCOMPARE(b.value("ColorAdd_L"), 0.0);
        QCOMPARE(b.value("ColorAdd_B"), 1.0);
        // Leaving amber out (a color effect with the Amber family off) puts it all on RGB.
        const QStringList noAmber = {"ColorAdd_R", "ColorAdd_G", "ColorAdd_B", "ColorAdd_L"};
        RawAttr na = emitterMix(&m, 1.0, 0.47, 0.0, &noAmber);
        QVERIFY(!na.contains("ColorAdd_A"));
        QVERIFY(std::fabs(na.value("ColorAdd_R") + 0.6 * na.value("ColorAdd_L") - 1.0) < 1e-6);   // red plus lime's red
        // Hue and saturation.
        FixtureMode vm;
        for (const char *a : {"HSB_Hue", "HSB_Saturation", "HSB_Brightness"}) {
            ChannelDef c;
            c.attribute = a;
            c.coarse = int(vm.channels.size());
            vm.channels << c;
        }
        RawAttr hv = emitterMix(&vm, 0.0, 1.0, 0.0);   // green: a third of the way round
        QVERIFY(std::fabs(hv.value("HSB_Hue") - 1.0 / 3.0) < 1e-6);
        QCOMPARE(hv.value("HSB_Saturation"), 1.0);
    }

    void relativeEffects()
    {
        Show s;
        setupShow(s);
        Engine engine(&s);
        // An executor holds the pars at 80%; a relative dip takes them down from there.
        s.select({1, 2, 3, 4});
        s.setAttribute("Dimmer", 0.8);
        s.storeCue(1, 1);
        s.clearAll();
        s.assignSequence(1, 1, 201);
        s.go(*s.executor(1, 201));
        engine.tick(3);
        QCOMPARE(s.lastOutput()[1]["Dimmer"], 0.8);
        QCOMPARE(s.lastGoSequence(), 1);   // the playback monitor follows it
        Effect dip = effectTemplates().last();
        QCOMPARE(dip.name, QString("Dip"));
        QVERIFY(dip.layers[0].relative);
        dip.id = 1;
        s.setEffect(dip);
        EffectInstance inst;
        inst.effectId = 1;
        inst.fixtures = {1, 2, 3, 4};
        double lowest = 1, highest = 0;
        for (int i = 0; i < 20; ++i) {
            RawTable out;
            evaluateEffect(s, *s.effect(1), inst, i / 20.0, [](int, const QString &) { return 0.8; }, out);
            lowest = std::min(lowest, out[1]["Dimmer"]);
            highest = std::max(highest, out[1]["Dimmer"]);
        }
        QVERIFY(std::fabs(lowest - 0.3) < 0.02);    // 80% - 50%
        QVERIFY(std::fabs(highest - 0.8) < 0.02);   // never above what's there
        // Saved with the layer.
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        QVERIFY(t.effect(1)->layers[0].relative);
        QCOMPARE(t.effect(1)->layers[0].points[0].values["Level"], -0.5);
    }

    void plots()
    {
        Show s;
        setupShow(s);
        CommandLine cl(&s);
        QVERIFY(cl.execute("Store Plot 1 \"Stage\"").ok);
        QCOMPARE(s.plot(1)->displayName(), QString("Stage"));
        QVERIFY(cl.execute("Assign Fixture 1 Thru 4 At Plot 1").ok);
        QCOMPARE(s.plot(1)->items.size(), 4);
        QCOMPARE(s.plot(1)->items[0].kind, PlotItem::Fixture);
        QCOMPARE(s.plot(1)->items[3].fid, 4);
        QVERIFY(cl.execute("Assign Fixture 3 Thru 6 At Plot 1").ok);   // 3 and 4 are on it already
        QCOMPARE(s.plot(1)->items.size(), 6);
        QVERIFY(s.plot(1)->items[4].rect.top() > s.plot(1)->items[0].rect.bottom());   // a new row
        QVERIFY(cl.execute("Fixture 1 Thru 2").ok);
        QVERIFY(cl.execute("Store Group 1").ok);
        QVERIFY(cl.execute("Assign Group 1 At Plot 1").ok);
        QVERIFY(cl.execute("Assign Preset 4.1 At Plot 1").ok);
        QVERIFY(cl.execute("Assign Macro 2 At Plot 1").ok);
        QCOMPARE(s.plot(1)->items[6].object, QString("Group 1"));
        QCOMPARE(s.plot(1)->items[7].object, QString("Preset 4.1"));
        QCOMPARE(s.plot(1)->items[8].object, QString("Macro 2"));
        QVERIFY(!cl.execute("Assign Group At Plot 1").ok);
        // A plot assigned to a plot that doesn't exist yet makes it.
        QVERIFY(cl.execute("Assign Fixture 5 At Plot 3").ok);
        QVERIFY(s.plot(3));
        // Store works like Assign, lower case too.
        QVERIFY(cl.execute("store fixture 1 thru 5 at plot 4").ok);
        QCOMPARE(s.plot(4)->items.size(), 5);
        QVERIFY(cl.execute("Store Group 1 At Plot 4").ok);
        QCOMPARE(s.plot(4)->items.last().object, QString("Group 1"));
        QVERIFY(cl.execute("Delete Plot 4").ok);
        QVERIFY(cl.execute("Delete Plot 3").ok);
        // Text and a picture, zoom and its lock: saved with the show.
        Plot p = *s.plot(1);
        PlotItem text;
        text.kind = PlotItem::Text;
        text.text = "Downstage";
        text.link = "Group 1";
        text.group = 2;
        PlotItem pic;
        pic.kind = PlotItem::Image;
        pic.image = "<svg xmlns='http://www.w3.org/2000/svg'/>";
        pic.group = 2;
        p.items << text << pic;
        QCOMPARE(p.nextGroup(), 3);
        QCOMPARE(p.groupOf(9), (QVector<int>{9, 10}));
        QCOMPARE(p.groupOf(0), QVector<int>{0});
        p.zoom = 1.5;
        p.zoomLocked = true;
        // A background is one thing: a color, or a picture; setting one replaces the other.
        p.setBackgroundColor("#203040");
        QVERIFY(p.hasBackground());
        p.setBackgroundImage("<svg xmlns='http://www.w3.org/2000/svg'/>", "plan.svg", QRectF(0, 0, 800, 600));
        QVERIFY(p.backgroundColor.isEmpty());
        s.setPlot(p);
        QVERIFY(cl.execute("Copy Plot 1 At 5").ok);
        QVERIFY(cl.execute("Label Plot 5 \"Copy\"").ok);
        Show t;
        QVERIFY(t.fromJson(s.toJson()));
        QCOMPARE(t.plot(5)->name, QString("Copy"));
        QCOMPARE(t.plot(1)->items.size(), 11);
        QCOMPARE(t.plot(1)->items[9].text, QString("Downstage"));
        QCOMPARE(t.plot(1)->items[9].link, QString("Group 1"));
        QCOMPARE(t.plot(1)->items[10].group, 2);
        QCOMPARE(t.plot(1)->items[10].image, pic.image);
        QCOMPARE(t.plot(1)->backgroundName, QString("plan.svg"));
        QCOMPARE(t.plot(1)->backgroundRect, QRectF(0, 0, 800, 600));
        QCOMPARE(t.plot(1)->backgroundImage, pic.image);
        // Shows from before kept the background as a picture item: it becomes the background.
        {
            QJsonObject old = p.toJson();
            old.remove("backgroundImage");
            QJsonArray items = old["items"].toArray();
            QJsonObject bgItem = pic.toJson();
            bgItem["background"] = true;
            bgItem["imageName"] = "old.svg";
            items.prepend(bgItem);
            old["items"] = items;
            const Plot migrated = Plot::fromJson(old);
            QCOMPARE(migrated.items.size(), p.items.size());
            QCOMPARE(migrated.backgroundName, QString("old.svg"));
        }
        QCOMPARE(t.plot(1)->zoom, 1.5);
        QVERIFY(t.plot(1)->zoomLocked);
        QVERIFY(cl.execute("Plot 1").ok);
        QVERIFY(!cl.execute("Plot 9").ok);
        QVERIFY(cl.execute("Move Plot 5 At 6").ok);
        QVERIFY(!s.plot(5) && s.plot(6));
        QVERIFY(cl.execute("Delete Plot 6").ok);
        QVERIFY(!s.plot(6));
    }

    void ratioMasterSteps()
    {
        RateMaster m;
        m.id = 3;
        m.mode = RateMode::Ratio;
        m.setFromFader(0.5);
        QCOMPARE(m.value, 1.0);
        m.setFromFader(1.0);
        QCOMPARE(m.value, 16.0);
        m.setFromFader(0.0);
        QCOMPARE(m.value, 1.0 / 16);
    }
};

QTEST_GUILESS_MAIN(CoreTest)
#include "test_core.moc"
