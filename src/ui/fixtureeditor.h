// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fixturetype.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTreeWidget;

namespace lumora {

class AppContext;

// Build a fixture type from scratch, or edit one from any library: its modes, each mode's
// channels (DMX slot, 16-bit fine slot, what parameter it controls, name, default and highlight)
// and each channel's named ranges (gobo slots, colors, shutter modes...).
// Channels come from a library of standard parameters (or groups of them, like RGBW), and can be
// virtual: no DMX slot, but programmable. RGB fixtures always get a virtual Dimmer.
// Saved into the show (and the show file); an edited type replaces the original everywhere it is
// patched in that show.
class FixtureEditorDialog : public QDialog {
    Q_OBJECT
public:
    // typeId empty: a new fixture type.
    FixtureEditorDialog(AppContext *ctx, const QString &typeId, QWidget *parent = nullptr);

    static QStringList knownAttributes();

private:
    void loadModes();
    void modeChanged();
    void loadChannels();
    void channelChanged();
    void loadRanges();
    void readChannels();   // table → m_type (current mode)
    void readRanges();     // table → current channel
    void validate();
    void save();
    // Adds library parameters to the current mode, on the lowest free DMX slots.
    void addParameters(const QStringList &attributes);
    void fillLibrary(const QString &filter);
    void ensureVirtualDimmer();   // after channel edits; reloads the table when it changes
    void makeCells();             // repeat one pixel's channels as N cells
    // The lowest slot with `width` free slots from it (gaps left by removed channels first).
    static int nextFreeSlot(const FixtureMode &m, int width = 1);
    FixtureMode *mode();
    ChannelDef *channel();

    AppContext *m_ctx = nullptr;
    FixtureType m_type;
    bool m_isNew = true;
    int m_mode = -1;
    int m_channel = -1;
    bool m_loading = false;

    QLineEdit *m_maker = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_short = nullptr;
    QListWidget *m_modes = nullptr;
    QTableWidget *m_channels = nullptr;
    QTreeWidget *m_library = nullptr;
    QTableWidget *m_ranges = nullptr;
    QLabel *m_rangeTitle = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_save = nullptr;
};

} // namespace lumora
