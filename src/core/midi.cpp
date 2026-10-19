// SPDX-License-Identifier: GPL-3.0-or-later
#include "midi.h"

#include "show.h"

#include "RtMidi.h"

#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace lumora {

// ===========================================================================
// Messages, sources, targets
// ===========================================================================

double MidiMessage::normalized() const
{
    if (type == PitchBend)
        return std::clamp(value / 16383.0, 0.0, 1.0);
    if (type == NoteOff)
        return 0.0;
    return std::clamp(value / 127.0, 0.0, 1.0);
}

bool MidiMessage::isPress() const
{
    return (type == NoteOn && value > 0) || (type == ControlChange && value >= 64) || type == ProgramChange;
}

bool MidiMessage::isRelease() const
{
    return type == NoteOff || (type == NoteOn && value == 0) || (type == ControlChange && value < 64);
}

QString MidiMessage::describe() const
{
    switch (type) {
    case NoteOn: return QStringLiteral("Ch%1 Note %2 On %3").arg(channel).arg(number).arg(value);
    case NoteOff: return QStringLiteral("Ch%1 Note %2 Off").arg(channel).arg(number);
    case ControlChange: return QStringLiteral("Ch%1 CC %2 = %3").arg(channel).arg(number).arg(value);
    case PitchBend: return QStringLiteral("Ch%1 Pitch %2").arg(channel).arg(value);
    case ProgramChange: return QStringLiteral("Ch%1 Program %2").arg(channel).arg(number);
    case Other: break;
    }
    return QStringLiteral("Other");
}

bool MidiMessage::parse(const std::vector<unsigned char> &b, MidiMessage *out)
{
    if (b.empty())
        return false;
    const int status = b[0] & 0xf0;
    MidiMessage m;
    m.channel = (b[0] & 0x0f) + 1;
    switch (status) {
    case 0x80:
        if (b.size() < 3) return false;
        m.type = NoteOff; m.number = b[1]; m.value = b[2];
        break;
    case 0x90:
        if (b.size() < 3) return false;
        m.type = b[2] == 0 ? NoteOff : NoteOn; m.number = b[1]; m.value = b[2];
        break;
    case 0xb0:
        if (b.size() < 3) return false;
        m.type = ControlChange; m.number = b[1]; m.value = b[2];
        break;
    case 0xc0:
        if (b.size() < 2) return false;
        m.type = ProgramChange; m.number = b[1]; m.value = 127;
        break;
    case 0xe0:
        if (b.size() < 3) return false;
        m.type = PitchBend; m.number = 0; m.value = b[1] | (b[2] << 7);
        break;
    default:
        return false;
    }
    *out = m;
    return true;
}

bool MidiSource::matches(const MidiMessage &m) const
{
    if (m.channel != channel)
        return false;
    if (!port.isEmpty() && !m.port.isEmpty() && port != m.port)
        return false;
    switch (kind) {
    case Note: return (m.type == MidiMessage::NoteOn || m.type == MidiMessage::NoteOff) && m.number == number;
    case CC: return m.type == MidiMessage::ControlChange && m.number == number;
    case PitchBend: return m.type == MidiMessage::PitchBend;
    case Program: return m.type == MidiMessage::ProgramChange && m.number == number;
    }
    return false;
}

QString MidiSource::describe() const
{
    QString s;
    switch (kind) {
    case Note: s = QStringLiteral("Note %1").arg(number); break;
    case CC: s = QStringLiteral("CC %1").arg(number); break;
    case PitchBend: s = QStringLiteral("Pitch Bend"); break;
    case Program: s = QStringLiteral("Program %1").arg(number); break;
    }
    return QStringLiteral("Ch%1 %2").arg(channel).arg(s);
}

MidiSource MidiSource::fromMessage(const MidiMessage &m)
{
    MidiSource s;
    s.channel = m.channel;
    s.number = m.number;
    s.port = m.port;
    switch (m.type) {
    case MidiMessage::NoteOn:
    case MidiMessage::NoteOff: s.kind = Note; break;
    case MidiMessage::PitchBend: s.kind = PitchBend; s.number = 0; break;
    case MidiMessage::ProgramChange: s.kind = Program; break;
    default: s.kind = CC; break;
    }
    return s;
}

bool MidiTarget::operator==(const MidiTarget &o) const
{
    if (kind != o.kind)
        return false;
    switch (kind) {
    case ExecutorFader: return page == o.page && executor == o.executor;
    case ExecutorButton: return page == o.page && executor == o.executor && button == o.button;
    case Encoder:
    case EncoderPush: return encoder == o.encoder;
    default: return true;
    }
}

QString MidiTarget::describe() const
{
    const QString exec = page == 0 ? QStringLiteral("Exec %1").arg(executor)
                                   : QStringLiteral("Exec %1.%2").arg(page).arg(executor);
    switch (kind) {
    case ExecutorFader: return exec + QStringLiteral(" Fader");
    case ExecutorButton: return exec + QStringLiteral(" Button %1").arg(button + 1);
    case GrandMaster: return QStringLiteral("Grand Master");
    case PageNext: return QStringLiteral("Page +");
    case PagePrev: return QStringLiteral("Page −");
    case ClearProgrammer: return QStringLiteral("Clear");
    case Blackout: return QStringLiteral("Blackout");
    case Encoder: return QStringLiteral("Encoder %1").arg(encoder);
    case EncoderPush: return QStringLiteral("Encoder %1 Push").arg(encoder);
    }
    return QString();
}

QJsonObject MidiBinding::toJson() const
{
    return {{"src", int(source.kind)}, {"channel", source.channel}, {"number", source.number},
            {"port", source.port}, {"target", int(target.kind)}, {"page", target.page},
            {"executor", target.executor}, {"button", target.button},
            {"pickup", pickup}, {"invert", invert}, {"encoder", target.encoder}, {"relative", relative}};
}

int MidiBinding::relativeSteps(int v)
{
    // Endless encoders speak one of two dialects; both fit here. Around 64: 65 = +1, 63 = −1.
    // Around 0: 1 = +1, 127 = −1. Fast turns send bigger numbers either way.
    if (v == 0 || v == 64)
        return 0;
    if (v < 32)
        return v;
    if (v > 96)
        return v - 128;
    return v - 64;
}

MidiBinding MidiBinding::fromJson(const QJsonObject &o)
{
    MidiBinding b;
    b.source.kind = MidiSource::Kind(std::clamp(o["src"].toInt(), 0, 3));
    b.source.channel = o["channel"].toInt(1);
    b.source.number = o["number"].toInt();
    b.source.port = o["port"].toString();
    b.target.kind = MidiTarget::Kind(std::clamp(o["target"].toInt(), 0, int(MidiTarget::EncoderPush)));
    b.target.encoder = std::max(1, o["encoder"].toInt(1));
    b.relative = o["relative"].toBool(false);
    b.target.page = o["page"].toInt();
    b.target.executor = o["executor"].toInt(201);
    b.target.button = o["button"].toInt();
    b.pickup = o["pickup"].toBool(true);
    b.invert = o["invert"].toBool(false);
    return b;
}

// ===========================================================================
// Manager
// ===========================================================================

namespace {
// ALSA appends client:port numbers that change between sessions; strip them.
QString stablePortName(const std::string &raw)
{
    QString n = QString::fromStdString(raw);
    n.remove(QRegularExpression(QStringLiteral("\\s+\\d+:\\d+$")));
    return n.trimmed();
}

// A controller's LED: lit like the button on screen, and Go-style buttons also while the
// playback runs, so a pad shows what's live.
int buttonFeedbackState(const Executor &e, int button)
{
    if (executorButtonLit(e, button))
        return 127;
    switch (e.buttons[button]) {
    case ButtonFn::Go:
    case ButtonFn::GoBack:
    case ButtonFn::Top:
    case ButtonFn::Select: return e.pb().isOn() ? 127 : 0;
    default: return 0;
    }
}
} // namespace

struct MidiManager::Input {
    std::unique_ptr<rt::midi::RtMidiIn> in;
    QString name;
    MidiManager *manager = nullptr;
};

static void rtMidiCallback(double, std::vector<unsigned char> *bytes, void *user)
{
    auto *input = static_cast<MidiManager::Input *>(user);
    MidiMessage m;
    if (!bytes || !MidiMessage::parse(*bytes, &m))
        return;
    m.port = input->name;
    MidiManager *mgr = input->manager;
    QMetaObject::invokeMethod(mgr, [mgr, m]() { mgr->handleMessage(m); }, Qt::QueuedConnection);
}

MidiManager::MidiManager(Show *show, QObject *parent) : QObject(parent), m_show(show)
{
    qRegisterMetaType<lumora::MidiMessage>();
    qRegisterMetaType<lumora::MidiBinding>();
    connect(&m_feedbackTimer, &QTimer::timeout, this, &MidiManager::sendFeedback);
    m_feedbackTimer.start(50);
}

MidiManager::~MidiManager()
{
    m_inputs.clear();
}

QStringList MidiManager::availableInputs()
{
    QStringList out;
    try {
        RtMidiIn probe(RtMidi::UNSPECIFIED, "Lumora probe");
        for (unsigned i = 0; i < probe.getPortCount(); ++i) {
            QString n = stablePortName(probe.getPortName(i));
            if (!n.contains(QLatin1String("Lumora")))
                out << n;
        }
        m_backendError.clear();
    } catch (const RtMidiError &e) {
        m_backendError = QString::fromStdString(e.getMessage());
    }
    return out;
}

QStringList MidiManager::availableOutputs()
{
    QStringList out;
    try {
        RtMidiOut probe(RtMidi::UNSPECIFIED, "Lumora probe");
        for (unsigned i = 0; i < probe.getPortCount(); ++i) {
            QString n = stablePortName(probe.getPortName(i));
            if (!n.contains(QLatin1String("Lumora")))
                out << n;
        }
    } catch (const RtMidiError &e) {
        m_backendError = QString::fromStdString(e.getMessage());
    }
    return out;
}

QStringList MidiManager::openInputs() const
{
    QStringList out;
    for (const auto &i : m_inputs)
        out << i->name;
    return out;
}

void MidiManager::setAutoOpen(bool on)
{
    m_autoOpen = on;
    setOpenInputs(m_wantedInputs);
}

void MidiManager::setOpenInputs(const QStringList &names)
{
    m_wantedInputs = names;
    m_inputs.clear();
    try {
        RtMidiIn probe(RtMidi::UNSPECIFIED, "Lumora probe");
        for (unsigned i = 0; i < probe.getPortCount(); ++i) {
            const QString name = stablePortName(probe.getPortName(i));
            if (name.contains(QLatin1String("Lumora")))
                continue;
            if (!m_autoOpen && !names.contains(name))
                continue;
            auto input = std::make_unique<Input>();
            input->name = name;
            input->manager = this;
            input->in = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "Lumora", 1000);
            input->in->openPort(i, "Lumora In");
            input->in->ignoreTypes(true, true, true);
            input->in->setCallback(&rtMidiCallback, input.get());
            m_inputs.push_back(std::move(input));
        }
        m_backendError.clear();
    } catch (const RtMidiError &e) {
        m_backendError = QString::fromStdString(e.getMessage());
    }
    emit portsChanged();
}

void MidiManager::setFeedbackOutput(const QString &name)
{
    m_feedbackName = name;
    m_out.reset();
    m_lastFeedback.clear();
    if (name.isEmpty()) {
        emit portsChanged();
        return;
    }
    try {
        auto out = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, "Lumora");
        for (unsigned i = 0; i < out->getPortCount(); ++i) {
            if (stablePortName(out->getPortName(i)) == name) {
                out->openPort(i, "Lumora Feedback");
                m_out = std::move(out);
                break;
            }
        }
    } catch (const RtMidiError &e) {
        m_backendError = QString::fromStdString(e.getMessage());
    }
    emit portsChanged();
}

void MidiManager::setLearnMode(bool on)
{
    m_learnMode = on;
    if (!on)
        m_armed = false;
    emit learnStateChanged();
}

void MidiManager::arm(const MidiTarget &t)
{
    m_armed = true;
    m_armedTarget = t;
    emit learnStateChanged();
}

void MidiManager::disarm()
{
    m_armed = false;
    emit learnStateChanged();
}

void MidiManager::addBinding(const MidiBinding &b)
{
    for (int i = m_bindings.size() - 1; i >= 0; --i)
        if (m_bindings[i].target == b.target || m_bindings[i].source == b.source)
            m_bindings.removeAt(i);
    m_bindings.append(b);
    m_lastIncoming.clear();
    m_pickedUp.clear();
    m_lastFeedback.clear();
    emit bindingsChanged();
}

void MidiManager::updateBinding(int index, const MidiBinding &b)
{
    if (index < 0 || index >= m_bindings.size())
        return;
    m_bindings[index] = b;
    m_pickedUp.remove(index);
    m_lastFeedback.remove(index);
    emit bindingsChanged();
}

void MidiManager::removeBinding(int index)
{
    if (index < 0 || index >= m_bindings.size())
        return;
    m_bindings.removeAt(index);
    m_lastIncoming.clear();
    m_pickedUp.clear();
    m_lastFeedback.clear();
    emit bindingsChanged();
}

void MidiManager::removeBindingsFor(const MidiTarget &t)
{
    bool any = false;
    for (int i = m_bindings.size() - 1; i >= 0; --i)
        if (m_bindings[i].target == t) {
            m_bindings.removeAt(i);
            any = true;
        }
    if (any) {
        m_lastIncoming.clear();
        m_pickedUp.clear();
        m_lastFeedback.clear();
        emit bindingsChanged();
    }
}

QVector<int> MidiManager::bindingsFor(const MidiTarget &t) const
{
    QVector<int> out;
    for (int i = 0; i < m_bindings.size(); ++i)
        if (m_bindings[i].target == t)
            out << i;
    return out;
}

Executor *MidiManager::resolveExecutor(const MidiTarget &t)
{
    const int page = t.page > 0 ? t.page : m_show->currentPage();
    return &m_show->ensureExecutor(page, t.executor);
}

void MidiManager::handleMessage(const MidiMessage &m)
{
    emit messageReceived(m);

    if (m_armed) {
        const bool learnable = m.type == MidiMessage::ControlChange || m.type == MidiMessage::PitchBend
                               || m.type == MidiMessage::ProgramChange
                               || (m.type == MidiMessage::NoteOn && m.value > 0);
        if (!learnable)
            return;
        MidiBinding b;
        b.source = MidiSource::fromMessage(m);
        b.target = m_armedTarget;
        // Buttons don't need pickup; faders do unless the controller has motors.
        b.pickup = (m_armedTarget.kind == MidiTarget::ExecutorFader || m_armedTarget.kind == MidiTarget::GrandMaster)
                   && !m_out;
        if (m_armedTarget.kind == MidiTarget::Encoder) {
            // An endless encoder sends small steps either side of 64 (or of 0); a knob sends where it is.
            // A guess from the first message; the MIDI window can change it.
            const int v = m.value;
            b.relative = m.type == MidiMessage::ControlChange && (std::abs(v - 64) <= 3 || v <= 3 || v >= 124) && v != 0 && v != 64;
        }
        addBinding(b);
        m_armed = false;
        emit learned(b);
        emit learnStateChanged();
        return;
    }

    for (int i = 0; i < m_bindings.size(); ++i)
        if (m_bindings[i].source.matches(m))
            apply(i, m);
}

void MidiManager::apply(int index, const MidiMessage &m)
{
    const MidiBinding &b = m_bindings[index];
    const MidiTarget &t = b.target;

    auto continuous = [&](double current, auto setter) {
        double v = m.normalized();
        if (b.invert)
            v = 1.0 - v;
        if (b.pickup && !m_pickedUp.value(index, false)) {
            const double prev = m_lastIncoming.value(index, -1.0);
            const bool close = std::fabs(v - current) < 0.03;
            const bool crossed = prev >= 0 && (prev - current) * (v - current) <= 0;
            m_lastIncoming[index] = v;
            if (!close && !crossed)
                return;
            m_pickedUp[index] = true;
        }
        m_lastIncoming[index] = v;
        setter(v);
    };

    switch (t.kind) {
    case MidiTarget::ExecutorFader: {
        Executor *e = resolveExecutor(t);
        continuous(e->fader, [&](double v) { m_show->setFader(*e, v); });
        break;
    }
    case MidiTarget::GrandMaster:
        continuous(m_show->grandMaster(), [&](double v) { m_show->setGrandMaster(v); });
        break;
    case MidiTarget::ExecutorButton: {
        Executor *e = resolveExecutor(t);
        if (m.isPress())
            m_show->pressButton(*e, t.button, true);
        else if (m.isRelease())
            m_show->pressButton(*e, t.button, false);
        if (m.type == MidiMessage::ProgramChange)
            m_show->pressButton(*e, t.button, false);
        break;
    }
    case MidiTarget::PageNext:
        if (m.isPress())
            m_show->setPage(m_show->currentPage() + 1);
        break;
    case MidiTarget::PagePrev:
        if (m.isPress())
            m_show->setPage(m_show->currentPage() - 1);
        break;
    case MidiTarget::ClearProgrammer:
        if (m.isPress())
            m_show->clearStep();
        break;
    case MidiTarget::Blackout:
        if (m.isPress())
            m_show->setBlackout(!m_show->blackout());
        break;
    case MidiTarget::Encoder: {
        double delta = 0;
        if (b.relative && m.type == MidiMessage::ControlChange) {
            delta = MidiBinding::relativeSteps(m.value) * 0.01;
        } else if (m.type == MidiMessage::ControlChange || m.type == MidiMessage::PitchBend) {
            // A knob: how far it moved since last time (the first message only tells where it is).
            const double v = m.normalized();
            const double prev = m_lastIncoming.value(index, -1.0);
            m_lastIncoming[index] = v;
            if (prev < 0)
                return;
            delta = v - prev;
        } else if (m.isPress()) {
            delta = 0.01;   // a key: one step up
        }
        if (b.invert)
            delta = -delta;
        if (delta != 0)
            emit encoderTurned(t.encoder, delta);
        break;
    }
    case MidiTarget::EncoderPush:
        if (m.isPress())
            emit encoderPushed(t.encoder);
        break;
    }
}

void MidiManager::sendRaw(const std::vector<unsigned char> &bytes)
{
    if (!m_out)
        return;
    try {
        m_out->sendMessage(&bytes);
    } catch (const RtMidiError &) {
    }
}

void MidiManager::sendFeedback()
{
    // Drop pickup when the value moved elsewhere (mouse, command line, another page).
    for (int i = 0; i < m_bindings.size(); ++i) {
        const MidiBinding &b = m_bindings[i];
        if (!m_pickedUp.value(i, false))
            continue;
        double cur = -1;
        if (b.target.kind == MidiTarget::ExecutorFader) {
            const int page = b.target.page > 0 ? b.target.page : m_show->currentPage();
            if (Executor *e = m_show->executor(page, b.target.executor))
                cur = e->fader;
        } else if (b.target.kind == MidiTarget::GrandMaster) {
            cur = m_show->grandMaster();
        }
        if (cur >= 0 && std::fabs(cur - m_lastIncoming.value(i, cur)) > 0.03)
            m_pickedUp[i] = false;
    }

    if (!m_out)
        return;
    for (int i = 0; i < m_bindings.size(); ++i) {
        const MidiBinding &b = m_bindings[i];
        int value = -1;
        const int page = b.target.page > 0 ? b.target.page : m_show->currentPage();
        Executor *e = m_show->executor(page, b.target.executor);
        switch (b.target.kind) {
        case MidiTarget::ExecutorFader: {
            double v = e ? e->fader : 0.0;
            if (b.invert) v = 1.0 - v;
            value = b.source.kind == MidiSource::PitchBend ? int(std::lround(v * 16383)) : int(std::lround(v * 127));
            break;
        }
        case MidiTarget::ExecutorButton:
            value = e ? buttonFeedbackState(*e, b.target.button) : 0;
            break;
        case MidiTarget::GrandMaster:
            value = int(std::lround(m_show->grandMaster() * 127));
            break;
        case MidiTarget::Blackout:
            value = m_show->blackout() ? 127 : 0;
            break;
        case MidiTarget::ClearProgrammer:
            value = m_show->programmerValues().isEmpty() ? 0 : 127;
            break;
        default:
            break;
        }
        if (value < 0 || m_lastFeedback.value(i, -1) == value)
            continue;
        m_lastFeedback[i] = value;
        const unsigned char ch = static_cast<unsigned char>((b.source.channel - 1) & 0x0f);
        const unsigned char num = static_cast<unsigned char>(b.source.number & 0x7f);
        switch (b.source.kind) {
        case MidiSource::Note:
            sendRaw({static_cast<unsigned char>(0x90 | ch), num, static_cast<unsigned char>(value & 0x7f)});
            break;
        case MidiSource::CC:
            sendRaw({static_cast<unsigned char>(0xb0 | ch), num, static_cast<unsigned char>(value & 0x7f)});
            break;
        case MidiSource::PitchBend:
            sendRaw({static_cast<unsigned char>(0xe0 | ch), static_cast<unsigned char>(value & 0x7f),
                     static_cast<unsigned char>((value >> 7) & 0x7f)});
            break;
        case MidiSource::Program:
            break;
        }
    }
}

QJsonObject MidiManager::toJson() const
{
    QJsonArray bindings;
    for (const MidiBinding &b : m_bindings)
        bindings.append(b.toJson());
    return {{"bindings", bindings}, {"inputs", QJsonArray::fromStringList(m_wantedInputs)},
            {"autoOpen", m_autoOpen}, {"feedback", m_feedbackName}};
}

void MidiManager::fromJson(const QJsonObject &o)
{
    m_bindings.clear();
    for (const QJsonValue &v : o["bindings"].toArray())
        m_bindings.append(MidiBinding::fromJson(v.toObject()));
    m_lastIncoming.clear();
    m_pickedUp.clear();
    m_lastFeedback.clear();
    m_autoOpen = o["autoOpen"].toBool(true);
    QStringList inputs;
    for (const QJsonValue &v : o["inputs"].toArray())
        inputs << v.toString();
    setOpenInputs(inputs);
    setFeedbackOutput(o["feedback"].toString());
    emit bindingsChanged();
}

} // namespace lumora
