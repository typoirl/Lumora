// SPDX-License-Identifier: GPL-3.0-or-later
#include "dmxoutput.h"

#include "types.h"

#include <QJsonArray>
#include <QNetworkInterface>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QUdpSocket>

#include <algorithm>
#include <cstring>

namespace lumora {

QString outputKindName(OutputKind k)
{
    switch (k) {
    case OutputKind::ArtNet: return "Art-Net";
    case OutputKind::Sacn: return "sACN";
    case OutputKind::EnttecPro: return "Enttec USB Pro";
    case OutputKind::OpenDmx: return "Enttec Open DMX";
    }
    return QString();
}

QJsonObject OutputConfig::toJson() const
{
    return {{"kind", int(kind)}, {"enabled", enabled}, {"firstUniverse", firstUniverse}, {"count", count},
            {"protocolUniverse", protocolUniverse}, {"host", host}, {"serialPort", serialPort},
            {"priority", priority}, {"interface", interfaceAddress}};
}

OutputConfig OutputConfig::fromJson(const QJsonObject &o)
{
    OutputConfig c;
    c.kind = OutputKind(std::clamp(o["kind"].toInt(), 0, 3));
    c.enabled = o["enabled"].toBool(true);
    c.firstUniverse = o["firstUniverse"].toInt(1);
    c.count = std::clamp(o["count"].toInt(1), 1, kMaxUniverses);
    c.firstUniverse = std::clamp(c.firstUniverse, 1, kMaxUniverses);
    c.protocolUniverse = o["protocolUniverse"].toInt(c.kind == OutputKind::Sacn ? 1 : 0);
    c.host = o["host"].toString();
    c.serialPort = o["serialPort"].toString();
    c.priority = o["priority"].toInt(100);
    c.interfaceAddress = o["interface"].toString();
    return c;
}

QList<QPair<QString, QString>> networkInterfaces()
{
    QList<QPair<QString, QString>> out;
    for (const QNetworkInterface &nic : QNetworkInterface::allInterfaces()) {
        if (!(nic.flags() & QNetworkInterface::IsUp))
            continue;
        for (const QNetworkAddressEntry &e : nic.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol)
                out << qMakePair(QStringLiteral("%1 — %2").arg(nic.humanReadableName(), e.ip().toString()), e.ip().toString());
    }
    return out;
}

namespace {
// The interface (and its broadcast address) that owns this local IPv4 address.
bool findInterface(const QString &address, QNetworkInterface *nic, QHostAddress *broadcast)
{
    const QHostAddress want(address);
    for (const QNetworkInterface &n : QNetworkInterface::allInterfaces())
        for (const QNetworkAddressEntry &e : n.addressEntries())
            if (e.ip() == want) {
                if (nic)
                    *nic = n;
                if (broadcast)
                    *broadcast = e.broadcast();
                return true;
            }
    return false;
}
} // namespace

QByteArray DmxOutput::universeData(const QMap<int, QByteArray> &u, int consoleUniverse) const
{
    QByteArray d = u.value(consoleUniverse);
    if (d.size() != 512)
        d = QByteArray(512, 0);
    return d;
}

// ===========================================================================
// Packets
// ===========================================================================

QByteArray buildArtDmx(int portAddress, quint8 sequence, const QByteArray &dmx)
{
    QByteArray p;
    p.reserve(18 + 512);
    p.append("Art-Net", 7);
    p.append(char(0));
    p.append(char(0x00));          // OpCode 0x5000, little endian
    p.append(char(0x50));
    p.append(char(0x00));          // protocol version 14, big endian
    p.append(char(14));
    p.append(char(sequence));
    p.append(char(0));             // physical
    p.append(char(portAddress & 0xff));          // SubUni
    p.append(char((portAddress >> 8) & 0x7f));   // Net
    p.append(char(0x02));          // length 512, big endian
    p.append(char(0x00));
    QByteArray data = dmx.left(512);
    data.append(QByteArray(512 - data.size(), 0));
    p.append(data);
    return p;
}

QHostAddress sacnMulticastAddress(int universe)
{
    return QHostAddress(QStringLiteral("239.255.%1.%2").arg((universe >> 8) & 0xff).arg(universe & 0xff));
}

namespace {
void put16(QByteArray &b, int at, quint16 v)
{
    b[at] = char(v >> 8);
    b[at + 1] = char(v & 0xff);
}
void put32(QByteArray &b, int at, quint32 v)
{
    b[at] = char(v >> 24);
    b[at + 1] = char((v >> 16) & 0xff);
    b[at + 2] = char((v >> 8) & 0xff);
    b[at + 3] = char(v & 0xff);
}
} // namespace

QByteArray buildSacn(int universe, quint8 sequence, const QByteArray &dmx, const QUuid &cid,
                     const QString &sourceName, int priority)
{
    constexpr int kLen = 638;
    QByteArray p(kLen, 0);
    // Root layer
    put16(p, 0, 0x0010);
    put16(p, 2, 0x0000);
    static const char acnId[12] = {0x41, 0x53, 0x43, 0x2d, 0x45, 0x31, 0x2e, 0x31, 0x37, 0x00, 0x00, 0x00};
    std::memcpy(p.data() + 4, acnId, 12);
    put16(p, 16, quint16(0x7000 | (kLen - 16)));
    put32(p, 18, 0x00000004);
    QByteArray cidBytes = cid.toRfc4122();
    std::memcpy(p.data() + 22, cidBytes.constData(), 16);
    // Framing layer
    put16(p, 38, quint16(0x7000 | (kLen - 38)));
    put32(p, 40, 0x00000002);
    QByteArray name = sourceName.toUtf8().left(63);
    std::memcpy(p.data() + 44, name.constData(), size_t(name.size()));
    p[108] = char(std::clamp(priority, 0, 200));
    put16(p, 109, 0);                 // sync address
    p[111] = char(sequence);
    p[112] = 0;                       // options
    put16(p, 113, quint16(universe));
    // DMP layer
    put16(p, 115, quint16(0x7000 | (kLen - 115)));
    p[117] = char(0x02);
    p[118] = char(0xa1);
    put16(p, 119, 0x0000);
    put16(p, 121, 0x0001);
    put16(p, 123, 513);
    p[125] = 0;                       // start code
    QByteArray data = dmx.left(512);
    std::memcpy(p.data() + 126, data.constData(), size_t(data.size()));
    return p;
}

QByteArray buildEnttecProDmx(const QByteArray &dmx)
{
    QByteArray p;
    p.append(char(0x7e));
    p.append(char(6));                // label: Output Only Send DMX Packet
    const int len = 513;
    p.append(char(len & 0xff));
    p.append(char(len >> 8));
    p.append(char(0));                // start code
    QByteArray data = dmx.left(512);
    data.append(QByteArray(512 - data.size(), 0));
    p.append(data);
    p.append(char(0xe7));
    return p;
}

// ===========================================================================
// Art-Net
// ===========================================================================

ArtNetOutput::~ArtNetOutput() { close(); }

bool ArtNetOutput::open(QString *error)
{
    m_socket = new QUdpSocket();
    m_target = QHostAddress::Broadcast;
    if (!m_config.interfaceAddress.isEmpty()) {
        // Send from this network card, and broadcast on its own subnet.
        QHostAddress bcast;
        if (!findInterface(m_config.interfaceAddress, nullptr, &bcast)) {
            if (error)
                *error = QStringLiteral("Network interface %1 isn't available").arg(m_config.interfaceAddress);
            delete m_socket;
            m_socket = nullptr;
            return false;
        }
        m_socket->bind(QHostAddress(m_config.interfaceAddress), 0);
        if (!bcast.isNull())
            m_target = bcast;
    } else {
        m_socket->bind(QHostAddress::AnyIPv4, 0);
    }
    if (!m_config.host.isEmpty())
        m_target = QHostAddress(m_config.host);
    m_status = QStringLiteral("Sending");
    return true;
}

void ArtNetOutput::close()
{
    delete m_socket;
    m_socket = nullptr;
}

void ArtNetOutput::send(const QMap<int, QByteArray> &universes)
{
    if (!m_socket)
        return;
    const QHostAddress target = m_target;
    // Only universes with patched fixtures go out: a 2048-universe route doesn't flood the network.
    for (auto it = universes.begin(); it != universes.end(); ++it) {
        const int cu = it.key();
        const int i = cu - m_config.firstUniverse;
        if (i < 0 || i >= m_config.count)
            continue;
        quint8 &seq = m_seq[cu];
        seq = quint8(seq == 255 ? 1 : seq + 1);   // 0 disables sequencing
        QByteArray pkt = buildArtDmx((m_config.protocolUniverse + i) & 0x7fff, seq, universeData(universes, cu));
        if (m_socket->writeDatagram(pkt, target, 6454) < 0)
            m_status = m_socket->errorString();
        else
            m_status = QStringLiteral("Sending");
    }
}

// ===========================================================================
// sACN
// ===========================================================================

SacnOutput::~SacnOutput() { close(); }

bool SacnOutput::open(QString *error)
{
    m_socket = new QUdpSocket();
    if (!m_config.interfaceAddress.isEmpty()) {
        QNetworkInterface nic;
        if (!findInterface(m_config.interfaceAddress, &nic, nullptr)) {
            if (error)
                *error = QStringLiteral("Network interface %1 isn't available").arg(m_config.interfaceAddress);
            delete m_socket;
            m_socket = nullptr;
            return false;
        }
        m_socket->bind(QHostAddress(m_config.interfaceAddress), 0);
        m_socket->setMulticastInterface(nic);   // multicast leaves through this card
    } else {
        m_socket->bind(QHostAddress::AnyIPv4, 0);
    }
    m_socket->setSocketOption(QAbstractSocket::MulticastTtlOption, 8);
    m_status = QStringLiteral("Sending");
    return true;
}

void SacnOutput::close()
{
    delete m_socket;
    m_socket = nullptr;
}

void SacnOutput::send(const QMap<int, QByteArray> &universes)
{
    if (!m_socket)
        return;
    for (auto it = universes.begin(); it != universes.end(); ++it) {
        const int cu = it.key();
        const int i = cu - m_config.firstUniverse;
        if (i < 0 || i >= m_config.count)
            continue;
        const int su = std::clamp(m_config.protocolUniverse + i, 1, 63999);
        quint8 &seq = m_seq[cu];
        ++seq;
        QByteArray pkt = buildSacn(su, seq, universeData(universes, cu), m_cid, QStringLiteral("Lumora"),
                                   m_config.priority);
        const QHostAddress target = m_config.host.isEmpty() ? sacnMulticastAddress(su) : QHostAddress(m_config.host);
        if (m_socket->writeDatagram(pkt, target, 5568) < 0)
            m_status = m_socket->errorString();
        else
            m_status = QStringLiteral("Sending");
    }
}

// ===========================================================================
// Enttec USB Pro
// ===========================================================================

EnttecProOutput::~EnttecProOutput() { close(); }

bool EnttecProOutput::open(QString *error)
{
    m_port = new QSerialPort(m_config.serialPort);
    m_port->setBaudRate(57600);
    if (!m_port->open(QIODevice::WriteOnly)) {
        m_status = m_port->errorString();
        if (error)
            *error = m_status;
        delete m_port;
        m_port = nullptr;
        return false;
    }
    m_status = QStringLiteral("Connected");
    return true;
}

void EnttecProOutput::close()
{
    if (m_port) {
        m_port->close();
        delete m_port;
        m_port = nullptr;
    }
}

void EnttecProOutput::send(const QMap<int, QByteArray> &universes)
{
    if (!m_port)
        return;
    if (m_port->bytesToWrite() > 0)   // device is behind: drop the frame rather than queue latency
        return;
    m_port->write(buildEnttecProDmx(universeData(universes, m_config.firstUniverse)));
    m_port->flush();
}

// ===========================================================================
// Enttec Open DMX
// ===========================================================================

OpenDmxOutput::~OpenDmxOutput() { close(); }

bool OpenDmxOutput::open(QString *error)
{
    // Probe the port on this thread so we can report errors synchronously.
    {
        QSerialPort probe(m_config.serialPort);
        if (!probe.open(QIODevice::WriteOnly)) {
            m_status = probe.errorString();
            if (error)
                *error = m_status;
            return false;
        }
        probe.close();
    }
    m_running = true;
    m_thread.reset(QThread::create([this]() { run(); }));
    m_thread->start(QThread::TimeCriticalPriority);
    m_status = QStringLiteral("Connected");
    return true;
}

void OpenDmxOutput::close()
{
    m_running = false;
    if (m_thread) {
        m_thread->wait(1000);
        m_thread.reset();
    }
}

void OpenDmxOutput::send(const QMap<int, QByteArray> &universes)
{
    QByteArray d = universeData(universes, m_config.firstUniverse);
    QMutexLocker lock(&m_mutex);
    m_frame = QByteArray(1, 0) + d;
}

void OpenDmxOutput::run()
{
    QSerialPort port(m_config.serialPort);
    if (!port.open(QIODevice::WriteOnly))
        return;
    port.setBaudRate(250000);
    port.setDataBits(QSerialPort::Data8);
    port.setStopBits(QSerialPort::TwoStop);
    port.setParity(QSerialPort::NoParity);
    port.setFlowControl(QSerialPort::NoFlowControl);
    while (m_running) {
        QByteArray frame;
        {
            QMutexLocker lock(&m_mutex);
            frame = m_frame;
        }
        port.setBreakEnabled(true);
        QThread::usleep(110);          // break >= 88 µs
        port.setBreakEnabled(false);
        QThread::usleep(16);           // mark after break >= 8 µs
        port.write(frame);
        port.waitForBytesWritten(40);
        QThread::msleep(4);            // ~ 40 frames per second including transmit time
    }
    port.close();
}

// ===========================================================================
// Manager
// ===========================================================================

OutputManager::OutputManager(QObject *parent) : QObject(parent), m_cid(QUuid::createUuid()) {}

OutputManager::~OutputManager() = default;

QStringList OutputManager::serialPorts()
{
    QStringList out;
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
        out << info.portName();
    return out;
}

QList<QPair<QString, QString>> OutputManager::serialPortLabels()
{
    QList<QPair<QString, QString>> out;
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        QString what = info.description();
        if (!info.manufacturer().isEmpty() && !what.contains(info.manufacturer(), Qt::CaseInsensitive))
            what += QStringLiteral(" (%1)").arg(info.manufacturer());
        out << qMakePair(what.isEmpty() ? info.portName() : QStringLiteral("%1 · %2").arg(info.portName(), what), info.portName());
    }
    return out;
}

QString OutputManager::freeSerialPort() const
{
    const QStringList ports = serialPorts();
    for (const QString &p : ports) {
        bool used = false;
        for (const OutputConfig &c : m_configs)
            used |= (c.kind == OutputKind::EnttecPro || c.kind == OutputKind::OpenDmx) && c.serialPort == p;
        if (!used)
            return p;
    }
    return ports.isEmpty() ? QString() : ports.first();
}

void OutputManager::setConfigs(const QList<OutputConfig> &c)
{
    m_configs = c;
    rebuild();
}

void OutputManager::addConfig(const OutputConfig &c)
{
    m_configs.append(c);
    rebuild();
}

void OutputManager::updateConfig(int index, const OutputConfig &c)
{
    if (index < 0 || index >= m_configs.size())
        return;
    m_configs[index] = c;
    rebuild();
}

void OutputManager::removeConfig(int index)
{
    if (index < 0 || index >= m_configs.size())
        return;
    m_configs.removeAt(index);
    rebuild();
}

QString OutputManager::status(int index) const
{
    if (index < 0 || index >= m_configs.size())
        return QString();
    if (!m_configs[index].enabled)
        return tr("Disabled");
    if (!m_errors.value(index).isEmpty())
        return m_errors.value(index);
    const auto &o = m_outputs[size_t(index)];
    return o ? o->status() : tr("Not running");
}

void OutputManager::rebuild()
{
    m_outputs.clear();
    m_errors.clear();
    for (const OutputConfig &c : m_configs) {
        std::unique_ptr<DmxOutput> out;
        if (c.enabled) {
            switch (c.kind) {
            case OutputKind::ArtNet: out = std::make_unique<ArtNetOutput>(c); break;
            case OutputKind::Sacn: out = std::make_unique<SacnOutput>(c, m_cid); break;
            case OutputKind::EnttecPro: out = std::make_unique<EnttecProOutput>(c); break;
            case OutputKind::OpenDmx: out = std::make_unique<OpenDmxOutput>(c); break;
            }
        }
        QString err;
        if (out && !out->open(&err)) {
            m_errors << err;
            out.reset();
        } else {
            m_errors << QString();
        }
        m_outputs.push_back(std::move(out));
    }
    emit changed();
}

void OutputManager::send(const QMap<int, QByteArray> &universes)
{
    for (auto &o : m_outputs)
        if (o)
            o->send(universes);
}

QJsonObject OutputDefaults::toJson() const
{
    return {{"kind", int(kind)}, {"interface", interfaceAddress}, {"artnetBase", artnetBase}};
}

OutputDefaults OutputDefaults::fromJson(const QJsonObject &o)
{
    OutputDefaults d;
    d.kind = o["kind"].toInt() == int(OutputKind::Sacn) ? OutputKind::Sacn : OutputKind::ArtNet;
    d.interfaceAddress = o["interface"].toString();
    d.artnetBase = std::clamp(o["artnetBase"].toInt(0), 0, 1);
    return d;
}

void OutputManager::setDefaults(const OutputDefaults &d)
{
    m_defaults = d;
    emit changed();
}

OutputConfig OutputManager::defaultFor(int consoleUniverse, std::optional<OutputKind> kind) const
{
    OutputConfig c;
    c.kind = kind.value_or(m_defaults.kind);
    c.enabled = true;
    c.firstUniverse = consoleUniverse;
    c.count = 1;
    switch (c.kind) {
    case OutputKind::ArtNet: c.protocolUniverse = consoleUniverse - 1 + m_defaults.artnetBase; break;
    case OutputKind::Sacn: c.protocolUniverse = consoleUniverse; break;
    default: c.protocolUniverse = 0; break;
    }
    if (c.kind == OutputKind::ArtNet || c.kind == OutputKind::Sacn)
        c.interfaceAddress = m_defaults.interfaceAddress;
    return c;
}

bool OutputManager::covers(int u) const
{
    for (const OutputConfig &c : m_configs)
        if (u >= c.firstUniverse && u < c.firstUniverse + c.count)
            return true;
    return false;
}

int OutputManager::ensureUniverses(const QList<int> &universes)
{
    int added = 0;
    for (int u : universes)
        if (u >= 1 && !covers(u)) {
            m_configs.append(defaultFor(u));
            ++added;
        }
    if (added) {
        std::stable_sort(m_configs.begin(), m_configs.end(),
                         [](const OutputConfig &a, const OutputConfig &b) { return a.firstUniverse < b.firstUniverse; });
        rebuild();
    }
    return added;
}

QJsonObject OutputManager::toJson() const
{
    QJsonArray a;
    for (const OutputConfig &c : m_configs)
        a.append(c.toJson());
    return {{"routes", a}, {"cid", m_cid.toString(QUuid::WithoutBraces)}, {"defaults", m_defaults.toJson()}};
}

void OutputManager::fromJson(const QJsonObject &o)
{
    QList<OutputConfig> list;
    for (const QJsonValue &v : o["routes"].toArray()) {
        const OutputConfig c = OutputConfig::fromJson(v.toObject());
        // One row per universe now: a route over several universes becomes one output each.
        for (int i = 0; i < c.count; ++i) {
            OutputConfig one = c;
            one.firstUniverse = c.firstUniverse + i;
            one.protocolUniverse = c.protocolUniverse + i;
            one.count = 1;
            list << one;
        }
    }
    // The old starter route (Art-Net, universe 1, switched off) is on now, like every output.
    if (list.size() == 1 && list[0].kind == OutputKind::ArtNet && !list[0].enabled && list[0].firstUniverse == 1
        && list[0].protocolUniverse == 0 && list[0].host.isEmpty())
        list[0].enabled = true;
    m_defaults = OutputDefaults::fromJson(o["defaults"].toObject());
    QUuid cid(o["cid"].toString());
    if (!cid.isNull())
        m_cid = cid;
    setConfigs(list);
}

} // namespace lumora
