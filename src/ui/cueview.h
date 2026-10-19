// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPair>
#include <QVector>
#include <QWidget>

#include <functional>

#include "paneloptions.h"
#include "poolgrid.h"

class QCheckBox;
class QComboBox;
class QDialog;
class QLabel;
class QPushButton;
class QTableWidget;

namespace lumora {

class AppContext;
class PoolPanel;
struct Sequence;

// Sequences pool, the cue list of the selected sequence, and the blueprint editor for the selected cue.
class Show;
// The blueprint preset picker (a tab per preset pool plus "All presets"), for screenshots and tests.
QWidget *makePresetPicker(Show *show, const QVector<QPair<int, int>> &current, QWidget *parent);

// Right-click → Appearance: the sequence's color, for its pool tile and executors.
void editAppearance(QWidget *parent, Show *show, int sequenceId);

// The sequence pool: tap to select (or store the next cue into it with Store), right-click for
// its name, priority, Off when overridden, appearance and executors. A window of its own, and
// the left side of the Cues window.
class SequencePool : public PoolPanel {
    Q_OBJECT
public:
    explicit SequencePool(AppContext *ctx, QWidget *parent = nullptr);
    void refresh();

private:
    AppContext *m_ctx = nullptr;
};

class CueView : public QWidget, public PanelOptions {
    Q_OBJECT
public:
    explicit CueView(AppContext *ctx, QWidget *parent = nullptr);
    // The sequence pool's grid (tiles wide and tall, first number, tile size).
    QJsonObject saveOptions() const override;
    void loadOptions(const QJsonObject &o) override;
    void buildOptions(QuickEditor *editor) override;

private:
    void refreshCues();
    void refreshCurrentCue();
    void refreshBlueprints();
    double selectedCueNumber(bool *ok) const;
    void editSequence(const std::function<void(Sequence &)> &fn);
    QVector<int> selectedCueRows() const;
    void triggerMenu(int row, const QPoint &globalPos);   // Go, Wait…, Follow for the selected cues

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:

    AppContext *m_ctx = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_execLabel = nullptr;
    QCheckBox *m_tracking = nullptr;
    QCheckBox *m_offOverridden = nullptr;
    QComboBox *m_priority = nullptr;
    QCheckBox *m_wrap = nullptr;
    QTableWidget *m_cues = nullptr;
    QTableWidget *m_blueprints = nullptr;
    QLabel *m_blueprintTitle = nullptr;
    QLabel *m_blueprintHint = nullptr;
    QPushButton *m_build = nullptr;
    QPushButton *m_addLine = nullptr;
    QPushButton *m_removeLine = nullptr;
    bool m_updating = false;
};

} // namespace lumora
