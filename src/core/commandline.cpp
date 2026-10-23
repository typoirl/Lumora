// SPDX-License-Identifier: GPL-3.0-or-later
#include "commandline.h"

#include "parameters.h"
#include "show.h"

#include <QHash>
#include <QObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <optional>

namespace lumora {

namespace {

struct Token {
    enum Kind { Word, Number, String, Plus, Minus, Option, End };
    Kind kind = End;
    QString text;     // canonical keyword for words, raw text otherwise
    double number = 0;
};

QString canonical(const QString &w)
{
    static const QHash<QString, QString> map = {
        {"fixture", "fixture"}, {"fix", "fixture"}, {"f", "fixture"}, {"channel", "fixture"}, {"ch", "fixture"},
        {"group", "group"}, {"g", "group"},
        {"thru", "thru"}, {"t", "thru"}, {"through", "thru"},
        {"at", "at"}, {"@", "at"}, {"full", "full"}, {"zero", "zero"},
        {"preset", "preset"}, {"p", "preset"},
        {"store", "store"}, {"st", "store"}, {"record", "store"},
        {"delete", "delete"}, {"del", "delete"},
        {"label", "label"}, {"lab", "label"}, {"name", "label"},
        {"sequence", "sequence"}, {"seq", "sequence"}, {"s", "sequence"},
        {"cue", "cue"}, {"q", "cue"},
        {"executor", "executor"}, {"exec", "executor"}, {"e", "executor"}, {"exe", "executor"},
        {"go", "go"}, {"go+", "go"}, {"goback", "goback"}, {"go-", "goback"}, {"back", "goback"},
        {"goto", "goto"}, {"load", "load"}, {"saveshow", "saveshow"}, {"off", "off"}, {"on", "on"}, {"pause", "pause"},
        {"assign", "assign"}, {"as", "assign"}, {"page", "page"}, {"build", "build"}, {"blueprint", "blueprint"}, {"bp", "blueprint"},
        {"fade", "fade"}, {"delay", "delay"}, {"time", "time"}, {"every", "every"}, {"interleave", "every"}, {"offset", "offset"},
        {"highlight", "highlight"}, {"hl", "highlight"}, {"progfade", "progfade"}, {"programmerfade", "progfade"}, {"nexty", "nexty"}, {"grid", "grid"}, {"gri", "grid"}, {"sendosc", "sendosc"}, {"x", "x"}, {"y", "y"}, {"blackout", "blackout"}, {"bo", "blackout"},
        {"clear", "clear"}, {"clr", "clear"}, {"clearall", "clearall"},
        {"attribute", "attribute"}, {"attr", "attribute"},
        {"update", "update"}, {"upd", "update"}, {"remove", "remove"}, {"rem", "remove"},
        {"copy", "copy"}, {"move", "move"},
        {"note", "note"}, {"notes", "note"},
        {"default", "default"}, {"defaults", "default"}, {"def", "default"}, {"home", "default"}, {"all", "all"},
        {"view", "view"}, {"vw", "view"},
        {"button", "button"}, {"encoder", "encoder"}, {"fader", "fader"}, {"top", "top"},
        {"cell", "cell"}, {"pixel", "cell"}, {"flip", "flip"},
        {"spread", "spread"}, {"spreads", "spread"}, {"spr", "spread"}, {"blocks", "blocks"}, {"groups", "groups"},
        {"wings", "wings"}, {"phase", "phase"}, {"direction", "direction"},
        {"effect", "effect"}, {"fx", "effect"}, {"rate", "rate"}, {"size", "size"},
        {"ratemaster", "ratemaster"}, {"rm", "ratemaster"}, {"speedmaster", "ratemaster"},
        {"park", "park"}, {"unpark", "unpark"}, {"next", "next"}, {"prev", "prev"}, {"previous", "prev"},
        {"last", "last"}, {"follow", "follow"}, {"wait", "wait"}, {"trigger", "trigger"},
        {"plot", "plot"}, {"plots", "plot"}, {"image", "image"}, {"images", "image"}, {"img", "image"}, {"macro", "macro"}, {"mac", "macro"}, {"ma", "macro"}, {"appearance", "appearance"}, {"app", "appearance"},
    };
    return map.value(w.toLower());
}

QVector<Token> tokenize(const QString &line, QString *error)
{
    QVector<Token> out;
    int i = 0;
    const int n = line.size();
    while (i < n) {
        const QChar c = line[i];
        if (c.isSpace()) {
            ++i;
            continue;
        }
        if (c == '"') {
            int j = line.indexOf('"', i + 1);
            if (j < 0) {
                *error = QObject::tr("Missing closing quote");
                return {};
            }
            out.append({Token::String, line.mid(i + 1, j - i - 1), 0});
            i = j + 1;
            continue;
        }
        if (c == '+') { out.append({Token::Plus, "+", 0}); ++i; continue; }
        if (c == '-') { out.append({Token::Minus, "-", 0}); ++i; continue; }
        if (c == '@') { out.append({Token::Word, "at", 0}); ++i; continue; }
        if (c == '/') {
            int j = i + 1;
            while (j < n && line[j].isLetter())
                ++j;
            out.append({Token::Option, line.mid(i + 1, j - i - 1).toLower(), 0});
            i = j;
            continue;
        }
        if (c.isDigit() || c == '.') {
            int j = i;
            while (j < n && (line[j].isDigit() || line[j] == '.'))
                ++j;
            const QString t = line.mid(i, j - i);
            bool ok = false;
            double v = t.toDouble(&ok);
            if (!ok && (t.count('.') < 1 || t.count('.') > 2)) {   // 101.1.2: a subfixture
                *error = QObject::tr("Bad number '%1'").arg(t);
                return {};
            }
            out.append({Token::Number, t, v});
            i = j;
            continue;
        }
        if (c.isLetter()) {
            int j = i;
            while (j < n && (line[j].isLetterOrNumber() || line[j] == '_'))
                ++j;
            QString w = line.mid(i, j - i);
            // "Color.1", "Position.12": a preset written by pool name. Becomes Preset 4.1.
            if (j + 1 < n && line[j] == '.' && line[j + 1].isDigit()) {
                if (const int type = presetTypeFromName(w)) {
                    int k = j + 1;
                    while (k < n && line[k].isDigit())
                        ++k;
                    const QString id = line.mid(j + 1, k - j - 1);
                    if (out.isEmpty() || out.last().kind != Token::Word || out.last().text != QLatin1String("preset"))
                        out.append({Token::Word, "preset", 0});
                    const QString ref = QStringLiteral("%1.%2").arg(type).arg(id);
                    out.append({Token::Number, ref, ref.toDouble()});
                    i = k;
                    continue;
                }
            }
            // "Go+" / "Go-"
            if (w.compare("go", Qt::CaseInsensitive) == 0 && j < n && (line[j] == '+' || line[j] == '-')) {
                w += line[j];
                ++j;
            }
            const QString k = canonical(w);
            out.append({Token::Word, k.isEmpty() ? w : k, 0});
            i = j;
            continue;
        }
        *error = QObject::tr("Unexpected character '%1'").arg(c);
        return {};
    }
    return out;
}

class Parser {
public:
    Parser(Show *show, QVector<Token> tokens) : s(show), t(std::move(tokens)) {}

    CommandResult run();

private:
    // ---- token helpers ----
    const Token &peek(int ahead = 0) const
    {
        static const Token end;
        return pos + ahead < t.size() ? t[pos + ahead] : end;
    }
    bool atEnd() const { return pos >= t.size(); }
    bool atEndAfter(int n) const { return pos + n >= t.size(); }
    bool isWord(const char *w, int ahead = 0) const
    {
        const Token &k = peek(ahead);
        return k.kind == Token::Word && k.text == QLatin1String(w);
    }
    bool accept(const char *w)
    {
        if (isWord(w)) {
            ++pos;
            return true;
        }
        return false;
    }
    bool acceptKind(Token::Kind k)
    {
        if (peek().kind == k) {
            ++pos;
            return true;
        }
        return false;
    }
    std::optional<double> number()
    {
        if (peek().kind == Token::Number)
            return t[pos++].number;
        return std::nullopt;
    }
    std::optional<QString> numberText()
    {
        if (peek().kind == Token::Number)
            return t[pos++].text;
        return std::nullopt;
    }
    std::optional<QString> string()
    {
        if (peek().kind == Token::String)
            return t[pos++].text;
        return std::nullopt;
    }
    QStringList options()
    {
        QStringList o;
        while (peek().kind == Token::Option)
            o << t[pos++].text;
        return o;
    }
    static Show::StoreMode storeMode(const QStringList &opts)
    {
        for (const QString &raw : opts) {
            const QString o = raw.toLower();
            if (o == "overwrite" || o == "o")
                return Show::StoreMode::Overwrite;
            if (o == "remove" || o == "rem" || o == "r")
                return Show::StoreMode::Remove;
        }
        return Show::StoreMode::Merge;
    }
    // Did the options say what to do with something that exists?
    static bool storeChosen(const QStringList &opts)
    {
        static const QStringList words = {"overwrite", "o", "merge", "m", "remove", "rem", "r", "new", "n",
                                          "global", "g", "selective", "s", "universal", "u", "type", "t", "fixture", "f"};
        for (const QString &o : opts)
            if (words.contains(o.toLower()))
                return true;
        return false;
    }
    CommandResult ask(const QString &question, const QVector<QPair<QString, QString>> &choices)
    {
        QStringList opts;
        for (const auto &c : choices)
            opts << '/' + c.first;
        CommandResult r{false, QObject::tr("%1: add %2").arg(question, opts.join(QStringLiteral(", ")))};
        r.question = question;
        r.choices = choices;
        return r;
    }
    // The answers, always in this order: Overwrite, Merge (or its kinds), Add cue, Remove.
    static QVector<QPair<QString, QString>> storeChoices(bool preset, double newCue = -1)
    {
        QVector<QPair<QString, QString>> c{{"overwrite", QObject::tr("Overwrite")}};
        if (preset)
            c << QPair<QString, QString>{"global", QObject::tr("Merge global")} << QPair<QString, QString>{"selective", QObject::tr("Merge selective")};
        else
            c << QPair<QString, QString>{"merge", QObject::tr("Merge")};
        if (newCue >= 0) {
            Cue tmp;
            tmp.number = newCue;
            c << QPair<QString, QString>{"new", QObject::tr("Add Cue %1").arg(tmp.displayNumber())};
        }
        c << QPair<QString, QString>{"remove", QObject::tr("Remove")};
        return c;
    }
    CommandResult fail(const QString &m) { return {false, m}; }
    CommandResult ok(const QString &m) { return {true, m}; }

    // ---- grammar pieces ----
    bool presetRef(int *type, int *id)
    {
        auto txt = numberText();
        if (!txt)
            return false;
        const QStringList parts = txt->split('.');
        if (parts.size() != 2)
            return false;
        *type = parts[0].toInt();
        *id = parts[1].toInt();
        return *type >= 1 && *type <= kPresetTypeAll && *id > 0;
    }
    bool executorRef(int *page, int *number)
    {
        auto txt = numberText();
        if (!txt)
            return false;
        const QStringList parts = txt->split('.');
        if (parts.size() == 2) {
            *page = parts[0].toInt();
            *number = parts[1].toInt();
        } else {
            *page = s->currentPage();
            *number = parts[0].toInt();
        }
        return *page > 0 && *number > 0;
    }
    // Optional "Sequence n" anywhere around a cue reference.
    void optionalSequence(int *seq)
    {
        if (accept("sequence"))
            if (auto n = number())
                *seq = int(*n);
    }
    QVector<int> idList(bool *ok)
    {
        // n [Thru m] { (+|-) [Keyword] n [Thru m] } ; returns signed ids (negative = remove)
        QVector<int> out;
        int sign = 1;
        *ok = false;
        while (true) {
            auto a = number();
            if (!a)
                break;
            int from = int(*a), to = from;
            if (accept("thru")) {
                auto b = number();
                if (!b)
                    return out;
                to = int(*b);
            }
            const int step = from <= to ? 1 : -1;
            for (int v = from;; v += step) {
                out.append(sign * v);
                if (v == to)
                    break;
            }
            *ok = true;
            if (acceptKind(Token::Plus))
                sign = 1;
            else if (acceptKind(Token::Minus))
                sign = -1;
            else
                break;
            accept("fixture");
            accept("group");
        }
        return out;
    }

    // Fixture IDs, subfixtures included: 101, 101.3, 101.1.2, 101 Cell 3, 101.1 Thru 101.8 (or
    // Thru 8), 101 Cell 1 Thru 4, 101 Thru 104 Cell 2, with + and - like idList. Signed:
    // negative = remove.
    QVector<int> fixtureList(bool *ok)
    {
        QVector<int> out;
        int sign = 1;
        *ok = false;
        // "101.1" Thru "8" means 101.8: a bare number replaces the last part.
        auto endText = [](const QString &start, const QString &end) {
            return start.contains('.') && !end.contains('.') ? start.section('.', 0, -2) + '.' + end : end;
        };
        while (true) {
            auto a = numberText();
            if (!a)
                break;
            const int from = s->parseId(*a);
            if (!from)
                return out;
            QVector<int> ids{from};
            if (accept("thru")) {
                auto b = numberText();
                const int to = b ? s->parseId(endText(*a, *b)) : 0;
                if (!to)
                    return out;
                ids = fixtureRange(from, to);
            }
            if (accept("cell")) {
                auto c1 = numberText();
                if (!c1)
                    return out;
                std::optional<QString> c2;
                if (accept("thru")) {
                    c2 = numberText();
                    if (!c2)
                        return out;
                }
                QVector<int> cells;
                for (int id : ids) {
                    const QString base = s->idText(id);
                    const int start = s->parseId(base + '.' + *c1);
                    const int end = c2 ? s->parseId(base + '.' + endText(*c1, *c2)) : start;
                    if (!start || !end)
                        continue;
                    for (int c : fixtureRange(start, end))
                        if (!cells.contains(c))
                            cells << c;
                }
                ids = cells;
            }
            for (int id : ids)
                out.append(sign * id);
            *ok = true;
            if (acceptKind(Token::Plus))
                sign = 1;
            else if (acceptKind(Token::Minus))
                sign = -1;
            else
                break;
            accept("fixture");
        }
        return out;
    }
    int depthOf(int fid) const
    {
        const Fixture *f = s->fixture(fid);
        return f ? f->depth() : (isCellId(fid) ? 1 : 0);
    }
    // Everything from one fixture or subfixture to another, in order (backwards when from > to).
    // Between subfixtures: those at the same level (heads, or their subs).
    QVector<int> fixtureRange(int from, int to) const
    {
        const bool back = fixtureSortKey(from) > fixtureSortKey(to);
        if (back)
            std::swap(from, to);
        QVector<int> out;
        if (!isCellId(from) && !isCellId(to)) {
            for (int v = from; v <= to; ++v)
                out << v;
        } else if (masterOf(from) == masterOf(to) && s->cellsOf(masterOf(from)).isEmpty()) {
            for (int c = cellOf(from); c <= cellOf(to); ++c)   // not patched yet: by slot
                out << cellId(masterOf(from), c);
        } else {
            const int depth = depthOf(isCellId(from) ? from : to);
            const qint64 lo = fixtureSortKey(from), hi = fixtureSortKey(to);
            // 101.1.1 Thru 101.4.1: the same part of each head (every ring), not all in between.
            const Fixture *a = s->fixture(from), *b = s->fixture(to);
            const int samePart = a && b && depth == 2 && a->sub == b->sub && a->head != b->head ? a->sub : 0;
            for (int id : s->fixtureIds()) {
                const qint64 k = fixtureSortKey(id);
                if (k < lo || k > hi)
                    continue;
                // Subfixtures at that level, and ordinary fixtures in between.
                const Fixture *f = s->fixture(id);
                if (samePart && f && f->sub != samePart)
                    continue;
                if (depthOf(id) == depth || (!isCellId(id) && s->cellsOf(id).isEmpty()))
                    out << id;
            }
        }
        if (back)
            std::reverse(out.begin(), out.end());
        return out;
    }

    struct Value {
        bool preset = false;
        double v = 0;
        int type = 0, id = 0;
        bool typed = false;   // a number: v is a percentage; raw is as typed (degrees for pan and tilt)
        double raw = 0;
        // "+10", "+-10": up or down from where each fixture is now, in the channel's own values.
        bool relative = false;
        double delta = 0;
    };
    bool value(Value *out)
    {
        // "At At" means At Full.
        if (accept("full") || accept("at")) {
            out->v = 1.0;
            return true;
        }
        if (accept("zero")) {
            out->v = 0.0;
            return true;
        }
        if (accept("preset")) {
            out->preset = true;
            return presetRef(&out->type, &out->id);
        }
        if (acceptKind(Token::Plus)) {   // +10 up, +-10 down
            const bool down = acceptKind(Token::Minus);
            auto n = number();
            if (!n)
                return false;
            out->relative = true;
            out->typed = true;
            out->delta = down ? -*n : *n;
            out->raw = out->delta;
            return true;
        }
        const bool negative = acceptKind(Token::Minus);   // -45 for pan and tilt
        if (auto n = number()) {
            out->typed = true;
            out->raw = negative ? -*n : *n;
            out->v = std::clamp(out->raw / 100.0, 0.0, 1.0);
            return true;
        }
        return false;
    }
    CommandResult applyValue(const Value &v, const QString &attribute = QStringLiteral("Dimmer"))
    {
        if (s->selection().isEmpty())
            return fail(QObject::tr("No fixtures selected"));
        if (v.preset) {
            if (!s->preset(v.type, v.id))
                return fail(QObject::tr("Preset %1 doesn't exist").arg(lumora::presetRef(v.type, v.id)));
            s->applyPreset(v.type, v.id);
            return ok(QObject::tr("Preset %1 applied").arg(lumora::presetRef(v.type, v.id)));
        }
        // A typed number is in the channel's own values: % for a dimmer, degrees for pan and
        // tilt, 30 for a zoom whose range is 5 to 50. Full and Zero are the ends of the range.
        int n = 0;
        QString shown;
        for (int fid : s->workingSelection()) {
            const ChannelDef *c = s->channelOf(fid, attribute);
            if (!c)
                continue;
            double value;
            if (v.relative) {
                // From where it is: the programmer's value, else what's on stage, else its rest.
                double cur = s->defaultValue(fid, attribute);
                const auto prog = s->programmerValues().constFind(fid);
                if (prog != s->programmerValues().constEnd() && prog->contains(attribute))
                    cur = s->resolveValue(fid, attribute, prog->value(attribute));
                else if (const auto out = s->lastOutput().constFind(fid); out != s->lastOutput().constEnd() && out->contains(attribute))
                    cur = out->value(attribute);
                value = c->isAngle() ? std::clamp(cur + v.delta, c->minValue(), c->maxValue())
                                     : std::clamp(c->fromPhysical(c->toPhysical(cur) + v.delta), 0.0, 1.0);
            } else if (v.typed)
                value = c->isAngle() ? v.raw : std::clamp(c->fromPhysical(v.raw), 0.0, 1.0);
            else
                value = c->isAngle() ? c->fromDmx(c->dmxFrom + (c->dmxTo - c->dmxFrom) * v.v) : v.v;
            s->setAttribute(fid, attribute, value);
            if (!n++)
                shown = c->valueText(s->programmerValues().value(fid).value(attribute).value, 1);
        }
        if (!n)
            return ok(QObject::tr("The selected fixtures have no %1").arg(attributeLabel(attribute)));
        if (v.relative)
            return ok(QObject::tr("%1 %2%3%4 (%5)").arg(attributeLabel(attribute), v.delta >= 0 ? QStringLiteral("+") : QString())
                          .arg(v.delta).arg(isAngle(attribute) ? QStringLiteral("°") : QString(), countOf(n, QObject::tr("fixture"), QObject::tr("fixtures"))));
        if (v.typed)
            shown = QString::number(v.raw) + (isAngle(attribute) ? QStringLiteral("°") : QString());
        else
            shown = v.v >= 1.0 ? QObject::tr("full") : v.v <= 0.0 ? QObject::tr("zero") : shown;
        return ok(QObject::tr("%1 at %2").arg(attributeLabel(attribute), shown));
    }
    // A feature by name: Dimmer (or Intensity), Position, Gobo, Color, Beam, Focus, Control.
    static int featureWord(const QString &w)
    {
        static const QHash<QString, int> words = {
            {"dimmer", 1}, {"intensity", 1}, {"int", 1}, {"position", 2}, {"pos", 2}, {"gobo", 3},
            {"color", 4}, {"colour", 4}, {"beam", 5}, {"focus", 6}, {"control", 7},
        };
        return words.value(w.toLower());
    }
    // An attribute by name, as typed: Pan, tilt, Zoom, Red, Gobo1, Dimmer… The selection's
    // attributes first, then any Lumora knows. Empty when it isn't one.
    QString attributeWord(const QString &word) const
    {
        const QString lw = word.toLower();
        if (!canonical(lw).isEmpty())   // a command word (Effect, Rate, Size…) stays one
            return QString();
        if (lw == QLatin1String("dimmer") || lw == QLatin1String("intensity") || lw == QLatin1String("int"))
            return QStringLiteral("Dimmer");
        QStringList candidates;
        for (int fid : s->selection())
            for (const QString &a : s->fixtureAttributes(fid))
                if (!candidates.contains(a))
                    candidates << a;
        for (const ParameterInfo &pi : parameterLibrary())
            if (!candidates.contains(pi.attribute))
                candidates << pi.attribute;
        auto matches = [&](const QString &a) {
            const QString al = a.toLower();
            if (al == lw || al == lw + QLatin1Char('1'))   // Focus: Focus1, Gobo: Gobo1
                return true;
            if (a.startsWith(QLatin1String("ColorAdd_")) && al.mid(9) == lw)
                return true;
            const QString label = attributeLabel(a).toLower();
            return label == lw || QString(label).remove(QLatin1Char(' ')) == lw;
        };
        for (const QString &a : candidates)
            if (matches(a))
                return a;
        return QString();
    }
    // "Pan 30", "Pan At 30", "Pan +10", "Tilt +-10": an attribute word followed by a value, on the
    // selection. Nothing when the next words aren't that.
    std::optional<CommandResult> attributeValue()
    {
        if (peek().kind != Token::Word)
            return std::nullopt;
        const Token::Kind next = peek(1).kind;
        if (next != Token::Plus && next != Token::Minus && next != Token::Number && !isWord("at", 1))
            return std::nullopt;
        const QString attr = attributeWord(peek().text);
        if (attr.isEmpty())
            return std::nullopt;
        ++pos;
        accept("at");
        if (isTimingWord())
            return timingFor(attr);
        Value v;
        if (!value(&v) || v.preset)
            return fail(QObject::tr("Expected a value: %1 50, %1 +10 or %1 +-10").arg(attributeLabel(attr)));
        return withTiming(applyValue(v, attr), v, attr);
    }
    // Optional "Attribute Pan" or a feature name ("Intensity", "Position").
    void attributeFilter(QString *attribute, int *feature)
    {
        if (accept("attribute")) {
            if (peek().kind == Token::Word || peek().kind == Token::String)
                *attribute = t[pos++].text;
            return;
        }
        if (peek().kind == Token::Word && featureWord(peek().text)) {
            *feature = featureWord(peek().text);
            ++pos;
        }
    }
    QString filterText(const QString &attribute, int feature) const
    {
        if (!attribute.isEmpty())
            return attributeLabel(attribute);
        if (feature)
            return featureName(static_cast<Feature>(feature));
        return QString();
    }
    CommandResult parkResult(bool on, int n, const QString &what)
    {
        if (!n)
            return fail(on ? QObject::tr("Nothing to park") : QObject::tr("Nothing parked there"));
        const QString count = countOf(n, QObject::tr("attribute"), QObject::tr("attributes"));
        if (!what.isEmpty())
            return ok(on ? QObject::tr("%1 parked (%2)").arg(what, count) : QObject::tr("%1 unparked (%2)").arg(what, count));
        return ok(on ? QObject::tr("Parked %1").arg(count) : QObject::tr("Unparked %1").arg(count));
    }
    // "0 Thru 5", "0 Thru 5 Thru 0", "3": seconds along the selection.
    bool timePoints(QVector<double> *points)
    {
        auto a = number();
        if (!a)
            return false;
        points->append(*a);
        while (accept("thru")) {
            auto b = number();
            if (!b)
                return false;
            points->append(*b);
        }
        return true;
    }
    // Fade 0 Thru 5 [Dimmer | Attribute Pan], Delay 0 Thru 2, Time = Fade; Fade Off clears.
    // Several can follow each other: "Fade 2 Delay 0 Thru 3".
    bool isTimingWord() const { return isWord("fade") || isWord("delay") || isWord("time"); }
    CommandResult timing(const QString &before = QString(), int onlyFeature = 0)
    {
        QStringList done;
        while (isTimingWord()) {
            const bool delay = accept("delay");
            if (!delay && !accept("fade"))
                accept("time");
            QVector<double> points;
            const bool clear = accept("off");
            if (!clear && !timePoints(&points))
                return fail(QObject::tr("Usage: Fade 0 Thru 5, Delay 0 Thru 2 Thru 0, Fade Off"));
            QString attribute;
            int feature = onlyFeature;
            attributeFilter(&attribute, &feature);
            if (s->selection().isEmpty())
                return fail(QObject::tr("No fixtures selected"));
            const int n = s->setProgrammerTiming(delay, points, feature, attribute);
            if (!n)
                return fail(QObject::tr("Give the selection values first: individual times go on programmer values"));
            QStringList pts;
            for (double p : points)
                pts << QString::number(p) + QStringLiteral("s");
            const QString what = delay ? QObject::tr("Delay") : QObject::tr("Fade");
            done << (clear ? QObject::tr("%1 cleared").arg(what) : QStringLiteral("%1 %2").arg(what, pts.join(QObject::tr(" thru "))));
        }
        const QString msg = done.join(QStringLiteral(", "));
        return ok(before.isEmpty() ? msg : before + QStringLiteral(" · ") + msg);
    }
    CommandResult timingFor(const QString &attribute, const QString &before = QString())
    {
        // Only this attribute: put "Attribute X" after each clause by hand.
        QStringList done;
        while (isTimingWord()) {
            const bool delay = accept("delay");
            if (!delay && !accept("fade"))
                accept("time");
            QVector<double> points;
            const bool clear = accept("off");
            if (!clear && !timePoints(&points))
                return fail(QObject::tr("Usage: Attribute Pan At Fade 0 Thru 5"));
            if (!s->setProgrammerTiming(delay, points, 0, attribute))
                return fail(QObject::tr("Give the selection a %1 value first").arg(attributeLabel(attribute)));
            QStringList pts;
            for (double p : points)
                pts << QString::number(p) + QStringLiteral("s");
            done << QStringLiteral("%1 %2").arg(delay ? QObject::tr("delay") : QObject::tr("fade"),
                                                clear ? QObject::tr("cleared") : pts.join(QObject::tr(" thru ")));
        }
        const QString msg = done.join(QStringLiteral(", "));
        return ok(before.isEmpty() ? attributeLabel(attribute) + ' ' + msg : before + QStringLiteral(" · ") + msg);
    }
    // After a value: an optional timing clause ("At 100 Fade 0 Thru 4").
    // The times go on what the value set: the dimmer for "At 50", the preset's feature for
    // "At Color.2", the attribute for "Attribute Pan At 30".
    CommandResult withTiming(const CommandResult &r, const Value &v, const QString &attribute = QString())
    {
        if (!r.ok || !isTimingWord())
            return r;
        if (!attribute.isEmpty())
            return timingFor(attribute, r.message);
        if (!v.preset)
            return timingFor(QStringLiteral("Dimmer"), r.message);
        if (v.type >= 1 && v.type <= 7)
            return timing(r.message, v.type);
        return timing(r.message);
    }

    CommandResult trailingAt(const QString &okMessage)
    {
        // "Fixture 101 Park", "Fixture 101 Intensity Park", "Fixture 101 Attribute Pan At 30",
        // "Fixture 101 Pan +10"
        if (auto r = attributeValue())
            return *r;
        QString attribute;
        int feature = 0;
        attributeFilter(&attribute, &feature);
        if (accept("park") || isWord("unpark")) {
            const bool on = !accept("unpark");
            const int n = on ? s->park(s->selection(), attribute, feature) : s->unpark(s->selection(), attribute, feature);
            return parkResult(on, n, filterText(attribute, feature));
        }
        if (!attribute.isEmpty() && accept("at")) {
            if (isTimingWord())
                return timing();
            Value v;
            if (!value(&v) || v.preset)
                return fail(QObject::tr("Expected a value"));
            return withTiming(applyValue(v, attribute), v, attribute);
        }
        if (accept("at")) {
            if (isTimingWord())   // "Fixture 1 Thru 8 At Fade 0 Thru 4"
                return timing();
            Value v;
            if (!value(&v))
                return fail(QObject::tr("Expected a value after At"));
            return withTiming(applyValue(v), v);
        }
        if (isTimingWord())   // "Fixture 1 Thru 8 Time 0 Thru 4"
            return timing();
        return ok(okMessage);
    }
    Executor *resolveExecutorArg(int *pageOut = nullptr, int *numOut = nullptr)
    {
        int page, num;
        if (accept("executor")) {
            if (!executorRef(&page, &num))
                return nullptr;
            if (pageOut) *pageOut = page;
            if (numOut) *numOut = num;
            return s->executor(page, num);
        }
        if (accept("sequence")) {   // Go Sequence 5: the executor it's on
            auto n = number();
            if (!n)
                return nullptr;
            Executor *e = s->executorForSequence(int(*n));
            if (e && pageOut) *pageOut = e->page;
            if (e && numOut) *numOut = e->number;
            return e;
        }
        return s->executorForSequence(s->selectedSequence());
    }

    CommandResult cmdSelectFixtures();
    CommandResult cmdSelectGroups();
    CommandResult cmdPark(bool on);
    CommandResult cmdStore();
    CommandResult cmdLoad();
    CommandResult cmdDelete();
    CommandResult cmdLabel();
    CommandResult cmdSpread();
    CommandResult cmdAssign();
    bool gridPoint(QPoint *out);
    CommandResult cmdAssignOther();
    std::optional<CommandResult> toPlot();   // "… At Plot N" after Assign or Store
    CommandResult cmdAssignFunction();
    CommandResult cmdPlayback(const QString &verb);
    CommandResult cmdGoto();
    CommandResult cmdExecutor();
    CommandResult cmdCue();
    CommandResult cmdBlueprint();
    CommandResult cmdBuild();
    CommandResult cmdUpdate();
    CommandResult cmdRemove();
    CommandResult cmdCopyMove(bool move);
    CommandResult cmdEffect();
    CommandResult runMacro(int id)
    {
        const Macro *m = s->macro(id);
        if (m->lines.isEmpty())
            return fail(QObject::tr("Macro %1 has no lines: right-click it to edit").arg(id));
        const bool wasWaiting = s->macroRun(id) && s->macroRun(id)->waiting;
        s->goMacro(id);
        return ok(wasWaiting ? QObject::tr("%1 goes on").arg(m->displayName()) : QObject::tr("%1").arg(m->displayName()));
    }

    Show *s;
    QVector<Token> t;
    int pos = 0;
};

bool Parser::gridPoint(QPoint *out)
{
    // x/y or x y (a slash or a space between them), as on the selection matrix.
    auto x = number();
    if (!x)
        return false;
    if (peek().kind == Token::Option && peek().text.isEmpty())
        ++pos;
    auto y = number();
    if (!y)
        return false;
    *out = QPoint(int(*x), int(*y));
    return true;
}

CommandResult Parser::cmdSelectFixtures()
{
    bool ok_ = false;
    QVector<int> ids = fixtureList(&ok_);
    if (!ok_)
        return fail(QObject::tr("Expected fixture numbers"));
    QVector<int> add, remove;
    for (int id : ids)
        (id > 0 ? add : remove).append(std::abs(id));
    // Selections build up until Clear: Fixture 1, then Fixture 3, selects both.
    s->select(add, Show::SelectMode::Add);
    if (!remove.isEmpty())
        s->select(remove, Show::SelectMode::Remove);
    // Fixture 5 Grid 3/2: placed on the selection matrix there, along X.
    if (accept("grid")) {
        QPoint at;
        if (!gridPoint(&at))
            return fail(QObject::tr("Usage: Fixture 5 Grid 3/2"));
        s->placeInGrid(add, at);
    }
    // Fixture 1 Thru 4 NextY Fixture 5 Thru 8: the next ones one step down in Y.
    if (accept("nexty")) {
        s->nextY();
        if (accept("fixture") || peek().kind == Token::Number)
            return cmdSelectFixtures();
        if (accept("group"))
            return cmdSelectGroups();
    }
    return trailingAt(countOf(int(s->selection().size()), QObject::tr("fixture selected"), QObject::tr("fixtures selected")));
}

// Park [Fixture 101 | Group 2] [Attribute Pan | Intensity]: the selection when no fixtures
// are named. Unpark the same way; Unpark All lets everything go.
CommandResult Parser::cmdPark(bool on)
{
    if (!on && accept("all")) {
        const int n = s->unpark({});
        return parkResult(false, n, QString());
    }
    QVector<int> fids;
    bool named = false;
    while (!atEnd()) {
        if (accept("fixture") || peek().kind == Token::Number) {
            bool ok_ = false;
            const QVector<int> ids = fixtureList(&ok_);
            if (!ok_)
                return fail(QObject::tr("Expected fixture numbers"));
            for (int id : ids)
                if (id > 0)
                    fids << id;
                else
                    fids.removeAll(-id);
            named = true;
        } else if (accept("group")) {
            bool ok_ = false;
            const QVector<int> ids = idList(&ok_);
            if (!ok_)
                return fail(QObject::tr("Expected a group number"));
            for (int id : ids) {
                const Group *g = s->group(std::abs(id));
                if (!g)
                    return fail(QObject::tr("Group %1 doesn't exist").arg(std::abs(id)));
                for (int f : g->fixtures)
                    if (id > 0 && !fids.contains(f))
                        fids << f;
                    else if (id < 0)
                        fids.removeAll(f);
            }
            named = true;
        } else {
            break;
        }
    }
    QString attribute;
    int feature = 0;
    attributeFilter(&attribute, &feature);
    if (!atEnd())
        return fail(QObject::tr("Usage: Park Fixture 101 [Attribute Pan | Intensity], or Fixture 101 Park"));
    if (!named)
        fids = s->selection();
    if (fids.isEmpty())
        return fail(on ? QObject::tr("Select fixtures (or name them) to park") : QObject::tr("Select fixtures (or name them), or Unpark All"));
    const int n = on ? s->park(fids, attribute, feature) : s->unpark(fids, attribute, feature);
    return parkResult(on, n, filterText(attribute, feature));
}

CommandResult Parser::cmdSelectGroups()
{
    bool ok_ = false;
    QVector<int> ids = idList(&ok_);
    if (!ok_)
        return fail(QObject::tr("Expected a group number"));
    for (int id : ids) {
        if (!s->group(std::abs(id)))
            return fail(QObject::tr("Group %1 doesn't exist").arg(std::abs(id)));
        s->selectGroup(std::abs(id), id < 0 ? Show::SelectMode::Remove : Show::SelectMode::Add);
    }
    return trailingAt(countOf(int(s->selection().size()), QObject::tr("fixture selected"), QObject::tr("fixtures selected")));
}

CommandResult Parser::cmdStore()
{
    // Store Fixture 1 Thru 5 At Plot 3, Store Group 2 At Plot 3: the same as Assign.
    if (!isWord("plot"))
        if (auto r = toPlot())
            return *r;
    if (accept("plot")) {
        auto n = number();
        auto name = string();
        options();
        if (!n || *n < 1)
            return fail(QObject::tr("Usage: Store Plot 3 \"Name\""));
        const bool existed = s->plot(int(*n));
        s->storePlot(int(*n), name.value_or(QString()));
        return ok(existed ? QObject::tr("Plot %1 kept").arg(int(*n)) : QObject::tr("Plot %1 stored").arg(int(*n)));
    }
    if (accept("macro")) {
        auto n = number();
        auto name = string();
        options();
        if (!n || *n < 1)
            return fail(QObject::tr("Usage: Store Macro 3 \"Name\""));
        const bool existed = s->macro(int(*n));
        s->storeMacro(int(*n), name.value_or(QString()));
        return ok(existed ? QObject::tr("Macro %1 kept").arg(int(*n)) : QObject::tr("Macro %1 stored: right-click it to add lines").arg(int(*n)));
    }
    if (accept("spread")) {
        auto n = number();
        auto name = string();
        const QStringList opts = options().join(' ').toLower().split(' ', Qt::SkipEmptyParts);
        if (!n || *n < 1)
            return fail(QObject::tr("Usage: Store Spread 3 \"Name\" (/x, /y or /both for one direction)"));
        int axis = -1, second = 0;
        if (opts.contains("x"))
            axis = 0;
        else if (opts.contains("y"))
            axis = 1;
        else if (opts.contains("both") || opts.contains("xy"))
            axis = 2;
        s->storeSpread(int(*n), name.value_or(QString()), axis, &second);
        if (second)
            return ok(QObject::tr("Spread %1 stored: %2; Spread %3: %4")
                          .arg(int(*n)).arg(s->spread(int(*n))->summary()).arg(second).arg(s->spread(second)->summary()));
        return ok(QObject::tr("Spread %1 stored: %2").arg(int(*n)).arg(s->spread(int(*n))->summary()));
    }
    if (accept("view")) {
        auto n = number();
        auto name = string();
        options();
        if (!n || *n < 1)
            return fail(QObject::tr("Usage: Store View 3 \"Name\""));
        const bool existed = s->view(int(*n));
        return s->storeView(int(*n), name.value_or(QString()))
                   ? ok(existed ? QObject::tr("View %1 updated").arg(int(*n)) : QObject::tr("View %1 stored").arg(int(*n)))
                   : fail(QObject::tr("Show a view first: Store View keeps the windows on the screen in use"));
    }
    if (accept("default")) {
        const Show::StoreMode mode = storeMode(options());
        const int n = int(s->programmerValues().size());
        return s->storeDefaults(mode)
                   ? ok(QObject::tr("Defaults stored for %1: they rest there when nothing plays")
                            .arg(countOf(n, QObject::tr("fixture"), QObject::tr("fixtures"))))
                   : fail(QObject::tr("Programmer is empty: set the values to rest at first"));
    }
    if (accept("group")) {
        auto n = number();
        if (!n)
            return fail(QObject::tr("Store Group needs a number"));
        auto name = string();
        const QStringList opts = options();
        if (s->group(int(*n)) && !storeChosen(opts) && !s->selection().isEmpty())
            return ask(QObject::tr("Group %1 exists").arg(int(*n)), storeChoices(false));
        Show::StoreMode mode = storeMode(opts);
        if (!opts.contains("merge", Qt::CaseInsensitive) && !opts.contains("m", Qt::CaseInsensitive) && mode == Show::StoreMode::Merge)
            mode = Show::StoreMode::Overwrite;   // a new group, or /overwrite
        if (!s->storeGroup(int(*n), name.value_or(QString()), mode))
            return fail(QObject::tr("Nothing selected"));
        return ok(mode == Show::StoreMode::Remove ? (s->group(int(*n)) ? QObject::tr("Selection removed from Group %1").arg(int(*n))
                                                                         : QObject::tr("Group %1 deleted: nothing left in it").arg(int(*n)))
                  : mode == Show::StoreMode::Merge ? QObject::tr("Group %1 merged").arg(int(*n))
                                                   : QObject::tr("Group %1 stored").arg(int(*n)));
    }
    if (accept("preset")) {
        int type, id;
        if (!presetRef(&type, &id))
            return fail(QObject::tr("Store Preset needs a pool and number, e.g. Color.1"));
        auto name = string();
        const QStringList opts = options();
        if (s->preset(type, id) && !storeChosen(opts))
            return ask(QObject::tr("Preset %1 exists").arg(lumora::presetRef(type, id)), storeChoices(true));
        Show::StoreMode mode = storeMode(opts);
        if (mode == Show::StoreMode::Remove) {
            const bool had = s->preset(type, id);
            if (!s->storePreset(type, id, mode))
                return fail(QObject::tr("Nothing in the programmer is in Preset %1").arg(lumora::presetRef(type, id)));
            return ok(had && !s->preset(type, id) ? QObject::tr("Preset %1 deleted: nothing left in it").arg(lumora::presetRef(type, id))
                                                  : QObject::tr("Programmer removed from Preset %1").arg(lumora::presetRef(type, id)));
        }
        std::optional<PresetScope> scope;
        for (const QString &o : opts) {
            const PresetScope sc = presetScopeFromName(o, PresetScope(-1));
            if (int(sc) >= 0)
                scope = sc;
        }
        return s->storePreset(type, id, mode, name.value_or(QString()), scope)
                   ? ok(QObject::tr("Preset %1 stored").arg(lumora::presetRef(type, id)))
                   : fail(QObject::tr("No matching programmer values for %1").arg(lumora::presetRef(type, id)));
    }
    int seq = s->selectedSequence();
    double cue = -1;
    optionalSequence(&seq);
    double cueTo = -1;
    if (accept("cue")) {
        if (auto n = number())
            cue = *n;
        if (accept("thru")) {
            accept("cue");
            auto m = number();
            if (!m || cue < 0 || *m < cue)
                return fail(QObject::tr("Usage: Store Cue 1 Thru 40"));
            cueTo = *m;
        }
    }
    optionalSequence(&seq);
    auto name = string();
    const QStringList opts = options();
    Show::StoreMode mode = storeMode(opts);
    if (!atEnd())
        return fail(QObject::tr("Don't know how to store '%1'").arg(peek().text));
    if (cueTo < 0 && !storeChosen(opts)) {
        // Onto a cue that exists, or a sequence with a single cue: ask. A sequence with more
        // cues takes the next one.
        const Sequence *sq = s->sequence(seq);
        if (cue >= 0 && sq && sq->indexOf(cue) >= 0) {
            Cue tmp;
            tmp.number = cue;
            return ask(QObject::tr("Cue %1 exists in Sequence %2").arg(tmp.displayNumber()).arg(seq), storeChoices(false));
        }
        if (cue < 0 && sq && sq->cues.size() == 1)
            return ask(QObject::tr("Sequence %1 has one cue").arg(seq), storeChoices(false, std::floor(sq->cues.first().number) + 1.0));
    }
    if (cue < 0 && cueTo < 0 && !opts.filter(QRegularExpression(QStringLiteral("^(new|n)$"), QRegularExpression::CaseInsensitiveOption)).isEmpty())
        cue = -1;   // /new: the next cue
    else if (cue < 0 && cueTo < 0 && storeChosen(opts)) {
        // Overwrite, Merge or Remove with no cue: the single cue a sequence has (or its last).
        if (const Sequence *sq = s->sequence(seq); sq && !sq->cues.isEmpty())
            cue = sq->cues.last().number;
    }
    if (cueTo >= 0) {
        // A run of cues, one per whole number: a cue list laid out in advance.
        if (cueTo - cue > 9999)
            return fail(QObject::tr("That's too many cues at once"));
        int made = 0;
        for (double n = cue; n <= cueTo + 1e-9; n += 1.0)
            made += s->storeCue(seq, n, mode) ? 1 : 0;
        return ok(QObject::tr("Stored cues %1 thru %2 in Sequence %3 (%4)")
                      .arg(cue).arg(cueTo).arg(seq).arg(countOf(made, QObject::tr("cue"), QObject::tr("cues"))));
    }
    double stored = 0;
    const bool empty = s->programmerValues().isEmpty() && s->programmerEffects().isEmpty();
    if (!s->storeCue(seq, cue, mode, &stored))
        return fail(QObject::tr("Couldn't store the cue"));
    if (mode == Show::StoreMode::Remove) {
        Cue tmp;
        tmp.number = stored;
        return ok(QObject::tr("Programmer removed from Cue %1 in Sequence %2").arg(tmp.displayNumber()).arg(seq));
    }
    if (name) {
        Sequence *sq = s->sequenceMutable(seq);
        sq->cues[sq->indexOf(stored)].name = *name;
        s->setSequence(*sq);
    }
    Cue tmp;
    tmp.number = stored;
    return ok((empty ? QObject::tr("Empty cue %1 stored in Sequence %2") : QObject::tr("Cue %1 stored in Sequence %2"))
                  .arg(tmp.displayNumber()).arg(seq));
}

CommandResult Parser::cmdDelete()
{
    if (accept("image")) {
        auto n = number();
        if (!n || !s->image(int(*n)))
            return fail(QObject::tr("No such image"));
        const int uses = s->imageUses(int(*n));
        s->deleteImage(int(*n));
        return ok(uses ? QObject::tr("Image %1 deleted (plots used it %2 times: they show it missing)").arg(int(*n)).arg(uses)
                       : QObject::tr("Image %1 deleted").arg(int(*n)));
    }
    if (accept("plot")) {
        auto n = number();
        if (!n || !s->plot(int(*n)))
            return fail(QObject::tr("No such plot"));
        s->deletePlot(int(*n));
        return ok(QObject::tr("Plot %1 deleted").arg(int(*n)));
    }
    if (accept("macro")) {
        auto n = number();
        if (!n || !s->macro(int(*n)))
            return fail(QObject::tr("No such macro"));
        s->deleteMacro(int(*n));
        return ok(QObject::tr("Macro %1 deleted").arg(int(*n)));
    }
    if (accept("spread")) {
        auto n = number();
        if (!n || !s->spread(int(*n)))
            return fail(QObject::tr("No such spread"));
        s->deleteSpread(int(*n));
        return ok(QObject::tr("Spread %1 deleted").arg(int(*n)));
    }
    if (accept("view")) {
        auto n = number();
        if (!n || !s->view(int(*n)))
            return fail(QObject::tr("No such view"));
        s->deleteView(int(*n));
        return ok(QObject::tr("View %1 deleted").arg(int(*n)));
    }
    if (accept("default")) {
        // The selection's stored defaults, or all of them; fixtures fall back to their type's.
        const QVector<int> sel = s->selection();
        s->clearDefaults(sel);
        return ok(sel.isEmpty() ? QObject::tr("All stored defaults removed")
                                : QObject::tr("Stored defaults removed for %1")
                                      .arg(countOf(int(sel.size()), QObject::tr("fixture"), QObject::tr("fixtures"))));
    }
    if (accept("group")) {
        auto n = number();
        if (!n || !s->group(int(*n)))
            return fail(QObject::tr("No such group"));
        s->deleteGroup(int(*n));
        return ok(QObject::tr("Group %1 deleted").arg(int(*n)));
    }
    if (accept("preset")) {
        int type, id;
        if (!presetRef(&type, &id) || !s->preset(type, id))
            return fail(QObject::tr("No such preset"));
        s->deletePreset(type, id);
        return ok(QObject::tr("Preset %1 deleted").arg(lumora::presetRef(type, id)));
    }
    if (accept("effect")) {
        auto n = number();
        if (!n || !s->effect(int(*n)))
            return fail(QObject::tr("No such effect"));
        s->deleteEffect(int(*n));
        return ok(QObject::tr("Effect %1 deleted").arg(int(*n)));
    }
    int seq = s->selectedSequence();
    if (accept("sequence")) {
        auto n = number();
        if (!n)
            return fail(QObject::tr("Expected a sequence number"));
        seq = int(*n);
        if (!isWord("cue")) {
            if (!s->sequence(seq))
                return fail(QObject::tr("No such sequence"));
            s->deleteSequence(seq);
            return ok(QObject::tr("Sequence %1 deleted").arg(seq));
        }
    }
    if (accept("cue")) {
        auto n = number();
        optionalSequence(&seq);
        if (!n || !s->deleteCue(seq, *n))
            return fail(QObject::tr("No such cue"));
        return ok(QObject::tr("Cue deleted"));
    }
    return fail(QObject::tr("Delete what? Group, Preset, Effect, Sequence or Cue"));
}

// spread 3 (recall) / spread Off / spread Every 2 Offset 2 Blocks 2 Groups 4 Wings 2
// Phase 0 Thru 180 Direction Backward: change the active one.
CommandResult Parser::cmdSpread()
{
    if (peek().kind == Token::Number && atEndAfter(1)) {
        const int id = int(*number());
        if (!s->recallSpread(id))
            return fail(QObject::tr("Spread %1 doesn't exist").arg(id));
        return ok(QObject::tr("Spread %1: %2").arg(id).arg(s->spread(id)->summary()));
    }
    // Spread Y …: the one along Y; Spread X … (or no axis): along X.
    Spread::Axis axis = Spread::Axis::X;
    bool named = false;
    if (isWord("y") || isWord("x")) {
        axis = t[pos].text == QLatin1String("y") ? Spread::Axis::Y : Spread::Axis::X;
        named = true;
        ++pos;
    }
    if (accept("off")) {   // back to none (clearing the selection does this too)
        s->setActiveSpread(axis, Spread::none(axis));
        if (!named)   // Spread Off: both
            s->setActiveSpread(Spread::Axis::Y, Spread::none(Spread::Axis::Y));
        return ok(QObject::tr("Spread reset"));
    }
    if (accept("on")) {
        s->setSpreadAxisOn(axis, true);
        return ok(QObject::tr("Spread %1 on").arg(axis == Spread::Axis::Y ? QStringLiteral("Y") : QStringLiteral("X")));
    }
    Spread m = s->activeSpread(axis);
    bool any = false;
    while (!atEnd()) {
        auto count = [&](int *field, int minimum) {
            auto v = number();
            if (!v)
                return false;
            *field = std::max(minimum, int(*v));
            return true;
        };
        if (accept("every")) {
            if (!count(&m.every, 1)) return fail(QObject::tr("Expected a number after Every"));
        } else if (accept("offset")) {
            int o = 1;
            if (!count(&o, 1)) return fail(QObject::tr("Expected a number after Offset"));
            m.offset = o - 1;   // operators count from 1
        } else if (accept("blocks")) {
            if (!count(&m.blocks, 1)) return fail(QObject::tr("Expected a number after Blocks"));
        } else if (accept("groups")) {
            if (!count(&m.groups, 0)) return fail(QObject::tr("Expected a number after Groups"));
        } else if (accept("wings")) {
            if (!count(&m.wings, 1)) return fail(QObject::tr("Expected a number after Wings"));
        } else if (accept("phase")) {
            const bool neg = acceptKind(Token::Minus);
            auto a = number();
            if (!a) return fail(QObject::tr("Usage: Spread Phase 0 Thru 180"));
            m.phaseFrom = std::clamp(neg ? -*a : *a, -720.0, 720.0);
            m.phaseTo = m.phaseFrom;
            if (accept("thru")) {
                const bool neg2 = acceptKind(Token::Minus);
                auto b = number();
                if (!b) return fail(QObject::tr("Usage: Spread Phase 0 Thru 180"));
                m.phaseTo = std::clamp(neg2 ? -*b : *b, -720.0, 720.0);
            }
        } else if (accept("direction")) {
            const QString d = peek().text.toLower();
            ++pos;
            bool found = false;
            static const char *keys[] = {"forward", "backward", "inward", "outward", "random"};
            for (int i = 0; i <= int(SpreadDirection::Random); ++i)
                if (QLatin1String(keys[i]).startsWith(d) || spreadDirectionName(SpreadDirection(i)).toLower().startsWith(d)) {
                    m.direction = SpreadDirection(i);
                    found = true;
                    break;
                }
            if (!found) return fail(QObject::tr("Direction is Forward, Backward, Inward, Outward or Random"));
        } else {
            return fail(QObject::tr("Unexpected '%1'").arg(peek().text));
        }
        any = true;
    }
    if (!any)
        return fail(QObject::tr("Usage: Spread 3, Spread Off, or Spread Wings 2 Blocks 2…"));
    s->setActiveSpread(axis, m);
    s->setSpreadAxisOn(axis, true);
    return ok(QObject::tr("Spread: %1").arg(s->activeSpread(axis).summary()));
}

CommandResult Parser::cmdLabel()
{
    if (accept("image")) {
        auto n = number();
        auto name = string();
        if (!n || !name || !s->image(int(*n)))
            return fail(QObject::tr("Usage: Label Image 3 \"Name\""));
        PoolImage img = *s->image(int(*n));
        img.name = *name;
        s->setImage(img);
        return ok(QObject::tr("Labelled"));
    }
    if (accept("plot")) {
        auto n = number();
        auto name = string();
        if (!n || !name || !s->plot(int(*n)))
            return fail(QObject::tr("Usage: Label Plot 3 \"Name\""));
        s->storePlot(int(*n), *name);
        return ok(QObject::tr("Labelled"));
    }
    if (accept("macro")) {
        auto n = number();
        auto name = string();
        if (!n || !name || !s->macro(int(*n)))
            return fail(QObject::tr("Usage: Label Macro 3 \"Name\""));
        s->storeMacro(int(*n), *name);
        return ok(QObject::tr("Labelled"));
    }
    if (accept("ratemaster")) {
        auto n = number();
        auto name = string();
        const RateMaster *m = n ? s->rateMaster(int(*n)) : nullptr;
        if (!m || !name)
            return fail(QObject::tr("Usage: Label RateMaster 1 \"Name\""));
        RateMaster copy = *m;
        copy.name = *name;
        s->setRateMaster(copy);
        return ok(QObject::tr("Labelled"));
    }
    if (accept("spread")) {
        auto n = number();
        auto name = string();
        const Spread *m = n ? s->spread(int(*n)) : nullptr;
        if (!m || !name)
            return fail(QObject::tr("Usage: Label Spread 1 \"Name\""));
        Spread copy = *m;
        copy.name = *name;
        s->setSpread(copy);
        return ok(QObject::tr("Labelled"));
    }
    if (accept("view")) {
        auto n = number();
        auto name = string();
        const StoredView *v = n ? s->view(int(*n)) : nullptr;
        if (!v || !name)
            return fail(QObject::tr("Usage: Label View 1 \"Name\""));
        StoredView copy = *v;
        copy.name = *name;
        s->setView(copy);
        return ok(QObject::tr("Labelled"));
    }
    if (accept("group")) {
        auto n = number();
        auto name = string();
        const Group *g = n ? s->group(int(*n)) : nullptr;
        if (!g || !name)
            return fail(QObject::tr("Usage: Label Group 1 \"Name\""));
        Group copy = *g;
        copy.name = *name;
        s->setGroup(copy);
        return ok(QObject::tr("Labelled"));
    }
    if (accept("preset")) {
        int type, id;
        bool okRef = presetRef(&type, &id);
        auto name = string();
        const Preset *p = okRef ? s->preset(type, id) : nullptr;
        if (!p || !name)
            return fail(QObject::tr("Usage: Label Color.1 \"Name\""));
        Preset copy = *p;
        copy.name = *name;
        s->setPreset(copy);
        return ok(QObject::tr("Labelled"));
    }
    if (accept("page")) {
        auto n = number();
        auto name = string();
        if (!n || *n < 1 || !name)
            return fail(QObject::tr("Usage: Label Page 2 \"Name\""));
        s->setPageName(int(*n), *name);
        return ok(QObject::tr("Labelled"));
    }
    if (accept("effect")) {
        auto n = number();
        auto name = string();
        const Effect *e = n ? s->effect(int(*n)) : nullptr;
        if (!e || !name)
            return fail(QObject::tr("Usage: Label Effect 1 \"Name\""));
        Effect copy = *e;
        copy.name = *name;
        s->setEffect(copy);
        return ok(QObject::tr("Labelled"));
    }
    if (accept("fixture")) {
        auto n = number();
        auto name = string();
        const Fixture *f = n ? s->fixture(int(*n)) : nullptr;
        if (!f || !name)
            return fail(QObject::tr("Usage: Label Fixture 1 \"Name\""));
        Fixture copy = *f;
        copy.name = *name;
        s->updateFixture(copy);
        return ok(QObject::tr("Labelled"));
    }
    int seq = s->selectedSequence();
    if (accept("sequence")) {
        auto n = number();
        if (!n)
            return fail(QObject::tr("Expected a sequence number"));
        seq = int(*n);
        if (!isWord("cue")) {
            auto name = string();
            Sequence *sq = s->sequenceMutable(seq);
            if (!sq || !name)
                return fail(QObject::tr("Usage: Label Sequence 1 \"Name\""));
            Sequence copy = *sq;
            copy.name = *name;
            s->setSequence(copy);
            return ok(QObject::tr("Labelled"));
        }
    }
    if (accept("cue")) {
        auto n = number();
        optionalSequence(&seq);
        auto name = string();
        Sequence *sq = s->sequenceMutable(seq);
        int idx = (sq && n) ? sq->indexOf(*n) : -1;
        if (idx < 0 || !name)
            return fail(QObject::tr("Usage: Label Cue 1 \"Name\""));
        Sequence copy = *sq;
        copy.cues[idx].name = *name;
        s->setSequence(copy);
        return ok(QObject::tr("Labelled"));
    }
    return fail(QObject::tr("Label what?"));
}

CommandResult Parser::cmdAssign()
{
    if (auto r = toPlot())
        return *r;
    return cmdAssignOther();
}

std::optional<CommandResult> Parser::toPlot()
{
    // Assign Fixture 1 Thru 8 At Plot 2, Assign Group 3 At Plot 2, Assign Preset 4.1 At Plot 2,
    // Assign Sequence / Macro / Effect / View / Spread / Plot N At Plot 2 (Store … At Plot 2 too)
    for (int k = pos; k + 1 < t.size(); ++k)
        if (t[k].kind == Token::Word && t[k].text == QLatin1String("at") && t[k + 1].kind == Token::Word
            && t[k + 1].text == QLatin1String("plot")) {
            const int end = k;
            auto target = [&]() -> int {
                pos = end + 2;
                auto n = number();
                return n && *n >= 1 ? int(*n) : 0;
            };
            if (isWord("fixture") || peek().kind == Token::Number) {
                accept("fixture");
                bool okList = false;
                const QVector<int> ids = fixtureList(&okList);
                if (!okList || pos != end)
                    return fail(QObject::tr("Usage: Assign Fixture 1 Thru 8 At Plot 2"));
                QVector<int> fids;
                for (int id : ids)
                    if (id > 0)
                        fids << id;
                const int plot = target();
                if (!plot)
                    return fail(QObject::tr("Expected a plot number"));
                const int n = s->addFixturesToPlot(plot, fids);
                return ok(QObject::tr("%1 on Plot %2").arg(countOf(n, QObject::tr("fixture"), QObject::tr("fixtures"))).arg(plot));
            }
            if (accept("image")) {   // Assign Image 3 At Plot 2: the picture, on the plot
                auto n = number();
                if (!n || !s->image(int(*n)) || pos != end)
                    return fail(QObject::tr("Usage: Assign Image 3 At Plot 2"));
                const int img = int(*n);
                const int plot = target();
                if (!plot)
                    return fail(QObject::tr("Expected a plot number"));
                s->addImageToPlot(plot, img);
                return ok(QObject::tr("Image %1 on Plot %2").arg(img).arg(plot));
            }
            static const QHash<QString, QString> kinds = {
                {"group", "Group"}, {"sequence", "Sequence"}, {"macro", "Macro"}, {"effect", "Effect"},
                {"view", "View"}, {"spread", "Spread"}, {"plot", "Plot"}, {"ratemaster", "RateMaster"}};
            QString object;
            if (accept("preset")) {
                int type, id;
                if (!presetRef(&type, &id))
                    return fail(QObject::tr("Expected a preset, e.g. Color.1"));
                object = QStringLiteral("Preset %1.%2").arg(type).arg(id);
            } else if (peek().kind == Token::Word && kinds.contains(peek().text)) {
                const QString kind = kinds.value(t[pos++].text);
                auto n = number();
                if (!n)
                    return fail(QObject::tr("Expected a number after %1").arg(kind));
                object = QStringLiteral("%1 %2").arg(kind).arg(int(*n));
            } else if (accept("executor")) {
                int page, num;
                if (!executorRef(&page, &num))
                    return fail(QObject::tr("Expected an executor number"));
                object = QStringLiteral("Executor %1.%2").arg(page).arg(num);
            }
            if (object.isEmpty() || pos != end)
                return fail(QObject::tr("Usage: Assign Group 3 At Plot 2 (or a fixture, preset, sequence, macro…)"));
            const int plot = target();
            if (!plot)
                return fail(QObject::tr("Expected a plot number"));
            s->addObjectToPlot(plot, object);
            return ok(QObject::tr("%1 on Plot %2").arg(object).arg(plot));
        }
    return std::nullopt;
}

CommandResult Parser::cmdAssignOther()
{
    // Assign RateMaster 2 At Executor 205   Assign RateMaster 2 At Effect 3   Assign Effect 3 At RateMaster 2
    if (isWord("ratemaster") || (isWord("effect") && isWord("ratemaster", 3)) || (isWord("effect") && isWord("ratemaster", 2))) {
        const QString usage = QObject::tr("Usage: Assign RateMaster 1 At Executor 201, or Assign Effect 3 At RateMaster 1");
        int master = 0, effect = 0;
        int page = 0, num = 0;
        bool toExecutor = false;
        for (int part = 0; part < 2; ++part) {
            if (part == 1)
                accept("at");
            if (accept("ratemaster")) {
                auto n = number();
                if (!n || !s->rateMaster(int(*n)))
                    return fail(QObject::tr("Rate masters are 1 to %1").arg(RateMaster::kCount));
                master = int(*n);
            } else if (accept("effect")) {
                auto n = number();
                if (!n || !s->effect(int(*n)))
                    return fail(QObject::tr("No such effect"));
                effect = int(*n);
            } else if (part == 1 && accept("executor")) {
                if (!executorRef(&page, &num))
                    return fail(QObject::tr("Expected an executor number"));
                toExecutor = true;
            } else {
                return fail(usage);
            }
        }
        if (!master)
            return fail(usage);
        if (toExecutor) {
            s->assignRateMaster(master, page, num);
            return ok(QObject::tr("%1 on Executor %2.%3").arg(s->rateMaster(master)->displayName()).arg(page).arg(num));
        }
        if (!effect)
            return fail(usage);
        Effect e = *s->effect(effect);
        e.rateMaster = master;
        s->setEffect(e);
        return ok(QObject::tr("Effect %1 follows %2").arg(effect).arg(s->rateMaster(master)->displayName()));
    }
    if (!isWord("sequence"))
        return cmdAssignFunction();
    accept("sequence");
    auto n = number();
    if (!n)
        return fail(QObject::tr("Expected a sequence number"));
    accept("at");
    if (!accept("executor"))
        return fail(QObject::tr("Usage: Assign Sequence 1 At Executor 201"));
    int page, num;
    if (!executorRef(&page, &num))
        return fail(QObject::tr("Expected an executor number"));
    s->assignSequence(int(*n), page, num);
    return ok(QObject::tr("Sequence %1 assigned to Executor %2.%3").arg(int(*n)).arg(page).arg(num));
}

// Assign Flash At Executor 201   Assign Toggle At Executor 101   Assign XFade At Executor 205
// (Temp goes to the fader with "Fader" at the end.) With no button given, button 1.
CommandResult Parser::cmdAssignFunction()
{
    const QString usage = QObject::tr("Usage: Assign Flash At Executor 201, or Assign XFade At Executor 201");
    if (peek().kind != Token::Word)
        return fail(usage);
    const QString word = t[pos++].text;
    std::optional<ButtonFn> button;
    for (ButtonFn fn : allButtonFns())
        if (buttonFnName(fn).compare(word, Qt::CaseInsensitive) == 0)
            button = fn;
    std::optional<FaderFn> fader;
    for (FaderFn fn : {FaderFn::Master, FaderFn::XFade, FaderFn::Temp})
        if (faderFnName(fn).compare(word, Qt::CaseInsensitive) == 0 || (fn == FaderFn::XFade && word.compare("crossfade", Qt::CaseInsensitive) == 0))
            fader = fn;
    if (!button && !fader)
        return fail(QObject::tr("'%1' isn't a button or fader function").arg(word));
    accept("at");
    if (!accept("executor"))
        return fail(usage);
    int page, num;
    if (!executorRef(&page, &num))
        return fail(QObject::tr("Expected an executor number"));
    int buttonNo = 1;
    bool toFader = false;
    if (accept("button")) {
        auto b = number();
        if (!b)
            return fail(usage);
        buttonNo = int(*b);
    } else if (accept("fader")) {
        toFader = true;
    }
    if (!button)
        toFader = true;   // Master and XFade only exist on faders
    if (toFader && !fader)
        return fail(QObject::tr("%1 is a button function").arg(word));
    Executor &e = s->ensureExecutor(page, num);
    const ExecKind kind = executorKind(num, &e);
    if (toFader) {
        if (kind == ExecKind::Button)
            return fail(QObject::tr("Executor %1 is a button: it has no fader").arg(num));
        e.faderFn = *fader;
        s->notifyExecutors();
        return ok(QObject::tr("Executor %1.%2 %3: %4").arg(page).arg(num)
                      .arg(kind == ExecKind::Encoder ? QObject::tr("encoder") : QObject::tr("fader"), faderFnName(*fader)));
    }
    if (buttonNo != 1)
        return fail(QObject::tr("Executor %1 has one button").arg(num));
    e.buttons[buttonNo - 1] = *button;
    s->notifyExecutors();
    return ok(QObject::tr("Executor %1.%2 button %3: %4").arg(page).arg(num).arg(buttonNo).arg(buttonFnName(*button)));
}

CommandResult Parser::cmdPlayback(const QString &verb)
{
    if (accept("macro")) {
        // Go Macro 3: start it, or go on from a Wait. Off Macro 3 stops it; Off Macro, all of them.
        auto n = number();
        if (verb == "off") {
            s->offMacro(n ? int(*n) : 0);
            return ok(n ? QObject::tr("Macro %1 stopped").arg(int(*n)) : QObject::tr("Every macro stopped"));
        }
        if (verb != "go" && verb != "on")
            return fail(QObject::tr("Usage: Go Macro 3, Off Macro 3"));
        if (!n || !s->macro(int(*n)))
            return fail(QObject::tr("No such macro"));
        return runMacro(int(*n));
    }
    Executor *e = resolveExecutorArg();
    if (!e || e->isEmpty())
        return fail(QObject::tr("No executor with a sequence"));
    if (verb == "go") s->go(*e);
    else if (verb == "goback") s->goBack(*e);
    else if (verb == "off") s->off(*e);
    else if (verb == "on") { if (!e->pb().isOn()) s->go(*e); }
    else if (verb == "pause") s->togglePause(*e);
    else if (verb == "top") s->gotoCue(*e, 0, true);
    return ok(QObject::tr("%1 Executor %2").arg(verb.left(1).toUpper() + verb.mid(1)).arg(e->number));
}

CommandResult Parser::cmdGoto()
{
    // Goto Cue 3 Executor 201 [Fade 2] (or Sequence 5: the executor it's on); Goto Executor 201
    // alone shows its cue list to pick from.
    double cue = -1;
    double fade = -1;
    Executor *e = nullptr;
    bool namedExecutor = false;
    for (int i = 0; i < 3; ++i) {
        if (accept("cue")) {
            auto n = number();
            if (!n)
                return fail(QObject::tr("Expected a cue number"));
            cue = *n;
        } else if (isWord("executor") || isWord("sequence")) {
            e = resolveExecutorArg();
            namedExecutor = true;
        } else if (accept("fade") || accept("time")) {
            auto n = number();
            if (!n || *n < 0)
                return fail(QObject::tr("Expected a fade time in seconds"));
            fade = *n;
        } else if (peek().kind == Token::Number && cue < 0) {
            cue = *number();   // Goto 3: the cue
        }
    }
    if (!e)
        e = s->executorForSequence(s->selectedSequence());
    if (!e || e->isEmpty())
        return fail(QObject::tr("Usage: Goto Cue 3 Executor 201 (Fade 2)"));
    if (cue < 0) {
        if (namedExecutor) {
            emit s->gotoRequested(e->page, e->number);
            return ok(QObject::tr("Goto: pick a cue"));
        }
        return fail(QObject::tr("Usage: Goto Cue 3 Executor 201 (Fade 2)"));
    }
    const Sequence *sq = s->sequence(e->sequenceId);
    int idx = sq ? sq->indexOf(cue) : -1;
    if (idx < 0)
        return fail(QObject::tr("No such cue"));
    if (fade >= 0)
        s->gotoCue(*e, idx, fade);
    else
        s->gotoCue(*e, idx, true);
    return ok(fade >= 0 ? QObject::tr("Goto cue %1 in %2 s").arg(cue).arg(fade) : QObject::tr("Goto cue %1").arg(cue));
}

CommandResult Parser::cmdLoad()
{
    // Load Cue 3 Executor 201 (or Sequence 5): the next Go there fires cue 3. Load Executor 201
    // alone shows its cue list to pick from; Load Executor 201 /clear takes the load back.
    double cue = -1;
    Executor *e = nullptr;
    bool namedExecutor = false;
    for (int i = 0; i < 2; ++i) {
        if (accept("cue")) {
            auto n = number();
            if (!n)
                return fail(QObject::tr("Expected a cue number"));
            cue = *n;
        } else if (isWord("executor") || isWord("sequence")) {
            e = resolveExecutorArg();
            namedExecutor = true;
        } else if (peek().kind == Token::Number && cue < 0) {
            cue = *number();   // Load 3: the cue
        }
    }
    const QStringList opts = options();
    if (!e)
        e = s->executorForSequence(s->selectedSequence());
    if (!e || e->isEmpty())
        return fail(QObject::tr("Usage: Load Cue 3 Executor 201"));
    if (opts.contains(QStringLiteral("clear"), Qt::CaseInsensitive)) {
        s->loadCue(*e, -1);
        return ok(QObject::tr("Load cleared: Go fires the next cue"));
    }
    if (cue < 0) {
        if (namedExecutor) {
            emit s->loadRequested(e->page, e->number);
            return ok(QObject::tr("Load: pick a cue"));
        }
        return fail(QObject::tr("Usage: Load Cue 3 Executor 201"));
    }
    const Sequence *sq = s->sequence(e->sequenceId);
    const int idx = sq ? sq->indexOf(cue) : -1;
    if (idx < 0)
        return fail(QObject::tr("No such cue"));
    s->loadCue(*e, idx);
    return ok(QObject::tr("Cue %1 loaded: the next Go fires it").arg(sq->cues[idx].displayNumber()));
}

CommandResult Parser::cmdExecutor()
{
    int page, num;
    if (!executorRef(&page, &num))
        return fail(QObject::tr("Expected an executor number"));
    // Executor 401 Button / Encoder / Fader: what a 401–499 executor is.
    for (const char *kw : {"button", "encoder", "fader"})
        if (accept(kw)) {
            const ExecKind k = execKindFromName(QString::fromLatin1(kw));
            if (!s->setExecutorKind(page, num, k))
                return fail(QObject::tr("Only executors 401–499 change type: 1xx are buttons, 2xx faders, 3xx encoders"));
            return ok(QObject::tr("Executor %1 is a %2").arg(num).arg(QObject::tr(kw)));
        }
    Executor &e = s->ensureExecutor(page, num);
    if (accept("at")) {
        Value v;
        if (!value(&v) || v.preset)
            return fail(QObject::tr("Expected a fader level"));
        s->setFader(e, v.v);
        return ok(QObject::tr("Executor %1 at %2%").arg(num).arg(std::lround(v.v * 100)));
    }
    if (e.sequenceId > 0)
        s->setSelectedSequence(e.sequenceId);
    return ok(QObject::tr("Executor %1 selected").arg(num));
}

CommandResult Parser::cmdCue()
{
    int seq = s->selectedSequence();
    if (accept("off")) {
        // The Off line: Cue Off Follow, Cue Off Wait 5, Cue Off Go, Cue Off Fade 3
        optionalSequence(&seq);
        Sequence *sq = s->sequenceMutable(seq);
        if (!sq)
            return fail(QObject::tr("No such sequence"));
        Sequence copy = *sq;
        while (!atEnd()) {
            if (accept("fade")) {
                auto v = number();
                if (!v) return fail(QObject::tr("Expected seconds"));
                copy.offFade = std::max(0.0, *v);
            } else if (accept("delay")) {
                auto v = number();
                if (!v) return fail(QObject::tr("Expected seconds"));
                copy.offDelay = std::max(0.0, *v);
            } else if (accept("trigger") || isWord("go") || isWord("wait") || isWord("follow")) {
                if (accept("follow")) {
                    copy.offTrigger = CueTrigger::Follow;
                } else if (accept("wait")) {
                    auto v = number();
                    if (!v) return fail(QObject::tr("Expected seconds after Wait"));
                    copy.offTrigger = CueTrigger::Wait;
                    copy.offWait = std::max(0.0, *v);
                } else if (accept("go")) {
                    copy.offTrigger = CueTrigger::Go;
                } else {
                    return fail(QObject::tr("Trigger: Go, Wait 5 or Follow"));
                }
            } else {
                return fail(QObject::tr("Unexpected '%1'").arg(peek().text));
            }
        }
        s->setSequence(copy);
        Cue shown;
        shown.trigger = copy.offTrigger;
        shown.wait = copy.offWait;
        return ok(QObject::tr("Off: fade %1 s, delay %2 s, %3").arg(copy.offFade).arg(copy.offDelay).arg(shown.triggerText()));
    }
    auto n = number();
    if (!n)
        return fail(QObject::tr("Expected a cue number"));
    if (accept("thru")) {
        // A run of cues at once: Cue 1 Thru 14 Sequence 5 Fade 3 (fade, delay, trigger, macro).
        auto last = number();
        if (!last)
            return fail(QObject::tr("Expected the last cue after Thru"));
        optionalSequence(&seq);
        Sequence *rs = s->sequenceMutable(seq);
        if (!rs)
            return fail(QObject::tr("No such sequence"));
        const double lo = std::min(*n, *last), hi = std::max(*n, *last);
        Sequence copy = *rs;
        QVector<int> idxs;
        for (int i = 0; i < copy.cues.size(); ++i)
            if (copy.cues[i].number >= lo - 1e-9 && copy.cues[i].number <= hi + 1e-9)
                idxs << i;
        if (idxs.isEmpty())
            return fail(QObject::tr("No cues in %1 Thru %2").arg(lo).arg(hi));
        if (atEnd())
            return fail(QObject::tr("Usage: Cue 1 Thru 14 Fade 3 (or Delay, Follow, Wait 2, Go, Macro 4)"));
        while (!atEnd()) {
            if (isWord("fade") || isWord("delay")) {
                const bool delay = accept("delay");
                if (!delay)
                    accept("fade");
                auto v = number();
                if (!v || *v < 0)
                    return fail(QObject::tr("Expected seconds"));
                for (int i : idxs)
                    (delay ? copy.cues[i].delay : copy.cues[i].fade) = *v;
            } else if (accept("follow")) {
                for (int i : idxs) copy.cues[i].trigger = CueTrigger::Follow;
            } else if (accept("wait")) {
                auto v = number();
                if (!v || *v < 0)
                    return fail(QObject::tr("Expected seconds after Wait"));
                for (int i : idxs) {
                    copy.cues[i].trigger = CueTrigger::Wait;
                    copy.cues[i].wait = *v;
                }
            } else if (accept("go")) {
                for (int i : idxs) copy.cues[i].trigger = CueTrigger::Go;
            } else if (accept("macro")) {
                int m = 0;
                if (!accept("off")) {
                    auto v = number();
                    if (!v || *v < 0)
                        return fail(QObject::tr("Expected a macro number (or Off) after Macro"));
                    m = int(*v);
                }
                for (int i : idxs) copy.cues[i].macro = m;
            } else {
                return fail(QObject::tr("Unexpected '%1'").arg(peek().text));
            }
        }
        s->setSequence(copy);
        return ok(QObject::tr("%1 changed").arg(countOf(int(idxs.size()), QObject::tr("cue"), QObject::tr("cues"))));
    }
    optionalSequence(&seq);
    Sequence *sq = s->sequenceMutable(seq);
    int idx = sq ? sq->indexOf(*n) : -1;
    if (idx < 0)
        return fail(QObject::tr("No such cue"));
    Sequence copy = *sq;
    bool changed = false;
    QString individual;
    // "Cue 3 Fade 0 Thru 5": individual times for the cue's values, spread
    // across the selected fixtures in selection order (or the active spread's layout), or across
    // all the cue's fixtures when nothing is selected. "Dimmer" or "Attribute Pan" after it: only those.
    auto spreadTimes = [&](bool delay, const QVector<double> &points) {
        Cue &c = copy.cues[idx];
        QString attribute;
        int feature = 0;
        attributeFilter(&attribute, &feature);
        QVector<int> fids;
        for (int fid : s->workingSelection())
            if (c.values.contains(fid))
                fids << fid;
        if (s->selection().isEmpty())
            fids = c.values.keys().toVector();
        const QVector<double> frac = s->activeSpread().timeFractions(int(fids.size()));
        int n = 0;
        for (int i = 0; i < fids.size(); ++i)
            for (auto a = c.values[fids[i]].begin(); a != c.values[fids[i]].end(); ++a) {
                if (!attribute.isEmpty() ? a.key() != attribute : feature && int(featureOf(a.key())) != feature)
                    continue;
                (delay ? a->delay : a->fade) = points.isEmpty() ? -1.0 : std::max(0.0, alongPoints(points, frac[i]));
                ++n;
            }
        return n;
    };
    while (!atEnd()) {
        if (isWord("fade") || isWord("delay")) {
            const bool delay = accept("delay");
            if (!delay)
                accept("fade");
            if (accept("off")) {   // Cue 3 Fade Off: no individual fades
                if (!spreadTimes(delay, {}))
                    return fail(QObject::tr("No values in the cue to time"));
                individual = delay ? QObject::tr("individual delays cleared") : QObject::tr("individual fades cleared");
                changed = true;
                continue;
            }
            QVector<double> points;
            if (!timePoints(&points)) return fail(QObject::tr("Expected seconds"));
            if (points.size() == 1) {
                (delay ? copy.cues[idx].delay : copy.cues[idx].fade) = points.first();
            } else {
                const int n = spreadTimes(delay, points);
                if (!n)
                    return fail(QObject::tr("None of the cue's values belong to the selection"));
                individual = QObject::tr("%1 %2 across %3").arg(delay ? QObject::tr("delay") : QObject::tr("fade"))
                                 .arg(QStringLiteral("%1 thru %2 s").arg(points.first()).arg(points.last()))
                                 .arg(countOf(n, QObject::tr("value"), QObject::tr("values")));
            }
            changed = true;
        } else if (accept("note")) {
            auto t = string();
            copy.cues[idx].notes = t.value_or(QString());
            changed = true;
        } else if (accept("macro")) {   // Cue 3 Macro 2: runs as the cue starts; Cue 3 Macro Off
            if (accept("off")) {
                copy.cues[idx].macro = 0;
            } else {
                auto m = number();
                if (!m || *m < 0)
                    return fail(QObject::tr("Expected a macro number (or Off) after Macro"));
                copy.cues[idx].macro = int(*m);
            }
            changed = true;
        } else if (accept("trigger") || isWord("go") || isWord("wait") || isWord("follow")) {
            // Cue 3 Follow, Cue 3 Wait 5, Cue 3 Go (also Cue 3 Trigger Follow)
            Cue &c = copy.cues[idx];
            if (accept("follow")) {
                c.trigger = CueTrigger::Follow;
            } else if (accept("wait")) {
                auto v = number();
                if (!v) return fail(QObject::tr("Expected seconds after Wait"));
                c.trigger = CueTrigger::Wait;
                c.wait = std::max(0.0, *v);
            } else if (accept("go")) {
                c.trigger = CueTrigger::Go;
            } else {
                return fail(QObject::tr("Trigger: Go, Wait 5 or Follow"));
            }
            changed = true;
        } else {
            return fail(QObject::tr("Unexpected '%1'").arg(peek().text));
        }
    }
    if (changed)
        s->setSequence(copy);
    if (!individual.isEmpty())
        return ok(QObject::tr("Cue %1: %2").arg(copy.cues[idx].displayNumber(), individual));
    return ok(QObject::tr("Cue %1: fade %2 s, delay %3 s, %4").arg(copy.cues[idx].displayNumber())
                  .arg(copy.cues[idx].fade).arg(copy.cues[idx].delay).arg(copy.cues[idx].triggerText()));
}

CommandResult Parser::cmdBlueprint()
{
    int seq = s->selectedSequence();
    optionalSequence(&seq);
    if (!accept("cue"))
        return fail(QObject::tr("Usage: Blueprint Cue 1 Group 2 Color.1 At 80"));
    auto n = number();
    if (!n)
        return fail(QObject::tr("Expected a cue number"));
    optionalSequence(&seq);
    BlueprintLine line;
    while (!atEnd()) {
        if (accept("group")) {
            auto g = number();
            if (!g || !s->group(int(*g)))
                return fail(QObject::tr("No such group"));
            line.groupId = int(*g);
        } else if (accept("preset")) {
            int type, id;
            if (!presetRef(&type, &id))
                return fail(QObject::tr("Expected a preset after Preset, e.g. Color.1"));
            line.presets.append({type, id});
        } else if (accept("at")) {
            Value v;
            if (!value(&v))
                return fail(QObject::tr("Expected a value after At"));
            // A level becomes the matching universal Dimmer preset (made if there's none).
            if (v.preset)
                line.presets.append({v.type, v.id});
            else
                line.presets.append({int(Feature::Dimmer), s->dimmerPresetFor(v.v, true)});
        } else if (accept("effect")) {
            auto v = number();
            if (!v || !s->effect(int(*v)))
                return fail(QObject::tr("No such effect"));
            line.effects << int(*v);
        } else if (accept("every")) {
            auto v = number();
            if (!v) return fail(QObject::tr("Expected a number after Every"));
            line.every = std::max(1, int(*v));
        } else if (accept("offset")) {
            auto v = number();
            if (!v) return fail(QObject::tr("Expected a number after Offset"));
            line.offset = std::max(0, int(*v) - 1);   // operators count from 1
        } else if (accept("spread")) {
            auto v = number();
            if (!v || !s->spread(int(*v)))
                return fail(QObject::tr("No such spread"));
            line.spreadId = int(*v);
        } else if (isWord("fade") || isWord("delay") || isWord("time")) {
            // Fade 0 Thru 3, Delay 0 Thru 2 Thru 0: individual times across the line's fixtures
            const bool delay = accept("delay");
            if (!delay && !accept("fade"))
                accept("time");
            QVector<double> points;
            if (!timePoints(&points))
                return fail(QObject::tr("Expected seconds: Fade 0 Thru 3"));
            (delay ? line.delayTimes : line.fadeTimes) = points;
        } else if (auto name = string()) {
            line.name = *name;
        } else {
            return fail(QObject::tr("Unexpected '%1'").arg(peek().text));
        }
    }
    if (line.groupId == 0)
        return fail(QObject::tr("A blueprint line needs a Group"));
    if (line.name.isEmpty())
        line.name = s->group(line.groupId)->name;
    s->addBlueprintLine(seq, *n, line);
    return ok(QObject::tr("Blueprint line added to Cue %1").arg(*n));
}

CommandResult Parser::cmdBuild()
{
    int seq = s->selectedSequence();
    optionalSequence(&seq);
    if (!accept("cue"))
        return fail(QObject::tr("Usage: Build Cue 1"));
    auto n = number();
    optionalSequence(&seq);
    if (!n || !s->buildCue(seq, *n))
        return fail(QObject::tr("Nothing to build"));
    return ok(QObject::tr("Cue built: blueprint output is now hard values"));
}

CommandResult Parser::cmdUpdate()
{
    if (accept("group")) {
        auto n = number();
        if (!n || !s->group(int(*n)))
            return fail(QObject::tr("No such group"));
        return s->storeGroup(int(*n)) ? ok(QObject::tr("Group %1 updated").arg(int(*n))) : fail(QObject::tr("Nothing selected"));
    }
    if (accept("preset")) {
        int type, id;
        if (!presetRef(&type, &id) || !s->preset(type, id))
            return fail(QObject::tr("No such preset"));
        return s->storePreset(type, id, Show::StoreMode::Merge)
                   ? ok(QObject::tr("Preset %1 updated").arg(lumora::presetRef(type, id)))
                   : fail(QObject::tr("No matching programmer values"));
    }
    int seq = s->selectedSequence();
    double cue = -1;
    optionalSequence(&seq);
    if (accept("cue")) {
        auto n = number();
        if (!n)
            return fail(QObject::tr("Expected a cue number"));
        cue = *n;
    }
    optionalSequence(&seq);
    const Sequence *sq = s->sequence(seq);
    if (!sq)
        return fail(QObject::tr("No such sequence"));
    if (cue < 0) {
        // No cue given: the cue that is live on this sequence's executor.
        Executor *e = s->executorForSequence(seq);
        if (!e || !e->pb().isOn() || e->pb().cueIndex >= sq->cues.size())
            return fail(QObject::tr("Sequence %1 isn't running. Use Update Cue <number>").arg(seq));
        cue = sq->cues[e->pb().cueIndex].number;
    }
    if (sq->indexOf(cue) < 0)
        return fail(QObject::tr("No such cue"));
    if (!s->storeCue(seq, cue, Show::StoreMode::Merge))
        return fail(QObject::tr("Programmer is empty"));
    Cue tmp;
    tmp.number = cue;
    return ok(QObject::tr("Cue %1 updated").arg(tmp.displayNumber()));
}

CommandResult Parser::cmdRemove()
{
    // Remove [Fixture list] -> drop programmer values; Remove ... Cue n -> drop values from a stored cue.
    QVector<int> fids = s->selection();
    int seq = s->selectedSequence();
    double cue = -1;
    while (!atEnd()) {
        if (accept("fixture") || peek().kind == Token::Number) {
            bool okIds = false;
            QVector<int> ids = fixtureList(&okIds);
            if (!okIds)
                return fail(QObject::tr("Expected fixture numbers"));
            fids.clear();
            for (int id : ids)
                if (id > 0)
                    fids << id;
            fids = s->expandMasters(fids);
        } else if (accept("group")) {
            auto n = number();
            const Group *g = n ? s->group(int(*n)) : nullptr;
            if (!g)
                return fail(QObject::tr("No such group"));
            fids = g->fixtures;
        } else if (accept("cue")) {
            auto n = number();
            if (!n)
                return fail(QObject::tr("Expected a cue number"));
            cue = *n;
        } else if (accept("sequence")) {
            auto n = number();
            if (!n)
                return fail(QObject::tr("Expected a sequence number"));
            seq = int(*n);
        } else {
            return fail(QObject::tr("Unexpected '%1'").arg(peek().text));
        }
    }
    if (fids.isEmpty())
        return fail(QObject::tr("Select fixtures first, or name them: Remove Fixture 3 Cue 2"));
    if (cue < 0) {
        s->removeProgrammerValues(fids);
        return ok(QObject::tr("Removed %1 from the programmer").arg(countOf(int(fids.size()), QObject::tr("fixture"), QObject::tr("fixtures"))));
    }
    const int removed = s->removeFromCue(seq, cue, fids);
    if (!removed)
        return fail(QObject::tr("Those fixtures have no values in that cue"));
    return ok(QObject::tr("Removed %1 from the cue").arg(countOf(removed, QObject::tr("fixture"), QObject::tr("fixtures"))));
}

CommandResult Parser::cmdCopyMove(bool move)
{
    const QString verb = move ? QObject::tr("Moved") : QObject::tr("Copied");
    const QString usage = move ? QObject::tr("Usage: Move Group 1 At 5") : QObject::tr("Usage: Copy Group 1 At 5");
    auto target = [&](int *out) -> bool {
        if (!accept("at"))
            return false;
        accept("group");
        accept("sequence");
        accept("cue");
        accept("preset");
        accept("effect");
        accept("view");
        auto n = numberText();
        if (!n)
            return false;
        // "4.7" for presets keeps only the id part
        *out = n->section('.', -1).toInt();
        return *out > 0;
    };
    if (accept("image")) {
        auto n = number();
        if (!n || !s->image(int(*n)) || !accept("at"))
            return fail(move ? QObject::tr("Usage: Move Image 1 At 5") : QObject::tr("Usage: Copy Image 1 At 5"));
        accept("image");
        auto t2 = number();
        const int to = t2 ? int(*t2) : 0;
        if (to <= 0)
            return fail(QObject::tr("Expected where it goes"));
        if (s->image(to))
            return fail(QObject::tr("Image %1 is used").arg(to));
        s->moveImage(int(*n), to, !move);
        return ok(QObject::tr("%1 Image %2 to %3").arg(verb).arg(int(*n)).arg(to));
    }
    if (accept("plot")) {
        auto n = number();
        if (!n || !s->plot(int(*n)) || !accept("at"))
            return fail(move ? QObject::tr("Usage: Move Plot 1 At 5") : QObject::tr("Usage: Copy Plot 1 At 5"));
        accept("plot");
        auto t2 = number();
        const int to = t2 ? int(*t2) : 0;
        if (to <= 0)
            return fail(QObject::tr("Expected where it goes"));
        if (s->plot(to))
            return fail(QObject::tr("Plot %1 is used").arg(to));
        s->movePlot(int(*n), to, !move);
        return ok(QObject::tr("%1 Plot %2 to %3").arg(verb).arg(int(*n)).arg(to));
    }
    if (accept("macro")) {
        auto n = number();
        int to;
        if (!n || !s->macro(int(*n)) || !accept("at"))
            return fail(move ? QObject::tr("Usage: Move Macro 1 At 5") : QObject::tr("Usage: Copy Macro 1 At 5"));
        accept("macro");
        auto t2 = number();
        to = t2 ? int(*t2) : 0;
        if (to <= 0)
            return fail(QObject::tr("Expected where it goes"));
        if (s->macro(to))
            return fail(QObject::tr("Macro %1 is used").arg(to));
        s->moveMacro(int(*n), to, !move);
        return ok(QObject::tr("%1 Macro %2 to %3").arg(verb).arg(int(*n)).arg(to));
    }
    if (accept("group")) {
        auto n = number();
        int to;
        const Group *g = n ? s->group(int(*n)) : nullptr;
        if (!g || !target(&to))
            return fail(usage);
        bool overwrite = storeMode(options()) == Show::StoreMode::Overwrite;
        if (s->group(to) && !overwrite)
            return fail(QObject::tr("Group %1 is used. Add /overwrite to replace it").arg(to));
        if (move && !s->group(to)) {
            s->renumberGroup(int(*n), to);
        } else {
            Group copy = *g;
            copy.id = to;
            s->setGroup(copy);
            if (move)
                s->deleteGroup(int(*n));
        }
        return ok(QObject::tr("%1 Group %2 to %3").arg(verb).arg(int(*n)).arg(to));
    }
    if (accept("preset")) {
        // Copy Color.1 At Color.9, or a run: Copy Color.1 Thru 10 At Color.15 (Thru Color.10 too).
        int type, id, to;
        const QString presetUsage = move ? QObject::tr("Usage: Move Color.1 At Color.9, or Move Color.1 Thru 10 At Color.15")
                                         : QObject::tr("Usage: Copy Color.1 At Color.9, or Copy Color.1 Thru 10 At Color.15");
        if (!presetRef(&type, &id))
            return fail(presetUsage);
        int last = id;
        if (accept("thru")) {
            accept("preset");
            auto t = numberText();
            if (!t)
                return fail(presetUsage);
            last = t->section('.', -1).toInt();
        }
        if (!target(&to))
            return fail(presetUsage);
        const int first = std::min(id, last), end = std::max(id, last);
        QVector<int> sources;
        for (int i = first; i <= end; ++i)
            if (s->preset(type, i))
                sources << i;
        if (sources.isEmpty())
            return fail(QObject::tr("No presets in %1 Thru %2").arg(lumora::presetRef(type, first)).arg(end));
        const int offset = to - first;
        if (offset == 0)
            return fail(QObject::tr("That's where they are"));
        const bool overwrite = storeMode(options()) == Show::StoreMode::Overwrite;
        for (int src : sources) {
            const int dest = src + offset;
            if (dest < 1)
                return fail(QObject::tr("Preset numbers start at 1"));
            const bool freedByMove = move && sources.contains(dest);
            if (s->preset(type, dest) && !overwrite && !freedByMove)
                return fail(QObject::tr("Preset %1 is used. Add /overwrite to replace it").arg(lumora::presetRef(type, dest)));
        }
        // Work from the far end, so a run moved onto itself never overwrites what's still to go.
        if (offset > 0)
            std::reverse(sources.begin(), sources.end());
        for (int src : sources) {
            const int dest = src + offset;
            if (move && !s->preset(type, dest)) {
                s->renumberPreset(type, src, dest);
            } else {
                Preset copy = *s->preset(type, src);
                copy.id = dest;
                s->setPreset(copy);
                if (move)
                    s->deletePreset(type, src);
            }
        }
        if (sources.size() == 1)
            return ok(QObject::tr("%1 %2 to %3").arg(verb, lumora::presetRef(type, sources.first()),
                                                      lumora::presetRef(type, sources.first() + offset)));
        return ok(QObject::tr("%1 %2 presets to %3 onwards").arg(verb).arg(sources.size()).arg(lumora::presetRef(type, to)));
    }
    if (accept("effect")) {
        auto n = number();
        int to;
        if (!n || !s->effect(int(*n)) || !target(&to))
            return fail(move ? QObject::tr("Usage: Move Effect 1 At 9") : QObject::tr("Usage: Copy Effect 1 At 9"));
        bool overwrite = storeMode(options()) == Show::StoreMode::Overwrite;
        if (s->effect(to) && !overwrite)
            return fail(QObject::tr("Effect %1 is used. Add /overwrite to replace it").arg(to));
        if (move && !s->effect(to)) {
            s->renumberEffect(int(*n), to);
        } else {
            Effect copy = *s->effect(int(*n));
            copy.id = to;
            s->setEffect(copy);
            if (move)
                s->deleteEffect(int(*n));
        }
        return ok(QObject::tr("%1 Effect %2 to %3").arg(verb).arg(int(*n)).arg(to));
    }
    if (accept("view")) {
        auto n = number();
        int to;
        if (!n || !s->view(int(*n)) || !target(&to))
            return fail(move ? QObject::tr("Usage: Move View 1 At 5") : QObject::tr("Usage: Copy View 1 At 5"));
        bool overwrite = storeMode(options()) == Show::StoreMode::Overwrite;
        if (s->view(to) && !overwrite)
            return fail(QObject::tr("View %1 is used. Add /overwrite to replace it").arg(to));
        StoredView copy = *s->view(int(*n));
        copy.id = to;
        if (!move)
            copy.name = QObject::tr("%1 copy").arg(copy.name);
        s->setView(copy);
        if (move)
            s->deleteView(int(*n));
        return ok(QObject::tr("%1 View %2 to %3").arg(verb).arg(int(*n)).arg(to));
    }
    if (isWord("sequence") && !isWord("cue", 2)) {
        ++pos;
        auto n = number();
        int to;
        if (!n || !s->sequence(int(*n)) || !target(&to))
            return fail(move ? QObject::tr("Usage: Move Sequence 1 At 4") : QObject::tr("Usage: Copy Sequence 1 At 4"));
        if (s->sequence(to))
            return fail(QObject::tr("Sequence %1 is used. Delete it first").arg(to));
        if (move) {
            s->renumberSequence(int(*n), to);
        } else {
            Sequence copy = *s->sequence(int(*n));
            copy.id = to;
            copy.name = QObject::tr("%1 copy").arg(copy.name);
            s->setSequence(copy);
        }
        return ok(QObject::tr("%1 Sequence %2 to %3").arg(verb).arg(int(*n)).arg(to));
    }
    int seq = s->selectedSequence();
    optionalSequence(&seq);
    if (accept("cue")) {
        auto n = number();
        optionalSequence(&seq);
        if (!n || !accept("at"))
            return fail(move ? QObject::tr("Usage: Move Cue 2 At 2.5") : QObject::tr("Usage: Copy Cue 2 At 5"));
        accept("cue");
        auto to = number();
        Sequence *sq = s->sequenceMutable(seq);
        const int idx = sq ? sq->indexOf(*n) : -1;
        if (idx < 0 || !to)
            return fail(QObject::tr("No such cue"));
        bool overwrite = storeMode(options()) == Show::StoreMode::Overwrite;
        if (sq->indexOf(*to) >= 0 && !overwrite)
            return fail(QObject::tr("That cue number is used. Add /overwrite to replace it"));
        Sequence copy = *sq;
        Cue c = copy.cues[idx];
        c.number = *to;
        if (move)
            copy.cues.removeAt(idx);
        copy.insertSorted(c);
        s->setSequence(copy);
        return ok(QObject::tr("%1 cue to %2").arg(verb).arg(c.displayNumber()));
    }
    return fail(QObject::tr("%1 what? Group, Preset, Effect, Sequence or Cue").arg(move ? QObject::tr("Move") : QObject::tr("Copy")));
}

CommandResult Parser::cmdEffect()
{
    // Effect Off               stop effects on the selection (or all programmer effects)
    // Effect 3 [Rate %] [Size %] [Spread °]   run effect 3 on the selection / adjust it
    // Effect 3 Off             stop effect 3
    if (accept("off")) {
        if (s->selection().isEmpty()) {
            for (int i = int(s->programmerEffects().size()) - 1; i >= 0; --i)
                s->removeProgrammerEffect(i);
            return ok(QObject::tr("All programmer effects stopped"));
        }
        s->removeEffectsFrom(s->selection());
        return ok(QObject::tr("Effects stopped on the selection"));
    }
    auto n = number();
    if (!n)
        return fail(QObject::tr("Usage: Effect 3, Effect 3 Rate 150 Size 50, Effect 3 Off, Effect Off"));
    const int id = int(*n);
    const Effect *e = s->effect(id);
    if (!e)
        return fail(QObject::tr("Effect %1 doesn't exist").arg(id));
    double rate = -1, size = -1, spread = -2;
    bool off = false;
    while (!atEnd()) {
        if (accept("off")) {
            off = true;
        } else if (accept("rate")) {
            auto v = number();
            if (!v) return fail(QObject::tr("Expected a percentage after Rate"));
            rate = std::max(0.01, *v / 100.0);
        } else if (accept("size")) {
            auto v = number();
            if (!v) return fail(QObject::tr("Expected a percentage after Size"));
            size = std::max(0.0, *v / 100.0);
        } else if (accept("spread")) {   // Effect 3 Spread 180: an older way to set its phase spread
            auto v = number();
            if (!v) return fail(QObject::tr("Expected degrees after Spread"));
            spread = *v;
        } else {
            return fail(QObject::tr("Unexpected '%1'").arg(peek().text));
        }
    }
    if (off) {
        s->removeEffect(id);
        return ok(QObject::tr("%1 stopped").arg(e->name));
    }
    // Adjusting an effect that's already running (and no new selection to apply to)
    const QVector<EffectInstance> &running = s->programmerEffects();
    for (int i = 0; i < running.size(); ++i) {
        if (running[i].effectId != id)
            continue;
        if (rate < 0 && size < 0 && spread < -1 && !s->selection().isEmpty())
            break;   // plain "Effect 3" with a selection re-applies below
        EffectInstance inst = running[i];
        if (rate >= 0) inst.rate = rate;
        if (size >= 0) inst.size = size;
        if (spread >= -1) inst.spread = spread;
        if (s->selection().isEmpty() || rate >= 0 || size >= 0 || spread >= -1) {
            s->setProgrammerEffect(i, inst);
            return ok(QObject::tr("%1 adjusted").arg(e->name));
        }
    }
    if (s->selection().isEmpty())
        return fail(QObject::tr("Select fixtures first"));
    if (!s->applyEffect(id, rate >= 0 ? rate : 1.0, size >= 0 ? size : 1.0, spread >= -1 ? spread : -1.0))
        return fail(QObject::tr("Couldn't run that effect"));
    return ok(QObject::tr("%1 running on %2").arg(e->name, countOf(int(s->selection().size()), QObject::tr("fixture"), QObject::tr("fixtures"))));
}

CommandResult Parser::run()
{
    if (t.isEmpty())
        return ok(QString());
    // A bare number means Fixture, like on most consoles.
    if (peek().kind == Token::Number)
        return cmdSelectFixtures();

    const Token &first = peek();
    if (first.kind != Token::Word)
        return fail(QObject::tr("Unexpected '%1'").arg(first.text));
    // "Pan +10", "Zoom 30": an attribute of the selection.
    if (first.text != QLatin1String("at") && first.text != QLatin1String("fixture") && first.text != QLatin1String("group"))
        if (auto r = attributeValue())
            return *r;
    const QString w = first.text;
    ++pos;

    if (w == "fixture") return cmdSelectFixtures();
    if (w == "group") return cmdSelectGroups();
    if (w == "at") {
        if (isTimingWord())
            return timing();
        Value v;
        if (!value(&v))
            return fail(QObject::tr("Expected a value after At"));
        return withTiming(applyValue(v), v);
    }
    if (w == "fade" || w == "delay" || w == "time") {
        --pos;
        return timing();
    }
    if (w == "full") return applyValue(Value{false, 1.0, 0, 0});
    if (w == "preset") {
        Value v;
        v.preset = true;
        if (!presetRef(&v.type, &v.id))
            return fail(QObject::tr("Expected a preset, e.g. Color.1"));
        return applyValue(v);
    }
    if (w == "attribute") {
        if (peek().kind != Token::Word && peek().kind != Token::String)
            return fail(QObject::tr("Usage: Attribute Pan At 50"));
        const QString attr = t[pos++].text;
        if (accept("park") || isWord("unpark")) {   // Attribute Pan Park: the selection's pan
            const bool on = !accept("unpark");
            if (s->selection().isEmpty())
                return fail(QObject::tr("No fixtures selected"));
            const int n = on ? s->park(s->selection(), attr) : s->unpark(s->selection(), attr);
            return parkResult(on, n, attributeLabel(attr));
        }
        if (!accept("at"))
            return fail(QObject::tr("Usage: Attribute Pan At 50, or Attribute Pan Park"));
        if (isTimingWord())   // Attribute Pan At Fade 0 Thru 3: that attribute's times
            return timingFor(attr);
        Value v;
        if (!value(&v) || v.preset)
            return fail(QObject::tr("Expected a value"));
        return withTiming(applyValue(v, attr), v, attr);
    }
    if (w == "store") return cmdStore();
    if (w == "delete") return cmdDelete();
    if (w == "label") return cmdLabel();
    if (w == "assign") return cmdAssign();
    if (w == "go" || w == "goback" || w == "off" || w == "on" || w == "pause" || w == "top") return cmdPlayback(w);
    if (w == "goto") return cmdGoto();
    if (w == "load") return cmdLoad();
    if (w == "executor") return cmdExecutor();
    if (w == "cue") return cmdCue();
    if (w == "blueprint") return cmdBlueprint();
    if (w == "build") return cmdBuild();
    if (w == "update") return cmdUpdate();
    if (w == "remove") return cmdRemove();
    if (w == "copy" || w == "move") return cmdCopyMove(w == "move");
    if (w == "default") {
        // Default Default (or Default on its own): the selection's default values into the
        // programmer. Default All, or nothing selected: every fixture. (Store Default and
        // Delete Default set and forget the defaults.)
        accept("default");
        const bool all = accept("all") || s->selection().isEmpty();
        const QVector<int> fids = all ? QVector<int>() : s->selection();
        const int n = s->home(fids);
        if (!n)
            return fail(QObject::tr("No fixtures to set to their defaults"));
        return ok(all ? QObject::tr("Every fixture to its defaults")
                      : QObject::tr("%1 to defaults").arg(countOf(int(fids.size()), QObject::tr("fixture"), QObject::tr("fixtures"))));
    }
    if (w == "effect") return cmdEffect();
    if (w == "ratemaster") {
        // RateMaster 1 At 150 (Rate %), At 128 (BPM), At 2 (Ratio). RateMaster 1 alone: Learn (a tap).
        accept("ratemaster");
        auto n = number();
        const RateMaster *cur = n ? s->rateMaster(int(*n)) : nullptr;
        if (!cur)
            return fail(QObject::tr("Rate masters are 1 to %1").arg(RateMaster::kCount));
        if (atEnd()) {
            s->tapRateMaster(cur->id);
            return ok(QObject::tr("%1: %2").arg(cur->displayName(), s->rateMaster(cur->id)->valueText()));
        }
        if (isWord("button") || isWord("fader")) {   // how the Rate masters window shows it
            RateMaster m = *cur;
            m.button = accept("button");
            accept("fader");
            s->setRateMaster(m);
            return ok(m.button ? QObject::tr("%1 shows as a button").arg(m.displayName()) : QObject::tr("%1 shows as a fader").arg(m.displayName()));
        }
        if (!accept("at"))
            return fail(QObject::tr("Usage: RateMaster 1 At 120"));
        auto v = number();
        if (!v)
            return fail(QObject::tr("Expected a value"));
        RateMaster m = *cur;
        m.setTyped(*v);
        s->setRateMaster(m);
        return ok(QObject::tr("%1: %2").arg(m.displayName(), m.valueText()));
    }
    if (w == "spread") return cmdSpread();
    if (w == "image") {
        auto n = number();
        if (!n || !s->image(int(*n)))
            return fail(QObject::tr("No such image"));
        return ok(QObject::tr("Image %1: %2 (Assign Image %1 At Plot 2 puts it on a plot)").arg(int(*n)).arg(s->image(int(*n))->displayName()));
    }
    if (w == "plot") {
        auto n = number();
        if (!n || !s->plot(int(*n)))
            return fail(QObject::tr("No such plot"));
        emit s->plotRequested(int(*n));
        return ok(QObject::tr("Plot %1: %2").arg(int(*n)).arg(s->plot(int(*n))->displayName()));
    }
    if (w == "macro") {
        auto n = number();
        if (!n || !s->macro(int(*n)))
            return fail(QObject::tr("No such macro"));
        return runMacro(int(*n));
    }
    if (w == "appearance") {
        // Appearance Sequence 2 "Red", Appearance Macro 3 "#ff8800", Appearance Macro 3 Off
        QString what;
        if (accept("sequence")) what = "sequence";
        else if (accept("macro")) what = "macro";
        else if (accept("ratemaster")) what = "ratemaster";
        else return fail(QObject::tr("Usage: Appearance Sequence 2 \"Red\", Appearance Macro 3 \"#ff8800\" or Appearance RateMaster 1 \"Blue\""));
        auto n = number();
        if (!n)
            return fail(QObject::tr("Expected a number"));
        QString color;
        if (!accept("off")) {
            QString name;
            if (auto str = string()) name = *str;
            else if (peek().kind == Token::Word) name = t[pos++].text;
            // A sequence can take its color from what it plays: Appearance Sequence 2 Follow.
            color = what == QLatin1String("sequence") && name.compare(QLatin1String("follow"), Qt::CaseInsensitive) == 0
                        ? QStringLiteral("follow") : colorFromName(name);
            if (color.isEmpty())
                return fail(QObject::tr("Unknown color '%1': use a name (Red, Orange…) or #rrggbb").arg(name));
        }
        if (!s->setAppearance(what, int(*n), color))
            return fail(QObject::tr("No such %1").arg(what));
        return ok(color.isEmpty() ? QObject::tr("Color removed")
                  : color == QLatin1String("follow") ? QObject::tr("Color: follows the fixtures")
                                                     : QObject::tr("Color %1").arg(color));
    }
    if (w == "park") return cmdPark(true);
    if (w == "unpark") return cmdPark(false);
    if (w == "next") return ok(s->stepSelection(1));
    if (w == "prev") return ok(s->stepSelection(-1));
    if (w == "all" && atEnd()) return ok(s->wholeSelection());
    if (w == "last") {
        if (!s->reselectLast())
            return fail(QObject::tr("No earlier selection"));
        return ok(QObject::tr("Last selection: %1").arg(countOf(int(s->selection().size()), QObject::tr("fixture"), QObject::tr("fixtures"))));
    }
    if (w == "flip") {
        if (s->selection().isEmpty())
            return fail(QObject::tr("Select fixtures to flip"));
        const int n = s->flip();
        return n ? ok(QObject::tr("Flipped %1").arg(countOf(n, QObject::tr("fixture"), QObject::tr("fixtures"))))
                 : fail(QObject::tr("No room to flip: pan can't go half a turn round from there"));
    }
    if (w == "view") {
        auto n = number();
        if (!n || !s->view(int(*n)))
            return fail(QObject::tr("No such view"));
        s->recallView(int(*n));
        return ok(QObject::tr("View %1: %2").arg(int(*n)).arg(s->view(int(*n))->name));
    }
    if (w == "sequence") {
        auto n = number();
        if (!n)
            return fail(QObject::tr("Expected a sequence number"));
        s->ensureSequence(int(*n));
        s->setSelectedSequence(int(*n));
        return ok(QObject::tr("Sequence %1 selected").arg(int(*n)));
    }
    if (w == "page") {
        if (acceptKind(Token::Plus)) s->setPage(s->currentPage() + 1);
        else if (acceptKind(Token::Minus)) s->setPage(s->currentPage() - 1);
        else if (auto n = number()) s->setPage(int(*n));
        else return fail(QObject::tr("Usage: Page 2, Page +, Page -"));
        return ok(QObject::tr("Page %1").arg(s->currentPage()));
    }
    if (w == "clear") return ok(s->clearStep());
    if (w == "clearall") {
        s->clearAll();
        return ok(QObject::tr("Programmer cleared"));
    }
    if (w == "highlight") {
        s->setHighlight(!s->highlight());
        return ok(s->highlight() ? QObject::tr("Highlight on") : QObject::tr("Highlight off"));
    }
    if (w == "sendosc") {
        // SendOSC 1 "/Page1/Fader201,i,50": out on OSC line 1 (as set up on the Control screen).
        auto line = number();
        auto text = string();
        if (!line || !text || !text->startsWith('/'))
            return fail(QObject::tr("Usage: SendOSC 1 \"/address,i,50\""));
        emit s->oscSendRequested(int(*line), *text);
        return ok(QObject::tr("OSC sent on line %1: %2").arg(int(*line)).arg(*text));
    }
    if (w == "grid") {
        // Grid 2/1: the next selected fixtures go there. Grid 0/0 Thru 3/1: the selection laid out
        // inside that area, along X then the next Y.
        QPoint a, b;
        if (!gridPoint(&a))
            return fail(QObject::tr("Usage: Grid 2/1, or Grid 0/0 Thru 3/1"));
        if (accept("thru")) {
            if (!gridPoint(&b))
                return fail(QObject::tr("Usage: Grid 0/0 Thru 3/1"));
            s->fillGridArea(a, b);
            return ok(QObject::tr("Selection laid out from %1/%2 to %3/%4").arg(a.x()).arg(a.y()).arg(b.x()).arg(b.y()));
        }
        s->setGridCursor(a);
        return ok(QObject::tr("Grid %1/%2").arg(a.x()).arg(a.y()));
    }
    if (w == "nexty") {   // the selection matrix: what's selected next starts a new row
        s->nextY();
        return ok(QObject::tr("Next row"));
    }
    if (w == "progfade") {
        // ProgFade 3 (on, 3 s), ProgFade On / Off, ProgFade alone toggles it.
        if (accept("off"))
            s->setProgFade(false);
        else if (accept("on"))
            s->setProgFade(true);
        else if (peek().kind == Token::Number) {
            const double t = *number();
            if (t < 0)
                return fail(QObject::tr("Usage: ProgFade 3, ProgFade On, ProgFade Off"));
            s->setProgFade(t > 0, t);
        } else if (atEnd()) {
            s->setProgFade(!s->progFadeOn());
        } else {
            return fail(QObject::tr("Usage: ProgFade 3, ProgFade On, ProgFade Off"));
        }
        return ok(s->progFadeOn() ? QObject::tr("Programmer fade on: %1 s").arg(s->progFadeTime()) : QObject::tr("Programmer fade off"));
    }
    if (w == "saveshow") {   // the same as Show → Save (Save as… for a show that has no file yet)
        emit s->saveRequested();
        return ok(QObject::tr("Show saved"));
    }
    if (w == "blackout") {
        s->setBlackout(!s->blackout());
        return ok(s->blackout() ? QObject::tr("Blackout on") : QObject::tr("Blackout off"));
    }
    return fail(QObject::tr("Unknown command '%1'").arg(w));
}

} // namespace

CommandResult CommandLine::execute(const QString &line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty())
        return {};
    m_history.append(trimmed);
    if (m_history.size() > 500)   // for Up/Down on the command line
        m_history.removeFirst();
    // SetVar $name = value (or "text"): a variable for macros and commands, used as $name.
    static const QRegularExpression setVar(QStringLiteral("^setvar\\s+\\$([A-Za-z_][A-Za-z0-9_]*)\\s*=?\\s*(.*)$"),
                                           QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch sv = setVar.match(trimmed);
    if (sv.hasMatch()) {
        QString v = m_show->expandVariables(sv.captured(2).trimmed());
        if (v.size() >= 2 && v.startsWith('"') && v.endsWith('"'))
            v = v.mid(1, v.size() - 2);
        m_show->setMacroVariable(sv.captured(1), v);
        return {true, QObject::tr("$%1 = %2").arg(sv.captured(1), v)};
    }
    const QString expanded = m_show->expandVariables(trimmed);
    QString error;
    QVector<Token> tokens = tokenize(expanded, &error);
    if (!error.isEmpty())
        return {false, error};
    Parser p(m_show, tokens);
    return p.run();
}

QStringList CommandLine::keywords()
{
    return {"Fixture", "Cell", "Flip", "Spread", "Blocks", "Wings", "Phase", "Group", "Thru", "At", "Full", "Zero", "Preset", "Store", "Delete", "Label",
            "Sequence", "Cue", "Executor", "Go", "GoBack", "Goto", "Load", "Off", "On", "Pause", "Assign",
            "Page", "Build", "Blueprint", "Fade", "Delay", "Time", "Every", "Offset", "Highlight", "Blackout",
            "Clear", "ClearAll", "Attribute", "Update", "Remove", "Copy", "Move", "Effect", "Rate", "RateMaster", "Size",
            "Default", "View", "Button", "Encoder", "Fader", "Top", "Macro", "Plot", "Appearance", "SetVar", "ProgFade", "NextY", "Grid", "SendOSC", "Undo", "Redo", "Image", "SaveShow"};
}

} // namespace lumora
