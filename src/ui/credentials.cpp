// SPDX-License-Identifier: GPL-3.0-or-later
#include "credentials.h"

#include <QtGlobal>


#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

namespace lumora {

#ifdef Q_OS_WIN

QByteArray protectSecret(const QByteArray &plain)
{
    DATA_BLOB in;
    in.cbData = DWORD(plain.size());
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()));
    DATA_BLOB out = {0, nullptr};
    if (!CryptProtectData(&in, L"Lumora", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return QByteArray();
    const QByteArray result = "dpapi:" + QByteArray(reinterpret_cast<const char *>(out.pbData), int(out.cbData)).toBase64();
    LocalFree(out.pbData);
    return result;
}

QByteArray unprotectSecret(const QByteArray &stored)
{
    if (!stored.startsWith("dpapi:"))
        return QByteArray();
    QByteArray blob = QByteArray::fromBase64(stored.mid(6));
    DATA_BLOB in;
    in.cbData = DWORD(blob.size());
    in.pbData = reinterpret_cast<BYTE *>(blob.data());
    DATA_BLOB out = {0, nullptr};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return QByteArray();
    const QByteArray result(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    return result;
}

#else

namespace {
QByteArray mask(QByteArray data)
{
    static const QByteArray key = "lumora-console-gdtf";
    for (int i = 0; i < data.size(); ++i)
        data[i] = char(data[i] ^ key[i % key.size()]);
    return data;
}
} // namespace

QByteArray protectSecret(const QByteArray &plain)
{
    return "obf:" + mask(plain).toBase64();
}

QByteArray unprotectSecret(const QByteArray &stored)
{
    if (!stored.startsWith("obf:"))
        return QByteArray();
    return mask(QByteArray::fromBase64(stored.mid(4)));
}

#endif

} // namespace lumora
