// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QLineEdit;
class QListWidget;
class QTextBrowser;
class QLabel;

namespace lumora {

class AppContext;

// The user guide, built in: the contents (or, while searching, the sections that match) on the
// left, a chapter on the right with its pictures and links. Add it as a window (+ → Help) or
// press F1.
class HelpView : public QWidget {
    Q_OBJECT
public:
    explicit HelpView(AppContext *ctx, QWidget *parent = nullptr);
    void showTopic(const QString &id);   // a chapter or section by its id ("plots", "goto")
    static void openWindow(AppContext *ctx, QWidget *parent);   // F1: the guide in a window of its own

protected:
    void resizeEvent(QResizeEvent *ev) override;
    void showEvent(QShowEvent *ev) override;

private:
    struct Section {
        int level = 2;        // 1 part, 2 chapter, 3 section
        QString title;
        QString id;           // its {#id}, or one made from the title
        int chapter = -1;     // the chapter (level 2) it's in
        QString text;         // its own text (to search)
    };
    void load();
    void ensureLoaded();   // reads the guide the first time it's needed
    bool m_loaded = false;
    void fillList();
    void showChapter(int chapter, const QString &headingTitle = QString());
    void highlight();
    void fitImages();
    AppContext *m_ctx = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QTextBrowser *m_page = nullptr;
    QLabel *m_count = nullptr;
    QVector<Section> m_sections;
    QStringList m_chapterMarkdown;   // per chapter index into m_sections (level 2 only)
    int m_shownChapter = -1;
};

} // namespace lumora
