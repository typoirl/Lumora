// SPDX-License-Identifier: GPL-3.0-or-later
#include "helpview.h"

#include "appcontext.h"
#include "theme.h"

#include <QAbstractTextDocumentLayout>
#include <QDialog>
#include <QFile>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QTimer>
#include <QVBoxLayout>

namespace lumora {

namespace {
QString slug(const QString &title)
{
    QString s = title.toLower();
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    return s.section('-', 0, -1, QString::SectionSkipEmpty);
}
} // namespace

HelpView::HelpView(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    setWindowTitle(tr("Help"));
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(6, 6, 6, 6);
    auto *split = new QSplitter(Qt::Horizontal, this);
    h->addWidget(split);

    auto *left = new QWidget(split);
    auto *lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->setSpacing(6);
    m_search = new QLineEdit(left);
    m_search->setPlaceholderText(tr("Search the guide"));
    m_search->setClearButtonEnabled(true);
    lv->addWidget(m_search);
    m_count = new QLabel(left);
    m_count->setProperty("role", "muted");
    lv->addWidget(m_count);
    m_list = new QListWidget(left);
    lv->addWidget(m_list, 1);
    split->addWidget(left);

    m_page = new QTextBrowser(split);
    m_page->setOpenLinks(false);
    m_page->setSearchPaths({QStringLiteral(":/help")});
    QFont f = theme::uiFont(10);
    m_page->document()->setDefaultFont(f);
    m_page->document()->setDocumentMargin(14);
    split->addWidget(m_page);
    split->setStretchFactor(1, 1);
    split->setSizes({260, 700});

    load();
    fillList();
    connect(m_search, &QLineEdit::textChanged, this, [this]() {
        fillList();
        highlight();
    });
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *it) {
        if (!it)
            return;
        const int i = it->data(Qt::UserRole).toInt();
        if (i < 0 || i >= m_sections.size())
            return;
        const Section &s = m_sections[i];
        if (s.level == 1)
            return;
        showChapter(s.chapter, s.level == 3 ? s.title : QString());
    });
    connect(m_page, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme().isEmpty() && url.toString().startsWith('#'))
            showTopic(url.fragment());
        else if (url.scheme() == QLatin1String("help"))
            showTopic(url.path());
    });
    if (!m_sections.isEmpty())
        for (int i = 0; i < m_sections.size(); ++i)
            if (m_sections[i].level == 2) {
                showChapter(i);
                break;
            }
}

void HelpView::openWindow(AppContext *ctx, QWidget *parent)
{
    static QPointer<QDialog> window;
    if (!window) {
        window = new QDialog(parent, Qt::Window);
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->setWindowTitle(tr("Lumora help"));
        auto *v = new QVBoxLayout(window);
        v->setContentsMargins(0, 0, 0, 0);
        v->addWidget(new HelpView(ctx, window));
        window->resize(1100, 760);
    }
    window->show();
    window->raise();
    window->activateWindow();
}

void HelpView::load()
{
    QFile f(QStringLiteral(":/help/manual.md"));
    if (!f.open(QIODevice::ReadOnly)) {
        m_page->setPlainText(tr("The user guide isn't built into this copy of Lumora."));
        return;
    }
    const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
    static const QRegularExpression heading(QStringLiteral("^(#{1,3})\\s+(.*?)\\s*(\\{#([A-Za-z0-9_-]+)\\})?\\s*$"));
    QString chapterText;
    int chapterIndex = -1;
    bool inCode = false;
    auto closeChapter = [&]() {
        if (chapterIndex >= 0) {
            while (m_chapterMarkdown.size() <= chapterIndex)
                m_chapterMarkdown << QString();
            m_chapterMarkdown[chapterIndex] = chapterText;
        }
        chapterText.clear();
    };
    for (const QString &raw : lines) {
        QString line = raw;
        if (line.startsWith(QLatin1String("```")))
            inCode = !inCode;
        const auto m = inCode ? QRegularExpressionMatch() : heading.match(line);
        if (m.hasMatch()) {
            Section s;
            s.level = int(m.captured(1).size());
            s.title = m.captured(2);
            s.id = m.captured(4).isEmpty() ? slug(s.title) : m.captured(4);
            if (s.level <= 2) {
                closeChapter();
                chapterIndex = s.level == 2 ? int(m_sections.size()) : -1;
            }
            s.chapter = chapterIndex;
            m_sections << s;
            line = m.captured(1) + ' ' + s.title;   // without its {#id}
        } else if (!m_sections.isEmpty()) {
            m_sections.last().text += raw + '\n';
        }
        if (chapterIndex >= 0)
            chapterText += line + '\n';
    }
    closeChapter();
}

void HelpView::fillList()
{
    const QStringList words = m_search->text().simplified().split(' ', Qt::SkipEmptyParts);
    m_list->blockSignals(true);
    m_list->clear();
    int hits = 0;
    for (int i = 0; i < m_sections.size(); ++i) {
        const Section &s = m_sections[i];
        if (words.isEmpty()) {
            auto *it = new QListWidgetItem(QString(s.level == 3 ? 4 : 0, ' ') + s.title, m_list);
            it->setData(Qt::UserRole, i);
            if (s.level == 1) {
                QFont f = m_list->font();
                f.setBold(true);
                it->setFont(f);
                it->setFlags(Qt::ItemIsEnabled);
                it->setForeground(theme::muted);
            }
            continue;
        }
        if (s.level == 1)
            continue;
        const QString hay = s.title + '\n' + s.text;
        bool all = true;
        for (const QString &w : words)
            all = all && hay.contains(w, Qt::CaseInsensitive);
        if (!all)
            continue;
        ++hits;
        QString where = s.chapter >= 0 && s.chapter != i ? m_sections[s.chapter].title + QStringLiteral(" › ") : QString();
        auto *it = new QListWidgetItem(where + s.title, m_list);
        it->setData(Qt::UserRole, i);
    }
    m_list->blockSignals(false);
    m_count->setText(words.isEmpty() ? tr("Contents") : hits ? tr("%n section(s) match", nullptr, hits) : tr("Nothing matches"));
    if (!words.isEmpty() && m_list->count())
        m_list->setCurrentRow(0);
}

void HelpView::showTopic(const QString &id)
{
    for (int i = 0; i < m_sections.size(); ++i)
        if (m_sections[i].id == id) {
            const Section &s = m_sections[i];
            if (s.chapter >= 0)
                showChapter(s.chapter, s.level == 3 ? s.title : QString());
            return;
        }
}

void HelpView::showChapter(int chapter, const QString &headingTitle)
{
    if (chapter < 0 || chapter >= m_chapterMarkdown.size())
        return;
    if (chapter != m_shownChapter) {
        QString md = m_chapterMarkdown[chapter];
        // Links between chapters ("(#plots)") go through the help.
        md.replace(QRegularExpression(QStringLiteral("\\]\\(#([A-Za-z0-9_-]+)\\)")), QStringLiteral("](help:\\1)"));
        m_page->document()->setMarkdown(md);
        m_shownChapter = chapter;
        fitImages();
        highlight();
    }
    int y = 0;
    if (!headingTitle.isEmpty())
        for (QTextBlock b = m_page->document()->begin(); b.isValid(); b = b.next())
            if (b.blockFormat().headingLevel() > 0 && b.text().trimmed() == headingTitle.trimmed()) {
                y = int(m_page->document()->documentLayout()->blockBoundingRect(b).top());
                break;
            }
    m_page->verticalScrollBar()->setValue(y);
    // Once laid out (pictures sized), again: the page may have been taller or shorter before.
    QTimer::singleShot(0, this, [this, y]() { m_page->verticalScrollBar()->setValue(y); });
}

void HelpView::fitImages()
{
    // Pictures as wide as the page at most.
    const int maxW = std::max(200, m_page->viewport()->width() - 40);
    QTextDocument *doc = m_page->document();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid() || !frag.charFormat().isImageFormat())
                continue;
            QTextImageFormat fmt = frag.charFormat().toImageFormat();
            const QImage img = doc->resource(QTextDocument::ImageResource, QUrl(fmt.name())).value<QImage>();
            const QSize natural = img.isNull() ? QSize() : img.size();
            if (natural.isEmpty())
                continue;
            const double k = std::min(1.0, double(maxW) / natural.width());
            fmt.setWidth(natural.width() * k);
            fmt.setHeight(natural.height() * k);
            QTextCursor c(doc);
            c.setPosition(frag.position());
            c.setPosition(frag.position() + frag.length(), QTextCursor::KeepAnchor);
            c.setCharFormat(fmt);
        }
}

void HelpView::highlight()
{
    QList<QTextEdit::ExtraSelection> marks;
    const QStringList words = m_search->text().simplified().split(' ', Qt::SkipEmptyParts);
    QTextCharFormat fmt;
    fmt.setBackground(QColor(0xe0, 0xa0, 0x30, 110));
    for (const QString &w : words) {
        QTextCursor c(m_page->document());
        while (!(c = m_page->document()->find(w, c)).isNull()) {
            QTextEdit::ExtraSelection sel;
            sel.cursor = c;
            sel.format = fmt;
            marks << sel;
            if (marks.size() > 500)
                break;
        }
    }
    m_page->setExtraSelections(marks);
}

void HelpView::resizeEvent(QResizeEvent *ev)
{
    QWidget::resizeEvent(ev);
    fitImages();
}

} // namespace lumora
