// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "effects.h"
#include "types.h"

#include <QJsonObject>
#include <QPair>
#include <QPoint>
#include <QString>
#include <QVector>

#include <memory>

namespace lumora {

// Names for several fixtures from one: a name ending in a number counts on from it ("Spot 5" →
// Spot 5, Spot 6…; "Par12" → Par12, Par13…), any other name gets 1, 2, 3… after a space.
QStringList numberedNames(const QString &name, int count);

struct Fixture {
    int fid = 0;
    QString name;
    QString typeId;
    QString mode;
    int universe = 1;   // 1-based
    int address = 1;    // 1-based start address; 0 = not patched (no DMX output)
    // A subfixture (made by the show from its fixture, never saved): the fixture's ID, its slot,
    // where it sits (head, and sub one level down: 101.head.sub) and the row it nests under.
    // Subfixtures share the fixture's address; their channels sit inside it.
    // Hung the other way round: these run the pan or tilt DMX backwards (saved with the patch).
    bool invertPan = false;
    bool invertTilt = false;
    int master = 0;
    int cell = 0;      // slot: 1… in the fixture type's order
    int head = 0;
    int sub = 0;
    int parent = 0;    // the fixture, or the head a sub belongs to
    int depth() const { return cell == 0 ? 0 : sub == 0 ? 1 : 2; }
    bool patched() const { return address > 0; }

    QJsonObject toJson() const;
    static Fixture fromJson(const QJsonObject &o);
};

struct Group {
    int id = 0;
    QString name;
    QVector<int> fixtures;   // ordered: selection order matters for spreads and fans
    // Where each sits on the selection matrix (parallel to fixtures); empty: one row, in order.
    QVector<QPoint> grid;

    QJsonObject toJson() const;
    static Group fromJson(const QJsonObject &o);
};

// A preset is layered:
//   Universal: one value per attribute, for any fixture that has the attribute.
//   Global:    one value per fixture type; overrides Universal for fixtures of that type.
//   Selective: one value per fixture; overrides both for that fixture.
// A store writes one layer (the pool's default, or the one asked for) and keeps the others.
enum class PresetScope { Universal, Type, Fixture };
QString presetScopeName(PresetScope s);   // "Universal", "Global", "Selective"
PresetScope presetScopeFromName(const QString &n, PresetScope fallback);
// Dimmer and Color presets store Universal by default; everything else stores Selective.
PresetScope defaultPresetLayer(int presetType);

struct Preset {
    int type = 0;   // 1..7 feature pools, 8 = All
    int id = 0;
    QString name;
    RawTable perFixture;                 // fixture id -> values
    QMap<QString, RawAttr> perType;      // fixture type id -> values
    RawAttr universal;                   // attribute -> value
    QVector<EffectInstance> effects;     // effects stored with the preset (no fixtures: they run on the selection)

    // The value for this fixture: Selective if stored, else Global for its type, else Universal.
    bool lookup(int fid, const QString &typeId, const QString &attribute, double *out) const;
    bool hasLayer(PresetScope layer) const;
    QString layerBadge() const;     // "U", "G", "S" for the layers present, e.g. "U·S"
    RawAttr sampleValues() const;   // some representative values, for swatches: Universal first
    QStringList attributes() const;

    QJsonObject toJson() const;
    static Preset fromJson(const QJsonObject &o);
};

using PresetRef = QPair<int, int>;   // (type, id)

// One line of a cue blueprint: "take this group, apply these presets, at this level".
// Blueprints are evaluated live, so editing the group or a preset changes the cue.
struct BlueprintLine {
    QString name;
    int groupId = 0;
    QVector<PresetRef> presets;
    double dimmer = -1.0;    // 0..1, or < 0 for "no dimmer value"
    int every = 1;           // interleave: take every Nth fixture...
    int offset = 0;          // ...starting at this index (0-based)
    int spreadId = 0;      // a spread from the pool: interleave and effect layout (0 = none)
    QVector<int> effects;    // effect ids to run on these fixtures
    // Individual times across the line's fixtures, in the group's order (laid out by the spread
    // when there is one): {0, 3} is 0 Thru 3 seconds, {0, 3, 0} 0 Thru 3 Thru 0, one value is the
    // same for all. Empty: the cue's own. Values and effects alike take them.
    QVector<double> fadeTimes;
    QVector<double> delayTimes;

    QJsonObject toJson() const;
    static BlueprintLine fromJson(const QJsonObject &o);
};

// How a cue starts: Go waits for a Go; Wait starts it its wait time after the
// cue before it started; Follow starts it as soon as the cue before it has finished its fade.
enum class CueTrigger { Go, Wait, Follow };
QString cueTriggerName(CueTrigger t);

struct Cue {
    double number = 1.0;
    QString name;
    double fade = 2.0;    // seconds
    double delay = 0.0;   // seconds
    CueTrigger trigger = CueTrigger::Go;
    double wait = 0.0;    // seconds, for a Wait trigger
    QString triggerText() const;   // "Go", "Wait 5", "Follow"
    QString notes;        // for the operator: shown in the cue list and the playback monitor
    int macro = 0;        // a macro that runs as the cue starts (0: none)
    QVector<BlueprintLine> blueprints;
    ValueTable values;    // hard (or preset-referenced) values; override blueprint output
    QVector<EffectInstance> effects;   // effects that run while this cue is live

    QString displayNumber() const;
    QJsonObject toJson() const;
    static Cue fromJson(const QJsonObject &o);
};

// How a sequence's values compete with other sequences', lowest to highest. Low, LTP and Ultra
// are latest-takes-precedence within their level; HTP takes the highest intensity among HTP
// sequences (the rest LTP). A higher level always wins; Ultra wins over the programmer too.
enum class SeqPriority { Low = 0, LTP = 1, HTP = 2, Ultra = 3 };
QString seqPriorityName(SeqPriority p);
SeqPriority seqPriorityFromName(const QString &n);

struct Sequence {
    int id = 0;
    QString name;
    SeqPriority priority = SeqPriority::LTP;   // everything LTP, dimmer included
    bool offWhenOverridden = true;   // turns itself off once other sequences have taken all its values
    QString color;                   // its appearance on pools and executors, "#rrggbb"; empty: none
    QVector<Cue> cues;   // kept sorted by number
    bool tracking = true;
    bool wrap = true;
    // The Off "cue": how it fades out when the executor is turned off, and whether it turns
    // itself off after the last cue (Wait: its wait after the last cue started; Follow: once
    // the last cue has finished). An automatic Off comes before Wrap.
    double offFade = 0.0;
    double offDelay = 0.0;
    CueTrigger offTrigger = CueTrigger::Go;
    double offWait = 0.0;

    int indexOf(double cueNumber) const;
    void insertSorted(const Cue &cue);   // replaces a cue with the same number

    QJsonObject toJson() const;
    static Sequence fromJson(const QJsonObject &o);
};

// ---------------------------------------------------------------------------
// Executors
// ---------------------------------------------------------------------------

// Goto: opens a list of the sequence's cues to pick one (with its fade).
enum class ButtonFn { None, Go, GoBack, Pause, Flash, Temp, Toggle, On, Off, Swop, Select, Top, Goto };
enum class FaderFn { Master, XFade, Temp };
// What an executor is on screen. The hundreds say it: 101–199 buttons, 201–299 faders,
// 301–399 encoders; 401–499 are whichever each one is set to (a fader until changed).
enum class ExecKind { Fader, Button, Encoder };
QString execKindName(ExecKind k);
ExecKind execKindFromName(const QString &n);
struct Executor;
ExecKind executorKind(int number, const Executor *e = nullptr);
inline bool executorKindAssignable(int number) { return number >= 400 && number < 500; }

QString buttonFnName(ButtonFn f);
ButtonFn buttonFnFromName(const QString &n);
QList<ButtonFn> allButtonFns();
QString faderFnName(FaderFn f);
FaderFn faderFnFromName(const QString &n);

// Individual times for one fade: fixture -> attribute -> (fade, delay) in seconds, < 0 for the
// cue's own. The attribute "*" covers the rest of that fixture (its effects, for one).
struct AttrTiming {
    double fade = -1.0;
    double delay = -1.0;
};
using TimingTable = QHash<int, QHash<QString, AttrTiming>>;

// Runtime playback state of one executor. Not saved with the show.
struct PlaybackState {
    int cueIndex = -1;        // -1 = off
    int fromCueIndex = -1;    // the cue it's fading from (its effects fade out as the new cue's fade in)
    RawTable from;            // snapshot at Go
    RawTable to;              // tracked target state
    RawTable current;         // what this playback outputs right now (before master)
    double fadeTime = 0.0;
    double delayTime = 0.0;
    TimingTable timing;       // individual fade and delay times of this fade
    double duration = 0.0;    // the whole fade: the longest delay + fade, individual times included
    double elapsed = 0.0;
    bool fading = false;
    bool paused = false;
    bool goingOff = false;    // fading out with the sequence's off time; off when it's done
    double sinceGo = 0.0;     // seconds since this cue started (for the next cue's Wait or Follow)
    quint64 stamp = 0;        // activation order for latest-takes-precedence merging

    // XFade fader bookkeeping
    double xfadeStart = 0.0;
    int xfadeTarget = -1;

    bool isOn() const { return cueIndex >= 0; }
    double progress() const;      // the whole fade, 0..1
    double clock() const;         // seconds into the fade (a crossfade fader scales the whole fade)
    double progressAt(int fid, const QString &attribute) const;   // with its individual times
    double progressFor(double fade, double delay) const;
};

struct Executor {
    int page = 1;
    int number = 201;   // 101… buttons, 201… faders, 301… encoders, 401… any (see executorKind)
    int sequenceId = 0;
    int rateMaster = 0;   // instead of a sequence: this executor controls rate master 1–10
    ButtonFn buttons[3] = {ButtonFn::Go, ButtonFn::GoBack, ButtonFn::Off};
    FaderFn faderFn = FaderFn::Master;
    double fader = 1.0;
    bool autoStart = true;   // raising the master from 0 starts the sequence
    bool autoStop = true;    // pulling the master to 0 turns it off
    ExecKind kind = ExecKind::Fader;   // used by 401–499

    // runtime. The playback is shared by every executor the sequence is on (Show links them),
    // so Go on one and Pause on another work on the same cue list.
    std::shared_ptr<PlaybackState> state = std::make_shared<PlaybackState>();
    PlaybackState &pb() { return *state; }
    const PlaybackState &pb() const { return *state; }
    bool flashHeld = false;
    bool tempHeld = false;
    bool swopHeld = false;
    bool flashStartedPlayback = false;
    bool buttonDown[3] = {false, false, false};

    bool isEmpty() const { return sequenceId == 0 && rateMaster == 0; }
    QJsonObject toJson() const;
    static Executor fromJson(const QJsonObject &o);
};

QJsonObject valueTableToJson(const ValueTable &t);
ValueTable valueTableFromJson(const QJsonObject &o);
// Whether an executor button shows as on: Pause while paused, Flash/Temp/Swop while held,
// Toggle/On while the playback runs, anything else while pressed.
bool executorButtonLit(const Executor &e, int button);

QJsonObject rawAttrToJson(const RawAttr &a);
RawAttr rawAttrFromJson(const QJsonObject &o);
QJsonObject rawTableToJson(const RawTable &t);
RawTable rawTableFromJson(const QJsonObject &o);

} // namespace lumora
