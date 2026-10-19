// SPDX-License-Identifier: GPL-3.0-or-later
#include "quickedit.h"

#include "theme.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QToolButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

#include <cmath>

namespace lumora {

QuickEditor::QuickEditor(const QString &title, QWidget *parent) : QFrame(parent, Qt::Popup)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName("quickEditor");
    setStyleSheet(QStringLiteral("QFrame#quickEditor { background: %1; border: 1px solid #4a5260; border-radius: 8px; }"
                                 "QPushButton[role=\"item\"] { text-align: left; padding: 5px 10px; border: none; background: transparent; }"
                                 "QPushButton[role=\"item\"]:hover { background: %2; }")
                      .arg(theme::panel.name(), theme::raised.name()));
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(10, 8, 10, 8);
    m_layout->setSpacing(4);
    auto *t = new QLabel(title, this);
    t->setFont(theme::uiFont(10, QFont::DemiBold));
    m_layout->addWidget(t);
    setMinimumWidth(240);
}

void QuickEditor::addNameField(const QString &label, const QString &current, std::function<void(const QString &)> apply,
                               bool allowEmpty, const QString &placeholder)
{
    m_allowEmpty = allowEmpty;
    addSection(label);
    m_name = new QLineEdit(current, this);
    m_name->setPlaceholderText(placeholder);
    m_name->setClearButtonEnabled(allowEmpty);
    m_name->selectAll();
    m_name->installEventFilter(this);
    m_originalName = current;
    m_applyName = std::move(apply);
    connect(m_name, &QLineEdit::returnPressed, this, [this]() {
        commit();
        close();
    });
    m_layout->addWidget(m_name);
}

void QuickEditor::addValueField(const QString &label, double value, std::function<void(double)> apply, double min,
                                double max, const QString &suffix, double scale)
{
    addSection(label);
    m_value = new QDoubleSpinBox(this);
    m_valueScale = scale;
    m_value->setRange(min, max);
    m_value->setDecimals(1);
    m_value->setSuffix(suffix);
    m_value->setValue(value * scale);
    m_value->installEventFilter(this);
    m_originalValue = m_value->value();
    m_applyValue = std::move(apply);
    m_layout->addWidget(m_value);
}

void QuickEditor::addSection(const QString &title)
{
    if (title.isEmpty())
        return;
    auto *l = new QLabel(title, this);
    l->setProperty("role", "muted");
    l->setContentsMargins(0, 6, 0, 0);
    m_layout->addWidget(l);
}

void QuickEditor::addAction(const QString &label, std::function<void()> fn, const QIcon &icon, bool danger)
{
    auto *b = new QPushButton(icon, label, this);
    b->setProperty("role", "item");
    b->setFocusPolicy(Qt::NoFocus);
    if (danger)
        b->setStyleSheet(QStringLiteral("color: %1;").arg(theme::programmer.name()));
    connect(b, &QPushButton::clicked, this, [this, fn]() {
        commit();
        close();
        if (fn)
            fn();
    });
    m_layout->addWidget(b);
}

void QuickEditor::addWidget(QWidget *w)
{
    w->setParent(this);
    m_layout->addWidget(w);
}

void QuickEditor::addSegmented(const QString &label, const QStringList &options, int current, std::function<void(int)> apply)
{
    addSection(label);
    auto *row = new QWidget(this);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(2);
    auto *group = new QButtonGroup(row);
    for (int i = 0; i < options.size(); ++i) {
        auto *b = new QToolButton(row);
        b->setText(options[i]);
        b->setCheckable(true);
        b->setChecked(i == current);
        b->setFocusPolicy(Qt::NoFocus);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setStyleSheet(QStringLiteral("QToolButton { padding: 4px 8px; } QToolButton:checked { background: #3a3220; color: %1; border-color: %1; }")
                             .arg(theme::live.name()));
        group->addButton(b, i);
        h->addWidget(b);
    }
    connect(group, &QButtonGroup::idClicked, this, [apply](int i) { apply(i); });
    m_layout->addWidget(row);
}

void QuickEditor::addNumber(const QString &label, int min, int max, int value, std::function<void(int)> apply, int step)
{
    addSection(label);
    auto *spin = new QSpinBox(this);
    spin->setRange(min, max);
    spin->setSingleStep(step);
    spin->setValue(value);
    connect(spin, &QSpinBox::valueChanged, this, [apply](int v) { apply(v); });
    m_layout->addWidget(spin);
}

void QuickEditor::addSeconds(const QString &label, double value, std::function<void(double)> apply, bool optional)
{
    addSection(label);
    auto *spin = new QDoubleSpinBox(this);
    spin->setDecimals(1);
    spin->setSingleStep(0.5);
    spin->setSuffix(QStringLiteral(" s"));
    spin->setRange(optional ? -0.1 : 0.0, 3600.0);
    if (optional)
        spin->setSpecialValueText(tr("None"));
    spin->setValue(value < 0 && optional ? -0.1 : std::max(0.0, value));
    connect(spin, &QDoubleSpinBox::valueChanged, this, [apply, optional](double v) { apply(optional && v < 0 ? -1.0 : std::max(0.0, v)); });
    m_layout->addWidget(spin);
}

void QuickEditor::addChoices(const QVector<Choice> &choices)
{
    if (choices.size() <= 10) {
        for (const Choice &c : choices)
            addAction(c.label, c.fn, c.icon);
        return;
    }
    auto *list = new QListWidget(this);
    list->setIconSize(QSize(28, 28));
    list->setFocusPolicy(Qt::NoFocus);
    for (const Choice &c : choices)
        new QListWidgetItem(c.icon, c.label, list);
    list->setFixedHeight(300);
    connect(list, &QListWidget::itemClicked, this, [this, list, choices](QListWidgetItem *it) {
        const int row = list->row(it);
        commit();
        close();
        if (row >= 0 && row < choices.size() && choices[row].fn)
            choices[row].fn();
    });
    m_layout->addWidget(list);
}

void QuickEditor::popup(const QPoint &globalPos)
{
    adjustSize();
    QRect r(globalPos, size());
    if (QScreen *s = QGuiApplication::screenAt(globalPos)) {
        const QRect area = s->availableGeometry();
        if (r.right() > area.right())
            r.moveRight(area.right());
        if (r.bottom() > area.bottom())
            r.moveBottom(area.bottom());
    }
    move(r.topLeft());
    show();
    if (m_name)
        m_name->setFocus();
    else if (m_value) {
        m_value->setFocus();
        m_value->selectAll();
    }
}

void QuickEditor::commit()
{
    if (m_committed || m_cancelled)
        return;
    m_committed = true;
    if (m_name && m_applyName && m_name->text().trimmed() != m_originalName
        && (m_allowEmpty || !m_name->text().trimmed().isEmpty()))
        m_applyName(m_name->text().trimmed());
    if (m_value && m_applyValue && std::fabs(m_value->value() - m_originalValue) > 1e-9)
        m_applyValue(m_value->value() / m_valueScale);
}

void QuickEditor::hideEvent(QHideEvent *ev)
{
    // Clicking away keeps what you typed, like leaving a cell; Esc throws it away.
    commit();
    QFrame::hideEvent(ev);
}

bool QuickEditor::eventFilter(QObject *obj, QEvent *ev)
{
    if (ev->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(ev)->key() == Qt::Key_Escape) {
        m_cancelled = true;
        close();
        return true;
    }
    if (obj == m_value && ev->type() == QEvent::KeyPress) {
        const int k = static_cast<QKeyEvent *>(ev)->key();
        if (k == Qt::Key_Return || k == Qt::Key_Enter) {
            m_value->interpretText();
            m_originalValue = -1;   // Enter always applies, even the same number
            commit();
            close();
            return true;
        }
    }
    return QFrame::eventFilter(obj, ev);
}

void enableRightClickEdit(QTableWidget *table, std::function<bool(int, int, const QPoint &)> custom)
{
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(table, &QWidget::customContextMenuRequested, table, [table, custom](const QPoint &pos) {
        const QModelIndex idx = table->indexAt(pos);
        if (!idx.isValid())
            return;
        const int row = idx.row(), col = idx.column();
        if (table->selectionBehavior() == QAbstractItemView::SelectRows && !table->selectionModel()->isRowSelected(row, QModelIndex()))
            table->selectRow(row);
        table->setCurrentCell(row, col, QItemSelectionModel::NoUpdate);
        const QPoint global = table->viewport()->mapToGlobal(pos);
        if (custom && custom(row, col, global))
            return;
        if (QWidget *w = table->cellWidget(row, col)) {
            if (auto *combo = qobject_cast<QComboBox *>(w))
                combo->showPopup();
            else if (auto *button = qobject_cast<QAbstractButton *>(w))
                button->click();
            return;
        }
        QTableWidgetItem *item = table->item(row, col);
        if (item && (item->flags() & Qt::ItemIsEditable))
            table->editItem(item);
    });
}

} // namespace lumora
