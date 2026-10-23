// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace lumora {

// A picture in the show's image pool: an SVG, PNG, JPEG, BMP or GIF, kept in the show file.
// Plots place them (as pictures or as a background) by their number, so replacing an image
// changes it everywhere it's used.
struct PoolImage {
    int id = 0;
    QString name;
    QByteArray data;   // the file's bytes
    QString file;      // the file it was imported from (for reference)
    QString builtin;   // one of Lumora's own symbols ("truss", "speaker"…); empty for imports
    bool tintable = false;   // a single-color symbol drawn in a color of choice

    bool isSvg() const { return dataIsSvg(data); }
    QString displayName() const;   // its name, or "Image 3"
    QJsonObject toJson() const;
    static PoolImage fromJson(const QJsonObject &o);
    static bool dataIsSvg(const QByteArray &bytes);
};

// The symbols a new show's image pool starts with, in slots 1 onwards: arrow, truss, stage,
// speaker, person, star, circle, square, triangle, door, camera, fog and three fixture symbols.
QVector<PoolImage> defaultPoolImages();

} // namespace lumora
