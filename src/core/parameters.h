// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fixturetype.h"
#include "types.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace lumora {

// The parameters fixtures have, ready to build a fixture type from: each knows its attribute
// name, how operators call it, where it rests, what Highlight does with it, and (for wheels,
// shutters and the like) a starting set of named ranges to adjust.
struct ParameterInfo {
    QString attribute;      // standardized name, as stored ("ColorAdd_R")
    QString label;          // what operators call it ("Red")
    QString description;    // one line, shown as a tip
    double defaultValue = 0.0;
    double highlightValue = 0.0;
    bool fine = false;      // usually 16-bit (Pan, Tilt)
    QVector<ChannelSet> sets;

    Feature feature() const { return featureOf(attribute); }
    ChannelDef channel() const;   // a channel of this parameter (no DMX slot yet: coarse -1)
};

const QVector<ParameterInfo> &parameterLibrary();
const ParameterInfo *parameterInfo(const QString &attribute);

// Common groups of channels, added together: "RGB", "RGBW", "CMY", "Pan/Tilt 16-bit"...
struct ParameterBundle {
    QString name;
    QStringList attributes;
};
const QVector<ParameterBundle> &parameterBundles();

} // namespace lumora
