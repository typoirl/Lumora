// SPDX-License-Identifier: GPL-3.0-or-later
#include "splash.h"
#include "theme.h"

#include <QApplication>
#include <QDateTime>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QScreen>
#include <QVBoxLayout>

namespace lumora {

Splash *Splash::s_current = nullptr;

Splash::Splash(bool active, QWidget *parent)
    : QWidget(parent, Qt::SplashScreen | Qt::FramelessWindowHint), m_active(active)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(460, 360);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(36, 34, 36, 26);
    lay->setSpacing(0);

    auto *logo = new QLabel(this);
    logo->setPixmap(QIcon(QStringLiteral(":/icons/lumora.png")).pixmap(128, 128));
    logo->setAlignment(Qt::AlignCenter);
    lay->addWidget(logo);
    lay->addSpacing(14);

    auto *name = new QLabel(QStringLiteral("Lumora"), this);
    QFont f = name->font();
    f.setPointSizeF(f.pointSizeF() * 1.9);
    f.setWeight(QFont::DemiBold);
    name->setFont(f);
    name->setAlignment(Qt::AlignCenter);
    name->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(theme::text.name()));
    lay->addWidget(name);

    auto *version = new QLabel(QApplication::applicationVersion(), this);
    version->setAlignment(Qt::AlignCenter);
    version->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(theme::muted.name()));
    lay->addWidget(version);
    lay->addStretch(1);

    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 100);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(6);
    m_bar->setStyleSheet(QStringLiteral(
        "QProgressBar { background: %1; border: none; border-radius: 3px; }"
        "QProgressBar::chunk { background: %2; border-radius: 3px; }").arg(theme::raised.name(), theme::live.name()));
    lay->addWidget(m_bar);
    lay->addSpacing(10);

    m_text = new QLabel(this);
    m_text->setAlignment(Qt::AlignCenter);
    m_text->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(theme::muted.name()));
    m_text->setMinimumWidth(1);   // long file names are elided, not widening the window
    lay->addWidget(m_text);

    if (QScreen *sc = QGuiApplication::primaryScreen())
        move(sc->availableGeometry().center() - rect().center());
    if (m_active)
        s_current = this;
}

Splash::~Splash()
{
    if (s_current == this)
        s_current = nullptr;
}

void Splash::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
    p.fillPath(path, theme::bg);
    p.setPen(QPen(theme::line, 1));
    p.drawPath(path);
}

void Splash::step(int percent, const QString &text, bool force)
{
    if (!m_active)
        return;
    m_bar->setValue(std::clamp(percent, 0, 100));
    if (!text.isNull())
        m_text->setText(QFontMetrics(m_text->font()).elidedText(text, Qt::ElideMiddle, width() - 72));
    refresh(force);
    // Developer: LUMORA_SPLASH_SHOT=file saves the loading screen partway through the library.
    static bool shot = false;
    if (!shot && percent >= 35 && qEnvironmentVariableIsSet("LUMORA_SPLASH_SHOT")) {
        shot = true;
        grab().save(qEnvironmentVariable("LUMORA_SPLASH_SHOT"));
    }
}

void Splash::finish()
{
    m_active = false;
    if (s_current == this)
        s_current = nullptr;
    close();
}

void Splash::status(const QString &text)
{
    if (s_current)
        s_current->step(s_current->m_bar->value(), text);
}

void Splash::refresh(bool force)
{
    // Startup runs on this thread, so the screen only updates when asked to. Thousands of fixture
    // files would spend more time painting than loading: at most ~60 updates a second.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && now - m_lastPaint < 16)
        return;
    m_lastPaint = now;
    if (!isVisible())
        show();
    repaint();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

}  // namespace lumora
