// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

namespace lumora {

// Minimal read-only zip archive reader (stored and deflate entries), enough for GDTF files.
class ZipArchive {
public:
    bool open(const QByteArray &data, QString *error = nullptr);
    QStringList names() const { return m_order; }
    bool contains(const QString &name) const { return m_entries.contains(name); }
    QByteArray file(const QString &name, bool *ok = nullptr) const;

private:
    struct Entry {
        quint16 method = 0;
        quint32 compressedSize = 0;
        quint32 size = 0;
        quint32 localOffset = 0;
    };
    QByteArray m_data;
    QHash<QString, Entry> m_entries;
    QStringList m_order;
};

} // namespace lumora
