// SPDX-License-Identifier: GPL-3.0-or-later
#include "librarybrowser.h"

#include "appcontext.h"
#include "credentials.h"
#include "theme.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTabBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QXmlStreamReader>

#include <algorithm>

namespace lumora {

namespace {

const QString kOflRaw = QStringLiteral("https://raw.githubusercontent.com/OpenLightingProject/open-fixture-library/master/");
const QString kOflTree = QStringLiteral("https://api.github.com/repos/OpenLightingProject/open-fixture-library/git/trees/master?recursive=1");
const QString kQlcRaw = QStringLiteral("https://raw.githubusercontent.com/mcallegari/qlcplus/master/resources/");
const QString kGdtfApi = QStringLiteral("https://gdtf-share.com/apis/public/");

const char *kCacheFiles[] = {"gdtf-list.json", "ofl-index.json", "qlcplus-index.json"};

QString prettyKey(const QString &key)
{
    QStringList words = key.split('-', Qt::SkipEmptyParts);
    for (QString &w : words)
        if (!w.isEmpty())
            w[0] = w[0].toUpper();
    return words.join(' ');
}

QString safeFileName(QString s)
{
    s.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9 ._()+-]")), QStringLiteral("_"));
    return s.trimmed();
}

QString encodePath(const QString &path)
{
    QStringList parts = path.split('/');
    for (QString &p : parts)
        p = QString::fromLatin1(QUrl::toPercentEncoding(p));
    return parts.join('/');
}

QNetworkRequest request(const QUrl &url)
{
    QNetworkRequest r(url);
    r.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Lumora/0.6 (open-source lighting console)"));
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(30000);
    return r;
}

void saveCache(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(data);
        f.commit();
    }
}

} // namespace

// ===========================================================================
// FixtureSources
// ===========================================================================

FixtureSources *FixtureSources::instance(AppContext *ctx)
{
    static FixtureSources *s = nullptr;
    if (!s)
        s = new FixtureSources(ctx);
    return s;
}

QString FixtureSources::sourceName(int source)
{
    switch (source) {
    case Gdtf: return tr("GDTF Share");
    case Ofl: return tr("Open Fixture Library");
    case Qlc: return tr("QLC+");
    }
    return QString();
}

// Keeps GDTF Share's session cookie between runs, so signing in lasts as long as the session.
class PersistentCookieJar : public QNetworkCookieJar {
public:
    using QNetworkCookieJar::QNetworkCookieJar;
    QList<QNetworkCookie> cookies() const { return allCookies(); }
    void setCookies(const QList<QNetworkCookie> &c) { setAllCookies(c); }
};

FixtureSources::FixtureSources(AppContext *ctx) : QObject(ctx), m_ctx(ctx), m_net(new QNetworkAccessManager(this))
{
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    m_jar = new PersistentCookieJar(this);
    m_net->setCookieJar(m_jar);
    QList<QNetworkCookie> saved;
    for (const QByteArray &raw : QSettings().value("library/gdtfCookies").value<QList<QByteArray>>())
        saved << QNetworkCookie::parseCookies(raw);
    m_jar->setCookies(saved);
    // A kept session counts as signed in until the server says otherwise.
    m_signedIn = !saved.isEmpty();
    // The lists are read when first needed (the Patch window's search, the library browser).
}

void FixtureSources::ensureIndex(int source)
{
    if (source < 0 || source >= SourceCount || m_indexLoaded[source])
        return;
    loadIndex(source);
}

QString FixtureSources::cacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/library";
}

QString FixtureSources::saveRoot(int source)
{
    // Only a download cache: the fixture itself goes into the show.
    static const char *dirs[] = {"gdtf", "ofl", "qlcplus"};
    return cacheDir() + "/downloads/" + dirs[source];
}

bool FixtureSources::installed(const Entry &e) const
{
    return m_ctx->show->library().find(e.typeId) != nullptr;
}

bool FixtureSources::fetching(int source, int index) const
{
    return m_outstanding.contains(jobKey(source, index));
}

void FixtureSources::loadIndex(int source)
{
    m_indexLoaded[source] = true;
    QFile cached(cacheDir() + '/' + kCacheFiles[source]);
    QByteArray data;
    if (cached.open(QIODevice::ReadOnly))
        data = cached.readAll();
    if (data.isEmpty() && source != Gdtf) {
        QFile bundled(QStringLiteral(":/library/") + kCacheFiles[source]);
        if (bundled.open(QIODevice::ReadOnly))
            data = bundled.readAll();
    }
    if (source == Ofl)
        parseOflIndex(data);
    else if (source == Qlc)
        parseQlcIndex(data);
    else
        parseGdtfList(data);
}

void FixtureSources::parseOflIndex(const QByteArray &json)
{
    m_indexLoaded[Ofl] = true;   // a fresh list from the network counts too
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QJsonObject makers = root["manufacturers"].toObject();
    m_oflMakers.clear();
    for (auto it = makers.begin(); it != makers.end(); ++it)
        m_oflMakers.insert(it.key(), it.value().toString());
    QVector<Entry> &list = m_entries[Ofl];
    list.clear();
    for (const QJsonValue &v : root["fixtures"].toArray()) {
        const QJsonObject o = v.toObject();
        Entry e;
        const QString m = o["m"].toString(), k = o["k"].toString();
        e.manufacturer = m_oflMakers.value(m, prettyKey(m));
        e.name = o["n"].toString(prettyKey(k));
        QStringList chans;
        for (const QJsonValue &c : o["ch"].toArray())
            if (c.toInt() > 0)
                chans << QString::number(c.toInt());
        QStringList parts;
        const QJsonArray cats = o["c"].toArray();
        if (!cats.isEmpty())
            parts << cats[0].toString();
        if (!chans.isEmpty())
            parts << tr("%1 ch").arg(chans.join('/'));
        e.details = parts.join(QStringLiteral(" · "));
        e.key = m + '/' + k;
        e.typeId = e.key;
        list.append(e);
    }
}

void FixtureSources::parseQlcIndex(const QByteArray &json)
{
    m_indexLoaded[Qlc] = true;   // a fresh list from the network counts too
    QVector<Entry> &list = m_entries[Qlc];
    list.clear();
    for (const QJsonValue &v : QJsonDocument::fromJson(json).object()["fixtures"].toArray()) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.manufacturer = o["m"].toString();
        e.name = o["n"].toString();
        e.key = e.manufacturer + '/' + o["f"].toString();
        e.typeId = FixtureLibrary::makeTypeId(e.manufacturer, e.name);
        list.append(e);
    }
}

void FixtureSources::parseGdtfList(const QByteArray &json)
{
    m_indexLoaded[Gdtf] = true;   // a fresh list from the network counts too
    // The list has one row per revision: keep the newest revision of each fixture.
    QHash<QString, QJsonObject> newest;
    for (const QJsonValue &v : QJsonDocument::fromJson(json).object()["list"].toArray()) {
        const QJsonObject o = v.toObject();
        const QString uuid = o["uuid"].toString(o["manufacturer"].toString() + '/' + o["fixture"].toString());
        auto it = newest.find(uuid);
        if (it == newest.end() || o["lastModified"].toDouble() > it->value("lastModified").toDouble())
            newest.insert(uuid, o);
    }
    QVector<Entry> &list = m_entries[Gdtf];
    list.clear();
    for (const QJsonObject &o : newest) {
        Entry e;
        e.manufacturer = o["manufacturer"].toString();
        e.name = o["fixture"].toString();
        QStringList fps;
        for (const QJsonValue &m : o["modes"].toArray()) {
            const QString fp = QString::number(m.toObject()["dmxfootprint"].toInt());
            if (!fps.contains(fp))
                fps << fp;
        }
        QStringList parts;
        if (!o["revision"].toString().isEmpty())
            parts << o["revision"].toString();
        if (!fps.isEmpty())
            parts << tr("%1 ch").arg(fps.mid(0, 6).join('/'));
        e.details = parts.join(QStringLiteral(" · "));
        e.key = QString::number(o["rid"].toInteger());
        e.typeId = FixtureLibrary::makeTypeId(e.manufacturer, e.name);
        if (o["uploader"].toString().startsWith(QLatin1String("Manuf")))
            e.tag = tr("Manufacturer");
        list.append(e);
    }
}

void FixtureSources::signIn(const QString &user, const QString &password, bool remember)
{
    if (user.isEmpty() || password.isEmpty()) {
        emit message(tr("Enter your GDTF Share user name and password"), true);
        return;
    }
    QSettings().setValue("library/gdtfUser", user);
    QNetworkRequest r = request(QUrl(kGdtfApi + "login.php"));
    r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QByteArray body = QJsonDocument(QJsonObject{{"user", user}, {"password", password}}).toJson(QJsonDocument::Compact);
    emit message(tr("Signing in…"), false);
    m_signingIn = true;
    QNetworkReply *reply = m_net->post(r, body);
    const bool silent = !m_afterSignIn.isEmpty();   // signing in again by itself
    connect(reply, &QNetworkReply::finished, this, [this, reply, password, remember, silent]() {
        reply->deleteLater();
        m_signingIn = false;
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const bool ok = o["result"].toBool();
        if (!ok) {
            emit message(o.contains("error") ? o["error"].toString() : tr("Couldn't sign in: %1").arg(reply->errorString()), true);
            if (o.contains("error"))   // wrong password: don't keep trying it
                QSettings().remove("library/gdtfPassword");
        } else {
            if (remember)
                QSettings().setValue("library/gdtfPassword", protectSecret(password.toUtf8()));
            else
                QSettings().remove("library/gdtfPassword");
            QSettings().setValue("library/gdtfRemember", remember);
            m_signedIn = true;
            saveCookies();
            emit signedInChanged(true);
            if (silent)
                emit message(tr("Signed in to GDTF Share again"), false);
        }
        const auto waiting = std::exchange(m_afterSignIn, {});
        for (const auto &f : waiting)
            f(ok);
        if (ok && !silent)
            refresh(Gdtf);
    });
}

void FixtureSources::signOut()
{
    m_jar->setCookies({});
    QSettings().remove("library/gdtfCookies");
    QSettings().remove("library/gdtfPassword");
    m_signedIn = false;
    emit signedInChanged(false);
    emit message(tr("Signed out of GDTF Share"), false);
}

QString FixtureSources::userName() const
{
    return QSettings().value("library/gdtfUser").toString();
}

bool FixtureSources::canSignInAutomatically() const
{
    return !userName().isEmpty() && !unprotectSecret(QSettings().value("library/gdtfPassword").toByteArray()).isEmpty();
}

void FixtureSources::saveCookies()
{
    QList<QByteArray> raw;
    for (const QNetworkCookie &c : m_jar->cookies())
        if (c.domain().contains(QLatin1String("gdtf-share")))
            raw << c.toRawForm();
    QSettings().setValue("library/gdtfCookies", QVariant::fromValue(raw));
}

void FixtureSources::ensureSession(const std::function<void(bool)> &then)
{
    if (m_signedIn) {
        then(true);
        return;
    }
    if (!canSignInAutomatically()) {
        then(false);
        return;
    }
    m_afterSignIn.append(then);
    if (m_signingIn)
        return;   // already on its way: run with that one
    const QByteArray password = unprotectSecret(QSettings().value("library/gdtfPassword").toByteArray());
    signIn(userName(), QString::fromUtf8(password), true);
}

void FixtureSources::refresh(int source)
{
    emit message(tr("Fetching the latest %1 list…").arg(sourceName(source)), false);
    auto fail = [this](QNetworkReply *reply) {
        emit message(tr("Couldn't refresh: %1").arg(reply->errorString()), true);
        emit indexChanged(-1);
    };

    if (source == Gdtf) {
        QNetworkReply *reply = m_net->get(request(QUrl(kGdtfApi + "getList.php")));
        connect(reply, &QNetworkReply::finished, this, [=]() {
            reply->deleteLater();
            const QByteArray data = reply->readAll();
            const QJsonObject o = QJsonDocument::fromJson(data).object();
            if (!o["result"].toBool()) {
                if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401) {
                    m_signedIn = false;
                    if (!m_listRetried && canSignInAutomatically()) {
                        // The session ran out: sign in again with the kept password, then retry.
                        m_listRetried = true;
                        ensureSession([this](bool ok) {
                            if (ok)
                                refresh(Gdtf);
                            else
                                emit signedInChanged(false);
                        });
                        return;
                    }
                    emit signedInChanged(false);
                    emit message(tr("Your GDTF Share session expired. Sign in again."), true);
                } else {
                    fail(reply);
                }
                return;
            }
            m_listRetried = false;
            saveCookies();
            saveCache(cacheDir() + '/' + kCacheFiles[Gdtf], data);
            parseGdtfList(data);
            emit message(tr("%1 fixtures on GDTF Share").arg(m_entries[Gdtf].size()), false);
            emit indexChanged(Gdtf);
        });
        return;
    }

    if (source == Qlc) {
        QNetworkReply *reply = m_net->get(request(QUrl(kQlcRaw + "fixtures/FixturesMap.xml")));
        connect(reply, &QNetworkReply::finished, this, [=]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError)
                return fail(reply);
            QXmlStreamReader r(reply->readAll());
            QJsonArray out;
            QString maker;
            while (!r.atEnd()) {
                r.readNext();
                if (!r.isStartElement())
                    continue;
                if (r.name() == QLatin1String("M"))
                    maker = r.attributes().value("n").toString();
                else if (r.name() == QLatin1String("F"))
                    out.append(QJsonObject{{"m", maker}, {"f", r.attributes().value("n").toString()},
                                           {"n", r.attributes().value("m").toString()}});
            }
            if (out.isEmpty())
                return fail(reply);
            const QByteArray data = QJsonDocument(QJsonObject{{"fixtures", out}}).toJson(QJsonDocument::Compact);
            saveCache(cacheDir() + '/' + kCacheFiles[Qlc], data);
            parseQlcIndex(data);
            emit message(tr("%1 QLC+ fixtures").arg(m_entries[Qlc].size()), false);
            emit indexChanged(Qlc);
        });
        return;
    }

    // OFL: the repository tree lists every fixture file; names we already know are kept,
    // new fixtures show their file name until they're imported.
    QNetworkReply *makers = m_net->get(request(QUrl(kOflRaw + "fixtures/manufacturers.json")));
    connect(makers, &QNetworkReply::finished, this, [=]() {
        makers->deleteLater();
        const QJsonObject mo = QJsonDocument::fromJson(makers->readAll()).object();
        QNetworkReply *tree = m_net->get(request(QUrl(kOflTree)));
        connect(tree, &QNetworkReply::finished, this, [=]() {
            tree->deleteLater();
            if (tree->error() != QNetworkReply::NoError)
                return fail(tree);
            QHash<QString, QJsonObject> known;
            {
                QFile f(cacheDir() + '/' + kCacheFiles[Ofl]);
                QFile b(QStringLiteral(":/library/") + kCacheFiles[Ofl]);
                QByteArray d = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
                if (d.isEmpty() && b.open(QIODevice::ReadOnly))
                    d = b.readAll();
                for (const QJsonValue &v : QJsonDocument::fromJson(d).object()["fixtures"].toArray())
                    known.insert(v["m"].toString() + '/' + v["k"].toString(), v.toObject());
            }
            QJsonArray fixtures;
            static const QRegularExpression re(QStringLiteral("^fixtures/([^/]+)/([^/]+)\\.json$"));
            for (const QJsonValue &v : QJsonDocument::fromJson(tree->readAll()).object()["tree"].toArray()) {
                const auto m = re.match(v["path"].toString());
                if (!m.hasMatch())
                    continue;
                const QString key = m.captured(1) + '/' + m.captured(2);
                fixtures.append(known.contains(key) ? known.value(key)
                                                    : QJsonObject{{"m", m.captured(1)}, {"k", m.captured(2)}});
            }
            if (fixtures.isEmpty())
                return fail(tree);
            QJsonObject names;
            for (auto it = mo.begin(); it != mo.end(); ++it)
                if (!it.key().startsWith('$'))
                    names.insert(it.key(), it.value().toObject().value("name").toString(it.key()));
            const QByteArray data = QJsonDocument(QJsonObject{{"manufacturers", names}, {"fixtures", fixtures}})
                                        .toJson(QJsonDocument::Compact);
            saveCache(cacheDir() + '/' + kCacheFiles[Ofl], data);
            parseOflIndex(data);
            emit message(tr("%1 Open Fixture Library fixtures").arg(m_entries[Ofl].size()), false);
            emit indexChanged(Ofl);
        });
    });
}

void FixtureSources::fetch(int source, int index)
{
    ensureIndex(source);
    if (index < 0 || index >= m_entries[source].size() || fetching(source, index))
        return;
    if (source == Gdtf && !m_signedIn) {
        if (canSignInAutomatically()) {
            ensureSession([this, source, index](bool ok) {
                if (ok)
                    fetch(source, index);
                else
                    emit failed(source, index, tr("Couldn't sign in to GDTF Share"));
            });
            return;
        }
        emit failed(source, index, tr("Sign in to GDTF Share first (Patch → Fixture library → GDTF Share)"));
        return;
    }
    const Entry &e = m_entries[source][index];
    if (source == Ofl) {
        // OFL fixture files only name their manufacturer by folder: give them the display names.
        QJsonObject makers;
        for (auto it = m_oflMakers.begin(); it != m_oflMakers.end(); ++it)
            makers.insert(it.key(), QJsonObject{{"name", it.value()}});
        saveCache(saveRoot(Ofl) + "/manufacturers.json", QJsonDocument(makers).toJson(QJsonDocument::Compact));
    }
    Job job;
    job.source = source;
    job.entry = index;
    if (source == Ofl) {
        job.url = QUrl(kOflRaw + "fixtures/" + encodePath(e.key) + ".json");
        job.savePath = saveRoot(Ofl) + '/' + e.key + ".json";
    } else if (source == Qlc) {
        job.url = QUrl(kQlcRaw + "fixtures/" + encodePath(e.key) + ".qxf");
        job.savePath = saveRoot(Qlc) + '/' + e.key + ".qxf";
    } else {
        job.url = QUrl(kGdtfApi + "downloadFile.php?rid=" + e.key);
        job.savePath = saveRoot(Gdtf) + '/' + safeFileName(e.manufacturer + '@' + e.name + '@' + e.key) + ".gdtf";
    }
    m_primaryPath.insert(jobKey(source, index), job.savePath);
    enqueue(job);
    pump();
}

void FixtureSources::enqueue(const Job &job)
{
    m_outstanding[jobKey(job.source, job.entry)] += 1;
    m_queue.append(job);
    ++m_jobsTotal;
    emit progress(m_jobsDone, m_jobsTotal);
}

void FixtureSources::pump()
{
    while (m_running < 4 && !m_queue.isEmpty()) {
        const Job job = m_queue.takeFirst();
        ++m_running;
        QNetworkReply *reply = m_net->get(request(job.url));
        connect(reply, &QNetworkReply::finished, this, [this, reply, job]() {
            reply->deleteLater();
            --m_running;
            ++m_jobsDone;
            finished(reply, job);
            pump();
            emit progress(m_jobsDone, m_jobsTotal);
            if (!busy()) {
                m_jobsDone = m_jobsTotal = 0;
                emit idle();
            }
        });
    }
}

void FixtureSources::finished(QNetworkReply *reply, const Job &job)
{
    const QString key = jobKey(job.source, job.entry);
    const QByteArray data = reply->readAll();
    const bool okHttp = reply->error() == QNetworkReply::NoError;
    const bool okData = job.source != Gdtf || !job.primary || data.startsWith("PK");
    if (!okHttp || !okData || data.isEmpty()) {
        if (job.primary) {
            QString why = reply->errorString();
            if (!okData) {
                const QJsonObject o = QJsonDocument::fromJson(data).object();
                why = o["error"].toString(tr("not a GDTF file"));
                if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401) {
                    m_signedIn = false;
                    if (job.retries == 0 && canSignInAutomatically()) {
                        // Session expired mid-download: sign in again and fetch it once more.
                        m_outstanding[key] += 1;   // still in flight
                        ensureSession([this, job, key](bool ok) {
                            if (ok) {
                                Job again = job;
                                again.retries = 1;
                                m_queue.append(again);
                                ++m_jobsTotal;
                                pump();
                                return;
                            }
                            m_primaryError.insert(key, tr("Couldn't sign in to GDTF Share again"));
                            if (--m_outstanding[key] <= 0) {
                                m_outstanding.remove(key);
                                entryDone(job.source, job.entry);
                            }
                        });
                        why.clear();
                    } else {
                        emit signedInChanged(false);
                        why = tr("GDTF Share session expired, sign in again");
                    }
                }
            }
            if (!why.isEmpty())
                m_primaryError.insert(key, why);
        }
        // A missing gobo image is not fatal.
    } else {
        QDir().mkpath(QFileInfo(job.savePath).absolutePath());
        QSaveFile f(job.savePath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(data);
            f.commit();
        }
        if (job.primary && job.source == Qlc) {
            // Fetch the gobo images the definition refers to.
            static const QRegularExpression res(QStringLiteral("Res1?=\"([^\"#][^\"]*\\.(?:png|svg|jpg))\""));
            QSet<QString> seen;
            for (auto it = res.globalMatch(QString::fromUtf8(data)); it.hasNext();) {
                const QString path = it.next().captured(1);
                const QString dest = saveRoot(Qlc) + "/gobos/" + path;
                if (seen.contains(path) || QFileInfo::exists(dest))
                    continue;
                seen.insert(path);
                Job g;
                g.source = Qlc;
                g.entry = job.entry;
                g.url = QUrl(kQlcRaw + "gobos/" + encodePath(path));
                g.savePath = dest;
                g.primary = false;
                enqueue(g);
            }
        }
        if (job.primary && job.source == Ofl) {
            // A renamed fixture is a stub pointing at its new file: fetch that too.
            const QString target = QJsonDocument::fromJson(data).object()["redirectTo"].toString();
            if (!target.isEmpty() && !QFileInfo::exists(saveRoot(Ofl) + '/' + target + ".json")) {
                Job r;
                r.source = Ofl;
                r.entry = job.entry;
                r.url = QUrl(kOflRaw + "fixtures/" + encodePath(target) + ".json");
                r.savePath = saveRoot(Ofl) + '/' + target + ".json";
                r.primary = false;
                enqueue(r);
            }
        }
    }
    if (--m_outstanding[key] <= 0) {
        m_outstanding.remove(key);
        entryDone(job.source, job.entry);
    }
}

void FixtureSources::entryDone(int source, int entry)
{
    const QString key = jobKey(source, entry);
    const QString path = m_primaryPath.take(key);
    const QString error = m_primaryError.take(key);
    if (!error.isEmpty() || path.isEmpty() || !QFileInfo::exists(path)) {
        emit failed(source, entry, error.isEmpty() ? tr("download failed") : error);
        return;
    }
    QString err, id;
    if (!m_ctx->show->importFixtureFile(path, &err, &id)) {
        QFile::remove(path);   // don't keep a file that won't load
        emit failed(source, entry, err);
        return;
    }
    m_entries[source][entry].typeId = id;   // the file knows best (GDTF names can differ from the listing)
    emit imported(source, entry, id);
}

// ===========================================================================
// FixtureLibraryDialog
// ===========================================================================

FixtureLibraryDialog::FixtureLibraryDialog(AppContext *ctx, QWidget *parent)
    : QDialog(parent), m_ctx(ctx), m_src(FixtureSources::instance(ctx))
{
    setWindowTitle(tr("Fixture library"));
    resize(900, 640);

    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(8);

    m_tabs = new QTabBar(this);
    m_tabs->setTabData(m_tabs->addTab(tr("Built in")), kBuiltin);
    for (int s = 0; s < FixtureSources::SourceCount; ++s)
        m_tabs->setTabData(m_tabs->addTab(FixtureSources::sourceName(s)), s);
    m_tabs->setExpanding(false);
    lay->addWidget(m_tabs);

    // GDTF Share sign-in
    m_login = new QWidget(this);
    auto *ll = new QHBoxLayout(m_login);
    ll->setContentsMargins(0, 0, 0, 0);
    m_user = new QLineEdit(m_login);
    m_user->setPlaceholderText(tr("GDTF Share user name"));
    m_user->setText(QSettings().value("library/gdtfUser").toString());
    m_password = new QLineEdit(m_login);
    m_password->setPlaceholderText(tr("Password"));
    m_password->setEchoMode(QLineEdit::Password);
    auto *remember = new QCheckBox(tr("Stay signed in"), m_login);
    remember->setChecked(QSettings().value("library/gdtfRemember", true).toBool());
    remember->setToolTip(tr("Keeps the session between runs and signs in again by itself when it expires. "
                            "The password is kept on this computer (encrypted for your Windows account)."));
    auto *signInBtn = new QPushButton(tr("Sign in"), m_login);
    auto *signup = new QLabel(tr("<a href=\"https://gdtf-share.com\">Free account at gdtf-share.com</a>"), m_login);
    signup->setOpenExternalLinks(true);
    ll->addWidget(m_user, 2);
    ll->addWidget(m_password, 2);
    ll->addWidget(remember);
    ll->addWidget(signInBtn);
    ll->addWidget(signup, 2);
    lay->addWidget(m_login);
    auto signIn = [this, remember]() {
        m_src->signIn(m_user->text(), m_password->text(), remember->isChecked());
        m_password->clear();
    };
    m_account = new QWidget(this);
    auto *al = new QHBoxLayout(m_account);
    al->setContentsMargins(0, 0, 0, 0);
    m_accountLabel = new QLabel(m_account);
    m_accountLabel->setProperty("role", "muted");
    auto *signOut = new QPushButton(tr("Sign out"), m_account);
    al->addWidget(m_accountLabel, 1);
    al->addWidget(signOut);
    lay->addWidget(m_account);
    connect(signOut, &QPushButton::clicked, m_src, &FixtureSources::signOut);
    connect(signInBtn, &QPushButton::clicked, this, signIn);
    connect(m_password, &QLineEdit::returnPressed, this, signIn);

    auto *filters = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search fixtures and manufacturers…"));
    m_search->setClearButtonEnabled(true);
    m_maker = new QComboBox(this);
    m_maker->setMinimumWidth(220);
    filters->addWidget(m_search, 3);
    filters->addWidget(m_maker, 1);
    lay->addLayout(filters);

    m_list = new QTreeWidget(this);
    m_list->setColumnCount(4);
    m_list->setHeaderLabels({tr("Fixture"), tr("Manufacturer"), tr("Details"), tr("")});
    m_list->setRootIsDecorated(true);   // grouped by manufacturer: open one to see its fixtures
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setUniformRowHeights(true);
    m_list->setSortingEnabled(true);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_list->header()->resizeSection(2, 220);
    m_list->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    lay->addWidget(m_list, 1);

    m_note = new QLabel(this);
    m_note->setProperty("role", "muted");
    m_note->setWordWrap(true);
    m_note->setOpenExternalLinks(true);
    lay->addWidget(m_note);

    auto *bottom = new QHBoxLayout;
    m_status = new QLabel(this);
    m_status->setProperty("role", "muted");
    m_progress = new QProgressBar(this);
    m_progress->setMaximumWidth(160);
    m_progress->setTextVisible(false);
    m_progress->hide();
    m_refresh = new QPushButton(tr("Refresh list"), this);
    m_import = new QPushButton(tr("Import selected"), this);
    m_import->setProperty("role", "primary");
    m_import->setEnabled(false);
    auto *close = new QPushButton(tr("Close"), this);
    bottom->addWidget(m_status, 1);
    bottom->addWidget(m_progress);
    bottom->addWidget(m_refresh);
    bottom->addWidget(m_import);
    bottom->addWidget(close);
    lay->addLayout(bottom);

    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_refresh, &QPushButton::clicked, this, [this]() {
        m_refresh->setEnabled(false);
        m_src->refresh(currentSource());
    });
    connect(m_import, &QPushButton::clicked, this, &FixtureLibraryDialog::importSelected);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
        if (it && it->parent())   // a manufacturer opens and closes instead
            importSelected();
    });
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, [this]() {
        int n = 0;
        for (QTreeWidgetItem *it : m_list->selectedItems())
            n += it->parent() != nullptr;
        m_import->setEnabled(n > 0);
        m_import->setText(n > 1 ? tr("Import %1 fixtures").arg(n) : tr("Import selected"));
    });
    connect(m_search, &QLineEdit::textChanged, this, &FixtureLibraryDialog::applyFilter);
    connect(m_maker, &QComboBox::currentIndexChanged, this, &FixtureLibraryDialog::applyFilter);
    connect(m_tabs, &QTabBar::currentChanged, this, [this]() {
        QSettings().setValue("library/tab", currentSource());
        populate();
        signInIfKept();
    });

    // The shared source service reports back here.
    connect(m_src, &FixtureSources::message, this, [this](const QString &text, bool error) {
        m_status->setText(text);
        m_status->setStyleSheet(error ? QStringLiteral("color: %1;").arg(theme::programmer.name()) : QString());
    });
    connect(m_src, &FixtureSources::indexChanged, this, [this]() {
        m_refresh->setEnabled(true);
        populate();
    });
    connect(m_src, &FixtureSources::signedInChanged, this, [this]() { populate(); });
    connect(m_src, &FixtureSources::progress, this, [this](int done, int total) {
        m_progress->setVisible(total > 0);
        m_progress->setRange(0, std::max(1, total));
        m_progress->setValue(done);
        if (total > 0)
            m_status->setText(tr("Downloading… %1 of %2").arg(done).arg(total));
    });
    connect(m_src, &FixtureSources::imported, this, [this](int source, int entry) {
        ++m_imported;
        markInstalled(source, entry);
    });
    connect(m_src, &FixtureSources::failed, this, [this](int source, int entry, const QString &why) {
        const FixtureSources::Entry &e = m_src->entries(source)[entry];
        m_errors << QStringLiteral("%1 %2: %3").arg(e.manufacturer, e.name, why);
    });
    connect(m_src, &FixtureSources::idle, this, [this]() {
        m_progress->hide();
        m_status->setStyleSheet(QString());
        if (!m_errors.isEmpty()) {
            m_status->setText(tr("%1 imported, %2 failed: %3").arg(m_imported).arg(m_errors.size()).arg(m_errors.first()));
            m_status->setToolTip(m_errors.join('\n'));
            m_status->setStyleSheet(QStringLiteral("color: %1;").arg(theme::programmer.name()));
            m_errors.clear();
        } else if (m_imported) {
            m_status->setText(tr("%1 in the library. Patch them from the Patch screen.")
                                  .arg(countOf(m_imported, tr("fixture type"), tr("fixture types"))));
        }
    });

    const int tab = QSettings().value("library/tab", int(FixtureSources::Ofl)).toInt();
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->tabData(i).toInt() == tab)
            m_tabs->setCurrentIndex(i);
    populate();
    signInIfKept();
}

void FixtureLibraryDialog::signInIfKept()
{
    // "Stay signed in": opening GDTF Share signs in by itself, then lists what's there.
    if (currentSource() != FixtureSources::Gdtf || m_src->signedIn() || !m_src->canSignInAutomatically())
        return;
    m_src->ensureSession([src = m_src](bool ok) {
        if (ok)
            src->refresh(FixtureSources::Gdtf);
    });
}

void FixtureLibraryDialog::debugSearch(int source, const QString &text, bool import)
{
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->tabData(i).toInt() == source)
            m_tabs->setCurrentIndex(i);
    m_search->setText(text);
    for (QTreeWidgetItem *it : fixtureItems())
        if (!it->isHidden() && !it->parent()->isHidden()) {
            m_list->setCurrentItem(it);
            break;
        }
    if (import)
        importSelected();
}

int FixtureLibraryDialog::currentSource() const
{
    return m_tabs->currentIndex() >= 0 ? m_tabs->tabData(m_tabs->currentIndex()).toInt() : int(FixtureSources::Ofl);
}

QList<QTreeWidgetItem *> FixtureLibraryDialog::fixtureItems() const
{
    QList<QTreeWidgetItem *> out;
    for (int g = 0; g < m_list->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_list->topLevelItem(g);
        for (int i = 0; i < group->childCount(); ++i)
            out << group->child(i);
    }
    return out;
}

void FixtureLibraryDialog::populate()
{
    const int source = currentSource();
    const bool builtin = source == kBuiltin;
    m_login->setVisible(source == FixtureSources::Gdtf && !m_src->signedIn());
    m_account->setVisible(source == FixtureSources::Gdtf && m_src->signedIn());
    m_accountLabel->setText(m_src->canSignInAutomatically()
                                ? tr("Signed in to GDTF Share as %1  ·  stays signed in").arg(m_src->userName())
                                : tr("Signed in to GDTF Share as %1").arg(m_src->userName()));

    // The rows: a source's index, or the fixture types Lumora ships with.
    QVector<FixtureSources::Entry> list;
    m_builtinIds.clear();
    if (builtin) {
        QList<FixtureType> types = m_ctx->show->library().shippedTypes();
        for (const FixtureType &t : types) {
            FixtureSources::Entry e;
            e.manufacturer = t.manufacturer.isEmpty() ? tr("Generic") : t.manufacturer;
            e.name = t.name;
            QStringList modes;
            for (const FixtureMode &m : t.modes)
                modes << QStringLiteral("%1 ch").arg(m.footprint);
            e.details = modes.join(QStringLiteral(", "));
            e.typeId = t.id;
            list << e;
            m_builtinIds << t.id;
        }
    } else {
        list = m_src->entries(source);
    }

    m_list->setSortingEnabled(false);
    m_list->clear();
    QStringList makers;
    QHash<QString, QTreeWidgetItem *> groups;
    QFont bold = m_list->font();
    bold.setBold(true);
    for (int i = 0; i < list.size(); ++i) {
        const FixtureSources::Entry &e = list[i];
        QTreeWidgetItem *group = groups.value(e.manufacturer);
        if (!group) {
            group = new QTreeWidgetItem;
            group->setText(0, e.manufacturer);
            group->setFont(0, bold);
            group->setFlags(Qt::ItemIsEnabled);   // opens; not imported itself
            group->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
            groups.insert(e.manufacturer, group);
            makers << e.manufacturer;
        }
        auto *it = new QTreeWidgetItem(group);
        it->setText(0, e.name);
        it->setText(1, e.manufacturer);
        it->setText(2, e.details);
        it->setData(0, Qt::UserRole, i);
        QString status = e.tag;
        const bool have = builtin ? m_ctx->show->library().isShowType(e.typeId) : m_src->installed(e);
        if (have) {
            status = builtin ? tr("In show") : tr("In library");
            for (int c = 0; c < 4; ++c)
                it->setForeground(c, theme::muted);
            it->setForeground(3, theme::live);
        } else if (builtin) {
            status = tr("Built in");
            it->setForeground(3, theme::muted);
        } else if (!e.tag.isEmpty()) {
            it->setForeground(3, theme::muted);
        }
        it->setText(3, status);
    }
    for (QTreeWidgetItem *group : std::as_const(groups)) {
        group->setText(2, countOf(group->childCount(), tr("fixture"), tr("fixtures")));
        group->setForeground(2, theme::muted);
    }
    m_list->addTopLevelItems(groups.values());
    m_list->setSortingEnabled(true);
    m_list->sortByColumn(0, Qt::AscendingOrder);

    const QString keep = m_maker->currentText();
    std::sort(makers.begin(), makers.end(), [](const QString &a, const QString &b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });
    m_maker->blockSignals(true);
    m_maker->clear();
    m_maker->addItem(tr("All manufacturers"));
    m_maker->addItems(makers);
    const int k = m_maker->findText(keep);
    m_maker->setCurrentIndex(k > 0 ? k : 0);
    m_maker->blockSignals(false);

    switch (source) {
    case kBuiltin:
        m_note->setText(tr("Fixtures that come with Lumora: generic fixtures, the Chauvet DJ and Chauvet Professional ranges "
                           "(every DMX mode from their manuals), Pangolin BEYOND (laser) and Resolume Arena (video). "
                           "They're always there to patch; Import copies one into the show so you can edit it."));
        break;
    case FixtureSources::Gdtf:
        m_note->setText(list.isEmpty()
                            ? tr("GDTF Share is the official library of GDTF files, most of them made by the manufacturers. "
                                 "Sign in with a free account to list and download them.")
                            : tr("GDTF files from <a href=\"https://gdtf-share.com\">gdtf-share.com</a>. Gobo images come with the file."));
        break;
    case FixtureSources::Ofl:
        m_note->setText(tr("Community fixtures from the <a href=\"https://open-fixture-library.org\">Open Fixture Library</a> (MIT). "
                           "Multi-cell fixtures import as one fixture with numbered cells."));
        break;
    case FixtureSources::Qlc:
        m_note->setText(tr("Fixture definitions from <a href=\"https://www.qlcplus.org\">QLC+</a> (Apache 2.0), "
                           "downloaded with their gobo images."));
        break;
    }
    m_refresh->setVisible(!builtin);
    m_refresh->setEnabled(source != FixtureSources::Gdtf || m_src->signedIn());
    applyFilter();
}

void FixtureLibraryDialog::applyFilter()
{
    const QStringList words = m_search->text().split(' ', Qt::SkipEmptyParts);
    const QString maker = m_maker->currentIndex() > 0 ? m_maker->currentText() : QString();
    // Searching (or picking a manufacturer) opens the groups with matches; otherwise they're closed
    // to a list of manufacturers.
    const bool open = !words.isEmpty() || !maker.isEmpty();
    int shown = 0, total = 0;
    m_list->setUpdatesEnabled(false);
    for (int g = 0; g < m_list->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_list->topLevelItem(g);
        int inGroup = 0;
        for (int i = 0; i < group->childCount(); ++i) {
            QTreeWidgetItem *it = group->child(i);
            bool show = maker.isEmpty() || it->text(1) == maker;
            if (show) {
                const QString hay = it->text(0) + ' ' + it->text(1) + ' ' + it->text(2);
                for (const QString &w : words)
                    if (!hay.contains(w, Qt::CaseInsensitive)) {
                        show = false;
                        break;
                    }
            }
            it->setHidden(!show);
            inGroup += show;
        }
        total += group->childCount();
        shown += inGroup;
        group->setHidden(inGroup == 0);
        group->setText(2, open && inGroup < group->childCount()
                              ? tr("%1 of %2").arg(inGroup).arg(countOf(group->childCount(), tr("fixture"), tr("fixtures")))
                              : countOf(group->childCount(), tr("fixture"), tr("fixtures")));
        if (open)
            group->setExpanded(inGroup > 0);
        else if (m_searchWasOpen)
            group->setExpanded(false);
    }
    m_searchWasOpen = open;
    m_list->setUpdatesEnabled(true);
    if (!m_src->busy())
        m_status->setText(total ? tr("%1 of %2").arg(shown).arg(countOf(total, tr("fixture"), tr("fixtures"))) : QString());
}

void FixtureLibraryDialog::importSelected()
{
    const int source = currentSource();
    if (source == kBuiltin) {
        int n = 0;
        for (QTreeWidgetItem *it : m_list->selectedItems()) {
            if (!it->parent())
                continue;
            const QString id = m_builtinIds.value(it->data(0, Qt::UserRole).toInt());
            const QList<FixtureType> types = m_ctx->show->library().shippedTypes();
            for (const FixtureType &t : types)
                if (t.id == id) {
                    m_ctx->show->updateFixtureType(t);
                    it->setText(3, tr("In show"));
                    for (int c = 0; c < 3; ++c)
                        it->setForeground(c, theme::muted);
                    it->setForeground(3, theme::live);
                    ++n;
                }
        }
        if (n)
            m_status->setText(tr("%1 copied into the show. Edit them from the Patch screen.")
                                  .arg(countOf(n, tr("fixture type"), tr("fixture types"))));
        return;
    }
    if (source == FixtureSources::Gdtf && !m_src->signedIn()) {
        m_status->setText(tr("Sign in to GDTF Share to download fixtures"));
        return;
    }
    for (QTreeWidgetItem *it : m_list->selectedItems())
        if (it->parent())
            m_src->fetch(source, it->data(0, Qt::UserRole).toInt());
}

void FixtureLibraryDialog::markInstalled(int source, int entry)
{
    if (source != currentSource())
        return;
    for (QTreeWidgetItem *it : fixtureItems()) {
        if (it->data(0, Qt::UserRole).toInt() == entry) {
            it->setText(3, tr("In library"));
            for (int c = 0; c < 3; ++c)
                it->setForeground(c, theme::muted);
            it->setForeground(3, theme::live);
        }
    }
}

} // namespace lumora
