// SPDX-License-Identifier: GPL-3.0-or-later
#include "osc.h"

#include "show.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QRegularExpression>
#include <QNetworkDatagram>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace lumora {

// ===========================================================================
// Messages
// ===========================================================================

namespace {
void putString(QByteArray &out, const QByteArray &s)
{
    out += s;
    out += '\0';
    while (out.size() % 4)
        out += '\0';
}
void putInt(QByteArray &out, qint32 v)
{
    char b[4];
    qToBigEndian(v, b);
    out.append(b, 4);
}

struct Reader {
    const QByteArray &d;
    int pos = 0;
    bool ok = true;
    QByteArray string()
    {
        const int end = d.indexOf('\0', pos);
        if (end < 0) {
            ok = false;
            return {};
        }
        QByteArray s = d.mid(pos, end - pos);
        pos = (end + 4) & ~3;
        return s;
    }
    qint32 int32()
    {
        if (pos + 4 > d.size()) {
            ok = false;
            return 0;
        }
        const qint32 v = qFromBigEndian<qint32>(d.constData() + pos);
        pos += 4;
        return v;
    }
    qint64 int64()
    {
        if (pos + 8 > d.size()) {
            ok = false;
            return 0;
        }
        const qint64 v = qFromBigEndian<qint64>(d.constData() + pos);
        pos += 8;
        return v;
    }
};

void decodeInto(const QByteArray &d, QVector<OscMessage> &out, bool &ok, int depth)
{
    if (depth > 8) {
        ok = false;
        return;
    }
    if (d.startsWith("#bundle")) {
        Reader r{d};
        r.string();   // #bundle
        r.int64();    // time tag: we act at once
        while (r.ok && r.pos + 4 <= d.size()) {
            const qint32 size = r.int32();
            if (size <= 0 || r.pos + size > d.size()) {
                ok = false;
                return;
            }
            decodeInto(d.mid(r.pos, size), out, ok, depth + 1);
            r.pos += size;
        }
        return;
    }
    Reader r{d};
    OscMessage m;
    m.address = QString::fromUtf8(r.string());
    if (!r.ok || !m.address.startsWith('/')) {
        ok = false;
        return;
    }
    QByteArray tags = r.pos < d.size() ? r.string() : QByteArray(",");
    if (!tags.startsWith(',')) {
        ok = false;
        return;
    }
    for (int i = 1; i < tags.size() && r.ok; ++i) {
        switch (tags[i]) {
        case 'i': m.args << QVariant(r.int32()); break;
        case 'f': {
            const qint32 bits = r.int32();
            float f;
            std::memcpy(&f, &bits, 4);
            m.args << QVariant(double(f));
            break;
        }
        case 'h': m.args << QVariant(qlonglong(r.int64())); break;
        case 'd': {
            const qint64 bits = r.int64();
            double v;
            std::memcpy(&v, &bits, 8);
            m.args << QVariant(v);
            break;
        }
        case 's':
        case 'S': m.args << QVariant(QString::fromUtf8(r.string())); break;
        case 'T': m.args << QVariant(true); break;
        case 'F': m.args << QVariant(false); break;
        case 'N':
        case 'I': break;
        case 'b': {
            const qint32 n = r.int32();
            if (n < 0 || r.pos + n > d.size()) {
                r.ok = false;
                break;
            }
            m.args << QVariant(d.mid(r.pos, n));
            r.pos = (r.pos + n + 3) & ~3;
            break;
        }
        default: r.ok = false; break;
        }
    }
    if (!r.ok) {
        ok = false;
        return;
    }
    out << m;
}
} // namespace

QByteArray OscMessage::encode() const
{
    QByteArray out;
    putString(out, address.toUtf8());
    QByteArray tags(",");
    QByteArray data;
    for (const QVariant &a : args) {
        switch (a.typeId()) {
        case QMetaType::Bool: tags += a.toBool() ? 'T' : 'F'; break;
        case QMetaType::Int:
        case QMetaType::LongLong:
            tags += 'i';
            putInt(data, qint32(a.toLongLong()));
            break;
        case QMetaType::Double:
        case QMetaType::Float: {
            tags += 'f';
            const float f = float(a.toDouble());
            qint32 bits;
            std::memcpy(&bits, &f, 4);
            putInt(data, bits);
            break;
        }
        default:
            tags += 's';
            putString(data, a.toString().toUtf8());
            break;
        }
    }
    putString(out, tags);
    return out + data;
}

QVector<OscMessage> OscMessage::decode(const QByteArray &packet, bool *ok)
{
    QVector<OscMessage> out;
    bool good = true;
    decodeInto(packet, out, good, 0);
    if (ok)
        *ok = good;
    return out;
}

QString OscMessage::describe() const
{
    QStringList parts{address};
    for (const QVariant &a : args)
        parts << (a.typeId() == QMetaType::QString ? QStringLiteral("\"%1\"").arg(a.toString()) : a.toString());
    return parts.join(' ');
}

QByteArray slipEncode(const QByteArray &packet)
{
    QByteArray out;
    out += char(0xC0);
    for (char c : packet) {
        if (uchar(c) == 0xC0)
            out += QByteArray("\xDB\xDC", 2);
        else if (uchar(c) == 0xDB)
            out += QByteArray("\xDB\xDD", 2);
        else
            out += c;
    }
    out += char(0xC0);
    return out;
}

QVector<QByteArray> takeTcpPackets(QByteArray &buffer)
{
    QVector<QByteArray> out;
    while (!buffer.isEmpty()) {
        if (uchar(buffer[0]) == 0xC0) {
            // SLIP: END, the packet (escaped), END.
            int i = 1;
            while (i < buffer.size() && uchar(buffer[i]) == 0xC0)
                ++i;
            const int end = buffer.indexOf(char(0xC0), i);
            if (end < 0)
                break;
            QByteArray p;
            for (int k = i; k < end; ++k) {
                if (uchar(buffer[k]) == 0xDB && k + 1 < end) {
                    ++k;
                    p += uchar(buffer[k]) == 0xDC ? char(0xC0) : char(0xDB);
                } else {
                    p += buffer[k];
                }
            }
            buffer.remove(0, end);   // keep the END: it may start the next packet
            if (!p.isEmpty())
                out << p;
            if (buffer.size() == 1) {
                buffer.clear();
                break;
            }
            continue;
        }
        // OSC 1.0 over TCP: a 32-bit length, then the packet.
        if (buffer.size() < 4)
            break;
        const qint32 n = qFromBigEndian<qint32>(buffer.constData());
        if (n <= 0 || n > (1 << 20)) {
            buffer.clear();   // not OSC
            break;
        }
        if (buffer.size() < 4 + n)
            break;
        out << buffer.mid(4, n);
        buffer.remove(0, 4 + n);
    }
    return out;
}

// ===========================================================================
// Lines
// ===========================================================================

QJsonObject OscLine::toJson() const
{
    return {{"name", name}, {"tcp", tcp}, {"destination", destination}, {"inPort", inPort}, {"outPort", outPort},
            {"prefix", prefix}, {"receive", receive}, {"send", send}, {"receiveCommands", receiveCommands}};
}

OscLine OscLine::fromJson(const QJsonObject &o)
{
    OscLine l;
    l.name = o["name"].toString();
    l.tcp = o["tcp"].toBool(false);
    l.destination = o["destination"].toString();
    l.inPort = std::clamp(o["inPort"].toInt(8000), 0, 65535);
    l.outPort = std::clamp(o["outPort"].toInt(9000), 0, 65535);
    l.prefix = o["prefix"].toString();
    l.receive = o["receive"].toBool(true);
    l.send = o["send"].toBool(false);
    l.receiveCommands = o["receiveCommands"].toBool(false);
    return l;
}

// ===========================================================================
// Manager
// ===========================================================================

OscManager::OscManager(Show *show, QObject *parent) : QObject(parent), m_show(show)
{
    connect(show, &Show::oscSendRequested, this, [this](int line, const QString &text) { sendText(line, text); });
    m_sender = new QUdpSocket(this);
    m_feedbackTimer.setInterval(100);
    connect(&m_feedbackTimer, &QTimer::timeout, this, [this]() { sendFeedback(); });
    m_feedbackTimer.start();
}

OscManager::~OscManager() = default;

void OscManager::setInputEnabled(bool on)
{
    m_inputOn = on;
    rebind();
}

void OscManager::setOutputEnabled(bool on)
{
    m_outputOn = on;
    m_lastSent.clear();
    emit statusChanged();
}

void OscManager::setLines(const QVector<OscLine> &lines)
{
    m_lines = lines;
    m_lastSent.clear();
    rebind();
    emit linesChanged();
}

void OscManager::rebind()
{
    for (QUdpSocket *s : std::as_const(m_udp))
        s->deleteLater();
    m_udp.clear();
    for (QTcpServer *s : std::as_const(m_tcpServers))
        s->deleteLater();
    m_tcpServers.clear();
    for (auto it = m_tcpClients.begin(); it != m_tcpClients.end(); ++it)
        it.key()->deleteLater();
    m_tcpClients.clear();
    m_tcpBuffers.clear();
    m_status.clear();
    if (m_inputOn)
        for (int i = 0; i < m_lines.size(); ++i) {
            const OscLine &l = m_lines[i];
            if (!(l.receive || l.receiveCommands || (l.tcp && l.send)) || l.inPort <= 0)
                continue;
            if (l.tcp) {
                auto *server = new QTcpServer(this);
                if (!server->listen(QHostAddress::Any, quint16(l.inPort))) {
                    m_status << tr("%1: can't listen on TCP %2 (%3)").arg(l.name, QString::number(l.inPort), server->errorString());
                    server->deleteLater();
                    continue;
                }
                m_status << tr("%1: listening on TCP %2").arg(l.name).arg(l.inPort);
                connect(server, &QTcpServer::newConnection, this, [this, server, i]() {
                    while (QTcpSocket *c = server->nextPendingConnection()) {
                        m_tcpClients.insert(c, i);
                        connect(c, &QTcpSocket::readyRead, this, [this, c, i]() { readTcp(c, i); });
                        connect(c, &QTcpSocket::disconnected, this, [this, c]() {
                            m_tcpClients.remove(c);
                            m_tcpBuffers.remove(c);
                            c->deleteLater();
                        });
                        m_lastSent.clear();   // a new app gets everything
                    }
                });
                m_tcpServers.insert(i, server);
            } else if (!m_udp.contains(l.inPort)) {
                auto *s = new QUdpSocket(this);
                if (!s->bind(QHostAddress::Any, quint16(l.inPort), QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
                    m_status << tr("%1: can't listen on UDP %2 (%3)").arg(l.name, QString::number(l.inPort), s->errorString());
                    s->deleteLater();
                    continue;
                }
                m_status << tr("%1: listening on UDP %2").arg(l.name).arg(l.inPort);
                connect(s, &QUdpSocket::readyRead, this, [this, s]() { readUdp(s); });
                m_udp.insert(l.inPort, s);
            }
        }
    emit statusChanged();
}

void OscManager::readUdp(QUdpSocket *socket)
{
    while (socket->hasPendingDatagrams()) {
        const QNetworkDatagram dg = socket->receiveDatagram();
        const int port = dg.destinationPort() > 0 ? dg.destinationPort() : int(socket->localPort());
        bool ok = false;
        const QVector<OscMessage> msgs = OscMessage::decode(dg.data(), &ok);
        // Every UDP line listening on this port hears it.
        for (int i = 0; i < m_lines.size(); ++i)
            if (!m_lines[i].tcp && m_lines[i].inPort == port)
                for (const OscMessage &m : msgs)
                    handle(m, i);
    }
}

void OscManager::readTcp(QTcpSocket *socket, int lineIndex)
{
    QByteArray &buf = m_tcpBuffers[socket];
    buf += socket->readAll();
    for (const QByteArray &p : takeTcpPackets(buf))
        for (const OscMessage &m : OscMessage::decode(p))
            handle(m, lineIndex);
}

void OscManager::handle(const OscMessage &msg, int lineIndex)
{
    if (lineIndex < 0 || lineIndex >= m_lines.size() || !m_inputOn)
        return;
    const OscLine &line = m_lines[lineIndex];
    QString a = msg.address;
    if (!line.prefix.isEmpty()) {
        const QString pre = QLatin1Char('/') + line.prefix;
        if (!a.startsWith(pre + QLatin1Char('/'), Qt::CaseInsensitive))
            return;   // not for us
        a = a.mid(pre.size());
    }
    emit messageSeen(msg.describe(), true, lineIndex);
    const QStringList p = a.mid(1).split('/', Qt::SkipEmptyParts);
    if (p.isEmpty())
        return;
    const QString head = p[0].toLower();
    auto arg = [&](int i, double fallback) { return i < msg.args.size() ? msg.args[i].toDouble() : fallback; };
    auto argString = [&](int i) { return i < msg.args.size() ? msg.args[i].toString() : QString(); };
    // A button edge: no argument is a press and release; 1 is down, 0 is up.
    auto edge = [&]() { return msg.args.isEmpty() ? -1 : (msg.args[0].toDouble() > 0.5 ? 1 : 0); };
    // A fader level: a float 0–1, or percent (an integer 0–100, or a float above 1).
    auto level = [&](double v) {
        const bool percent = v > 1.0 || (!msg.args.isEmpty() && msg.args[0].typeId() == QMetaType::Int);
        return std::clamp(percent ? v / 100.0 : v, 0.0, 1.0);
    };
    // Executor addresses as some apps write them: /Page1/Fader201, /Fader201, /Page1/Key201.
    {
        static const QRegularExpression pageRe(QStringLiteral("^page(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression execRe(QStringLiteral("^(fader|key|button)(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
        int page = 0, k = 0;
        if (const auto pm = pageRe.match(p[0]); pm.hasMatch() && p.size() >= 2) {
            page = pm.captured(1).toInt();
            k = 1;
        }
        if (const auto em = execRe.match(p.value(k)); em.hasMatch() && line.receive) {
            const int number = em.captured(2).toInt();
            if (number < 101 || number > 499)
                return;
            Executor &e = m_show->ensureExecutor(page > 0 ? page : m_show->currentPage(), number);
            if (em.captured(1).compare(QLatin1String("fader"), Qt::CaseInsensitive) == 0) {
                m_show->setFader(e, level(arg(0, 0.0)));
            } else {
                const int ed = edge();
                if (ed != 0) m_show->pressButton(e, 0, true);
                if (ed != 1) m_show->pressButton(e, 0, false);
            }
            return;
        }
    }

    if (head == QLatin1String("cmd") || head == QLatin1String("newcmd")) {
        if (!line.receiveCommands)
            return;
        // Placeholders, as in "Fixture %1 At %2" 5, 80.
        QString cmd = argString(0);
        for (int i = 1; i < msg.args.size(); ++i)
            cmd.replace(QStringLiteral("%%1").arg(i), msg.args[i].toString());
        if (!cmd.trimmed().isEmpty())
            emit commandRequested(cmd.trimmed(), head == QLatin1String("newcmd"));
        return;
    }
    if (!line.receive)
        return;
    if (head == QLatin1String("undo") || head == QLatin1String("redo")) {   // like Ctrl+Z, Ctrl+Y
        if (edge() != 0)
            emit commandRequested(head == QLatin1String("undo") ? QStringLiteral("Undo") : QStringLiteral("Redo"), false);
        return;
    }
    if (head == QLatin1String("ping")) {
        sendOnLine(lineIndex, OscMessage{QStringLiteral("/out/ping"), msg.args});
        m_lastSent.clear();   // an app saying hello gets everything on its next feedback
        return;
    }
    if (head == QLatin1String("key") && p.size() >= 2) {
        if (edge() != 0)
            emit keyRequested(p[1]);
        return;
    }
    if (head == QLatin1String("exec") && p.size() >= 3) {
        // /exec/<page>/<n>/… or /exec/<n>/… (the current page)
        bool twoNumbers = false;
        p.value(2).toInt(&twoNumbers);
        const int page = twoNumbers && p[1].toInt() > 0 ? p[1].toInt() : m_show->currentPage();
        const int number = twoNumbers ? p[2].toInt() : p[1].toInt();
        const QString what = (twoNumbers ? p.value(3) : p.value(2)).toLower();
        const QString sub = twoNumbers ? p.value(4) : p.value(3);
        if (number < 101 || number > 499)
            return;
        Executor &e = m_show->ensureExecutor(page, number);
        if (what == QLatin1String("fader")) {
            m_show->setFader(e, level(arg(0, 0.0)));
        } else if (what == QLatin1String("button")) {
            const int b = std::clamp(sub.toInt() - 1, 0, 2);
            const int ed = edge();
            if (ed != 0)
                m_show->pressButton(e, b, true);
            if (ed != 1)
                m_show->pressButton(e, b, false);
        } else if (!e.isEmpty() && edge() != 0) {
            if (what == QLatin1String("go")) m_show->go(e);
            else if (what == QLatin1String("goback")) m_show->goBack(e);
            else if (what == QLatin1String("off")) m_show->off(e);
            else if (what == QLatin1String("on")) { if (!e.pb().isOn()) m_show->go(e); }
            else if (what == QLatin1String("top")) m_show->gotoCue(e, 0, true);
            else if (what == QLatin1String("pause")) m_show->togglePause(e);
        }
        return;
    }
    if (head == QLatin1String("cue")) {
        // /cue/<sequence>/<cue>/fire, or /cue/fire <cue> on the selected sequence
        if (p.size() >= 4 && p[3].compare(QLatin1String("fire"), Qt::CaseInsensitive) == 0) {
            if (Executor *e = m_show->executorForSequence(p[1].toInt()))
                emit commandRequested(QStringLiteral("Goto Cue %1 Executor %2.%3").arg(p[2]).arg(e->page).arg(e->number), false);
        } else if (p.size() >= 2 && p[1].compare(QLatin1String("fire"), Qt::CaseInsensitive) == 0 && !msg.args.isEmpty()) {
            emit commandRequested(QStringLiteral("Goto Cue %1").arg(argString(0)), false);
        }
        return;
    }
    if (head == QLatin1String("macro")) {
        const QString n = p.size() >= 3 ? p[1] : argString(0);
        if (!n.isEmpty() && edge() != 0)
            emit commandRequested(QStringLiteral("Macro %1").arg(n), false);
        return;
    }
    if (head == QLatin1String("fixture") && p.size() >= 2) {
        const int fid = m_show->parseId(p[1]);
        if (!fid || !m_show->hasFixture(fid))
            return;
        if (p.size() == 2) {   // intensity, 0–100
            m_show->setAttribute(fid, QStringLiteral("Dimmer"), std::clamp(arg(0, 100.0), 0.0, 100.0) / 100.0);
            return;
        }
        // An attribute by name (any case): pan and tilt in degrees, the rest 0–100.
        for (const QString &attr : m_show->fixtureAttributes(fid))
            if (attr.compare(p[2], Qt::CaseInsensitive) == 0) {
                const double v = arg(0, 0.0);
                m_show->setAttribute(fid, attr, isAngle(attr) ? v : std::clamp(v, 0.0, 100.0) / 100.0);
                return;
            }
        return;
    }
    if (head == QLatin1String("select")) {
        const QString which = p.size() >= 2 ? p.mid(1).join(' ') : argString(0);
        if (!which.isEmpty())
            emit commandRequested(QStringLiteral("Fixture %1").arg(which), false);
        return;
    }
    if (head == QLatin1String("encoder") && p.size() >= 2) {
        const bool fine = p[1].compare(QLatin1String("fine"), Qt::CaseInsensitive) == 0;
        const int n = (fine ? p.value(2) : p[1]).toInt();
        if (n >= 1)
            emit encoderTurned(n, arg(0, 1.0) * (fine ? 0.001 : 0.01));
        return;
    }
    if (head == QLatin1String("group") && p.size() >= 2) {
        // /group/<n>: selects it; with a level, sets their intensity (0–100).
        const int g = p[1].toInt();
        if (!m_show->group(g))
            return;
        if (msg.args.isEmpty() || p.size() >= 3)
            emit commandRequested(QStringLiteral("Group %1").arg(g), false);
        else
            emit commandRequested(QStringLiteral("Group %1 At %2").arg(g).arg(std::clamp(arg(0, 100.0), 0.0, 100.0)), false);
        return;
    }
    if (head == QLatin1String("preset") && p.size() >= 3) {
        // /preset/<pool>/<n>/fire: applies it to the selection (/preset/4/2 too).
        if (edge() != 0)
            emit commandRequested(QStringLiteral("Preset %1.%2").arg(p[1]).arg(p[2]), false);
        return;
    }
    if (head == QLatin1String("master") && p.size() >= 2) {
        // /master/<n> <0..1>: its fader; /master/<n>/key: its key (Learn or Reset); /master/<n>/tap.
        const int n = p[1].toInt();
        const RateMaster *rm = m_show->rateMaster(n);
        if (!rm)
            return;
        const QString what = p.value(2).toLower();
        if (what == QLatin1String("key")) {
            if (edge() != 0) m_show->pressRateMasterKey(n);
        } else if (what == QLatin1String("tap")) {
            if (edge() != 0) m_show->tapRateMaster(n);
        } else if (what == QLatin1String("reset")) {
            if (edge() != 0) m_show->resetRateMaster(n);
        } else if (!msg.args.isEmpty()) {
            RateMaster m = *rm;
            m.setFromFader(level(arg(0, 0.0)));
            m_show->setRateMaster(m);
        }
        return;
    }
    if (head == QLatin1String("get") && p.size() >= 2) {
        // /get/version, /get/page, /get/cmd: answered on the line it came from.
        const QString what = p[1].toLower();
        if (what == QLatin1String("version"))
            sendOnLine(lineIndex, OscMessage{QStringLiteral("/out/get/version"), {QCoreApplication::applicationVersion()}});
        else if (what == QLatin1String("page"))
            sendOnLine(lineIndex, OscMessage{QStringLiteral("/out/get/page"), {m_show->currentPage()}});
        else if (what == QLatin1String("cmd") && m_commandLine)
            sendOnLine(lineIndex, OscMessage{QStringLiteral("/out/get/cmd"), {m_commandLine()}});
        return;
    }
    if (head == QLatin1String("gm")) {
        m_show->setGrandMaster(level(arg(0, 1.0)));
        return;
    }
    if (head == QLatin1String("blackout")) {
        const int ed = edge();
        m_show->setBlackout(ed < 0 ? !m_show->blackout() : ed == 1);
        return;
    }
    if (head == QLatin1String("page")) {
        if (p.size() >= 2 && p[1].compare(QLatin1String("next"), Qt::CaseInsensitive) == 0)
            m_show->setPage(m_show->currentPage() + 1);
        else if (p.size() >= 2 && p[1].compare(QLatin1String("prev"), Qt::CaseInsensitive) == 0)
            m_show->setPage(m_show->currentPage() - 1);
        else
            m_show->setPage(int(arg(0, m_show->currentPage())));
        return;
    }
    if (head == QLatin1String("progfade")) {
        const double t = arg(0, 0.0);
        m_show->setProgFade(t > 0, t);
        return;
    }
}

void OscManager::sendOnLine(int i, const OscMessage &msg)
{
    if (!m_outputOn || i < 0 || i >= m_lines.size())
        return;
    const OscLine &l = m_lines[i];
    OscMessage m = msg;
    if (!l.prefix.isEmpty())
        m.address = QLatin1Char('/') + l.prefix + m.address;
    const QByteArray packet = m.encode();
    if (l.tcp) {
        for (auto it = m_tcpClients.begin(); it != m_tcpClients.end(); ++it)
            if (it.value() == i)
                it.key()->write(slipEncode(packet));
    } else if (!l.destination.isEmpty() && l.outPort > 0) {
        m_sender->writeDatagram(packet, QHostAddress(l.destination), quint16(l.outPort));
    }
    emit messageSeen(m.describe(), false, i);
}

void OscManager::send(const OscMessage &m)
{
    for (int i = 0; i < m_lines.size(); ++i)
        if (m_lines[i].send)
            sendOnLine(i, m);
}

void OscManager::sendFeedback(bool everything)
{
    if (!m_outputOn)
        return;
    bool anySend = false;
    for (const OscLine &l : std::as_const(m_lines))
        anySend |= l.send;
    if (!anySend)
        return;
    auto put = [&](const QString &address, const QVariant &v) {
        auto it = m_lastSent.constFind(address);
        if (!everything && it != m_lastSent.constEnd() && *it == v)
            return;
        m_lastSent.insert(address, v);
        send(OscMessage{address, {v}});
    };
    const int page = m_show->currentPage();
    put(QStringLiteral("/out/page"), page);
    if (m_commandLine)
        put(QStringLiteral("/out/cmd"), m_commandLine());
    put(QStringLiteral("/out/progfade"), m_show->progFadeOn() ? m_show->progFadeTime() : 0.0);
    for (int n = 1; n <= RateMaster::kCount; ++n)
        if (const RateMaster *rm = m_show->rateMaster(n)) {
            put(QStringLiteral("/out/master/%1/name").arg(n), rm->displayName());
            put(QStringLiteral("/out/master/%1/value").arg(n), rm->valueText());
            put(QStringLiteral("/out/master/%1/fader").arg(n), rm->faderPosition());
        }
    put(QStringLiteral("/out/gm"), m_show->grandMaster());
    put(QStringLiteral("/out/blackout"), m_show->blackout() ? 1 : 0);
    for (Executor *e : m_show->allExecutors()) {
        if (e->page != page || e->isEmpty())
            continue;
        const QString base = QStringLiteral("/out/exec/%1/%2").arg(page).arg(e->number);
        put(base + QStringLiteral("/fader"), e->fader);
        QString name, cue;
        if (const Sequence *s = m_show->sequence(e->sequenceId)) {
            name = s->name;
            const int ci = e->pb().cueIndex;
            if (e->pb().isOn() && ci >= 0 && ci < s->cues.size())
                cue = QStringLiteral("%1 %2").arg(s->cues[ci].displayNumber(), s->cues[ci].name).trimmed();
        } else if (const RateMaster *rm = m_show->rateMaster(e->rateMaster)) {
            name = rm->displayName();
            cue = rm->valueText();
        }
        put(base + QStringLiteral("/name"), name);
        put(base + QStringLiteral("/cue"), cue);
        for (int b = 0; b < 3; ++b)
            put(base + QStringLiteral("/button/%1").arg(b + 1), executorButtonLit(*e, b) ? 1 : 0);
    }
    if (m_encoders) {
        const auto enc = m_encoders();
        for (int i = 0; i < enc.size(); ++i) {
            put(QStringLiteral("/out/encoder/%1/name").arg(i + 1), enc[i].first);
            put(QStringLiteral("/out/encoder/%1/value").arg(i + 1), enc[i].second);
        }
    }
}

bool OscManager::parseText(const QString &text, OscMessage *out, QString *error)
{
    // "/address,types,value,value…", as written in macros: i int, f float, s string, T/F true/false.
    const QStringList parts = text.split(',');
    OscMessage m;
    m.address = parts.value(0).trimmed();
    if (!m.address.startsWith('/')) {
        if (error) *error = QObject::tr("An OSC address starts with /");
        return false;
    }
    const QString types = parts.value(1).trimmed();
    int vi = 2;
    for (const QChar t : types) {
        const QString v = parts.value(vi).trimmed();
        switch (t.toLatin1()) {
        case 'i': m.args << QVariant(v.toInt()); ++vi; break;
        case 'f': m.args << QVariant(v.toDouble()); ++vi; break;
        case 's': m.args << QVariant(v); ++vi; break;
        case 'T': m.args << QVariant(true); break;
        case 'F': m.args << QVariant(false); break;
        default:
            if (error) *error = QObject::tr("Unknown OSC type '%1' (i, f, s, T or F)").arg(t);
            return false;
        }
    }
    *out = m;
    return true;
}

bool OscManager::sendText(int line, const QString &text, QString *error)
{
    OscMessage m;
    if (!parseText(text, &m, error))
        return false;
    if (line < 1 || line > m_lines.size()) {
        if (error) *error = QObject::tr("No OSC line %1").arg(line);
        return false;
    }
    sendOnLine(line - 1, m);
    return true;
}

QJsonObject OscManager::toJson() const
{
    QJsonArray a;
    for (const OscLine &l : m_lines)
        a.append(l.toJson());
    return {{"input", m_inputOn}, {"output", m_outputOn}, {"lines", a}};
}

void OscManager::fromJson(const QJsonObject &o)
{
    m_inputOn = o["input"].toBool(true);
    m_outputOn = o["output"].toBool(true);
    QVector<OscLine> lines;
    for (const QJsonValue &v : o["lines"].toArray())
        lines << OscLine::fromJson(v.toObject());
    setLines(lines);
}

} // namespace lumora
