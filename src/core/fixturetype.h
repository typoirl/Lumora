// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "types.h"

#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>
#include <vector>

namespace lumora {

// A named DMX range on a channel, e.g. a color-wheel slot or a gobo.
struct ChannelSet {
    double from = 0.0;   // normalized 0..1
    double to = 1.0;
    QString name;
    QString media;       // key into FixtureType::media (e.g. a gobo image), or empty
};

struct ChannelDef {
    QString attribute;   // standardized attribute name
    QString name;        // display name from the fixture file
    int coarse = 0;      // 0-based offset inside the footprint; -1: virtual (no DMX slot)
    int fine = -1;       // 0-based offset of the 16-bit fine byte, or -1
    double defaultValue = 0.0;
    double highlightValue = 1.0;
    bool hasHighlight = true;   // false: Highlight leaves this channel alone
    QVector<ChannelSet> sets;
    // Subfixtures (two levels): cell 0 is the fixture itself; cell 1… a subfixture
    // (a head, a pixel); sub 1… a subfixture of that one (a head's ring, its beam).
    int cell = 0;
    int sub = 0;
    // The value range: the physical values at the two ends of the DMX range (below). Pan and
    // tilt: degrees (-270..270 for a 540° pan; reversed when the low DMX end is the high angle),
    // and values, defaults and highlight are stored in degrees. Everything else: what the ends
    // are called (0..100 % for a dimmer, 5..50° for a zoom); values are stored as 0..1 of it.
    // 0..0 means the file didn't say (540 pan, 270 tilt, 0..100 for the rest).
    double physFrom = 0.0;
    double physTo = 0.0;
    // The DMX range the value range covers, as 0..1 of the channel's full range (0..255, or
    // 0..65535 with a fine channel). Usually all of it.
    double dmxFrom = 0.0;
    double dmxTo = 1.0;
    bool isAngle() const;
    double rangeFrom() const;   // the value at the low end of the DMX range (degrees for pan/tilt)
    double rangeTo() const;     // and at the high end
    double toPhysical(double value) const;     // a stored value as the value range shows it
    double fromPhysical(double physical) const;
    QString unit() const;   // "°" for pan and tilt
    double minValue() const;    // the lowest value it takes: 0 or the lower angle
    double maxValue() const;
    double span() const { return maxValue() - minValue(); }   // 1, or the degrees it covers
    double toDmx(double value) const;     // to 0..1 of the channel's full DMX range
    double fromDmx(double level) const;   // and back
    int dmxMax() const { return fine >= 0 ? 65535 : 255; }
    QString valueText(double value, int decimals = 0) const;   // "50", or "-90°"
    // A file's physical range, centered on 0 the way Lumora counts (0..540 becomes -270..270).
    void setPhysicalRange(double from, double to);

    // A virtual channel has no DMX slot: it's programmed, stored and played back like any other,
    // but nothing goes on the wire. A virtual Dimmer scales the additive color channels.
    bool isVirtual() const { return coarse < 0; }

    const ChannelSet *setFor(double value) const;   // the named range containing value, or nullptr
    QString setNameFor(double value) const;
};

struct FixtureMode {
    QString name;
    QVector<ChannelDef> channels;
    int footprint = 0;            // number of DMX slots
    bool virtualDimmer = false;   // its Dimmer is a virtual channel that scales the additive colors
    bool noAutoDimmer = false;    // the virtual Dimmer was removed by hand: don't add it back
    // Subfixtures, in order: (1,0), (1,1), (1,2), (2,0)… Slot 0 is the fixture itself; slot n is
    // its nth subfixture at either level. cellCount is how many (0: an ordinary fixture).
    int cellCount = 0;

    const ChannelDef *channel(const QString &attribute) const;
    QStringList attributes() const;
    // RGB(W…) with no Dimmer gets a virtual Dimmer channel, first in the list; a real Dimmer
    // replaces a virtual one. Pixel fixtures: per cell. Returns true when it changed the channels.
    bool ensureVirtualDimmer();
    // The channels of the fixture itself (slot 0) or one subfixture, as a mode of its own: same
    // footprint, offsets from the fixture's start address. An ordinary fixture's part 0 is itself.
    const FixtureMode *part(int slot) const;
    QPair<int, int> partPath(int slot) const;   // (cell, sub); (0, 0) for the fixture itself
    int partSlot(int cell, int sub) const;      // -1 when there's no such subfixture
    void buildParts();   // after the channels change (finalize does it)

private:
    struct Parts {
        std::vector<FixtureMode> modes;
        QVector<QPair<int, int>> paths;
    };
    std::shared_ptr<const Parts> m_parts;
};

struct FixtureType {
    QString id;            // "manufacturer/model"
    QString manufacturer;
    QString name;
    QString shortName;
    QVector<FixtureMode> modes;
    QHash<QString, QByteArray> media;   // images from the fixture file (gobos), PNG bytes
    QString source;                     // "OFL", "GDTF", "QLC+" or "Lumora" (built in the fixture editor)
    // Rest values have been set to "open": white light, open shutter and gobo, centered position.
    // Types built or edited by hand keep exactly the defaults they were given.
    bool openDefaults = false;
    // Pan and tilt defaults are in degrees. Fixture files give them as 0..1 of the DMX range;
    // finalize() turns them into degrees once.
    bool angles = true;
    // Types from before highlight could be left out of a channel: finalize() keeps highlight on
    // the dimmer, color and shutter channels only, the way it always worked.
    bool highlightsExplicit = false;

    const FixtureMode *mode(const QString &name) const;

    QJsonObject toJson() const;
    static FixtureType fromJson(const QJsonObject &o);
    void finalize();   // virtual dimmers, footprints and flags
    void applyOpenDefaults();
};

// Loads fixture types from Open Fixture Library JSON, GDTF archives and QLC+ definitions.
class FixtureLibrary {
public:
    // Parses an Open Fixture Library fixture file. `manufacturerKey` and `fixtureKey`
    // come from the OFL folder layout (manufacturer/fixture.json) when known.
    static bool parseOfl(const QJsonObject &ofl, const QString &manufacturerKey, const QString &fixtureKey,
                         FixtureType *out, QString *error);

    bool loadOflFile(const QString &path, QString *error = nullptr, QString *typeId = nullptr);
    // GDTF (.gdtf): DMX modes, channel functions and sets, and wheel slot images.
    static bool parseGdtf(const QByteArray &archive, FixtureType *out, QString *error);
    bool loadGdtfFile(const QString &path, QString *error = nullptr, QString *typeId = nullptr);
    // QLC+ (.qxf). `mediaResolver` turns a gobo path such as "Robe/gobo00010.svg" into image bytes.
    static bool parseQxf(const QByteArray &xml, FixtureType *out, QString *error,
                         const std::function<QByteArray(const QString &)> &mediaResolver = {});
    bool loadQxfFile(const QString &path, QString *error = nullptr, QString *typeId = nullptr);
    // Lumora's own format (.lft), written by the fixture editor: FixtureType::toJson.
    bool loadLftFile(const QString &path, QString *error = nullptr, QString *typeId = nullptr);
    bool loadFile(const QString &path, QString *error = nullptr, QString *typeId = nullptr);   // by extension
    int loadDirectory(const QString &dir);   // recursive .json (OFL), .gdtf and .qxf; returns number loaded

    // The id GDTF and QLC+ fixtures get: "manufacturer/name", lower case, dashes for the rest.
    static QString makeTypeId(const QString &manufacturer, const QString &name);

    // Two layers. The library Lumora ships with, to pick from, and the show's own fixture types:
    // whatever it patched, imported, downloaded or edited. The show's win, and are saved in the
    // show file, so a show carries its fixtures with it and never depends on this computer.
    void add(const FixtureType &t);           // the shipped library (or the show's, inside importFile)
    void addToShow(const FixtureType &t);
    // Loads a fixture file into the show's layer.
    bool importFile(const QString &path, QString *error = nullptr, QString *typeId = nullptr);
    bool isShowType(const QString &id) const { return m_show.contains(id); }
    QList<FixtureType> showTypes() const { return m_show.values(); }
    void clearShowTypes() { m_show.clear(); }
    const FixtureType *find(const QString &id) const;   // the show's first
    QList<FixtureType> all() const;
    QList<FixtureType> shippedTypes() const { return m_types.values(); }   // the library Lumora ships with
    QStringList ids() const;
    bool sharesDataWith(const FixtureLibrary &o) const { return m_types.isSharedWith(o.m_types) && m_show.isSharedWith(o.m_show); }

private:
    QMap<QString, FixtureType> m_types;
    QMap<QString, FixtureType> m_show;
    bool m_importing = false;
};

} // namespace lumora
