// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "poolgrid.h"

namespace lumora {

class AppContext;

// The image pool: the show's pictures (Lumora's own symbols, and anything imported), numbered
// like any pool. Tap an empty slot (or drop files on the window) to import pictures; right-click
// one to rename, replace, put it on a plot or make it a plot's background, export or delete it.
class ImagesPanel : public PoolPanel {
    Q_OBJECT
public:
    explicit ImagesPanel(AppContext *ctx, QWidget *parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent *ev) override;
    void dropEvent(QDropEvent *ev) override;

private:
    void refresh();
    void importInto(int firstSlot, bool replace);
    int importFiles(const QStringList &paths, int firstSlot, bool replaceFirst);
    AppContext *m_ctx = nullptr;
};

// A picker of the image pool's pictures (current one marked). Returns the chosen image, or 0.
int pickPoolImage(AppContext *ctx, QWidget *parent, int current);

} // namespace lumora
