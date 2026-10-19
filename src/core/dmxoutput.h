// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QUuid>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

class QUdpSocket;
class QSerialPort;

namespace lumora {

enum class OutputKind { ArtNet, Sacn, EnttecPro, OpenDmx };
QString outputKindName(OutputKind k);

// One output route: console universes [firstUniverse, firstUniverse + count) go to
// protocol universes starting at `protocolUniverse` on the chosen device/protocol.
struct OutputConfig {
    OutputKind kind = OutputKind::ArtNet;
    bool enabled = true;
    int firstUniverse = 1;
    int count = 1;
    int protocolUniverse = 0;   // Art-Net port-address (0-based) or sACN universe (1-based)
    QString host;               // Art-Net / sACN unicast target; empty = broadcast / multicast
    QString interfaceAddress;   // the local IPv4 address (network card) to send from; empty = any
    QString serialPort;         // USB interfaces
    int priority = 100;         // sACN

    QJsonObject toJson() const;
    static OutputConfig fromJson(const QJsonObject &o);
};

// The computer's IPv4 network interfaces, as "name — address" with the address.
QList<QPair<QString, QString>> networkInterfaces();

class DmxOutput {
public:
    explicit DmxOutput(const OutputConfig &c) : m_config(c) {}
    virtual ~DmxOutput() = default;
    virtual bool open(QString *error) = 0;
    virtual void close() {}
    virtual void send(const QMap<int, QByteArray> &universes) = 0;
    const OutputConfig &config() const { return m_config; }
    QString status() const { return m_status; }

protected:
    QByteArray universeData(const QMap<int, QByteArray> &u, int consoleUniverse) const;
    OutputConfig m_config;
    QString m_status;
};

// ---- Packet builders (exposed for tests) ----------------------------------
QByteArray buildArtDmx(int portAddress, quint8 sequence, const QByteArray &dmx);
QByteArray buildSacn(int universe, quint8 sequence, const QByteArray &dmx, const QUuid &cid,
                     const QString &sourceName, int priority);
QByteArray buildEnttecProDmx(const QByteArray &dmx);
QHostAddress sacnMulticastAddress(int universe);

class ArtNetOutput : public DmxOutput {
public:
    using DmxOutput::DmxOutput;
    ~ArtNetOutput() override;
    bool open(QString *error) override;
    void close() override;
    void send(const QMap<int, QByteArray> &universes) override;

private:
    QUdpSocket *m_socket = nullptr;
    QHostAddress m_target;
    QMap<int, quint8> m_seq;
};

class SacnOutput : public DmxOutput {
public:
    SacnOutput(const OutputConfig &c, const QUuid &cid) : DmxOutput(c), m_cid(cid) {}
    ~SacnOutput() override;
    bool open(QString *error) override;
    void close() override;
    void send(const QMap<int, QByteArray> &universes) override;

private:
    QUdpSocket *m_socket = nullptr;
    QUuid m_cid;
    QMap<int, quint8> m_seq;
};

class EnttecProOutput : public DmxOutput {
public:
    using DmxOutput::DmxOutput;
    ~EnttecProOutput() override;
    bool open(QString *error) override;
    void close() override;
    void send(const QMap<int, QByteArray> &universes) override;

private:
    QSerialPort *m_port = nullptr;
};

// Enttec Open DMX has no microcontroller: the host generates break + 250 kbaud frames,
// so it runs on its own thread to keep timing steady.
class OpenDmxOutput : public DmxOutput {
public:
    using DmxOutput::DmxOutput;
    ~OpenDmxOutput() override;
    bool open(QString *error) override;
    void close() override;
    void send(const QMap<int, QByteArray> &universes) override;

private:
    void run();
    std::unique_ptr<QThread> m_thread;
    std::atomic<bool> m_running{false};
    QMutex m_mutex;
    QByteArray m_frame{513, 0};
};

// What a universe gets when it's first patched: a protocol, a network card, and where Art-Net
// numbering starts (console universe 1 → Art-Net 0 or 1). sACN universes match the console's.
struct OutputDefaults {
    OutputKind kind = OutputKind::ArtNet;
    QString interfaceAddress;   // empty: any
    int artnetBase = 0;         // the Art-Net universe console universe 1 goes to

    QJsonObject toJson() const;
    static OutputDefaults fromJson(const QJsonObject &o);
};

// One output per row: a console universe sent somewhere. A universe can have several (Art-Net and
// sACN at once, two USB interfaces…), and its network universe can be anything.
class OutputManager : public QObject {
    Q_OBJECT
public:
    explicit OutputManager(QObject *parent = nullptr);
    ~OutputManager() override;

    const OutputDefaults &defaults() const { return m_defaults; }
    void setDefaults(const OutputDefaults &d);
    // An output for this universe with the defaults (or this protocol), on.
    OutputConfig defaultFor(int consoleUniverse, std::optional<OutputKind> kind = std::nullopt) const;
    bool covers(int consoleUniverse) const;   // any output, on or off, sends it
    // Adds a default output for each of these universes that has none. Returns how many.
    int ensureUniverses(const QList<int> &universes);

    const QList<OutputConfig> &configs() const { return m_configs; }
    void setConfigs(const QList<OutputConfig> &c);
    void addConfig(const OutputConfig &c);
    void updateConfig(int index, const OutputConfig &c);
    void removeConfig(int index);
    QString status(int index) const;
    static QStringList serialPorts();
    // USB serial ports with what they are ("COM3 · DMX USB PRO"), as (label, port name).
    static QList<QPair<QString, QString>> serialPortLabels();
    // A port no USB output uses yet (the first one when all are taken), or empty.
    QString freeSerialPort() const;

    void send(const QMap<int, QByteArray> &universes);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &o);

signals:
    void changed();

private:
    void rebuild();
    QList<OutputConfig> m_configs;
    OutputDefaults m_defaults;
    std::vector<std::unique_ptr<DmxOutput>> m_outputs;   // parallel to m_configs (nullptr if disabled)
    QStringList m_errors;
    QUuid m_cid;
};

} // namespace lumora
