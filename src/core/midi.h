// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include <memory>
#include <vector>

namespace rt::midi {
class RtMidiIn;
class RtMidiOut;
}

namespace lumora {

class Show;
struct Executor;

struct MidiMessage {
    enum Type { NoteOn, NoteOff, ControlChange, PitchBend, ProgramChange, Other };
    Type type = Other;
    int channel = 1;     // 1..16
    int number = 0;      // note / controller / program
    int value = 0;       // 0..127, pitch bend 0..16383
    QString port;

    double normalized() const;
    bool isPress() const;     // note on with velocity, CC >= 64, program change
    bool isRelease() const;   // note off, note on velocity 0, CC < 64
    QString describe() const;

    static bool parse(const std::vector<unsigned char> &bytes, MidiMessage *out);
};

// What a binding listens to. Notes match both NoteOn and NoteOff.
struct MidiSource {
    enum Kind { Note, CC, PitchBend, Program };
    Kind kind = CC;
    int channel = 1;
    int number = 0;
    QString port;   // empty = any input

    bool matches(const MidiMessage &m) const;
    bool operator==(const MidiSource &o) const {
        return kind == o.kind && channel == o.channel && number == o.number && port == o.port;
    }
    QString describe() const;
    static MidiSource fromMessage(const MidiMessage &m);
};

struct MidiTarget {
    // Encoder: the nth encoder on the encoder bar, whatever it shows (Pan on Position, Dimmer on
    // Dimmer). EncoderPush: that encoder's ranges, as a right-click shows them.
    enum Kind { ExecutorFader, ExecutorButton, GrandMaster, PageNext, PagePrev, ClearProgrammer, Blackout, Encoder, EncoderPush };
    Kind kind = ExecutorFader;
    int page = 0;        // 0 = follow the current page
    int executor = 201;
    int button = 0;      // 0..2
    int encoder = 1;     // Encoder, EncoderPush: 1 = the first on the bar

    bool operator==(const MidiTarget &o) const;
    QString describe() const;
};

struct MidiBinding {
    MidiSource source;
    MidiTarget target;
    bool pickup = true;   // soft takeover for faders without motors
    bool invert = false;
    bool relative = false;   // encoders: an endless encoder sending steps (65 = +1, 63 = −1, or 1 / 127)

    static int relativeSteps(int value);   // a relative CC's steps: +3, −1…

    QJsonObject toJson() const;
    static MidiBinding fromJson(const QJsonObject &o);
};

// Opens MIDI ports, routes incoming messages to executors and handles MIDI Learn.
//
// Learn flow: the UI arms a target (an executor fader or button, the grand master, ...);
// the next fader move, key or knob that arrives is bound to it. Any existing binding for
// that target, or from that physical control, is replaced so one control does one thing.
class MidiManager : public QObject {
    Q_OBJECT
public:
    explicit MidiManager(Show *show, QObject *parent = nullptr);
    ~MidiManager() override;

    // Ports
    QStringList availableInputs();
    QStringList availableOutputs();
    QStringList openInputs() const;
    void setOpenInputs(const QStringList &names);   // empty list + autoOpen = open every input
    QString feedbackOutput() const { return m_feedbackName; }
    void setFeedbackOutput(const QString &name);
    bool autoOpen() const { return m_autoOpen; }
    void setAutoOpen(bool on);
    QString backendError() const { return m_backendError; }

    // Learn
    bool learnMode() const { return m_learnMode; }
    void setLearnMode(bool on);
    void arm(const MidiTarget &t);
    void disarm();
    bool isArmed() const { return m_armed; }
    MidiTarget armedTarget() const { return m_armedTarget; }

    // Bindings
    const QVector<MidiBinding> &bindings() const { return m_bindings; }
    void addBinding(const MidiBinding &b);   // replaces conflicts
    void updateBinding(int index, const MidiBinding &b);
    void removeBinding(int index);
    void removeBindingsFor(const MidiTarget &t);
    QVector<int> bindingsFor(const MidiTarget &t) const;

    // Entry point for parsed messages (RtMidi thread hops here; tests call it directly).
    void handleMessage(const MidiMessage &m);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &o);

    struct Input;   // one open RtMidi input (public so the C callback can reach it)

signals:
    void messageReceived(const lumora::MidiMessage &m);
    void learned(const lumora::MidiBinding &b);
    void learnStateChanged();
    void bindingsChanged();
    void encoderTurned(int encoder, double delta);   // delta in parts of the range: 0.01 a step
    void encoderPushed(int encoder);
    void portsChanged();

private:
    Executor *resolveExecutor(const MidiTarget &t);
    void apply(int bindingIndex, const MidiMessage &m);
    void sendFeedback();
    void sendRaw(const std::vector<unsigned char> &bytes);

    Show *m_show;
    std::vector<std::unique_ptr<Input>> m_inputs;
    std::unique_ptr<rt::midi::RtMidiOut> m_out;
    QString m_feedbackName;
    QStringList m_wantedInputs;
    bool m_autoOpen = true;
    QString m_backendError;

    bool m_learnMode = false;
    bool m_armed = false;
    MidiTarget m_armedTarget;

    QVector<MidiBinding> m_bindings;
    QHash<int, double> m_lastIncoming;   // binding index -> last raw position (for pickup)
    QHash<int, bool> m_pickedUp;
    QHash<int, int> m_lastFeedback;      // binding index -> last value sent
    QTimer m_feedbackTimer;
};

} // namespace lumora

Q_DECLARE_METATYPE(lumora::MidiMessage)
Q_DECLARE_METATYPE(lumora::MidiBinding)
