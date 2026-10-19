// SPDX-License-Identifier: GPL-3.0-or-later
#include "commandbar.h"
#include "shortcuts.h"

#include "appcontext.h"
#include "theme.h"

#include <QApplication>
#include <QCompleter>
#include <QDateTime>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>

namespace lumora {

CommandBar::CommandBar(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(10, 6, 10, 0);
    h->setSpacing(12);
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Type a command, e.g. Fixture 1 Thru 8 At 50, Store Cue 3, Go Executor 201"));
    m_input->setMinimumHeight(32);
    m_input->setFont(theme::uiFont(11));
    m_input->installEventFilter(this);
    auto *completer = new QCompleter(CommandLine::keywords(), m_input);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setCompletionMode(QCompleter::InlineCompletion);
    m_input->setCompleter(completer);
    h->addWidget(m_input, 3);

    m_feedback = new QLabel(this);
    m_feedback->setProperty("role", "muted");
    m_feedback->setMinimumWidth(260);
    h->addWidget(m_feedback, 2);

    connect(m_input, &QLineEdit::returnPressed, this, &CommandBar::execute);
    connect(m_input, &QLineEdit::textChanged, ctx, &AppContext::noteCommandLine);
    connect(ctx, &AppContext::commandLineEdit, this, [this](const QString &t) {
        if (!isActiveBar())
            return;
        m_input->setText(t);
        m_input->end(false);
    });
    connect(ctx, &AppContext::keyRequested, this, [this](const QString &key) {
        if (isActiveBar())
            pressKey(key);
    });
    // Store mirrors into the command line, like a console: "Store" waits for its target.
    connect(ctx, &AppContext::storeArmedChanged, this, [this](bool on) {
        const QString t = m_input->text().trimmed();
        if (on && t.isEmpty())
            m_input->setText(QStringLiteral("Store "));
        else if (!on && t.compare(QLatin1String("Store"), Qt::CaseInsensitive) == 0)
            m_input->clear();
    });
    connect(ctx, &AppContext::commandRan, this, [this](const QString &, bool ok, const QString &msg) {
        showMessage(msg, !ok);
    });
    connect(ctx, &AppContext::status, this, [this](const QString &msg) { showMessage(msg); });
}

bool CommandBar::isActiveBar() const
{
    // With several screens open, keys go to the command line of the screen in front; with none
    // in front (a dialog, another app), to screen 1.
    QWidget *active = QApplication::activeWindow();
    QWidget *mine = window();
    if (active && active->property("lumoraScreen").isValid())
        return active == mine;
    return mine->property("lumoraScreen").toInt() <= 1;
}

void CommandBar::showMessage(const QString &text, bool error)
{
    m_feedback->setText(text);
    m_feedback->setStyleSheet(QStringLiteral("color: %1;").arg(error ? theme::programmer.name() : theme::muted.name()));
}

namespace {
// The name a "Label ..." line would replace, so the dialog starts from it. *known is false
// when the line doesn't name something that exists.
QString currentLabel(Show *show, const QString &rest, bool *known)
{
    *known = true;
    static const QRegularExpression numbered(
        QStringLiteral("^(view|group|page|effect|fixture|sequence|cue)\\s+(\\d+(?:\\.\\d+)?)$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression preset(QStringLiteral("^preset\\s+(\\w+)\\s*\\.\\s*(\\d+)$"),
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression seqCue(QStringLiteral("^sequence\\s+(\\d+)\\s+cue\\s+(\\d+(?:\\.\\d+)?)$"),
                                           QRegularExpression::CaseInsensitiveOption);
    if (auto m = seqCue.match(rest); m.hasMatch()) {
        const Sequence *sq = show->sequence(m.captured(1).toInt());
        const int i = sq ? sq->indexOf(m.captured(2).toDouble()) : -1;
        if (i >= 0)
            return sq->cues[i].name;
    } else if (auto m = preset.match(rest); m.hasMatch()) {
        bool digit = false;
        int type = m.captured(1).toInt(&digit);
        if (!digit)
            type = presetTypeFromName(m.captured(1));
        if (const Preset *p = show->preset(type, m.captured(2).toInt()))
            return p->name;
    } else if (auto m = numbered.match(rest); m.hasMatch()) {
        const QString what = m.captured(1).toLower();
        const int n = m.captured(2).toInt();
        if (what == "view") { if (const StoredView *v = show->view(n)) return v->name; }
        else if (what == "group") { if (const Group *g = show->group(n)) return g->name; }
        else if (what == "page") { if (n >= 1) return show->pageName(n); }
        else if (what == "effect") { if (const Effect *e = show->effect(n)) return e->name; }
        else if (what == "ratemaster") { if (const RateMaster *r = show->rateMaster(n)) return r->name; }
        else if (what == "fixture") { if (const Fixture *f = show->fixture(n)) return f->name; }
        else if (what == "sequence") { if (const Sequence *sq = show->sequence(n)) return sq->name; }
        else if (what == "cue") {
            const Sequence *sq = show->sequence(show->selectedSequence());
            const int i = sq ? sq->indexOf(m.captured(2).toDouble()) : -1;
            if (i >= 0)
                return sq->cues[i].name;
        }
    }
    *known = false;
    return QString();
}
} // namespace

bool CommandBar::labelDialog(const QString &line)
{
    // "Label" (or "Label Group 3") with no name yet: Enter asks for one.
    static const QRegularExpression verb(QStringLiteral("^(label|lab|name)\\b\\s*(.*)$"),
                                         QRegularExpression::CaseInsensitiveOption);
    const auto m = verb.match(line.simplified());
    if (!m.hasMatch() || line.contains('"'))
        return false;
    Show *show = m_ctx->show;
    const QString rest = m.captured(2).trimmed();
    auto ask = [this](const QString &prompt, const QString &current, bool *ok) {
        return QInputDialog::getText(window(), tr("Label"), prompt, QLineEdit::Normal, current, ok).trimmed();
    };
    bool ok = false;
    if (rest.isEmpty()) {
        // No object: the selected fixtures, numbered from the name's number like patch does.
        const QVector<int> sel = show->selection();
        if (sel.isEmpty()) {
            showMessage(tr("Label what? Select fixtures, or type the object, e.g. Label Group 3"), true);
            return true;
        }
        const Fixture *first = show->fixture(sel.first());
        const QString name = ask(sel.size() == 1 ? tr("Name for fixture %1").arg(sel.first())
                                                 : tr("Name for %1 fixtures (numbered in order)").arg(sel.size()),
                                 first ? first->name : QString(), &ok);
        if (!ok || name.isEmpty())
            return true;
        const QStringList names = sel.size() == 1 ? QStringList{name} : numberedNames(name, sel.size());
        for (int i = 0; i < sel.size(); ++i)
            if (const Fixture *f = show->fixture(sel[i]); f && !f->cell) {
                Fixture copy = *f;
                copy.name = names.value(i);
                show->updateFixture(copy);
            }
        m_input->clear();
        showMessage(tr("Labelled %n fixture(s)", nullptr, sel.size()));
        return true;
    }
    bool known = false;
    const QString current = currentLabel(show, rest, &known);
    if (!known)
        return false;   // let the command line explain what's wrong
    const QString name = ask(tr("Name for %1").arg(rest), current, &ok);
    if (!ok || name.isEmpty())
        return true;
    QString quoted = name;
    quoted.replace('"', '\'');
    m_ctx->run(QStringLiteral("Label %1 \"%2\"").arg(rest, quoted));
    m_input->clear();
    return true;
}

void CommandBar::execute()
{
    const QString line = m_input->text();
    if (line.trimmed().isEmpty())
        return;
    if (labelDialog(line)) {
        m_historyPos = -1;
        m_ctx->setStoreArmed(false);
        return;
    }
    m_ctx->run(line);
    m_input->clear();
    m_historyPos = -1;
    m_ctx->setStoreArmed(false);
}

void CommandBar::pressKey(const QString &key)
{
    QString t = m_input->text();
    if (key == QLatin1String("Enter")) {
        execute();
        return;
    }
    if (key == QLatin1String("Clear")) {
        if (t.trimmed().isEmpty())
            showMessage(m_ctx->show->clearStep());
        else
            m_input->clear();
        m_ctx->setStoreArmed(false);
        return;
    }
    if (key == QLatin1String("Back")) {
        t = t.trimmed();
        // Whole keywords go in one press; numbers one character at a time.
        if (!t.isEmpty() && t.back().isLetter()) {
            const int cut = t.lastIndexOf(' ');
            t = cut < 0 ? QString() : t.left(cut + 1);
        } else {
            t.chop(1);
            if (!t.isEmpty() && t.back() != ' ' && !t.back().isDigit() && t.back() != '.')
                t += ' ';
        }
        m_input->setText(t);
        if (t.trimmed().isEmpty())
            m_ctx->setStoreArmed(false);
        return;
    }
    // Default Default: the selection's default values into the programmer, right away.
    // (One Default waits for more: Store Default, Delete Default.)
    if (key == QLatin1String("Default") && t.simplified().compare(QLatin1String("Default"), Qt::CaseInsensitive) == 0) {
        m_input->clear();
        m_ctx->run(QStringLiteral("Default Default"));
        return;
    }
    // At pressed twice (At At): full, right away.
    if (key == QLatin1String("At")) {
        const QString line = t.simplified();
        if (line.compare(QLatin1String("At"), Qt::CaseInsensitive) == 0
            || line.endsWith(QLatin1String(" At"), Qt::CaseInsensitive) || line == QLatin1String("@")
            || line.endsWith(QLatin1String(" @"))) {
            m_input->clear();
            m_ctx->run(line + QStringLiteral(" At"));
            return;
        }
    }
    // Clear All always clears everything, the half-typed command included.
    if (key == QLatin1String("ClearAll")) {
        m_input->clear();
        m_ctx->setStoreArmed(false);
        m_ctx->run(key);
        return;
    }
    // Actions that don't need arguments run right away
    if (t.trimmed().isEmpty() && (key == QLatin1String("Highlight") || key == QLatin1String("Blackout"))) {
        m_ctx->run(key);
        return;
    }
    // Next, Prev and Select Last act on the selection right away, whatever is half typed.
    if (key == QLatin1String("Next") || key == QLatin1String("Prev") || key == QLatin1String("Last")) {
        m_ctx->run(key);
        return;
    }
    if (key == QLatin1String("Store") && t.trimmed().isEmpty()) {
        m_ctx->setStoreArmed(true);   // fills in "Store " and lets you tap a target
        m_input->end(false);
        return;
    }
    // A preset pool ("Color."): the number follows straight on, "Color.3".
    if (key.size() > 1 && key.endsWith('.')) {
        if (!t.isEmpty() && !t.endsWith(' '))
            t += ' ';
        t += key;
        m_input->setText(t);
        m_input->end(false);
        return;
    }
    const bool numeric = key.size() == 1 && (key[0].isDigit() || key[0] == '.');
    if (numeric) {
        if (!t.isEmpty() && t.back().isLetter())
            t += ' ';
        t += key;
    } else {
        if (!t.isEmpty() && !t.endsWith(' '))
            t += ' ';
        t += key + ' ';
    }
    // Keep keyboard focus where it is: with shortcuts on, the next key press is another command key.
    m_input->setText(t);
    m_input->end(false);
}

void CommandBar::focusInput()
{
    m_input->setFocus();
}

bool CommandBar::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_input && ev->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(ev);
        const QStringList &hist = m_ctx->commands.history();
        if (k->key() == Qt::Key_Up && !hist.isEmpty()) {
            m_historyPos = m_historyPos < 0 ? hist.size() - 1 : std::max(0, m_historyPos - 1);
            m_input->setText(hist[m_historyPos]);
            return true;
        }
        if (k->key() == Qt::Key_Down && m_historyPos >= 0) {
            m_historyPos++;
            if (m_historyPos >= hist.size()) {
                m_historyPos = -1;
                m_input->clear();
            } else {
                m_input->setText(hist[m_historyPos]);
            }
            return true;
        }
        if (k->key() == Qt::Key_Escape) {
            m_input->clear();
            m_historyPos = -1;
            return true;
        }
        // Alt and a pool's letter work while typing too (Alt doesn't type anything).
        if (k->modifiers() == Qt::AltModifier) {
            const QString cmd = commandForKey(k);
            if (cmd.endsWith('.')) {
                pressKey(cmd);
                m_input->setFocus();
                return true;
            }
        }
        // Page Up / Down and Ctrl+L act on the selection while typing too.
        if (k->key() == Qt::Key_PageDown || k->key() == Qt::Key_PageUp
            || (k->key() == Qt::Key_L && k->modifiers() == Qt::ControlModifier)) {
            pressKey(commandForKey(k));
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

} // namespace lumora
