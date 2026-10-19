// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QKeyEvent>
#include <QList>
#include <QString>

namespace lumora {

// Single-key command shortcuts. They work whenever the keyboard focus is not in a text
// field (so naming things, or typing into the command line, is never hijacked). Insert turns
// them on and off. Digits, ".", "+", "-", "@", Enter, Backspace and Esc also feed the command line.
struct KeyShortcut {
    QString command;              // keypad key / command keyword
    int key;                      // Qt::Key
    Qt::KeyboardModifiers mods;   // exact modifiers (Shift, Ctrl)
    QString label;                // shorthand shown under keypad keys
};

inline const QList<KeyShortcut> &keyShortcuts()
{
    static const QList<KeyShortcut> list = {
        {"Store", Qt::Key_S, Qt::NoModifier, "S"},
        {"Update", Qt::Key_U, Qt::NoModifier, "U"},
        {"Delete", Qt::Key_Delete, Qt::NoModifier, "Del"},
        {"Remove", Qt::Key_R, Qt::NoModifier, "R"},
        {"Copy", Qt::Key_C, Qt::NoModifier, "C"},
        {"Move", Qt::Key_M, Qt::NoModifier, "M"},
        {"Label", Qt::Key_L, Qt::NoModifier, "L"},
        {"Assign", Qt::Key_A, Qt::ShiftModifier, "⇧A"},
        {"Fixture", Qt::Key_F, Qt::NoModifier, "F"},
        {"Group", Qt::Key_G, Qt::NoModifier, "G"},
        {"Preset", Qt::Key_P, Qt::NoModifier, "P"},
        {"Sequence", Qt::Key_S, Qt::ShiftModifier, "⇧S"},
        {"Cue", Qt::Key_Q, Qt::NoModifier, "Q"},
        {"Executor", Qt::Key_E, Qt::NoModifier, "E"},
        {"Page", Qt::Key_P, Qt::ShiftModifier, "⇧P"},
        {"Effect", Qt::Key_E, Qt::ShiftModifier, "⇧E"},
        {"Go", Qt::Key_Space, Qt::NoModifier, "Space"},
        {"GoBack", Qt::Key_Space, Qt::ShiftModifier, "⇧Space"},
        {"Goto", Qt::Key_G, Qt::ShiftModifier, "⇧G"},
        {"Off", Qt::Key_O, Qt::NoModifier, "O"},
        {"On", Qt::Key_O, Qt::ShiftModifier, "⇧O"},
        {"Pause", Qt::Key_Space, Qt::ControlModifier, "Ctrl Space"},
        {"Highlight", Qt::Key_H, Qt::NoModifier, "H"},
        // Preset pools: Alt and the pool's letter types "Color." ready for the number.
        {"Dimmer.", Qt::Key_D, Qt::AltModifier, "Alt D"},
        {"Position.", Qt::Key_P, Qt::AltModifier, "Alt P"},
        {"Gobo.", Qt::Key_G, Qt::AltModifier, "Alt G"},
        {"Color.", Qt::Key_C, Qt::AltModifier, "Alt C"},
        {"Beam.", Qt::Key_B, Qt::AltModifier, "Alt B"},
        {"Focus.", Qt::Key_F, Qt::AltModifier, "Alt F"},
        {"Control.", Qt::Key_O, Qt::AltModifier, "Alt O"},
        {"All.", Qt::Key_A, Qt::AltModifier, "Alt A"},
        {"Next", Qt::Key_PageDown, Qt::NoModifier, "PgDn"},
        {"Prev", Qt::Key_PageUp, Qt::NoModifier, "PgUp"},
        {"Last", Qt::Key_L, Qt::ControlModifier, "Ctrl L"},
        {"Default", Qt::Key_D, Qt::NoModifier, "D"},
        {"Default", Qt::Key_Home, Qt::NoModifier, "Home"},   // the Home key works as Default too
        {"Attribute", Qt::Key_X, Qt::NoModifier, "X"},
        {"Fade", Qt::Key_D, Qt::ShiftModifier, "⇧D"},
        {"Delay", Qt::Key_L, Qt::ShiftModifier, "⇧L"},
        {"Full", Qt::Key_F, Qt::ShiftModifier, "⇧F"},
        {"Zero", Qt::Key_Z, Qt::ShiftModifier, "⇧Z"},
        {"Blackout", Qt::Key_B, Qt::ShiftModifier, "⇧B"},
        {"ClearAll", Qt::Key_Escape, Qt::ShiftModifier, "⇧Esc"},
        {"Thru", Qt::Key_T, Qt::NoModifier, "T"},
        {"At", Qt::Key_A, Qt::NoModifier, "A"},
        {"Back", Qt::Key_Backspace, Qt::NoModifier, "⌫"},
        {"Enter", Qt::Key_Return, Qt::NoModifier, "↵"},
        {"Enter", Qt::Key_Enter, Qt::KeypadModifier, "↵"},
        {"Clear", Qt::Key_Escape, Qt::NoModifier, "Esc"},
    };
    return list;
}

inline QString shortcutLabel(const QString &command)
{
    if (command.size() == 1 && (command[0].isDigit() || command == "." || command == "+" || command == "-"))
        return command;
    for (const KeyShortcut &k : keyShortcuts())
        if (k.command == command)
            return k.label;
    return QString();
}

// The keypad key a key press stands for, or an empty string.
inline QString commandForKey(const QKeyEvent *ev)
{
    const Qt::KeyboardModifiers mods = ev->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const QString text = ev->text();
    if (!(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) && text.size() == 1) {
        const QChar c = text[0];
        if (c.isDigit() || c == '.' || c == '+' || c == '-')
            return QString(c);
        if (c == '@')
            return QStringLiteral("At");
    }
    for (const KeyShortcut &k : keyShortcuts()) {
        Qt::KeyboardModifiers want = k.mods & ~Qt::KeypadModifier;
        if (k.key == ev->key() && want == mods)
            return k.command;
    }
    return QString();
}

// Types a command key onto a line the way the command line does: keywords with a space either
// side, digits straight on after a number, "Color." with its number following.
inline QString typeCommandKey(QString t, const QString &key)
{
    if (key.size() > 1 && key.endsWith('.')) {
        if (!t.isEmpty() && !t.endsWith(' '))
            t += ' ';
        return t + key;
    }
    const bool numeric = key.size() == 1 && (key[0].isDigit() || key[0] == '.');
    if (numeric) {
        if (!t.isEmpty() && t.back().isLetter())
            t += ' ';
        return t + key;
    }
    if (!t.isEmpty() && !t.endsWith(' '))
        t += ' ';
    return t + key + ' ';
}

} // namespace lumora
