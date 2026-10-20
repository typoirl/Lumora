// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

namespace lumora {

class AppContext;

// Makes presets in one go: dimmer levels, colors (the standard colors, plus gels and color
// temperatures), gobos from the patched fixtures' wheels (or blank ones to fill in later), and
// beam looks (zoom, prism, frost, iris). Dimmer, color and zoom need no fixtures patched.
void openPresetBuilder(AppContext *ctx, QWidget *parent);

} // namespace lumora
