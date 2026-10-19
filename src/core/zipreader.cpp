// SPDX-License-Identifier: GPL-3.0-or-later
#include "zipreader.h"

#include <algorithm>
#include <cstdlib>

// stb_image provides a small, dependency-free raw-deflate decoder.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace lumora {

namespace {
quint16 u16(const QByteArray &d, qsizetype at)
{
    return quint16(quint8(d[at])) | (quint16(quint8(d[at + 1])) << 8);
}
quint32 u32(const QByteArray &d, qsizetype at)
{
    return quint32(u16(d, at)) | (quint32(u16(d, at + 2)) << 16);
}
} // namespace

bool ZipArchive::open(const QByteArray &data, QString *error)
{
    auto fail = [error](const char *msg) {
        if (error)
            *error = QString::fromLatin1(msg);
        return false;
    };
    m_data = data;
    m_entries.clear();
    m_order.clear();
    if (data.size() < 22)
        return fail("File is too small to be a zip archive");

    // End of central directory record: scan back over a possible trailing comment.
    qsizetype eocd = -1;
    const qsizetype stop = std::max<qsizetype>(0, data.size() - 22 - 65535);
    for (qsizetype i = data.size() - 22; i >= stop; --i)
        if (u32(data, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0)
        return fail("Not a zip archive (no central directory)");
    const quint16 count = u16(data, eocd + 10);
    qsizetype p = u32(data, eocd + 16);
    for (int n = 0; n < count; ++n) {
        if (p + 46 > data.size() || u32(data, p) != 0x02014b50)
            return fail("Corrupt zip central directory");
        Entry e;
        e.method = u16(data, p + 10);
        e.compressedSize = u32(data, p + 20);
        e.size = u32(data, p + 24);
        const quint16 nameLen = u16(data, p + 28);
        const quint16 extraLen = u16(data, p + 30);
        const quint16 commentLen = u16(data, p + 32);
        e.localOffset = u32(data, p + 42);
        if (p + 46 + nameLen > data.size())
            return fail("Corrupt zip entry name");
        const QString name = QString::fromUtf8(data.mid(p + 46, nameLen));
        if (!name.endsWith('/')) {
            m_entries.insert(name, e);
            m_order << name;
        }
        p += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

QByteArray ZipArchive::file(const QString &name, bool *ok) const
{
    if (ok)
        *ok = false;
    auto it = m_entries.constFind(name);
    if (it == m_entries.constEnd())
        return {};
    const Entry &e = it.value();
    const qsizetype lh = e.localOffset;
    if (lh + 30 > m_data.size() || u32(m_data, lh) != 0x04034b50)
        return {};
    const qsizetype start = lh + 30 + u16(m_data, lh + 26) + u16(m_data, lh + 28);
    if (start + e.compressedSize > m_data.size())
        return {};
    const QByteArray raw = m_data.mid(start, e.compressedSize);
    if (e.method == 0) {
        if (ok)
            *ok = true;
        return raw;
    }
    if (e.method != 8)
        return {};
    int outLen = 0;
    char *out = stbi_zlib_decode_noheader_malloc(raw.constData(), int(raw.size()), &outLen);
    if (!out)
        return {};
    QByteArray result(out, outLen);
    free(out);
    if (ok)
        *ok = true;
    return result;
}

} // namespace lumora
