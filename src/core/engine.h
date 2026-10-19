// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "show.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QMap>
#include <QHash>
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
    bool isRunning() const { return m_timer.isActive(); }

    // Advances playbacks by dt seconds and recomputes output. Public so tests can drive time.
    void tick(double dt);

    const QMap<int, QByteArray> &universes() const { return m_universes; }
    const RawTable &merged() const { return m_merged; }   // pre-grand-master attribute values
    double frameRate() const { return m_fps; }

    // Intensity an executor currently contributes (0..1), for UI meters.
    static double executorMaster(const Executor &e, bool anySwop);
    static double sequenceMaster(Show &show, const Executor &e, bool anySwop);

private:
    void advance(Executor &e, double dt);
    int nextCueTriggers(Executor &e, double dt);   // 1: the next cue's Wait or Follow is due; 2: the Off line's
    void render(double dt, QList<Executor *> execs);
    // Advances an effect's phase (in cycles) and returns it; phases persist per running instance.
    double effectPhase(const QString &key, const EffectInstance &inst, double dt);

    Show *m_show;
    OutputManager *m_outputs = nullptr;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QMap<int, QByteArray> m_universes;
    RawTable m_merged;
    double m_fps = 0.0;
    QHash<QString, double> m_phases;
    QSet<QString> m_seenPhases;
};

} // namespace lumora
