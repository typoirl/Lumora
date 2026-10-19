// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>

namespace lumora {

// Keeps a secret (the GDTF Share password, for "Stay signed in") on this computer. On Windows it
// is encrypted for the signed-in Windows user (DPAPI), so another account or computer can't read
// it. Elsewhere it is only obscured.
QByteArray protectSecret(const QByteArray &plain);
QByteArray unprotectSecret(const QByteArray &stored);

} // namespace lumora
