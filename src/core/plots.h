// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <QVector>

namespace lumora {

// One thing on a plot: a fixture (or subfixture), a pool object (group, preset, sequence,
// macro…), a line of text, or a picture (SVG or PNG): an icon, or the plot's background.
struct PlotItem {
    enum Kind { Fixture, Object, Text, Image };
    Kind kind = Fixture;
    QRectF rect{0, 0, 48, 48};   // in plot units (pixels at 100%)
    int fid = 0;                 // Fixture
    QString object;              // Object: as the command line writes it, "Group 3", "Preset 4.1", "Macro 2"
    QString text;                // Text, or a label under an object or picture
    QString color;               // Text: its color; Image: a tint for single-color icons ("" none)
    double fontSize = 14.0;      // Text
    QByteArray image;            // Image: the file's bytes (SVG or PNG), kept in the show
    QString imageName;           // the file or built-in icon it came from
    bool background = false;     // Image: behind everything, not picked when tapping
    bool round = false;          // Fixture: drawn as a circle instead of a square
    int group = 0;               // items with the same group number move and are picked together (0: none)
    // Text, pictures and fixtures can stand for something else: tapped, they act like it.
    // "Fixture 21.3" toggles that fixture in the selection; "Group 3", "Preset 4.1", "Macro 2"…
    // act like their pool tiles. Empty: the item's own behavior.
    QString link;

    QJsonObject toJson() const;
    static PlotItem fromJson(const QJsonObject &o);
};

// A plot: fixtures and pool objects laid out on a sheet to work from, with text and pictures.
struct Plot {
    int id = 0;
    QString name;
    QVector<PlotItem> items;
    double zoom = 1.0;           // 1 = 100%
    double panX = 0, panY = 0;   // the plot point at the window's top-left
    bool zoomLocked = false;     // zoom and position stay put (+, -, the wheel and dragging don't move it)

    QString displayName() const;    // its name, or "Plot 3"
    QRectF bounds() const;          // everything on it
    int indexOfFixture(int fid) const;
    int nextGroup() const;          // a group number not in use
    QVector<int> groupOf(int index) const;   // the item and everything grouped with it
    QJsonObject toJson() const;
    static Plot fromJson(const QJsonObject &o);
};

} // namespace lumora
