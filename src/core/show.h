// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fixturetype.h"
#include "macros.h"
#include "model.h"
#include "plots.h"

#include <QJsonArray>
#include <QObject>
#include <QPoint>
#include <QSet>

#include <functional>
#include <optional>

namespace lumora {

// A stored view: a named arrangement of windows. The layout is the interface's own JSON; the show
// only keeps it. Views are recalled on any screen and change only when stored again.
struct StoredView {
    int id = 0;
    QString name;
    QJsonArray layout;
    QJsonObject toJson() const { return {{"id", id}, {"name", name}, {"layout", layout}}; }
    static StoredView fromJson(const QJsonObject &o)
    {
        return {o["id"].toInt(), o["name"].toString(), o["layout"].toArray()};
    }
};

// The whole show: patch, pools, sequences, executors and the programmer.
// Everything that changes show data goes through here so views can stay in sync.
class Show : public QObject {
    Q_OBJECT
public:
    enum Part {
        PatchPart = 1 << 0,
        GroupsPart = 1 << 1,
        PresetsPart = 1 << 2,
        SequencesPart = 1 << 3,
        ExecutorsPart = 1 << 4,
        ProgrammerPart = 1 << 5,
        SettingsPart = 1 << 6,
        EffectsPart = 1 << 7,
        ViewsPart = 1 << 8,
        SpreadsPart = 1 << 9,
        ParkPart = 1 << 10,
        MacrosPart = 1 << 11,
        PlotsPart = 1 << 12,
        AllParts = 0x1fff,
    };
    Q_DECLARE_FLAGS(Parts, Part)

    enum class SelectMode { Replace, Add, Remove };
    enum class StoreMode { Merge, Overwrite };

    explicit Show(QObject *parent = nullptr);

    FixtureLibrary &library() { return m_library; }
    const FixtureLibrary &library() const { return m_library; }

    QString name() const { return m_name; }
    void setName(const QString &n);
    void clearShow();

    // ---- Patch -----------------------------------------------------------
    bool addFixture(const Fixture &f, QString *error = nullptr);
    bool updateFixture(const Fixture &f, QString *error = nullptr);
    // Changes several fixtures at once (type, mode, address...). All-or-nothing:
    // validated against the resulting patch so fixtures can be shifted together.
    bool updateFixtures(const QVector<Fixture> &list, QString *error = nullptr);
    // Replaces (or adds) a fixture type; patched fixtures of that type use it at once. Fixtures
    // whose mode no longer exists move to the type's first mode.
    void updateFixtureType(const FixtureType &type);
    // Imports a fixture file (GDTF, OFL, QLC+, .lft) into this show.
    bool importFixtureFile(const QString &path, QString *error = nullptr, QString *typeId = nullptr);
    // Copies a type from the shipped library into the show (patching does this).
    void adoptType(const QString &typeId);
    // The fixture types this show holds: patched, imported, downloaded or edited (not the
    // library Lumora ships with, until something from it is patched).
    QStringList typeIdsInShow() const;
    void removeFixture(int fid);
    // Gives fixtures new IDs (old -> new) all at once. Groups, presets, cues, defaults, effects
    // and the programmer follow. Fails, changing nothing, if a new ID is taken or used twice.
    bool renumberFixtures(const QMap<int, int> &oldToNew, QString *error = nullptr);
    // The patch: ordinary fixtures and pixel fixtures' masters. Cells are listed by cells().
    const QMap<int, Fixture> &fixtures() const { return m_fixtures; }
    const QMap<int, Fixture> &cells() const { return m_cells; }
    const Fixture *fixture(int fid) const;   // a fixture or a cell
    bool hasFixture(int fid) const { return fixture(fid) != nullptr; }
    QVector<int> cellsOf(int master) const;    // every subfixture, both levels, in order
    QVector<int> childrenOf(int fid) const;    // one level down: a fixture's heads, a head's subs
    // "101", "101.3", "101.1.2". Unknown subfixtures are written by their slot.
    QString idText(int fid) const;
    // The other way: 0 when it isn't an ID. "101.3" for a fixture that isn't patched (or has no
    // such subfixture) still gives 101's third slot, so a selection can name it.
    int parseId(const QString &text) const;
    // Every fixture and cell, in ID order with each master followed by its cells.
    QVector<int> fixtureIds() const;
    // A fixture or head that has no channels of its own stands for its subfixtures (selecting it
    // selects them, all the way down).
    QVector<int> expandMasters(const QVector<int> &fids) const;
    const FixtureType *fixtureType(int fid) const;
    const FixtureMode *fixtureMode(int fid) const;
    QStringList fixtureAttributes(int fid) const;
    bool fixtureHas(int fid, const QString &attribute) const;
    // What a fixture rests at when nothing plays: the show's stored defaults (Store Default),
    // else its type's open defaults (white, open shutter and gobo, centered position).
    double defaultValue(int fid, const QString &attribute) const;
    double typeDefault(int fid, const QString &attribute) const;
    // The values an attribute takes on a fixture: 0..1, or its physical degrees for pan and tilt.
    const ChannelDef *channelOf(int fid, const QString &attribute) const;
    double clampValue(int fid, const QString &attribute, double value) const;
    double valueSpan(int fid, const QString &attribute) const;   // 1, or the degrees it covers
    const ValueTable &storedDefaults() const { return m_defaults; }
    // Store Default: the programmer's values (presets stay references) become the defaults.
    bool storeDefaults(StoreMode mode);
    void clearDefaults(const QVector<int> &fids = {});   // empty: every fixture
    // Home: these fixtures' attributes back to their defaults, in the programmer. An empty
    // attribute list means every attribute; an empty fixture list means every fixture.
    int home(const QVector<int> &fids, const QStringList &attributes = {});
    int nextFreeFid() const;
    int nextFreeAddress(int universe, int footprint) const;
    // Lays fixtures out one after another from universe.address: each takes the first gap big
    // enough at or after the one before. A fixture never straddles a universe: when it doesn't
    // fit, it starts the next universe at 1. Fixtures in the list are placed afresh (where they
    // are now doesn't count). False, with a reason, when it runs out of universes.
    bool layOut(QVector<Fixture> &list, int universe, int address, QString *error = nullptr) const;
    int footprintOf(const Fixture &f) const;   // DMX slots the fixture's mode needs
    QString addressConflict(const Fixture &f) const;   // empty when OK
    QString conflictIn(const QMap<int, Fixture> &patch, const Fixture &f) const;

    // ---- Groups ----------------------------------------------------------
    const QMap<int, Group> &groups() const { return m_groups; }
    const Group *group(int id) const;
    bool storeGroup(int id, const QString &name = QString());
    void setGroup(const Group &g);
    void deleteGroup(int id);

    // ---- Presets ---------------------------------------------------------
    const QMap<int, Preset> &presets(int type) const;
    const Preset *preset(int type, int id) const;
    // layer: Universal, Global (Type) or Selective (Fixture); empty means the pool's default
    // (Universal for Dimmer and Color, Selective for the rest). Other layers are kept on merge.
    bool storePreset(int type, int id, StoreMode mode = StoreMode::Merge, const QString &name = QString(),
                     std::optional<PresetScope> scope = std::nullopt);
    bool presetValue(const Preset &p, int fid, const QString &attribute, double *out) const;
    void setPreset(const Preset &p);
    void deletePreset(int type, int id);
    bool resolvePreset(int type, int id, int fid, const QString &attribute, double *out) const;
    // The universal Dimmer preset at this level ("Full", "Out", "25%"), or 0. With create, makes
    // one in the next free Dimmer slot when there's none.
    int dimmerPresetFor(double level, bool create);

    // ---- Programmer ------------------------------------------------------
    const QVector<int> &selection() const { return m_selection; }
    const ValueTable &programmerValues() const { return m_programmer; }
    void select(const QVector<int> &fids, SelectMode mode = SelectMode::Replace);
    void selectGroup(int id, SelectMode mode = SelectMode::Replace);
    // ---- The selection matrix: where each selected fixture sits, X across and Y down. Selecting
    // adds along X; nextY() starts a new row; a group with a layout keeps it.
    QPoint gridPosition(int fid) const { return m_selGrid.value(fid); }
    QVector<QPoint> selectionGrid(const QVector<int> &fids) const;   // positions for these (selected) fixtures
    void nextY();
    QPoint gridCursor() const { return m_gridCursor; }
    void setGridCursor(const QPoint &at);              // Grid 2/1: where the next selected fixtures go
    void placeInGrid(const QVector<int> &fids, const QPoint &at);   // along X from there (Fixture 5 Grid 3/2)
    void fillGridArea(const QPoint &from, const QPoint &to);        // the selection, X then Y, inside the area
    void setGridPosition(int fid, const QPoint &at);   // swaps with a fixture already there
    // Moves these fixtures by an offset, keeping their shape; fixtures in the way take the
    // cells they left. Nothing goes below X 0 or Y 0.
    void moveInGrid(const QVector<int> &fids, const QPoint &offset);
    void resetGrid();                                  // one row, in selection order
    void setAttribute(const QString &attribute, double value);              // for the selection
    void setAttribute(int fid, const QString &attribute, double value);
    // Relative, for encoders: delta is a share of the attribute's range (0.01 = 1%, or 5.4° of
    // a 540° pan).
    void nudgeAttribute(const QString &attribute, double delta);
    // Flip: the same physical direction with pan half a turn round and tilt mirrored, for the
    // selection (or these fixtures). Returns how many flipped.
    int flip(const QVector<int> &fids = {});
    bool applyPreset(int type, int id);                                      // to the selection
    // Names the color values now in the programmer ("L201 Full CT Blue", "Red", "Tungsten
    // 3200 K"), as the color picker sets them. A Color preset stored while the programmer still
    // holds exactly those values takes this name, unless it's given one.
    void setColorName(const QString &name);
    QString colorName() const;   // empty once the programmer's colors have changed
    void removeProgrammerValues(const QVector<int> &fids);   // "knock out" fixtures from the programmer
    void clearSelection();
    void clearProgrammer();
    void clearAll();
    QString clearStep();    // Clear: selection first, then values. Returns what was cleared.
    // Next / Prev: narrow the selection to one of its fixtures at a time, in selection order.
    // After the last (or before the first) the whole selection comes back. All: the whole
    // selection now. Each returns what it did.
    QString stepSelection(int direction);
    QString wholeSelection();
    int stepIndex() const { return m_stepIndex; }        // -1: not stepping
    int stepCount() const { return int(m_stepList.size()); }
    // Select Last: the selection from before the last Clear (or replacement) comes back; the
    // current one is kept in its place, so doing it twice swaps back.
    bool reselectLast();

    // ---- Park ----------------------------------------------------------------
    // Parked attributes hold their output where they were, whatever plays or is programmed,
    // until unparked. An empty attribute and feature 0 mean the whole fixture; a fixture
    // includes its subfixtures. Park returns how many attributes it parked.
    const RawTable &parked() const { return m_parked; }
    int park(const QVector<int> &fids, const QString &attribute = QString(), int feature = 0);
    int unpark(const QVector<int> &fids, const QString &attribute = QString(), int feature = 0);   // empty fids: everything
    bool isParked(int fid, const QString &attribute) const { return m_parked.value(fid).contains(attribute); }
    double resolveValue(int fid, const QString &attribute, const ValueRef &v) const;
    RawTable programmerOutput() const;

    // ---- Sequences and cues ---------------------------------------------
    const QMap<int, Sequence> &sequences() const { return m_sequences; }
    const Sequence *sequence(int id) const;
    Sequence *sequenceMutable(int id);
    Sequence &ensureSequence(int id);
    void setSequence(const Sequence &s);
    void deleteSequence(int id);
    // Stores the programmer as a cue (number < 0: the next whole number). An empty programmer
    // makes an empty cue, ready for blueprints.
    bool storeCue(int sequenceId, double number, StoreMode mode = StoreMode::Merge, double *storedNumber = nullptr);
    // A blank cue whatever is in the programmer. Returns false if that number is taken.
    bool addEmptyCue(int sequenceId, double number = -1, double *storedNumber = nullptr);
    bool deleteCue(int sequenceId, double number);
    bool addBlueprintLine(int sequenceId, double number, const BlueprintLine &line);
    bool buildCue(int sequenceId, double number);
    // Removes the given fixtures' hard values from a cue. Returns the number of fixtures removed.
    int removeFromCue(int sequenceId, double number, const QVector<int> &fids);
    // Moves a sequence to a new number; executors follow it.
    bool renumberSequence(int from, int to);
    RawTable evaluateCue(const Cue &cue) const;
    RawTable trackedState(const Sequence &s, int index) const;
    // Which preset each tracked value came from at that cue (blueprint lines and preset-referenced
    // values); hard values are (0, 0).
    QMap<int, QHash<QString, PresetRef>> trackedPresets(const Sequence &s, int index) const;
    // The line's fixtures (with their places on the group's matrix layout, when asked).
    QVector<int> blueprintFixtures(const BlueprintLine &line, QVector<QPoint> *grid = nullptr) const;
    // The cue's individual fade and delay times: its values' own, and those of blueprint lines
    // whose spread has a fade or delay range, laid out across the line's fixtures.
    TimingTable cueTiming(const Cue &cue) const;
    int selectedSequence() const { return m_selectedSequence; }
    void setSelectedSequence(int id);

    // ---- Effects -----------------------------------------------------------
    const QMap<int, Effect> &effects() const { return m_effects; }
    const Effect *effect(int id) const;
    void setEffect(const Effect &e);   // one with no spread chosen yet (-1) gets the pool spread matching its layout
    int spreadForLayout(const Spread &layout, const QString &name);   // finds or makes it
    void deleteEffect(int id);
    // Moves an effect to a free id and points every preset, cue, blueprint and running instance at it.
    bool renumberEffect(int from, int to);
    // What a blueprint line or cue refers to that no longer exists (deleted groups, presets,
    // effects), as readable lines; empty when everything is there.
    QStringList blueprintProblems(const BlueprintLine &line) const;
    QStringList cueProblems(const Cue &cue) const;
    // Same for presets (cue values, blueprints, effect points, programmer) and groups (blueprints).
    bool renumberPreset(int type, int from, int to);
    bool renumberGroup(int from, int to);
    int nextFreeEffectId() const;
    const QMap<int, Shape> &shapes() const { return m_shapes; }
    const Shape *shape(int id) const;   // custom shapes (positive ids)
    void setShape(const Shape &s);
    int nextFreeShapeId() const;

    // Effects running in the programmer
    const QVector<EffectInstance> &programmerEffects() const { return m_progEffects; }
    // Runs an effect on the selection. Fixtures leave any other programmer effect that drives the same features.
    bool applyEffect(int effectId, double rate = 1.0, double size = 1.0, double spread = -1.0);
    void setProgrammerEffect(int index, const EffectInstance &inst);
    void removeProgrammerEffect(int index);
    void removeEffectsFrom(const QVector<int> &fids);   // all programmer effects, these fixtures
    void removeEffect(int effectId);                    // one effect from the programmer
    // Effects a cue runs: stored instances plus effects on blueprint lines.
    QVector<EffectInstance> cueEffects(const Cue &cue) const;
    // A preset's effect: which of these fixtures it plays on (its Universal / Global / Selective
    // layer), and running it in the programmer with its own spread.
    QVector<int> presetEffectFixtures(const EffectInstance &spec, const QVector<int> &candidates) const;
    bool applyEffectInstance(const EffectInstance &spec, const QVector<int> &fids);

    // ---- Executors -------------------------------------------------------
    int currentPage() const { return m_page; }
    void setPage(int page);
    // Executor pages have optional names; pages() lists every page that has a name or executors,
    // plus the current one, in order.
    QString pageName(int page) const { return m_pageNames.value(page); }
    QString pageLabel(int page) const;   // the name, or "Page n"
    void setPageName(int page, const QString &name);
    QList<int> pages() const;
    Executor *executor(int page, int number);              // nullptr if not created
    Executor &ensureExecutor(int page, int number);
    QList<Executor *> allExecutors();
    bool assignSequence(int sequenceId, int page, int number);
    // ---- Rate masters (1–10): effects follow them, executors control them ----
    const RateMaster *rateMaster(int id) const;
    const QVector<RateMaster> &rateMasters() const { return m_rateMasters; }
    void setRateMaster(const RateMaster &m);
    bool assignRateMaster(int id, int page, int number);
    void tapRateMaster(int id);          // Learn: tap tempo
    void resetRateMaster(int id);        // 100% / 120 BPM / ×1
    void pressRateMasterKey(int id);     // the key on its strip: tap (Learn) or Reset
    void toggleRateMasterFreeze(int id);
    // ---- Plots: fixtures and pool objects laid out on a sheet, with text and pictures ----
    const Plot *plot(int id) const;
    QList<Plot> plots() const { return m_plots.values(); }
    void setPlot(const Plot &p);                         // adds or replaces
    bool storePlot(int id, const QString &name = QString());   // a new empty plot, or renames
    void deletePlot(int id);
    bool movePlot(int from, int to, bool copy);
    // Puts fixtures (in a row under what's there, skipping ones already on it) or a pool object
    // on a plot, making the plot if needed. Returns how many were added.
    int addFixturesToPlot(int id, const QVector<int> &fids);
    bool addObjectToPlot(int id, const QString &object);

    // ---- Macros: lists of commands, run in the foreground (typed on the command line) or the
    //      background, line by line with Follow times or a Wait for the next tap ----
    const Macro *macro(int id) const;
    QList<Macro> macros() const { return m_macros.values(); }
    void setMacro(const Macro &m);                       // adds or replaces
    bool storeMacro(int id, const QString &name = QString());   // a new empty macro, or renames
    void deleteMacro(int id);
    bool moveMacro(int from, int to, bool copy);
    // Tapping a macro: starts it, or goes on from a Wait (or skips the rest of a Follow time).
    void goMacro(int id);
    void offMacro(int id = 0);                            // stops it (0: every macro)
    const MacroRun *macroRun(int id) const;               // nullptr when it isn't running
    void tickMacros(double dt);
    // Runs one line. The interface sets this to type foreground lines on the command line, ask
    // for (popup) input and keep the history; returning false stops the macro. Without it,
    // lines run straight through a command line of their own.
    using MacroExecutor = std::function<bool(const Macro &, const MacroLine &, const QString &command)>;
    void setMacroExecutor(MacroExecutor fn) { m_macroExec = std::move(fn); }
    // Variables for macros: "SetVar $speed = 120", then $speed in any command.
    QString macroVariable(const QString &name) const { return m_macroVars.value(name.toLower()); }
    bool hasMacroVariable(const QString &name) const { return m_macroVars.contains(name.toLower()); }
    void setMacroVariable(const QString &name, const QString &value);
    QString expandVariables(const QString &command) const;
    // Appearance: the color of a sequence or macro ("#rrggbb", empty for none).
    bool setAppearance(const QString &what, int id, const QString &color);

    // Executors of one sequence share its playback: after anything that changes assignments.
    void linkPlaybacks();
    QList<Executor *> executorsOf(int sequenceId);   // on every page
    void unassign(int page, int number);
    // 401–499: make an executor a fader, a button or an encoder. Buttons play at full; buttons
    // and encoders press Toggle.
    bool setExecutorKind(int page, int number, ExecKind kind);
    Executor *executorForSequence(int sequenceId);

    // Playback control (works on any executor that has a sequence)
    void go(Executor &e);
    // The sequence a Go last went to (the playback monitor follows it), 0 before any.
    int lastGoSequence() const { return m_lastGoSequence; }
    void goBack(Executor &e);
    void gotoCue(Executor &e, int index, bool useFade = true);
    void gotoCue(Executor &e, int index, double fade);   // in this fade, for every value (no delay)
    // Off: with the sequence's off fade and delay unless `immediately` (a flash let go, a
    // fader pulled to zero).
    void off(Executor &e, bool immediately = false);
    void togglePause(Executor &e);
    void pressButton(Executor &e, int button, bool down);
    void setFader(Executor &e, double value);
    void notifyExecutors() { emit changed(ExecutorsPart); }
    bool anySwopHeld() const;   // a Swop button is down on a running playback

    // ---- spread -------------------------------------------------------------
    const QMap<int, Spread> &spreads() const { return m_spreads; }
    const Spread *spread(int id) const;
    void setSpread(const Spread &m);
    void deleteSpread(int id);
    // The active spread on an axis into the pool (or the one in use, X first, when not given).
    // axis -1: what's on (X and Y the same: one spread for both; different: X into id, Y into the
    // next free slot after it, reported in *secondId).
    bool storeSpread(int id, const QString &name = QString(), int axis = -1, int *secondId = nullptr);
    bool recallSpread(int id);                                   // a pool item onto the selection, on its axis
    // The spreads on the programmer's selection: one across (X), one down (Y), each switched on
    // or off; they stack. Cleared with the selection.
    const Spread &activeSpread() const { return m_activeSpread; }     // X
    const Spread &activeSpreadY() const { return m_activeSpreadY; }
    const Spread &activeSpread(Spread::Axis axis) const { return axis == Spread::Axis::Y ? m_activeSpreadY : m_activeSpread; }
    void setActiveSpread(const Spread &m);   // into the slot of each axis it works on
    void setActiveSpread(Spread::Axis axis, const Spread &m);   // into this axis's slot only
    // storeSpread's axis: 0 the X slot, 1 the Y slot, 2 the X slot's settings working both ways.
    bool spreadAxisOn(Spread::Axis axis) const { return axis == Spread::Axis::Y ? m_spreadYOn : m_spreadXOn; }
    void setSpreadAxisOn(Spread::Axis axis, bool on);
    const Spread *liveSpread(Spread::Axis axis) const;   // the one in use on that axis, or nullptr
    QString activeSpreadSummary() const;                  // "Every 2 · Y: Blocks 2", or empty
    // The selection as values and effects see it: thinned by the active spread' interleave.
    QVector<int> workingSelection() const;
    // Individual fade or delay times on the selection's programmer values (all, one feature or
    // one attribute), spread across the selection in order, or in the active spread's layout:
    // {0, 5} is "0 Thru 5", {0, 5, 0} "0 Thru 5 Thru 0". Empty points clear them. Returns how
    // many values took it.
    int setProgrammerTiming(bool delay, const QVector<double> &points, int feature = 0, const QString &attribute = QString());

    // ---- Views ---------------------------------------------------------------
    const QMap<int, StoredView> &views() const { return m_views; }
    const StoredView *view(int id) const;
    // Store View n: the layout on the screen in use (from the provider the interface sets).
    // Keeps the view's name unless one is given. False when there's no layout to store.
    bool storeView(int id, const QString &name = QString());
    void setView(const StoredView &v);
    void setViews(const QList<StoredView> &views);   // replaces them all
    void deleteView(int id);
    int nextFreeViewId() const;
    // Asks the screen in use to show a view (the interface listens for viewRecallRequested).
    void recallView(int id) { emit viewRecallRequested(id); }
    using LayoutProvider = std::function<std::optional<QJsonArray>()>;
    void setViewLayoutProvider(LayoutProvider f) { m_layoutProvider = std::move(f); }

    // ---- Masters ---------------------------------------------------------
    double grandMaster() const { return m_grandMaster; }
    void setGrandMaster(double v);
    bool blackout() const { return m_blackout; }
    void setBlackout(bool on);
    bool highlight() const { return m_highlight; }
    void setHighlight(bool on);
    // Programmer fade: while on, programmer changes fade in (and Clear fades them out) over this time.
    bool progFadeOn() const { return m_progFadeOn; }
    double progFadeTime() const { return m_progFadeTime; }
    void setProgFade(bool on, double seconds = -1);   // seconds < 0: keep the time
    // The fade (and delay) new cues get, in seconds (Show → Config).
    double defaultCueFade() const { return m_defaultCueFade; }
    double defaultCueDelay() const { return m_defaultCueDelay; }
    void setDefaultCueTimes(double fade, double delay);

    // Engine writes its last merged output here so fades can start from what's on stage.
    void setLastOutput(const RawTable &t) { m_lastOutput = t; }
    const RawTable &lastOutput() const { return m_lastOutput; }

    // ---- Persistence -----------------------------------------------------
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject &o, QString *error = nullptr);
    // Extra sections other modules (MIDI, outputs) store in the show file.
    QJsonObject extra(const QString &key) const { return m_extra.value(key).toObject(); }
    // markChanged: it's a change the operator would want to save (marks the show as changed).
    void setExtra(const QString &key, const QJsonObject &o, bool markChanged = false)
    {
        m_extra[key] = o;
        if (markChanged)
            emit changed(SettingsPart);
    }
    void removeExtra(const QString &key) { m_extra.remove(key); }

    // ---- Undo ------------------------------------------------------------
    // A copy of the show's data. Cheap: Qt's containers share their data until one side changes.
    // Restoring leaves what's running alone (playbacks, fader levels, page, masters); plots have
    // their own undo, so they're not in it.
    struct Snapshot {
        FixtureLibrary library;
        QString name;
        QMap<int, Fixture> fixtures;
        QMap<int, Group> groups;
        QMap<int, QMap<int, Preset>> presets;
        QMap<int, Sequence> sequences;
        QMap<int, Effect> effects;
        QMap<int, Shape> shapes;
        QVector<EffectInstance> progEffects;
        QMap<int, Executor> executors;
        QVector<RateMaster> rateMasters;
        QMap<int, Macro> macros;
        QVector<int> selection;
        RawTable parked;
        ValueTable programmer;
        ValueTable defaults;
        QJsonObject extra;
        QMap<int, QString> pageNames;
        QMap<int, StoredView> views;
        QMap<int, Spread> spreads;
        Spread activeSpread, activeSpreadY;
        bool spreadXOn = true, spreadYOn = true;
        QHash<int, QPoint> selGrid;
        QPoint gridCursor;
        bool progFadeOn = false;
        double progFadeTime = 2.0, cueFade = 2.0, cueDelay = 0.0;
        // Nothing changed since o (as far as can be told without comparing contents: the
        // containers still share their data).
        bool sameData(const Snapshot &o) const;
    };
    Snapshot snapshot() const;
    void restore(const Snapshot &s);
    QMap<int, Plot> plotsSnapshot() const { return m_plots; }
    void restorePlots(const QMap<int, Plot> &plots);

signals:
    void changed(lumora::Show::Parts parts);
    void message(const QString &text);   // short status feedback for the UI
    void viewRecallRequested(int id);
    void plotRequested(int id);   // "Plot 3": show it in the plot windows
    void gotoRequested(int page, int executor);   // a Goto button, or "Goto Executor 201": pick a cue
    void oscSendRequested(int line, const QString &message);   // SendOSC 1 "/address,i,5"

private:
    void startFade(Executor &e, int targetIndex, double fade, double delay, bool individual = true);
    void releaseFlash(Executor &e, bool useOffTime = false);
    double cueValueLookupFrom(const Executor &e, int fid, const QString &attr) const;

    FixtureLibrary m_library;
    QString m_name = QStringLiteral("Untitled Show");
    void rebuildCells();   // after the patch or a fixture type changes
    void anglesToDegrees();   // shows from before pan and tilt were degrees

    QMap<int, Fixture> m_fixtures;
    QMap<int, Fixture> m_cells;   // made from the masters, not saved
    QMap<int, Group> m_groups;
    QMap<int, QMap<int, Preset>> m_presets;
    QMap<int, Sequence> m_sequences;
    QMap<int, Effect> m_effects;
    QMap<int, Shape> m_shapes;
    QVector<EffectInstance> m_progEffects;
    QMap<int, Executor> m_executors;   // key = page * 1000 + number
    QVector<RateMaster> m_rateMasters;   // always RateMaster::kCount, ids 1…
    QMap<int, Macro> m_macros;
    QMap<int, Plot> m_plots;
    QVector<MacroRun> m_macroRuns;
    MacroExecutor m_macroExec;
    QHash<QString, QString> m_macroVars;
    bool m_processingMacros = false;
    int m_lastGoSequence = 0;
    int m_executingMacro = 0;
    QSet<int> m_macroRestart;
    void processMacros();
    bool runMacroLine(const Macro &m, const MacroLine &line);
    void resetRateMasters();
    QVector<int> m_selection;
    QVector<int> m_lastSelection;   // for Select Last
    QVector<int> m_stepList;        // the whole selection while stepping with Next / Prev
    int m_stepIndex = -1;
    RawTable m_parked;
    void rememberSelection();
    QVector<int> withSubfixtures(const QVector<int> &fids) const;
    QStringList matchingAttributes(int fid, const QString &attribute, int feature) const;
    ValueTable m_programmer;
    void putProgrammer(int fid, const QString &attribute, ValueRef r);
    ValueTable m_defaults;
    RawTable m_lastOutput;
    QJsonObject m_extra;
    int m_page = 1;
    QMap<int, QString> m_pageNames;
    QMap<int, StoredView> m_views;
    QMap<int, Spread> m_spreads;
    Spread m_activeSpread;
    Spread m_activeSpreadY;
    bool m_spreadXOn = true, m_spreadYOn = true;
    QHash<int, QPoint> m_selGrid;   // the selection matrix
    QPoint m_gridCursor;            // where the next selected fixture goes
    QString m_colorName;
    QHash<QString, double> m_colorSnapshot;
    QHash<QString, double> colorSnapshot() const;
    LayoutProvider m_layoutProvider;
    int m_selectedSequence = 1;
    double m_grandMaster = 1.0;
    bool m_blackout = false;
    bool m_highlight = false;
    bool m_progFadeOn = false;
    double m_progFadeTime = 2.0;
    double m_defaultCueFade = 2.0;
    double m_defaultCueDelay = 0.0;
    quint64 m_stamp = 0;
};

} // namespace lumora

Q_DECLARE_OPERATORS_FOR_FLAGS(lumora::Show::Parts)
