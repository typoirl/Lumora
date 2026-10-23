// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "show.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QMap>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>

namespace lumora {

class OutputManager;

// Runs playback fades and merges executors + programmer into DMX universes.
//
// Merge rules:
//  * Dimmer is highest-takes-precedence across executors, scaled by each executor's master.
//  * Every other attribute is latest-takes-precedence (the most recently triggered executor wins).
//  * The programmer overrides playbacks; highlight overrides the programmer for selected fixtures.
//  * Effects run on top of the playback (or programmer) they belong to, so a playback's
//    master scales its effect's intensity and programmer effects override playbacks.
//  * Grand master and blackout scale intensity last. Fixtures without a dimmer channel get a
//    virtual dimmer that scales their additive color channels.
class Engine : public QObject {
    Q_OBJECT
public:
    explicit Engine(Show *show, QObject *parent = nullptr);

    void setOutputs(OutputManager *outputs) { m_outputs = outputs; }
    void start(int hz = 40);
    void stop();

    // Advances playbacks by dt seconds and recomputes output. Public so tests can drive time.
    void tick(double dt);
    // Where the running effects are in their cycles, to save with the show (and pick up again
    // when it opens: a show opening takes them from its "effectPhases").
    QJsonObject phasesJson() const;

    const QMap<int, QByteArray> &universes() const { return m_universes; }
    const RawTable &merged() const { return m_merged; }   // pre-grand-master attribute values

    // Intensity an executor currently contributes (0..1), for UI meters.
    static double executorMaster(const Executor &e, bool anySwop);
    static double sequenceMaster(Show &show, const Executor &e, bool anySwop);

signals:
    // After every frame of output (the effect editor's preview runs on it).
    void frameDone();
    // After a frame whose values differ from the one before: live views repaint on it, so they
    // show what was just sent, and rest while the stage doesn't change.
    void outputChanged();

private:
    void advance(Executor &e, double dt);
    int nextCueTriggers(Executor &e, double dt);   // 1: the next cue's Wait or Follow is due; 2: the Off line's
    void render(double dt, QList<Executor *> execs);
    // Advances an effect's phase (in cycles) and returns it; phases persist per running instance.
    double effectPhase(const QString &key, const EffectInstance &inst, double dt);
    // The programmer as it reaches the stage: with Programmer fade on, values fade to what's
    // programmed (from what was there) and, once cleared, back to what's underneath.
    RawTable fadedProgrammer(const RawTable &under, double dt);
    struct ProgFade {
        double from = 0, to = 0, shown = 0, elapsed = 0, duration = 0;
        bool releasing = false;
    };
    QHash<int, QHash<QString, ProgFade>> m_progFades;
    bool m_progFadeWasOn = false;   // switched on just now: what's programmed already stays put

    Show *m_show;
    OutputManager *m_outputs = nullptr;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QMap<int, QByteArray> m_universes;
    RawTable m_merged;
    bool m_outputChanged = true;
    bool snaps(int fid, const QString &attribute) const;   // the channel jumps instead of fading
    QHash<QString, double> m_phases;
    QSet<QString> m_seenPhases;
};

} // namespace lumora
