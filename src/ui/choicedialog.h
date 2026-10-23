// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QStringList>

namespace lumora {

// A question with a row of big answer buttons and Cancel: what to do when storing onto
// something that exists (Overwrite, Merge, …, Remove). Same look and order everywhere.
class ChoiceDialog : public QDialog {
    Q_OBJECT
public:
    // Blocks until answered: the chosen answer's index, or -1 for Cancel.
    static int ask(QWidget *parent, const QString &title, const QString &question, const QStringList &answers);

private:
    ChoiceDialog(QWidget *parent, const QString &title, const QString &question, const QStringList &answers);
};

} // namespace lumora
