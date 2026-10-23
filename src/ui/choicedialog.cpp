// SPDX-License-Identifier: GPL-3.0-or-later
#include "choicedialog.h"

#include "theme.h"

#include <QApplication>
#include <QCursor>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace lumora {

ChoiceDialog::ChoiceDialog(QWidget *parent, const QString &title, const QString &question, const QStringList &answers)
    : QDialog(parent, Qt::Dialog | Qt::WindowStaysOnTopHint)
{
    setWindowTitle(title);
    auto *v = new QVBoxLayout(this);
    v->setSpacing(14);
    v->setContentsMargins(20, 18, 20, 18);
    auto *q = new QLabel(question, this);
    q->setFont(theme::uiFont(14, QFont::DemiBold));
    v->addWidget(q);
    auto *row = new QHBoxLayout;
    row->setSpacing(8);
    for (int i = 0; i < answers.size(); ++i) {
        auto *b = new QPushButton(answers[i], this);
        b->setFont(theme::uiFont(13, QFont::DemiBold));
        b->setMinimumSize(std::max(120, QFontMetrics(b->font()).horizontalAdvance(answers[i]) + 44), 56);
        b->setAutoDefault(false);
        connect(b, &QPushButton::clicked, this, [this, i]() { done(i + 1); });
        row->addWidget(b);
    }
    row->addSpacing(12);
    auto *cancel = new QPushButton(tr("Cancel"), this);
    cancel->setFont(theme::uiFont(13));
    cancel->setMinimumSize(110, 56);
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, [this]() { done(0); });
    row->addWidget(cancel);
    v->addLayout(row);
}

int ChoiceDialog::ask(QWidget *parent, const QString &title, const QString &question, const QStringList &answers)
{
    ChoiceDialog d(parent ? parent : QApplication::activeWindow(), title, question, answers);
    d.adjustSize();
    // In the middle of the screen the tap came from.
    const QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = d.parentWidget() ? d.parentWidget()->screen() : QGuiApplication::primaryScreen();
    if (screen)
        d.move(screen->availableGeometry().center() - QPoint(d.width() / 2, d.height() / 2));
    // Developer: LUMORA_CHOICE_SHOT=file saves the question and cancels it.
    if (qEnvironmentVariableIsSet("LUMORA_CHOICE_SHOT"))
        QTimer::singleShot(400, &d, [&d]() {
            d.grab().save(qEnvironmentVariable("LUMORA_CHOICE_SHOT"));
            d.done(0);
            QTimer::singleShot(0, qApp, &QApplication::quit);
        });
    return d.exec() - 1;
}

} // namespace lumora
