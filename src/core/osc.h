// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariant>
#include <QVector>

class QUdpSocket;
class QTcpServer;
class QTcpSocket;

namespace lumora {

class Show;

// One OSC message: an address and its arguments (int, float, string, bool).
struct OscMessage {
    QString address;
    QVariantList args;

    QByteArray encode() const;
    // One message, or each message in a bundle (nested bundles too).
    static QVector<OscMessage> decode(const QByteArray &packet, bool *ok = nullptr);
    QString describe() const;   // "/exec/1/201/fader 0.5"
};

// Where OSC comes from and goes to. Like a line on a console's OSC setup: a name, UDP or TCP,
// the other device's address, the ports, an optional prefix, and what it may do.
//   UDP: listens on inPort; sends to destination:outPort.
//   TCP: listens on inPort for apps that connect; sends to them.
struct OscLine {
    QString name;
    bool tcp = false;
    QString destination;          // the other device's IP (or a broadcast address); UDP sends here
    int inPort = 8000;
    int outPort = 9000;
    QString prefix;               // optional, without slashes: messages must start /prefix/, and ours do
    bool receive = true;          // playback and data messages (/exec, /gm, /fixture…)
    bool send = false;            // feedback: fader levels, cues, button lights, encoders
    bool receiveCommands = false; // /cmd: whole command lines

    QJsonObject toJson() const;
    static OscLine fromJson(const QJsonObject &o);
    bool operator==(const OscLine &o) const { return toJson() == o.toJson(); }
};

// Lumora's OSC: what it understands (after the line's prefix):
//   /cmd "Fixture 1 At 50"          a command line (Receive commands)
//   /newcmd "…"                     the same, from a clean command line
//   /key/<name> [1|0]               a key, like on the command keys (Go, Clear, Enter, 7…)
//   /exec/<page>/<n>/fader <0..1>   an executor's fader (page 0 or left out: the current page)
//   /exec/<page>/<n>/button/<b> [1|0]   its button 1–3, pressed and released
//   /exec/<page>/<n>/go | goback | off | on | top | pause
//   /cue/<sequence>/<cue>/fire      a cue on the sequence's executor   /cue/fire <cue>: the selected sequence
//   /macro/<n>/fire                 a macro      /macro/fire <n>
//   /fixture/<id> <0..100>          a fixture's intensity   /fixture/<id>/<attribute> <value> (pan/tilt in degrees, else 0–100)
//   /select/<id> or /select "1 Thru 8"
//   /encoder/<n> <ticks>            the encoder bar's nth encoder (/encoder/fine/<n> for fine)
//   /gm <0..1>  /blackout [1|0]  /page <n> | /page/next | /page/prev  /progfade <seconds>  /ping
//   /group/<n> [level]   /preset/<pool>/<n>/fire   /master/<n> <0..1> | /master/<n>/key | tap | reset
//   /Page1/Fader201 <level>, /Fader201, /Page1/Key201 [1|0]   (levels: 0–1, or integers 0–100)
//   /get/version | page | cmd           answered under /out/get/…
// Feedback (lines with Send on) goes out under /out/…: /out/exec/<page>/<n>/fader, /name, /cue,
// /button/<b>; /out/gm, /out/blackout, /out/page, /out/encoder/<n>/name and /value.
class OscManager : public QObject {
    Q_OBJECT
public:
    explicit OscManager(Show *show, QObject *parent = nullptr);
    ~OscManager() override;

    bool inputEnabled() const { return m_inputOn; }
    bool outputEnabled() const { return m_outputOn; }
    void setInputEnabled(bool on);
    void setOutputEnabled(bool on);
    const QVector<OscLine> &lines() const { return m_lines; }
    void setLines(const QVector<OscLine> &lines);
    QStringList status() const { return m_status; }   // ports listening, errors

    // Feedback the program supplies: the encoder bar's names and values, in order.
    void setEncoderSource(std::function<QVector<QPair<QString, QString>>()> source) { m_encoders = std::move(source); }
    // …and the command line's text (sent as /out/cmd, and asked for with /get/cmd).
    void setCommandLineSource(std::function<QString()> source) { m_commandLine = std::move(source); }
    // SendOSC 1 "/address,if,5,0.5": a message written as text (address, then its types and values
    // after commas) sent on line 1, whether or not that line sends feedback.
    bool sendText(int line, const QString &text, QString *error = nullptr);
    static bool parseText(const QString &text, OscMessage *out, QString *error = nullptr);

    // Entry point for a received message (sockets call it; tests call it directly).
    void handle(const OscMessage &m, int lineIndex);
    // Sends to every line with Send on (with its prefix).
    void send(const OscMessage &m);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &o);

signals:
    void commandRequested(const QString &command, bool fresh);
    void keyRequested(const QString &key);
    void encoderTurned(int encoder, double delta);   // like MIDI: parts of the range
    void messageSeen(const QString &text, bool incoming, int lineIndex);
    void statusChanged();
    void linesChanged();

private:
    void rebind();
    void readUdp(QUdpSocket *socket);
    void readTcp(QTcpSocket *socket, int lineIndex);
    void sendFeedback(bool everything = false);
    void sendOnLine(int lineIndex, const OscMessage &m);
    Show *m_show;
    bool m_inputOn = true, m_outputOn = true;
    QVector<OscLine> m_lines;
    QHash<int, QUdpSocket *> m_udp;             // port -> socket
    QHash<int, QTcpServer *> m_tcpServers;      // line -> server
    QHash<QTcpSocket *, int> m_tcpClients;      // connected apps -> line
    QHash<QTcpSocket *, QByteArray> m_tcpBuffers;
    QUdpSocket *m_sender = nullptr;
    QStringList m_status;
    QTimer m_feedbackTimer;
    QHash<QString, QVariant> m_lastSent;        // address -> value, so only changes go out
    std::function<QVector<QPair<QString, QString>>()> m_encoders;
    std::function<QString()> m_commandLine;
};

// OSC over TCP: OSC 1.1's SLIP framing (what Lumora sends); length-prefixed packets (OSC 1.0)
// are read too.
QByteArray slipEncode(const QByteArray &packet);
QVector<QByteArray> takeTcpPackets(QByteArray &buffer);

} // namespace lumora
