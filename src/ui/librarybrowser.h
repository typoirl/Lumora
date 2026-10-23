// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QHash>
#include <QObject>

#include <functional>
#include <QSet>
#include <QUrl>
#include <QVector>

class QComboBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QTabBar;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

namespace lumora {

class AppContext;

// The open fixture libraries Lumora can pull fixture types from:
//   GDTF Share            manufacturer-made GDTF files (needs a free gdtf-share.com account)
//   Open Fixture Library  community fixtures, JSON
//   QLC+                  the QLC+ fixture definitions, with their gobo images
// The OFL and QLC+ indexes ship with Lumora so browsing works offline; refresh() fetches the
// current ones. Downloaded fixtures go into the open show; the files are only a download cache.
// One instance is shared by the library browser and the patch screen.
class FixtureSources : public QObject {
    Q_OBJECT
public:
    enum Source { Gdtf, Ofl, Qlc, SourceCount };
    struct Entry {
        QString manufacturer;
        QString name;
        QString details;    // modes / footprints / revision
        QString key;        // OFL "maker/key", QLC+ "Maker/File", GDTF rid
        QString typeId;     // id the fixture type gets once imported
        QString tag;        // e.g. "Manufacturer" for manufacturer-made GDTF files
    };

    static FixtureSources *instance(AppContext *ctx);
    static QString sourceName(int source);

    // A source's list, read from its cache (or the bundled copy) the first time it's asked for.
    const QVector<Entry> &entries(int source) const
    {
        const_cast<FixtureSources *>(this)->ensureIndex(source);
        return m_entries[source];
    }
    bool installed(const Entry &e) const;
    bool signedIn() const { return m_signedIn; }
    // remember: "Stay signed in". The session cookie is kept between runs, and the password is
    // kept (encrypted on Windows) so an expired session signs in again by itself.
    void signIn(const QString &user, const QString &password, bool remember = true);
    void signOut();
    // Runs `then(true)` once there's a GDTF Share session, signing in with the remembered
    // password when needed; `then(false)` when that isn't possible.
    void ensureSession(const std::function<void(bool)> &then);
    QString userName() const;
    bool canSignInAutomatically() const;
    void refresh(int source);
    // Downloads an entry (and its gobo images) and loads it into the library.
    void fetch(int source, int index);
    bool fetching(int source, int index) const;
    bool busy() const { return m_running > 0 || !m_queue.isEmpty(); }

signals:
    void indexChanged(int source);
    void signedInChanged(bool on);
    void message(const QString &text, bool error);
    void progress(int done, int total);
    void imported(int source, int index, const QString &typeId);
    void failed(int source, int index, const QString &why);
    void idle();

private:
    explicit FixtureSources(AppContext *ctx);
    struct Job {
        int source = 0;
        int entry = -1;
        QUrl url;
        QString savePath;
        bool primary = true;   // the fixture file itself (gobos are secondary)
        int retries = 0;       // signed in again after the session expired
    };
    void saveCookies();
    void loadIndex(int source);
    void ensureIndex(int source);
    void parseOflIndex(const QByteArray &json);
    void parseQlcIndex(const QByteArray &json);
    void parseGdtfList(const QByteArray &json);
    void enqueue(const Job &job);
    void pump();
    void finished(QNetworkReply *reply, const Job &job);
    void entryDone(int source, int entry);
    static QString cacheDir();
    static QString saveRoot(int source);
    static QString jobKey(int source, int entry) { return QStringLiteral("%1:%2").arg(source).arg(entry); }

    AppContext *m_ctx = nullptr;
    QNetworkAccessManager *m_net = nullptr;
    QVector<Entry> m_entries[SourceCount];
    bool m_indexLoaded[SourceCount] = {};
    QHash<QString, QString> m_oflMakers;
    bool m_signedIn = false;
    bool m_signingIn = false;
    bool m_listRetried = false;
    QVector<std::function<void(bool)>> m_afterSignIn;
    class PersistentCookieJar *m_jar = nullptr;
    QVector<Job> m_queue;
    int m_running = 0;
    int m_jobsTotal = 0, m_jobsDone = 0;
    QHash<QString, int> m_outstanding;
    QHash<QString, QString> m_primaryPath;
    QHash<QString, QString> m_primaryError;
};

// Browse one library at a time and import many fixture types at once.
class FixtureLibraryDialog : public QDialog {
    Q_OBJECT
public:
    explicit FixtureLibraryDialog(AppContext *ctx, QWidget *parent = nullptr);
    // Developer: switch to a source, search, and import the first match (or just show it).
    void debugSearch(int source, const QString &text, bool import);

private:
    void populate();
    void signInIfKept();
    void applyFilter();
    void importSelected();
    void markInstalled(int source, int entry);
    // The tab's source: one of FixtureSources::Source, or kBuiltin for Lumora's own fixtures.
    int currentSource() const;
    QList<QTreeWidgetItem *> fixtureItems() const;   // every fixture row, inside its manufacturer
    static constexpr int kBuiltin = -1;

    AppContext *m_ctx = nullptr;
    FixtureSources *m_src = nullptr;
    QTabBar *m_tabs = nullptr;
    QWidget *m_login = nullptr;
    QWidget *m_account = nullptr;
    QLabel *m_accountLabel = nullptr;
    QLineEdit *m_user = nullptr;
    QLineEdit *m_password = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_maker = nullptr;
    QTreeWidget *m_list = nullptr;
    QLabel *m_note = nullptr;
    QLabel *m_status = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_refresh = nullptr;
    QPushButton *m_import = nullptr;
    int m_imported = 0;
    QStringList m_errors;
    QVector<QString> m_builtinIds;
    bool m_searchWasOpen = false;   // groups were opened by a search: close them when it's cleared   // the Built in tab's rows → fixture type ids
};

} // namespace lumora
