// SPDX-License-Identifier: GPL-3.0-or-later
#include "keypad.h"

#include "appcontext.h"
#include "shortcuts.h"
#include "theme.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

namespace lumora {

namespace {
enum class KeyKind { Edit, Object, Playback, Number, Action };

QPushButton *makeKey(const QString &label, const QString &command, KeyKind kind, QWidget *parent, QList<QLabel *> *hints)
{
    auto *b = new QPushButton(parent);
    b->setFocusPolicy(Qt::NoFocus);   // keys never steal keyboard focus
    b->setMinimumSize(60, 42);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *l = new QVBoxLayout(b);
    l->setContentsMargins(2, 3, 2, 3);
    l->setSpacing(0);
    auto *main = new QLabel(label, b);
    main->setAlignment(Qt::AlignCenter);
    main->setAttribute(Qt::WA_TransparentForMouseEvents);
    main->setStyleSheet(QStringLiteral("background: transparent; border: none; font-weight: 500;%1")
                            .arg(kind == KeyKind::Number ? QStringLiteral(" font-size: 12pt;") : QString()));
    l->addStretch(1);
    l->addWidget(main);
    const QString sc = shortcutLabel(command);
    if (!sc.isEmpty() && sc != label) {
        auto *hint = new QLabel(sc, b);
        hint->setAlignment(Qt::AlignCenter);
        hint->setAttribute(Qt::WA_TransparentForMouseEvents);
        hint->setFont(theme::uiFont(7));
        hint->setStyleSheet(QStringLiteral("background: transparent; border: none; color: %1;").arg(theme::muted.name()));
        l->addWidget(hint);
        hints->append(hint);
    }
    l->addStretch(1);
    QString accent;
    switch (kind) {
    case KeyKind::Edit: accent = theme::programmer.name(); break;    // changes show data
    case KeyKind::Playback: accent = theme::live.name(); break;      // affects the stage
    default: break;
    }
    QString css = QStringLiteral("QPushButton { padding: 2px; }");
    if (!accent.isEmpty())
        css += QStringLiteral("QPushButton { border-bottom: 2px solid %1; }").arg(accent);
    if (kind == KeyKind::Number || kind == KeyKind::Action)
        css += QStringLiteral("QPushButton { background: #2f353f; }");
    b->setStyleSheet(css);
    return b;
}
} // namespace

CommandKeypad::CommandKeypad(AppContext *ctx, bool embedded, QWidget *parent)
    : QFrame(parent), m_ctx(ctx), m_embedded(embedded)
{
    setObjectName("keypad");
    if (!embedded)
        setStyleSheet(QStringLiteral("QFrame#keypad { background: %1; border: 1px solid #4a5260; border-radius: 10px; }")
                          .arg(theme::panel.name()));
    setCursor(Qt::ArrowCursor);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(embedded ? 0 : 12, embedded ? 0 : 8, embedded ? 0 : 12, embedded ? 0 : 12);
    v->setSpacing(8);
    QList<QLabel *> hints;

    auto *head = new QHBoxLayout;
    auto *title = new QLabel(tr("Command keys"), this);
    title->setFont(theme::uiFont(10, QFont::DemiBold));
    title->setCursor(Qt::SizeAllCursor);
    auto *hint = new QLabel(tr("drag to move"), this);
    hint->setProperty("role", "muted");
    auto *close = new QToolButton(this);
    close->setText(QStringLiteral("✕"));
    close->setToolTip(tr("Close keypad"));
    close->setFocusPolicy(Qt::NoFocus);
    head->addWidget(title);
    head->addSpacing(6);
    head->addWidget(hint);
    head->addStretch(1);
    head->addWidget(close);
    if (embedded) {
        title->hide();
        hint->hide();
        close->hide();
    } else {
        v->addLayout(head);
    }
    connect(close, &QToolButton::clicked, this, &CommandKeypad::closeRequested);

    auto *body = new QHBoxLayout;
    body->setSpacing(14);

    // Left: command keys in three bands
    auto *words = new QGridLayout;
    words->setSpacing(5);
    const QList<QPair<QString, KeyKind>> rows[] = {
        {{"Store", KeyKind::Edit}, {"Update", KeyKind::Edit}, {"Delete", KeyKind::Edit}, {"Remove", KeyKind::Edit},
         {"Copy", KeyKind::Edit}, {"Move", KeyKind::Edit}, {"Label", KeyKind::Edit}, {"Assign", KeyKind::Edit},
         {"NextY", KeyKind::Action}},
        {{"Fixture", KeyKind::Object}, {"Group", KeyKind::Object}, {"Preset", KeyKind::Object}, {"Sequence", KeyKind::Object},
         {"Cue", KeyKind::Object}, {"Executor", KeyKind::Object}, {"Page", KeyKind::Object}, {"Attribute", KeyKind::Object},
         {"Default", KeyKind::Object}},
        {{"Go", KeyKind::Playback}, {"GoBack", KeyKind::Playback}, {"Goto", KeyKind::Playback}, {"Off", KeyKind::Playback},
         {"On", KeyKind::Playback}, {"Pause", KeyKind::Playback}, {"Effect", KeyKind::Object}, {"Highlight", KeyKind::Action},
         {"Prev", KeyKind::Action}},
        {{"Fade", KeyKind::Object}, {"Delay", KeyKind::Object}, {"Full", KeyKind::Object}, {"Zero", KeyKind::Object},
         {"Rate", KeyKind::Object}, {"Size", KeyKind::Object}, {"Blackout", KeyKind::Action}, {"Clear", KeyKind::Action},
         {"Next", KeyKind::Action}},
    };
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < rows[r].size(); ++c) {
            const QString key = rows[r][c].first;
            // Back and Next step through the selection one fixture at a time.
            const QString label = key == QLatin1String("Prev") ? tr("‹ Back") : key == QLatin1String("Next") ? tr("Next ›")
                                  : key == QLatin1String("NextY") ? tr("Next Y") : key;
            QPushButton *b = makeKey(label, key, rows[r][c].second, this, &hints);
            connect(b, &QPushButton::clicked, this, [this, key]() { m_ctx->pressKey(key); });
            words->addWidget(b, r, c);
        }
    body->addLayout(words, 8);

    // Right: number pad
    auto *pad = new QGridLayout;
    pad->setSpacing(5);
    const QStringList padKeys = {"7", "8", "9", "Thru", "4", "5", "6", "+", "1", "2", "3", "-", "0", ".", "At", "Back"};
    for (int i = 0; i < padKeys.size(); ++i) {
        const QString key = padKeys[i];
        const bool digit = key.size() == 1 && (key[0].isDigit() || key == ".");
        QPushButton *b = makeKey(key == "Back" ? QStringLiteral("⌫") : key, key,
                                 digit ? KeyKind::Number : (key == "Back" ? KeyKind::Action : KeyKind::Object), this, &hints);
        connect(b, &QPushButton::clicked, this, [this, key]() { m_ctx->pressKey(key); });
        pad->addWidget(b, i / 4, i % 4);
    }
    QPushButton *clearAll = makeKey(tr("Clear All"), QStringLiteral("ClearAll"), KeyKind::Action, this, &hints);
    QPushButton *enter = makeKey(tr("Enter"), QStringLiteral("Enter"), KeyKind::Action, this, &hints);
    enter->setStyleSheet(QStringLiteral("QPushButton { background: #3a3220; border-color: %1; } QLabel { color: %1; font-weight: 600; }")
                             .arg(theme::live.name()));
    connect(clearAll, &QPushButton::clicked, this, [this]() { m_ctx->pressKey("ClearAll"); });
    connect(enter, &QPushButton::clicked, this, [this]() { m_ctx->pressKey("Enter"); });
    pad->addWidget(clearAll, 4, 0, 1, 2);
    pad->addWidget(enter, 4, 2, 1, 2);
    body->addLayout(pad, 4);
    v->addLayout(body, 1);
    resize(940, 320);

    // Shortcut hints dim while keyboard shortcuts are off (Insert).
    auto syncHints = [hints](bool on) {
        for (QLabel *h : hints)
            h->setStyleSheet(QStringLiteral("background: transparent; border: none; color: %1;")
                                 .arg(on ? theme::muted.name() : QStringLiteral("#3c424d")));
    };
    connect(ctx, &AppContext::shortcutsChanged, this, syncHints);
    syncHints(ctx->shortcutsEnabled());
}

void CommandKeypad::mousePressEvent(QMouseEvent *ev)
{
    if (!m_embedded && ev->button() == Qt::LeftButton && ev->position().y() < 34) {
        m_dragging = true;
        m_dragOffset = ev->position().toPoint();
    }
}

void CommandKeypad::mouseMoveEvent(QMouseEvent *ev)
{
    if (!m_dragging || !parentWidget())
        return;
    QPoint p = mapToParent(ev->position().toPoint()) - m_dragOffset;
    const QRect area = parentWidget()->rect();
    p.setX(qBound(0, p.x(), area.width() - width()));
    p.setY(qBound(0, p.y(), area.height() - height()));
    move(p);
}

void CommandKeypad::mouseReleaseEvent(QMouseEvent *)
{
    if (m_dragging)
        QSettings().setValue("ui/keypadPos", pos());
    m_dragging = false;
}

} // namespace lumora
