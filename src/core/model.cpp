// SPDX-License-Identifier: GPL-3.0-or-later
#include "model.h"

#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace lumora {

QStringList numberedNames(const QString &name, int count)
{
    QString base = name.trimmed();
    QString sep = QStringLiteral(" ");
    int start = 1;
    static const QRegularExpression trailing(QStringLiteral("^(.*?)(\\s*)(\\d+)$"));
    const QRegularExpressionMatch m = trailing.match(base);
    if (m.hasMatch() && !m.captured(1).isEmpty()) {
        base = m.captured(1);
        sep = m.captured(2);
        start = m.captured(3).toInt();
    }
    QStringList out;
    for (int i = 0; i < count; ++i)
        out << base + sep + QString::number(start + i);
    return out;
}

// ---- Fixture -------------------------------------------------------------

QJsonObject Fixture::toJson() const
{
    QJsonObject o{{"fid", fid}, {"name", name}, {"type", typeId}, {"mode", mode},
                  {"universe", universe}, {"address", address}};
    if (invertPan)
        o["invertPan"] = true;
    if (invertTilt)
        o["invertTilt"] = true;
    return o;
}

Fixture Fixture::fromJson(const QJsonObject &o)
{
    Fixture f;
    f.fid = o["fid"].toInt();
    f.name = o["name"].toString();
    f.typeId = o["type"].toString();
    f.mode = o["mode"].toString();
    f.universe = o["universe"].toInt(1);
    f.address = o["address"].toInt(1);
    f.invertPan = o["invertPan"].toBool();
    f.invertTilt = o["invertTilt"].toBool();
    return f;
}

// ---- Group ---------------------------------------------------------------

QJsonObject Group::toJson() const
{
    QJsonArray a;
    for (int f : fixtures)
        a.append(f);
    QJsonObject o{{"id", id}, {"name", name}, {"fixtures", a}};
    if (!grid.isEmpty()) {
        QJsonArray g;
        for (const QPoint &p : grid)
            g.append(QJsonArray{p.x(), p.y()});
        o["grid"] = g;
    }
    return o;
}

Group Group::fromJson(const QJsonObject &o)
{
    Group g;
    g.id = o["id"].toInt();
    g.name = o["name"].toString();
    for (const QJsonValue &v : o["fixtures"].toArray())
        g.fixtures.append(v.toInt());
    for (const QJsonValue &v : o["grid"].toArray())
        g.grid << QPoint(v.toArray().at(0).toInt(), v.toArray().at(1).toInt());
    if (g.grid.size() != g.fixtures.size())
        g.grid.clear();
    return g;
}

// ---- Preset --------------------------------------------------------------

QString presetScopeName(PresetScope s)
{
    switch (s) {
    case PresetScope::Universal: return QStringLiteral("Universal");
    case PresetScope::Type: return QStringLiteral("Global");
    case PresetScope::Fixture: return QStringLiteral("Selective");
    }
    return QStringLiteral("Global");
}

PresetScope defaultPresetLayer(int presetType)
{
    return (presetType == int(Feature::Dimmer) || presetType == int(Feature::Color)) ? PresetScope::Universal
                                                                                      : PresetScope::Fixture;
}

bool Preset::hasLayer(PresetScope layer) const
{
    auto any = [](const auto &container) {
        for (const auto &v : container)
            if (!v.isEmpty())
                return true;
        return false;
    };
    switch (layer) {
    case PresetScope::Universal: return !universal.isEmpty();
    case PresetScope::Type: return any(perType);
    case PresetScope::Fixture: return any(perFixture);
    }
    return false;
}

RawAttr Preset::sampleValues() const
{
    if (!universal.isEmpty())
        return universal;
    for (const RawAttr &a : perType)
        if (!a.isEmpty())
            return a;
    for (const RawAttr &a : perFixture)
        if (!a.isEmpty())
            return a;
    return {};
}

QString Preset::layerBadge() const
{
    QStringList out;
    if (hasLayer(PresetScope::Universal))
        out << QStringLiteral("U");
    if (hasLayer(PresetScope::Type))
        out << QStringLiteral("G");
    if (hasLayer(PresetScope::Fixture))
        out << QStringLiteral("S");
    return out.join(QStringLiteral("·"));
}

PresetScope presetScopeFromName(const QString &raw, PresetScope fallback)
{
    const QString n = raw.trimmed().toLower();
    if (n == "universal" || n == "u")
        return PresetScope::Universal;
    if (n == "type" || n == "t" || n == "global" || n == "g")   // "global" is another name for it
        return PresetScope::Type;
    if (n == "fixture" || n == "f" || n == "selective" || n == "s")   // "selective" is another name for it
        return PresetScope::Fixture;
    return fallback;
}

bool Preset::lookup(int fid, const QString &typeId, const QString &attribute, double *out) const
{
    // Most specific first: this fixture, then its type, then anyone.
    auto f = perFixture.constFind(fid);
    if (f != perFixture.constEnd()) {
        auto a = f->constFind(attribute);
        if (a != f->constEnd()) {
            *out = a.value();
            return true;
        }
    }
    auto t = perType.constFind(typeId);
    if (t != perType.constEnd()) {
        auto a = t->constFind(attribute);
        if (a != t->constEnd()) {
            *out = a.value();
            return true;
        }
    }
    auto u = universal.constFind(attribute);
    if (u != universal.constEnd()) {
        *out = u.value();
        return true;
    }
    return false;
}

QStringList Preset::attributes() const
{
    QStringList out = universal.keys();
    auto addAll = [&out](const RawAttr &a) {
        for (auto it = a.begin(); it != a.end(); ++it)
            if (!out.contains(it.key()))
                out << it.key();
    };
    for (const RawAttr &a : perType)
        addAll(a);
    for (const RawAttr &a : perFixture)
        addAll(a);
    return out;
}

QJsonObject Preset::toJson() const
{
    QJsonObject t;
    for (auto it = perType.begin(); it != perType.end(); ++it)
        t[it.key()] = rawAttrToJson(it.value());
    QJsonArray fx;
    for (const EffectInstance &e : effects)
        fx.append(e.toJson());
    return {{"type", type}, {"id", id}, {"name", name}, {"layered", true},
            {"fixtures", rawTableToJson(perFixture)}, {"types", t}, {"universal", rawAttrToJson(universal)}, {"effects", fx}};
}

Preset Preset::fromJson(const QJsonObject &o)
{
    Preset p;
    p.type = o["type"].toInt();
    p.id = o["id"].toInt();
    p.name = o["name"].toString();
    p.perFixture = rawTableFromJson(o["fixtures"].toObject());
    const QJsonObject t = o["types"].toObject();
    for (auto it = t.begin(); it != t.end(); ++it)
        p.perType.insert(it.key(), rawAttrFromJson(it->toObject()));
    p.universal = rawAttrFromJson(o["universal"].toObject());
    for (const QJsonValue &v : o["effects"].toArray())
        p.effects.append(EffectInstance::fromJson(v.toObject()));
    // Before layering (0.5 and earlier) every store recorded all three representations and a
    // "scope" picked one. Keep just that one so old shows play back the same.
    if (!o["layered"].toBool()) {
        const PresetScope old = presetScopeFromName(o["scope"].toString(), PresetScope::Fixture);
        if (old != PresetScope::Universal)
            p.universal.clear();
        if (old != PresetScope::Type)
            p.perType.clear();
        if (old != PresetScope::Fixture)
            p.perFixture.clear();
    }
    return p;
}

// ---- Blueprint --------------------------------------------------------------

QJsonObject BlueprintLine::toJson() const
{
    QJsonArray ps;
    for (const PresetRef &r : presets)
        ps.append(QStringLiteral("%1.%2").arg(r.first).arg(r.second));
    QJsonArray fx;
    for (int e : effects)
        fx.append(e);
    QJsonObject o{{"name", name}, {"group", groupId}, {"presets", ps}, {"dimmer", dimmer},
                  {"every", every}, {"offset", offset}, {"effects", fx}};
    if (spreadId)
        o["spread"] = spreadId;
    auto times = [](const QVector<double> &t) {
        QJsonArray a;
        for (double v : t)
            a.append(v);
        return a;
    };
    if (!fadeTimes.isEmpty())
        o["fade"] = times(fadeTimes);
    if (!delayTimes.isEmpty())
        o["delay"] = times(delayTimes);
    return o;
}

BlueprintLine BlueprintLine::fromJson(const QJsonObject &o)
{
    BlueprintLine r;
    r.name = o["name"].toString();
    r.groupId = o["group"].toInt();
    for (const QJsonValue &v : o["presets"].toArray()) {
        QStringList parts = v.toString().split('.');
        if (parts.size() == 2)
            r.presets.append({parts[0].toInt(), parts[1].toInt()});
    }
    r.dimmer = o["dimmer"].toDouble(-1.0);
    r.every = std::max(1, o["every"].toInt(1));
    r.offset = o["offset"].toInt(0);
    r.spreadId = std::max(0, o["spread"].toInt(0));
    for (const QJsonValue &v : o["effects"].toArray())
        r.effects << v.toInt();
    for (const QJsonValue &v : o["fade"].toArray())
        r.fadeTimes << std::max(0.0, v.toDouble());
    for (const QJsonValue &v : o["delay"].toArray())
        r.delayTimes << std::max(0.0, v.toDouble());
    return r;
}

// ---- Cue / Sequence ------------------------------------------------------

QString Cue::displayNumber() const
{
    if (std::fabs(number - std::round(number)) < 1e-9)
        return QString::number(qint64(std::llround(number)));
    QString s = QString::number(number, 'f', 3);
    while (s.endsWith('0'))
        s.chop(1);
    return s;
}

QString cueTriggerName(CueTrigger t)
{
    switch (t) {
    case CueTrigger::Go: return QStringLiteral("Go");
    case CueTrigger::Wait: return QStringLiteral("Wait");
    case CueTrigger::Follow: return QStringLiteral("Follow");
    }
    return QStringLiteral("Go");
}

QString Cue::triggerText() const
{
    if (trigger == CueTrigger::Wait)
        return QStringLiteral("Wait %1").arg(wait);
    return cueTriggerName(trigger);
}

QJsonObject Cue::toJson() const
{
    QJsonArray rs;
    for (const BlueprintLine &r : blueprints)
        rs.append(r.toJson());
    QJsonArray fx;
    for (const EffectInstance &e : effects)
        fx.append(e.toJson());
    QJsonObject o{{"number", number}, {"name", name}, {"fade", fade}, {"delay", delay},
                  {"blueprints", rs}, {"values", valueTableToJson(values)}, {"effects", fx}};
    if (!notes.isEmpty())
        o["notes"] = notes;
    if (macro > 0)
        o["macro"] = macro;
    if (trigger != CueTrigger::Go)
        o["trigger"] = cueTriggerName(trigger);
    if (trigger == CueTrigger::Wait)
        o["wait"] = wait;
    return o;
}

Cue Cue::fromJson(const QJsonObject &o)
{
    Cue c;
    c.number = o["number"].toDouble(1);
    c.name = o["name"].toString();
    c.fade = o["fade"].toDouble(2.0);
    c.delay = o["delay"].toDouble(0.0);
    c.notes = o["notes"].toString();
    c.macro = std::max(0, o["macro"].toInt(0));
    const QString trig = o.value("trigger").toString();
    c.trigger = trig == QLatin1String("Wait") ? CueTrigger::Wait : trig == QLatin1String("Follow") ? CueTrigger::Follow : CueTrigger::Go;
    c.wait = std::max(0.0, o.value("wait").toDouble(0.0));
    // Shows saved before blueprints were called that keep them under "recipes".
    for (const QJsonValue &v : (o.contains("blueprints") ? o["blueprints"] : o["recipes"]).toArray())
        c.blueprints.append(BlueprintLine::fromJson(v.toObject()));
    c.values = valueTableFromJson(o["values"].toObject());
    for (const QJsonValue &v : o["effects"].toArray())
        c.effects.append(EffectInstance::fromJson(v.toObject()));
    return c;
}

int Sequence::indexOf(double n) const
{
    for (int i = 0; i < cues.size(); ++i)
        if (std::fabs(cues[i].number - n) < 1e-6)
            return i;
    return -1;
}

void Sequence::insertSorted(const Cue &cue)
{
    int existing = indexOf(cue.number);
    if (existing >= 0) {
        cues[existing] = cue;
        return;
    }
    auto it = std::lower_bound(cues.begin(), cues.end(), cue.number,
                               [](const Cue &c, double n) { return c.number < n; });
    cues.insert(it, cue);
}

QString seqPriorityName(SeqPriority p)
{
    switch (p) {
    case SeqPriority::Low: return QStringLiteral("Low");
    case SeqPriority::LTP: return QStringLiteral("LTP");
    case SeqPriority::HTP: return QStringLiteral("HTP");
    case SeqPriority::Ultra: return QStringLiteral("Ultra");
    }
    return QStringLiteral("LTP");
}

SeqPriority seqPriorityFromName(const QString &n)
{
    for (SeqPriority p : {SeqPriority::Low, SeqPriority::LTP, SeqPriority::HTP, SeqPriority::Ultra})
        if (seqPriorityName(p).compare(n, Qt::CaseInsensitive) == 0)
            return p;
    return SeqPriority::LTP;
}

QJsonObject Sequence::toJson() const
{
    QJsonArray cs;
    for (const Cue &c : cues)
        cs.append(c.toJson());
    QJsonObject o{{"id", id}, {"name", name}, {"tracking", tracking}, {"wrap", wrap}, {"cues", cs},
                  {"priority", seqPriorityName(priority)}, {"offWhenOverridden", offWhenOverridden}};
    if (!color.isEmpty())
        o["color"] = color;
    if (offFade > 0)
        o["offFade"] = offFade;
    if (offDelay > 0)
        o["offDelay"] = offDelay;
    if (offTrigger != CueTrigger::Go)
        o["offTrigger"] = cueTriggerName(offTrigger);
    if (offTrigger == CueTrigger::Wait)
        o["offWait"] = offWait;
    return o;
}

Sequence Sequence::fromJson(const QJsonObject &o)
{
    Sequence s;
    s.id = o["id"].toInt();
    s.name = o["name"].toString();
    s.tracking = o["tracking"].toBool(true);
    s.wrap = o["wrap"].toBool(true);
    s.priority = seqPriorityFromName(o.value("priority").toString());
    s.offWhenOverridden = o.value("offWhenOverridden").toBool(true);
    s.color = o.value("color").toString();
    s.offFade = std::max(0.0, o["offFade"].toDouble(0.0));
    s.offDelay = std::max(0.0, o["offDelay"].toDouble(0.0));
    const QString ot = o.value("offTrigger").toString();
    s.offTrigger = ot == QLatin1String("Wait") ? CueTrigger::Wait : ot == QLatin1String("Follow") ? CueTrigger::Follow : CueTrigger::Go;
    s.offWait = std::max(0.0, o.value("offWait").toDouble(0.0));
    for (const QJsonValue &v : o["cues"].toArray())
        s.cues.append(Cue::fromJson(v.toObject()));
    std::sort(s.cues.begin(), s.cues.end(), [](const Cue &a, const Cue &b) { return a.number < b.number; });
    return s;
}

// ---- Executor ------------------------------------------------------------

namespace {
const QList<QPair<ButtonFn, QString>> &buttonNames()
{
    static const QList<QPair<ButtonFn, QString>> names = {
        {ButtonFn::None, "None"}, {ButtonFn::Go, "Go"}, {ButtonFn::GoBack, "GoBack"},
        {ButtonFn::Pause, "Pause"}, {ButtonFn::Flash, "Flash"}, {ButtonFn::Temp, "Temp"},
        {ButtonFn::Toggle, "Toggle"}, {ButtonFn::On, "On"}, {ButtonFn::Off, "Off"},
        {ButtonFn::Swop, "Swop"}, {ButtonFn::Select, "Select"}, {ButtonFn::Top, "Top"},
        {ButtonFn::Goto, "Goto"},
    };
    return names;
}
} // namespace

QString buttonFnName(ButtonFn f)
{
    for (const auto &p : buttonNames())
        if (p.first == f)
            return p.second;
    return "None";
}

ButtonFn buttonFnFromName(const QString &n)
{
    for (const auto &p : buttonNames())
        if (p.second.compare(n, Qt::CaseInsensitive) == 0)
            return p.first;
    return ButtonFn::None;
}

QList<ButtonFn> allButtonFns()
{
    QList<ButtonFn> out;
    for (const auto &p : buttonNames())
        out << p.first;
    return out;
}

QString faderFnName(FaderFn f)
{
    switch (f) {
    case FaderFn::Master: return "Master";
    case FaderFn::XFade: return "XFade";
    case FaderFn::Temp: return "Temp";
    }
    return "Master";
}

FaderFn faderFnFromName(const QString &n)
{
    if (n.compare("XFade", Qt::CaseInsensitive) == 0)
        return FaderFn::XFade;
    if (n.compare("Temp", Qt::CaseInsensitive) == 0)
        return FaderFn::Temp;
    return FaderFn::Master;
}

bool executorButtonLit(const Executor &e, int i)
{
    if (i < 0 || i > 2)
        return false;
    switch (e.buttons[i]) {
    case ButtonFn::None: return false;
    case ButtonFn::Pause: return e.pb().paused;
    case ButtonFn::Flash: return e.flashHeld;
    case ButtonFn::Temp: return e.tempHeld;
    case ButtonFn::Swop: return e.swopHeld;
    case ButtonFn::Toggle:
    case ButtonFn::On: return e.pb().isOn();
    default: return e.buttonDown[i];
    }
}

QString execKindName(ExecKind k)
{
    switch (k) {
    case ExecKind::Fader: return "Fader";
    case ExecKind::Button: return "Button";
    case ExecKind::Encoder: return "Encoder";
    }
    return "Fader";
}

ExecKind execKindFromName(const QString &n)
{
    if (n.compare("Button", Qt::CaseInsensitive) == 0)
        return ExecKind::Button;
    if (n.compare("Encoder", Qt::CaseInsensitive) == 0)
        return ExecKind::Encoder;
    return ExecKind::Fader;
}

ExecKind executorKind(int number, const Executor *e)
{
    if (number >= 100 && number < 200)
        return ExecKind::Button;
    if (number >= 300 && number < 400)
        return ExecKind::Encoder;
    if (executorKindAssignable(number))
        return e ? e->kind : ExecKind::Fader;
    return ExecKind::Fader;
}

double PlaybackState::clock() const
{
    return xfadeTarget >= 0 ? std::clamp(elapsed, 0.0, 1.0) * std::max(duration, 1e-6) : elapsed;
}

double PlaybackState::progressFor(double fade, double delay) const
{
    if (!fading)
        return 1.0;
    const double t = clock() - delay;
    if (t <= 0)
        return 0.0;
    if (fade <= 1e-6)
        return 1.0;
    return std::min(1.0, t / fade);
}

double PlaybackState::progress() const
{
    if (!fading)
        return 1.0;
    if (timing.isEmpty())
        return progressFor(fadeTime, delayTime);
    if (duration <= 1e-6)
        return 1.0;
    return std::min(1.0, clock() / duration);
}

double PlaybackState::progressAt(int fid, const QString &attribute) const
{
    if (!fading)
        return 1.0;
    double fade = fadeTime, delay = delayTime;
    const auto f = timing.constFind(fid);
    if (f != timing.constEnd()) {
        auto a = f->constFind(attribute);
        if (a == f->constEnd())
            a = f->constFind(QStringLiteral("*"));
        if (a != f->constEnd()) {
            if (a->fade >= 0)
                fade = a->fade;
            if (a->delay >= 0)
                delay = a->delay;
        }
    }
    return progressFor(fade, delay);
}

QJsonObject Executor::toJson() const
{
    QJsonObject o{{"page", page}, {"number", number}, {"sequence", sequenceId},
                  {"buttons", QJsonArray{buttonFnName(buttons[0]), buttonFnName(buttons[1]), buttonFnName(buttons[2])}},
                  {"fader", faderFnName(faderFn)}, {"level", fader},
                  {"autoStart", autoStart}, {"autoStop", autoStop}, {"kind", execKindName(kind)}};
    if (rateMaster)
        o["rateMaster"] = rateMaster;
    return o;
}

Executor Executor::fromJson(const QJsonObject &o)
{
    Executor e;
    e.page = o["page"].toInt(1);
    e.number = o["number"].toInt(201);
    e.sequenceId = o["sequence"].toInt();
    e.rateMaster = e.sequenceId ? 0 : std::clamp(o["rateMaster"].toInt(0), 0, 10);
    QJsonArray b = o["buttons"].toArray();
    for (int i = 0; i < 3 && i < b.size(); ++i)
        e.buttons[i] = buttonFnFromName(b[i].toString());
    e.faderFn = faderFnFromName(o["fader"].toString());
    e.fader = o["level"].toDouble(1.0);
    e.autoStart = o["autoStart"].toBool(true);
    e.autoStop = o["autoStop"].toBool(true);
    e.kind = execKindFromName(o["kind"].toString());
    return e;
}

// ---- Tables --------------------------------------------------------------

QJsonObject valueTableToJson(const ValueTable &t)
{
    QJsonObject out;
    for (auto f = t.begin(); f != t.end(); ++f) {
        QJsonObject attrs;
        for (auto a = f->begin(); a != f->end(); ++a) {
            if (a->isPreset() || a->hasTiming()) {
                QJsonObject vo{{"v", a->value}};
                if (a->isPreset())
                    vo["p"] = QStringLiteral("%1.%2").arg(a->presetType).arg(a->presetId);
                if (a->fade >= 0)
                    vo["f"] = a->fade;
                if (a->delay >= 0)
                    vo["d"] = a->delay;
                attrs[a.key()] = vo;
            } else {
                attrs[a.key()] = a->value;
            }
        }
        out[QString::number(f.key())] = attrs;
    }
    return out;
}

ValueTable valueTableFromJson(const QJsonObject &o)
{
    ValueTable t;
    for (auto f = o.begin(); f != o.end(); ++f) {
        AttrValues attrs;
        QJsonObject ao = f->toObject();
        for (auto a = ao.begin(); a != ao.end(); ++a) {
            ValueRef r;
            if (a->isObject()) {
                QJsonObject vo = a->toObject();
                r.value = vo["v"].toDouble();
                QStringList p = vo["p"].toString().split('.');
                if (p.size() == 2) {
                    r.presetType = p[0].toInt();
                    r.presetId = p[1].toInt();
                }
                r.fade = vo.contains("f") ? std::max(0.0, vo["f"].toDouble()) : -1.0;
                r.delay = vo.contains("d") ? std::max(0.0, vo["d"].toDouble()) : -1.0;
            } else {
                r.value = a->toDouble();
            }
            attrs.insert(a.key(), r);
        }
        t.insert(f.key().toInt(), attrs);
    }
    return t;
}

QJsonObject rawAttrToJson(const RawAttr &a)
{
    QJsonObject out;
    for (auto it = a.begin(); it != a.end(); ++it)
        out[it.key()] = it.value();
    return out;
}

RawAttr rawAttrFromJson(const QJsonObject &o)
{
    RawAttr a;
    for (auto it = o.begin(); it != o.end(); ++it)
        a.insert(it.key(), it->toDouble());
    return a;
}

QJsonObject rawTableToJson(const RawTable &t)
{
    QJsonObject out;
    for (auto f = t.begin(); f != t.end(); ++f)
        out[QString::number(f.key())] = rawAttrToJson(f.value());
    return out;
}

RawTable rawTableFromJson(const QJsonObject &o)
{
    RawTable t;
    for (auto f = o.begin(); f != o.end(); ++f)
        t.insert(f.key().toInt(), rawAttrFromJson(f->toObject()));
    return t;
}

} // namespace lumora
