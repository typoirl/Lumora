// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFrame>
#include <QIcon>
#include <QVector>

#include <functional>

class QLineEdit;
class QTableWidget;
class QDoubleSpinBox;
class QVBoxLayout;

namespace lumora {

// The small editor that opens when you right-click something: its name or value is ready to
// type into (Enter applies, Esc cancels), with the object's other actions underneath.
class QuickEditor : public QFrame {
    Q_OBJECT
public:
    explicit QuickEditor(const QString &title, QWidget *parent = nullptr);

    // allowEmpty: clearing the field applies too (e.g. an address cleared to unpatch).
    void addNameField(const QString &label, const QString &current, std::function<void(const QString &)> apply,
                      bool allowEmpty = false, const QString &placeholder = QString());
    // A value in percent (0..100), applied as 0..1.
    // A 0..1 value shown as a percentage, or (with a scale of 1) any range: degrees for pan and tilt.
    void addValueField(const QString &label, double value, std::function<void(double)> apply, double min = 0.0,
                       double max = 100.0, const QString &suffix = QStringLiteral(" %"), double scale = 100.0);
    void addSection(const QString &title);
    void addAction(const QString &label, std::function<void()> fn, const QIcon &icon = QIcon(), bool danger = false);
    void addWidget(QWidget *w);
    // Window options: a row of choices (one is current) and a number. Both apply as you change them.
    void addSegmented(const QString &label, const QStringList &options, int current, std::function<void(int)> apply);
    void addNumber(const QString &label, int min, int max, int value, std::function<void(int)> apply, int step = 1);
    // Seconds, applied as you change them; with `optional`, the bottom of the range is "None" (-1).
    // A scrolling list of choices, for long lists such as a gobo wheel's slots.
    struct Choice {
        QString label;
        QIcon icon;
        std::function<void()> fn;
    };
    void addChoices(const QVector<Choice> &choices);

    // Shows it at a screen position (kept on screen) and deletes it when closed.
    void popup(const QPoint &globalPos);

protected:
    void hideEvent(QHideEvent *ev) override;
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    void commit();
    QVBoxLayout *m_layout = nullptr;
    QLineEdit *m_name = nullptr;
    QString m_originalName;
    std::function<void(const QString &)> m_applyName;
    QDoubleSpinBox *m_value = nullptr;
    double m_originalValue = 0;
    double m_valueScale = 100.0;
    std::function<void(double)> m_applyValue;
    bool m_cancelled = false;
    bool m_allowEmpty = false;
    bool m_committed = false;
};

// Right-clicking a table cell opens that cell's editor: text cells start editing, dropdowns
// open, buttons (like a blueprint's preset picker) are pressed. `custom` runs first and returns true
// when it handled the click itself (e.g. opening a dialog for that column).
void enableRightClickEdit(QTableWidget *table,
                          std::function<bool(int row, int column, const QPoint &globalPos)> custom = {});

} // namespace lumora
