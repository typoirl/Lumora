// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine.h"

#include "dmxoutput.h"

#include <QSet>

#include <algorithm>
#include <cmath>

namespace lumora {

Engine::Engine(Show *show, QObject *parent) : QObject(parent), m_show(show)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        const qint64 ns = m_clock.nsecsElapsed();
        m_clock.restart();
        const double dt = std::min(0.25, ns / 1e9);
        tick(dt);
        emit frameDone();
    });
    connect(show, &Show::changed, this, [this](Show::Parts parts) {
        if (parts != Show::Parts(Show::AllParts))
            return;
        // A show opened (or a new one): its effects carry on where they were saved.
        m_phases.clear();
        const QJsonObject saved = m_show->extra(QStringLiteral("effectPhases"));
        for (auto it = saved.begin(); it != saved.end(); ++it)
            m_phases.insert(it.key(), it.value().toDouble());
    });
}

QJsonObject Engine::phasesJson() const
{
    QJsonObject o;
    for (auto it = m_phases.begin(); it != m_phases.end(); ++it)
        if (!it.key().startsWith(QLatin1Char('p')))   // the programmer isn't saved: nor are its effects
            o[it.key()] = std::fmod(it.value(), 1.0);
    return o;
}

void Engine::start(int hz)
{
    m_clock.start();
    m_timer.start(std::max(1, 1000 / hz));
}

void Engine::stop()
{
    m_timer.stop();
}

double Engine::sequenceMaster(Show &show, const Executor &e, bool anySwop)
{
    // Every executor of the sequence plays one playback: any held flash or swop counts, and the
    // master is the sequence's master fader (they move together), or full when it has none.
    const QList<Executor *> all = e.sequenceId ? show.executorsOf(e.sequenceId) : QList<Executor *>{};
    if (all.isEmpty())
        return executorMaster(e, anySwop);
    if (!e.pb().isOn())
        return 0.0;
    bool swop = false, flash = false, anyFader = false;
    double master = 0.0;
    for (const Executor *x : all) {
        swop |= x->swopHeld;
        flash |= x->flashHeld || x->swopHeld;
        if (executorKind(x->number, x) == ExecKind::Fader && x->faderFn != FaderFn::XFade) {
            anyFader = true;
            master = std::max(master, x->fader);
        }
    }
    if (anySwop && !swop)
        return 0.0;
    if (flash)
        return 1.0;
    return anyFader ? master : 1.0;
}

double Engine::executorMaster(const Executor &e, bool anySwop)
{
    if (!e.pb().isOn())
        return 0.0;
    if (anySwop && !e.swopHeld)
        return 0.0;
    if (e.flashHeld || e.swopHeld)
        return 1.0;
    if (e.faderFn == FaderFn::XFade)
        return 1.0;
    return e.fader;
}

bool Engine::snaps(int fid, const QString &attribute) const
{
    const ChannelDef *c = m_show->channelOf(fid, attribute);
    return c && c->snap;
}

void Engine::advance(Executor &e, double dt)
{
    PlaybackState &pb = e.pb();
    if (!pb.isOn())
        return;
    if (pb.fading && !pb.paused && pb.xfadeTarget < 0)
        pb.elapsed += dt;

    const double p = pb.progress();
    const bool done = pb.fading && pb.xfadeTarget < 0 && p >= 1.0;

    if (!pb.fading) {
        pb.current = pb.to;
        return;
    }

    // Each attribute fades with its own times when it has individual ones, otherwise
    // with the cue's.
    const bool individual = !pb.timing.isEmpty();
    auto at = [&](int fid, const QString &a) {
        const double q = individual ? pb.progressAt(fid, a) : pb.progressFor(pb.fadeTime, pb.delayTime);
        return q > 0.0 && snaps(fid, a) ? 1.0 : q;   // a snapping channel jumps once its fade starts
    };
    RawTable cur;
    for (auto f = pb.to.begin(); f != pb.to.end(); ++f) {
        const RawAttr fromAttrs = pb.from.value(f.key());
        RawAttr &out = cur[f.key()];
        for (auto a = f->begin(); a != f->end(); ++a) {
            const double from = fromAttrs.value(a.key(), a.value());
            out.insert(a.key(), from + (a.value() - from) * at(f.key(), a.key()));
        }
    }
    // Values that are leaving this playback (non-tracking cue, or going back):
    // intensity fades out, other attributes hold until the fade completes.
    for (auto f = pb.from.begin(); f != pb.from.end(); ++f) {
        const RawAttr toAttrs = pb.to.value(f.key());
        for (auto a = f->begin(); a != f->end(); ++a) {
            if (toAttrs.contains(a.key()))
                continue;
            if (isIntensity(a.key()))
                cur[f.key()].insert(a.key(), a.value() * (1.0 - at(f.key(), a.key())));
            else if (!done)
                cur[f.key()].insert(a.key(), a.value());
        }
    }
    pb.current = cur;
    if (done) {
        pb.fading = false;
        pb.current = pb.to;
        if (pb.goingOff) {   // the off fade finished: the playback is off now
            const quint64 stamp = pb.stamp;
            const int loaded = pb.loadedIndex;   // a loaded cue waits for the next Go
            e.pb() = PlaybackState();
            e.pb().stamp = stamp;
            e.pb().loadedIndex = loaded;
            e.flashStartedPlayback = false;
        }
        m_show->notifyExecutors();
    }
}

int Engine::nextCueTriggers(Executor &e, double dt)
{
    PlaybackState &pb = e.pb();
    if (!pb.isOn() || pb.paused || pb.goingOff || pb.xfadeTarget >= 0)
        return 0;
    pb.sinceGo += dt;
    const Sequence *s = m_show->sequence(e.sequenceId);
    if (!s || s->cues.isEmpty())
        return 0;
    const int count = int(s->cues.size());
    const Cue &cur = s->cues[std::clamp(pb.cueIndex, 0, count - 1)];
    auto due = [&](CueTrigger trigger, double wait) {
        switch (trigger) {
        case CueTrigger::Go:
            return false;
        case CueTrigger::Wait:   // its wait time after this cue started
            return pb.sinceGo >= wait;
        case CueTrigger::Follow:   // when this cue's delay and fade are done
            return !pb.fading && pb.sinceGo >= cur.delay + cur.fade - 1e-6;
        }
        return false;
    };
    // After the last cue: the Off line's trigger, before Wrap.
    if (pb.cueIndex >= count - 1 && s->offTrigger != CueTrigger::Go)
        return due(s->offTrigger, s->offWait) ? 2 : 0;
    int next = pb.cueIndex + 1;
    if (next >= count) {
        if (!s->wrap)
            return 0;
        next = 0;
    }
    if (next == pb.cueIndex)
        return 0;
    return due(s->cues[next].trigger, s->cues[next].wait) ? 1 : 0;
}

void Engine::tick(double dt)
{
    m_show->tickMacros(dt);
    m_show->linkPlaybacks();
    const QList<Executor *> execs = m_show->allExecutors();
    QSet<const PlaybackState *> advanced;   // a sequence on two executors moves once
    QVector<QPair<Executor *, int>> triggered;
    for (Executor *e : execs)
        if (!advanced.contains(e->state.get())) {
            advanced.insert(e->state.get());
            advance(*e, dt);
            if (const int t = nextCueTriggers(*e, dt))
                triggered.append({e, t});
        }
    for (const auto &t : triggered) {   // Wait and Follow cues go by themselves, and so can Off
        if (t.second == 2)
            m_show->off(*t.first);
        else
            m_show->go(*t.first);
    }
    render(dt, execs);
    if (m_outputChanged)
        emit outputChanged();
}

namespace {
// Values from `src` replace `out`'s, for attributes the fixture has (LTP).
void overlay(RawTable &out, const RawTable &src)
{
    for (auto f = src.begin(); f != src.end(); ++f) {
        auto target = out.find(f.key());
        if (target == out.end())
            continue;
        for (auto a = f->begin(); a != f->end(); ++a)
            if (target->contains(a.key()))
                (*target)[a.key()] = a.value();
    }
}
} // namespace

RawTable Engine::fadedProgrammer(const RawTable &under, double dt)
{
    const RawTable target = m_show->programmerOutput();
    const double dur = m_show->progFadeOn() ? m_show->progFadeTime() : 0.0;
    if (dur <= 0.0 && m_progFades.isEmpty())
        return target;   // the usual case: nothing fading
    auto underValue = [&](int fid, const QString &a) {
        const auto f = under.constFind(fid);
        if (f != under.constEnd() && f->contains(a))
            return f->value(a);
        return isIntensity(a) ? 0.0 : m_show->defaultValue(fid, a);
    };
    auto step = [&](ProgFade &lf) {
        lf.elapsed += dt;
        const double p = lf.duration > 1e-9 ? std::clamp(lf.elapsed / lf.duration, 0.0, 1.0) : 1.0;
        lf.shown = lf.from + (lf.to - lf.from) * p;
        return p;
    };
    if (dur > 0.0 && !m_progFadeWasOn) {
        // Switched on with values already programmed: they're on stage already, nothing to fade.
        for (auto f = target.begin(); f != target.end(); ++f)
            for (auto a = f->begin(); a != f->end(); ++a) {
                ProgFade lf;
                lf.from = lf.to = lf.shown = a.value();
                lf.duration = dur;
                lf.elapsed = dur;
                m_progFades[f.key()].insert(a.key(), lf);
            }
    }
    m_progFadeWasOn = dur > 0.0;
    RawTable out;
    // What's programmed: fade to it from what was on stage (or from where it had got to).
    for (auto f = target.begin(); f != target.end(); ++f)
        for (auto a = f->begin(); a != f->end(); ++a) {
            QHash<QString, ProgFade> &fx = m_progFades[f.key()];
            auto it = fx.find(a.key());
            if (it == fx.end()) {
                ProgFade lf;
                lf.from = lf.shown = underValue(f.key(), a.key());
                lf.to = a.value();
                lf.duration = snaps(f.key(), a.key()) ? 0.0 : dur;
                it = fx.insert(a.key(), lf);
            } else if (it->releasing || std::fabs(it->to - a.value()) > 1e-6) {
                it->from = it->shown;
                it->to = a.value();
                it->elapsed = 0;
                it->duration = snaps(f.key(), it.key()) ? 0.0 : dur;
                it->releasing = false;
            }
            step(*it);
            out[f.key()][a.key()] = it->shown;
        }
    // What's been cleared: back to what's underneath, then gone.
    for (auto f = m_progFades.begin(); f != m_progFades.end();) {
        const auto t = target.constFind(f.key());
        for (auto a = f->begin(); a != f->end();) {
            if (t != target.constEnd() && t->contains(a.key())) {
                ++a;
                continue;
            }
            if (!a->releasing) {
                a->releasing = true;
                a->from = a->shown;
                a->elapsed = 0;
                a->duration = snaps(f.key(), a.key()) ? 0.0 : dur;
            }
            a->to = underValue(f.key(), a.key());   // follows the playbacks while it fades
            if (step(*a) >= 1.0) {
                a = f->erase(a);
                continue;
            }
            out[f.key()][a.key()] = a->shown;
            ++a;
        }
        if (f->isEmpty())
            f = m_progFades.erase(f);
        else
            ++f;
    }
    // Programmer fade off: the programmer as it is, and nothing left over.
    if (dur <= 0.0) {
        m_progFades.clear();
        return target;
    }
    return out;
}

double Engine::effectPhase(const QString &key, const EffectInstance &inst, double dt)
{
    const Effect *e = m_show->effect(inst.effectId);
    double &phase = m_phases[key];
    if (e) {
        if (const RateMaster *m = e->rateMaster ? m_show->rateMaster(e->rateMaster) : nullptr)
            phase += dt * m->cyclesPerSecond(e->cycle, inst.rate);
        else
            phase += dt * inst.rate / std::max(0.05, e->cycle);
    }
    m_seenPhases.insert(key);
    return phase;
}

void Engine::render(double dt, QList<Executor *> execs)
{
    m_seenPhases.clear();
    // Fixtures, then pixel fixtures' cells (their channels sit inside the master's footprint).
    QVector<const Fixture *> fixtures;
    fixtures.reserve(m_show->fixtures().size() + m_show->cells().size());
    for (const Fixture &f : m_show->fixtures())
        fixtures << &f;
    for (const Fixture &f : m_show->cells())
        fixtures << &f;
    const bool anySwop = m_show->anySwopHeld();
    // One source per running playback (a sequence on several executors is one), ordered by
    // priority, then by when it last went (latest wins within a priority).
    struct Source {
        Executor *lead;
        const Sequence *seq;
        int level;
        double master;
        bool held;   // a flash, temp or swop is held: never turned off for being overridden
        // On a Temp fader: how far it has taken over from what's underneath (0..1), every
        // attribute alike; -1 when it isn't on one.
        double temp = -1.0;
    };
    QVector<Source> sources;
    QSet<const PlaybackState *> seen;
    for (Executor *e : execs) {
        if (!e->pb().isOn() || seen.contains(e->state.get()))
            continue;
        seen.insert(e->state.get());
        const Sequence *seq = m_show->sequence(e->sequenceId);
        bool held = false, swop = false;
        double temp = -1.0;
        bool keyHeld = false;
        for (const Executor *x : m_show->executorsOf(e->sequenceId)) {
            held |= x->flashHeld || x->tempHeld || x->swopHeld;
            keyHeld |= x->flashHeld || x->tempHeld || x->swopHeld;
            swop |= x->swopHeld;
            if (x->faderFn == FaderFn::Temp && executorKind(x->number, x) == ExecKind::Fader)
                temp = std::max(temp, x->fader);
        }
        Source src{e, seq, int(seq ? seq->priority : SeqPriority::LTP), sequenceMaster(*m_show, *e, anySwop), held};
        if (temp >= 0.0) {
            // A Temp fader: a temporary takeover. Moved by hand, the fader says how far it has
            // come, whatever the times. A Flash, Temp or Swop key takes it all the way in the
            // cue's fade while held, and back out in the Off line's times once let go.
            PlaybackState &pb = e->pb();
            if (keyHeld) {
                const double fade = seq && pb.cueIndex >= 0 && pb.cueIndex < seq->cues.size() ? seq->cues[pb.cueIndex].fade : 0.0;
                const double was = std::max(0.0, pb.tempShown);
                pb.tempShown = fade > 1e-6 ? std::min(1.0, was + dt / fade) : 1.0;
                pb.tempFrom = -1.0;
            } else if (pb.goingOff) {
                if (pb.tempFrom < 0.0)
                    pb.tempFrom = std::max(0.0, pb.tempShown);
                pb.tempShown = pb.tempFrom * (1.0 - pb.progressFor(pb.fadeTime, pb.delayTime));
            } else {
                pb.tempShown = temp;
                pb.tempFrom = -1.0;
            }
            src.temp = anySwop && !swop ? 0.0 : pb.tempShown;
            src.master = 1.0;
            src.held = true;
        }
        sources.append(src);
    }
    std::sort(sources.begin(), sources.end(), [](const Source &a, const Source &b) {
        return a.level != b.level ? a.level < b.level : a.lead->pb().stamp < b.lead->pb().stamp;
    });

    // 1. Defaults: what Store Default stored, else the channel's own rest value.
    RawTable out;
    const ValueTable &stored = m_show->storedDefaults();
    for (const Fixture *fp : fixtures) {
        const Fixture &f = *fp;
        const FixtureMode *m = m_show->fixtureMode(f.fid);
        if (!m)
            continue;
        RawAttr &attrs = out[f.fid];
        const auto sd = stored.constFind(f.fid);
        for (const ChannelDef &c : m->channels) {
            double v = c.defaultValue;
            if (sd != stored.constEnd())
                if (auto a = sd->constFind(c.attribute); a != sd->constEnd())
                    v = m_show->resolveValue(f.fid, c.attribute, a.value());
            attrs.insert(c.attribute, v);
        }
    }

    // 2. Playbacks' values, by priority. Effects come afterwards in a layer of their own,
    //    relative values kept apart from absolute ones: a position from one executor
    //    doesn't stop a circle from another, it moves where the circle goes round. Who wrote
    //    each value is kept, for Off When Overridden.
    using Owners = QHash<int, QHash<QString, int>>;   // fixture -> attribute -> source (-1: programmer)
    auto levelOf = [&](int who) { return who >= 0 ? sources[who].level : -1; };
    auto merge = [&](const RawTable &values, int who, double master, Owners &owners) {
        const int level = levelOf(who);
        for (auto f = values.begin(); f != values.end(); ++f) {
            auto target = out.find(f.key());
            if (target == out.end())
                continue;
            QHash<QString, int> &ow = owners[f.key()];
            const double temp = who >= 0 ? sources[who].temp : -1.0;
            for (auto a = f->begin(); a != f->end(); ++a) {
                if (!target->contains(a.key()))
                    continue;
                double v = a.value();
                if (temp >= 0.0) {
                    // Temp fader: from what's underneath to its own value, as far as the fader
                    // is up; a snapping channel goes all the way as soon as it leaves zero.
                    const double q = temp > 0.0 && snaps(f.key(), a.key()) ? 1.0 : temp;
                    const double under = target->value(a.key());
                    (*target)[a.key()] = under + (v - under) * q;
                    ow[a.key()] = who;
                    continue;
                }
                if (isIntensity(a.key())) {
                    v *= master;
                    // HTP: the highest intensity of the HTP sequences.
                    const auto prev = ow.constFind(a.key());
                    if (level == int(SeqPriority::HTP) && prev != ow.constEnd() && levelOf(*prev) == int(SeqPriority::HTP)
                        && target->value(a.key()) > v)
                        continue;
                }
                (*target)[a.key()] = v;
                ow[a.key()] = who;
            }
        }
    };
    // A Temp fader going off fades by how far it shows, from the values it had (not the Off
    // line's own fade on top of that).
    auto valuesOf = [&](int i) -> const RawTable & {
        const PlaybackState &pb = sources[i].lead->pb();
        return sources[i].temp >= 0.0 && pb.goingOff ? pb.from : pb.current;
    };
    Owners owner, fxOwner;
    for (int i = 0; i < sources.size(); ++i)
        if (sources[i].level < int(SeqPriority::Ultra))
            merge(valuesOf(i), i, sources[i].master, owner);
    // 3. Programmer (fading in and out with Programmer fade), then Ultra sequences over it.
    overlay(out, fadedProgrammer(out, dt));
    for (int i = 0; i < sources.size(); ++i)
        if (sources[i].level == int(SeqPriority::Ultra))
            merge(valuesOf(i), i, sources[i].master, owner);

    // 4. Effects, on top of those values. Among effects on the same attribute: the higher
    //    priority, then the latest, wins (the programmer's sit above every sequence but Ultra).
    //    A Low effect still runs over an LTP preset; a newer Low effect takes over from it.
    const RawTable statics = out;
    auto staticValue = [&](int fid, const QString &a) {
        const auto f = statics.constFind(fid);
        if (f != statics.constEnd() && f->contains(a))
            return f->value(a);
        return isIntensity(a) ? 0.0 : m_show->defaultValue(fid, a);
    };
    auto evaluate = [&](const QVector<EffectInstance> &fx, const QString &keyPrefix) {
        RawTable table;
        for (int k = 0; k < fx.size(); ++k) {
            const Effect *eff = m_show->effect(fx[k].effectId);
            if (!eff)
                continue;
            const double phase = effectPhase(QStringLiteral("%1.%2.%3").arg(keyPrefix).arg(k).arg(fx[k].effectId), fx[k], dt);
            evaluateEffect(*m_show, *eff, fx[k], phase, staticValue, table);
        }
        return table;
    };
    // Each source's effects: the cue it's on, and while it fades, the cue it's leaving. A fade
    // goes from whatever is underneath to the new effect (and back to what's underneath when
    // it ends), underneath being everything merged so far: the values, and the effects of the
    // sources below it. So one color effect crossfades into another instead of passing
    // through the rest values.
    struct FxPlan {
        RawTable was, will;
        bool blending = false;
        const PlaybackState *pb = nullptr;
    };
    QVector<FxPlan> plans(sources.size());
    QVector<RawTable> sourceFx(sources.size());
    for (int i = 0; i < sources.size(); ++i) {
        const Source &src = sources[i];
        const PlaybackState &pb = src.lead->pb();
        auto cueFx = [&](int cueIndex) {
            if (!src.seq || cueIndex < 0 || cueIndex >= src.seq->cues.size())
                return RawTable();
            return evaluate(m_show->cueEffects(src.seq->cues[cueIndex]),
                            QStringLiteral("s%1.%2").arg(src.lead->sequenceId).arg(cueIndex));
        };
        const double p = pb.progress();
        const int toCue = pb.goingOff ? -1 : pb.cueIndex;   // fading out: its effects fade out too
        FxPlan &plan = plans[i];
        plan.pb = &pb;
        plan.will = cueFx(toCue);
        if (pb.fading && p < 1.0 && pb.fromCueIndex != toCue) {
            plan.was = cueFx(pb.fromCueIndex);
            plan.blending = true;
        }
    }
    auto fxTable = [&](int i) {
        const FxPlan &plan = plans[i];
        if (sources[i].temp >= 0.0 && plan.pb->goingOff)
            return plan.was;   // a Temp fader going off: its effects fade with it, by how far it shows
        if (!plan.blending)
            return plan.will;
        RawTable t = plan.will;
        for (auto f = plan.was.begin(); f != plan.was.end(); ++f)
            for (auto a = f->begin(); a != f->end(); ++a)
                t[f.key()].insert(a.key(), 0.0);   // the keys of both
        const PlaybackState &pb = *plan.pb;
        for (auto f = t.begin(); f != t.end(); ++f) {
            const auto under = out.constFind(f.key());
            const auto wasF = plan.was.constFind(f.key());
            const auto willF = plan.will.constFind(f.key());
            for (auto a = f->begin(); a != f->end(); ++a) {
                const double below = under != out.constEnd() && under->contains(a.key()) ? under->value(a.key())
                                                                                        : staticValue(f.key(), a.key());
                const double from = wasF != plan.was.constEnd() && wasF->contains(a.key()) ? wasF->value(a.key()) : below;
                const double to = willF != plan.will.constEnd() && willF->contains(a.key()) ? willF->value(a.key()) : below;
                // Effects take the fixture's individual times too (a spread's fade across a line).
                double q = pb.timing.isEmpty() ? pb.progressFor(pb.fadeTime, pb.delayTime) : pb.progressAt(f.key(), a.key());
                if (q > 0.0 && snaps(f.key(), a.key()))
                    q = 1.0;
                a.value() = from + (to - from) * q;
            }
        }
        return t;
    };
    const RawTable progFx = evaluate(m_show->programmerEffects(), QStringLiteral("p"));
    for (int i = 0; i < sources.size(); ++i)
        if (sources[i].level < int(SeqPriority::Ultra)) {
            sourceFx[i] = fxTable(i);
            merge(sourceFx[i], i, sources[i].master, fxOwner);
        }
    merge(progFx, -1, 1.0, fxOwner);
    for (int i = 0; i < sources.size(); ++i)
        if (sources[i].level == int(SeqPriority::Ultra)) {
            sourceFx[i] = fxTable(i);
            merge(sourceFx[i], i, sources[i].master, fxOwner);
        }

    // Off When Overridden: a sequence turns off once other sequences have taken everything it
    // has. A sequence with values is judged by its values (its effects go with it); one with
    // only effects by its effects, which only other effects take. Low sequences take things away
    // only from other Low sequences, HTP intensities never lose to each other, the programmer
    // never turns a sequence off, and nothing is taken until the taking cue has finished fading.
    QVector<Executor *> overridden;
    for (int i = 0; i < sources.size(); ++i) {
        const Source &src = sources[i];
        if (!src.seq || !src.seq->offWhenOverridden || src.held || src.lead->pb().goingOff)
            continue;
        const RawTable &values = src.lead->pb().current;
        const bool byValues = !values.isEmpty();
        const RawTable &mineTable = byValues ? values : sourceFx[i];
        const Owners &owners = byValues ? owner : fxOwner;
        int mine = 0, taken = 0;
        for (auto f = mineTable.begin(); f != mineTable.end() && !mine; ++f) {
            const QHash<QString, int> ow = owners.value(f.key());
            for (auto a = f->begin(); a != f->end(); ++a) {
                const auto w = ow.constFind(a.key());
                if (w == ow.constEnd())
                    continue;   // the fixture lacks it, or the programmer has it: doesn't count
                const bool htpPair = isIntensity(a.key()) && src.level == int(SeqPriority::HTP)
                                     && levelOf(*w) == int(SeqPriority::HTP);
                // Low sequences take things only from other Low ones; and a sequence still
                // fading in hasn't taken anything yet (the one it replaces plays on under it
                // until its fade is done, so nothing snaps off and back).
                const bool lowOverHigher = levelOf(*w) == int(SeqPriority::Low) && src.level != int(SeqPriority::Low);
                // A Temp fader takes nothing for good: what it covers plays on underneath.
                const bool byTemp = *w >= 0 && sources[*w].temp >= 0.0;
                if (*w == i || *w < 0 || htpPair || lowOverHigher || byTemp || sources[*w].lead->pb().fading) {
                    ++mine;
                    break;
                }
                ++taken;
            }
        }
        if (!mine && taken)
            overridden << src.lead;
    }

    // Forget phases of effects that stopped.
    for (auto it = m_phases.begin(); it != m_phases.end();) {
        if (!m_seenPhases.contains(it.key()))
            it = m_phases.erase(it);
        else
            ++it;
    }

    // 4. Highlight selected fixtures
    if (m_show->highlight()) {
        for (int fid : m_show->selection()) {
            const FixtureMode *m = m_show->fixtureMode(fid);
            if (!m)
                continue;
            RawAttr &attrs = out[fid];
            for (const ChannelDef &c : m->channels)   // a virtual Dimmer is a channel too
                if (c.hasHighlight)
                    attrs[c.attribute] = c.highlightValue;
        }
    }

    // 5. Parked attributes hold where they were, over everything else
    const RawTable &parked = m_show->parked();
    for (auto f = parked.begin(); f != parked.end(); ++f) {
        RawAttr &attrs = out[f.key()];
        for (auto a = f->begin(); a != f->end(); ++a)
            attrs[a.key()] = a.value();
    }

    m_outputChanged = out != m_merged;
    m_merged = out;
    m_show->setLastOutput(m_merged);
    for (Executor *e : overridden)
        m_show->off(*e, true);   // shares the data

    // 5. Render to DMX
    const double gm = m_show->blackout() ? 0.0 : m_show->grandMaster();
    QSet<int> touched;
    for (const Fixture *fp : fixtures) {
        const Fixture &f = *fp;
        const FixtureMode *m = m_show->fixtureMode(f.fid);
        if (!m || !f.patched())
            continue;
        QByteArray &uni = m_universes[f.universe];
        if (!touched.contains(f.universe)) {
            uni.fill(0, 512);
            touched.insert(f.universe);
        }
        const RawAttr &attrs = out[f.fid];
        const double dim = attrs.value("Dimmer", 0.0) * gm;
        for (const ChannelDef &c : m->channels) {
            if (c.isVirtual())
                continue;   // programmed and played back, but nothing on the wire
            double v = c.toDmx(attrs.value(c.attribute, c.defaultValue));   // pan and tilt: degrees to DMX
            if (c.isAngle() && (c.attribute.startsWith(QLatin1String("Pan")) ? f.invertPan : f.invertTilt))
                v = 1.0 - v;   // hung the other way round
            if (c.attribute == QLatin1String("Dimmer"))
                v = dim;
            else if (m->virtualDimmer && isColorAdd(c.attribute))
                v *= dim;
            v = std::clamp(v, 0.0, 1.0);
            const int base = f.address - 1;
            if (c.fine >= 0) {
                const int raw = int(std::lround(v * 65535.0));
                if (base + c.coarse < 512)
                    uni[base + c.coarse] = char(raw >> 8);
                if (base + c.fine < 512)
                    uni[base + c.fine] = char(raw & 0xff);
            } else if (base + c.coarse < 512) {
                uni[base + c.coarse] = char(std::lround(v * 255.0));
            }
        }
    }
    // Drop universes no fixture uses any more.
    for (auto it = m_universes.begin(); it != m_universes.end();) {
        if (!touched.contains(it.key()))
            it = m_universes.erase(it);
        else
            ++it;
    }

    if (m_outputs)
        m_outputs->send(m_universes);
}

} // namespace lumora
