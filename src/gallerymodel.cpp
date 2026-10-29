#include "gallerymodel.h"
#include "imagediskcache.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QCryptographicHash>
#include <QMimeDatabase>
#include <QImage>
#include <QImageReader>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>

// KIO
#include <KIO/ListJob>
#include <KIO/StatJob>
#include <KIO/CopyJob>
#include <KIO/DeleteJob>
#include <KIO/MkdirJob>
#include <KIO/OpenUrlJob>
#include <KIO/StoredTransferJob> // KIO::storedGet, used in generateThumb()
#include <KIO/JobUiDelegateFactory>
#include <KFileItem>
#include <QAtomicInteger>

// ──────────────────────────────────────────────
// helpers
// ──────────────────────────────────────────────

static QString photoId(const QUrl &url)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(url.toString().toUtf8(),
                                 QCryptographicHash::Md5).toHex());
}

static bool isMediaFile(const QString &filename)
{
    static const QStringList imgExts = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("png"), QStringLiteral("webp"),
        QStringLiteral("heic"), QStringLiteral("heif"),
        QStringLiteral("bmp"), QStringLiteral("gif"),
        QStringLiteral("tiff"), QStringLiteral("tif"),
    };
    static const QStringList vidExts = {
        QStringLiteral("mp4"), QStringLiteral("mkv"),
        QStringLiteral("mov"), QStringLiteral("avi"),
        QStringLiteral("webm"), QStringLiteral("3gp"),
    };
    const QString ext = QFileInfo(filename).suffix().toLower();
    return imgExts.contains(ext) || vidExts.contains(ext);
}

static bool isVideoFile(const QString &filename)
{
    static const QStringList vidExts = {
        QStringLiteral("mp4"), QStringLiteral("mkv"),
        QStringLiteral("mov"), QStringLiteral("avi"),
        QStringLiteral("webm"), QStringLiteral("3gp"),
    };
    return vidExts.contains(QFileInfo(filename).suffix().toLower());
}

// ──────────────────────────────────────────────
// GalleryModel
// ──────────────────────────────────────────────

GalleryModel::GalleryModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_scanWatcher  = new QFutureWatcher<void>(this);
    m_thumbWatcher = new QFutureWatcher<void>(this);

    connect(m_scanWatcher, &QFutureWatcher<void>::finished, this, [this]() {
        m_scanning = false;
        emit scanningChanged();
        loadFromDb();
        emit scanFinished();
        generateThumbs();
    });

    connect(m_thumbWatcher, &QFutureWatcher<void>::finished, this, [this]() {
        m_generatingThumbs = false;
        emit generatingThumbsChanged();
        loadFromDb();
        bool needsMore = false;
        for (const PhotoEntry &e : std::as_const(m_all)) {
            if (!e.isVideo && (e.localThumb.isEmpty() || !QFile::exists(e.localThumb))) {
                needsMore = true;
                break;
            }
        }
        if (needsMore)
            generateThumbs();
    });

    connect(this, &GalleryModel::thumbReady, this, [this](const QString &id) {
        const QString thumbPath = thumbCacheDir() + QStringLiteral("/") + id + QStringLiteral(".jpg");
        for (auto &e : m_all) {
            if (e.id == id) {
                e.localThumb = thumbPath;
                break;
            }
        }
        for (int i = 0; i < m_filtered.size(); ++i) {
            if (m_filtered[i].id == id) {
                m_filtered[i].localThumb = thumbPath;
                emit dataChanged(index(i), index(i), {HasThumbRole, LocalThumbRole});
                break;
            }
        }
    });

    connect(qApp, &QCoreApplication::aboutToQuit, this, &GalleryModel::cancelBackgroundTasks);

    initDb();
    loadFromDb();
}

GalleryModel::~GalleryModel()
{
    cancelBackgroundTasks();
}

void GalleryModel::cancelBackgroundTasks()
{
    m_abort.store(true);
    if (m_scanWatcher) {
        m_scanWatcher->cancel();
    }
    if (m_thumbWatcher) {
        m_thumbWatcher->cancel();
    }
}

// ── DB ────────────────────────────────────────

QString GalleryModel::dbPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/kamera.db");
}

QString GalleryModel::thumbCacheDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
           + QStringLiteral("/thumbs");
}

void GalleryModel::initDb()
{
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    QDir().mkpath(thumbCacheDir());

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("kamera_db"));
    m_db.setDatabaseName(dbPath());
    if (!m_db.open()) {
        qWarning() << "Cannot open DB:" << m_db.lastError().text();
        return;
    }

    QSqlQuery q(m_db);
    q.exec(QStringLiteral(R"sql(
        CREATE TABLE IF NOT EXISTS photos (
            id          TEXT PRIMARY KEY,
            url         TEXT NOT NULL,
            filename    TEXT,
            date_taken  TEXT,
            folder      TEXT,
            file_size   INTEGER,
            is_video    INTEGER,
            local_thumb TEXT
        )
    )sql"));

    q.exec(QStringLiteral(R"sql(
        CREATE TABLE IF NOT EXISTS watched_folders (
            path TEXT PRIMARY KEY
        )
    )sql"));

    // Clean up any corrupt rows from previous bug where folder URL was saved as photo URL
    q.exec(QStringLiteral(
        "DELETE FROM photos WHERE url IN (SELECT path FROM watched_folders) "
        "OR url IN (SELECT rtrim(path, '/') FROM watched_folders) "
        "OR url IN (SELECT path || '/' FROM watched_folders)"
    ));

    // Load watched folders from db
    if (q.exec(QStringLiteral("SELECT path FROM watched_folders"))) {
        while (q.next())
            m_watchedFolders << q.value(0).toString();
    }
}

void GalleryModel::loadFromDb()
{
    if (!m_db.isOpen()) return;

    QSqlQuery q(m_db);
    q.exec(QStringLiteral(
        "SELECT id, url, filename, date_taken, folder, file_size, is_video, local_thumb "
        "FROM photos ORDER BY date_taken DESC"));

    QList<PhotoEntry> all;
    while (q.next()) {
        PhotoEntry e;
        e.id         = q.value(0).toString();
        e.url        = QUrl(q.value(1).toString());
        e.filename   = q.value(2).toString();
        e.dateTaken  = QDateTime::fromString(q.value(3).toString(), Qt::ISODate);
        e.folder     = q.value(4).toString();
        e.fileSize   = q.value(5).toLongLong();
        e.isVideo    = q.value(6).toBool();
        e.localThumb = q.value(7).toString();
        all << e;
    }

    beginResetModel();
    m_all = all;
    applyFilter();
    endResetModel();
    emit countChanged();
}

// ── Filter ────────────────────────────────────

void GalleryModel::applyFilter()
{
    if (m_viewMode == QLatin1String("folder") && !m_activeFolder.isEmpty()) {
        m_filtered.clear();
        for (const auto &e : std::as_const(m_all)) {
            if (e.folder == m_activeFolder)
                m_filtered << e;
        }
    } else {
        m_filtered = m_all;  // date mode: show everything, sorted by date (already is)
    }
}

// ── QAbstractListModel ────────────────────────

int GalleryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_filtered.size();
}

QVariant GalleryModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() >= m_filtered.size())
        return {};

    const PhotoEntry &e = m_filtered.at(idx.row());
    switch (role) {
    case UrlRole:       return e.url;
    case FilenameRole:  return e.filename;
    case DateTakenRole: return e.dateTaken;
    case DateStringRole:
        return e.dateTaken.isValid()
               ? e.dateTaken.toString(QStringLiteral("yyyy-MM-dd"))
               : QStringLiteral("Unknown date");
    case FolderRole:    return e.folder;
    case IsVideoRole:   return e.isVideo;
    case LocalThumbRole:return e.localThumb;
    case HasThumbRole:  return !e.localThumb.isEmpty() && QFile::exists(e.localThumb);
    }
    return {};
}

QHash<int, QByteArray> GalleryModel::roleNames() const
{
    return {
        {UrlRole,        "url"},
        {FilenameRole,   "filename"},
        {DateTakenRole,  "dateTaken"},
        {DateStringRole, "dateString"},
        {FolderRole,     "folder"},
        {IsVideoRole,    "isVideo"},
        {LocalThumbRole, "localThumb"},
        {HasThumbRole,   "hasThumb"},
    };
}

// ── Watched folders ───────────────────────────

void GalleryModel::addWatchedFolder(const QString &kioPath)
{
    if (m_watchedFolders.contains(kioPath)) return;
    m_watchedFolders << kioPath;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO watched_folders (path) VALUES (?)"));
    q.addBindValue(kioPath);
    q.exec();

    emit watchedFoldersChanged();
    scan();
}

void GalleryModel::removeWatchedFolder(const QString &kioPath)
{
    m_watchedFolders.removeAll(kioPath);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM watched_folders WHERE path = ?"));
    q.addBindValue(kioPath);
    q.exec();

    QString trimmed = kioPath;
    if (trimmed.endsWith(QLatin1Char('/'))) trimmed.chop(1);
    const QString folderName = QUrl(trimmed).fileName();
    QSqlQuery q2(m_db);
    q2.prepare(QStringLiteral("DELETE FROM photos WHERE folder = ?"));
    q2.addBindValue(folderName);
    q2.exec();

    emit watchedFoldersChanged();
    loadFromDb();
}

// ── Scan ──────────────────────────────────────

void GalleryModel::scan()
{
    if (m_scanning || m_watchedFolders.isEmpty()) return;

    m_scanning = true;
    emit scanningChanged();

    // Capture by value so the lambda owns everything it needs
    QStringList folders = m_watchedFolders;
    QString dbPathCopy  = dbPath();

    auto future = QtConcurrent::run([folders, dbPathCopy, this]() {
        // Each concurrent scan needs its own uniquely-named db connection.
        // QThread::currentThread()->objectName() is empty for threads pulled
        // from QtConcurrent's global thread pool, so two scans (or two scans
        // that happen to land on the same pooled thread) would collide on
        // the same connection name and one QSqlDatabase::addDatabase() call
        // would silently replace the other's connection. Use a process-wide
        // atomic counter instead, which is unique per scan() invocation
        // regardless of which thread actually executes it.
        static QAtomicInteger<quint64> s_scanConnectionCounter{0};
        const QString connectionName =
            QStringLiteral("kamera_scan_%1").arg(s_scanConnectionCounter.fetchAndAddOrdered(1));

        QSqlDatabase db = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(dbPathCopy);
        if (!db.open()) {
            qWarning() << "Scan thread cannot open DB";
            return;
        }

        for (const QString &folderPath : folders) {
            if (m_abort.load()) break;
            QUrl folderUrl(folderPath);

            // KIO::listJob must run on the main thread (event loop required).
            // We schedule it there and block until it finishes.
            QList<KFileItem> items;
            QEventLoop loop;

            QMetaObject::invokeMethod(qApp, [&]() {
                auto *job = KIO::listDir(folderUrl, KIO::HideProgressInfo);
                QObject::connect(job, &KIO::ListJob::entries,
                                 [&items, folderUrl](KIO::Job *, const KIO::UDSEntryList &list) {
                    for (const auto &entry : list) {
                        KFileItem item(entry, folderUrl, false, true);
                        if (!item.isDir())
                            items << item;
                    }
                });
                QObject::connect(job, &KJob::finished, &loop, &QEventLoop::quit);
                job->start();
            }, Qt::BlockingQueuedConnection);

            loop.exec();
            if (m_abort.load()) break;

            QSqlQuery ins(db);
            ins.prepare(QStringLiteral(R"sql(
                INSERT OR IGNORE INTO photos
                    (id, url, filename, date_taken, folder, file_size, is_video, local_thumb)
                VALUES (?, ?, ?, ?, ?, ?, ?, '')
            )sql"));

            // Folder display/filter name must exactly match what
            // Sidebar.qml derives from the same watched-folder path (it
            // strips a trailing slash, then takes the last '/'-segment).
            // QUrl::fileName() alone returns "" when the path ends in '/',
            // which would silently break folder-mode filtering for any
            // folder chosen with a trailing slash (KUrlRequesterDialog can
            // produce these). Strip it first so both sides agree.
            QString trimmedFolderPath = folderPath;
            if (trimmedFolderPath.endsWith(QLatin1Char('/')))
                trimmedFolderPath.chop(1);
            const QString folderName = QUrl(trimmedFolderPath).fileName();

            for (const KFileItem &item : std::as_const(items)) {
                if (m_abort.load()) break;
                if (!isMediaFile(item.name())) continue;

                const QUrl url = item.url();
                const QString id = photoId(url);

                // Check if already in DB
                QSqlQuery chk(db);
                chk.prepare(QStringLiteral("SELECT id FROM photos WHERE id = ?"));
                chk.addBindValue(id);
                chk.exec();
                if (chk.next()) continue; // already indexed

                QDateTime dt = item.time(KFileItem::ModificationTime);

                ins.addBindValue(id);
                ins.addBindValue(url.toString());
                ins.addBindValue(item.name());
                ins.addBindValue(dt.toString(Qt::ISODate));
                ins.addBindValue(folderName);
                ins.addBindValue(item.size());
                ins.addBindValue(isVideoFile(item.name()) ? 1 : 0);
                ins.exec();
            }
        }

        db.close();
        QSqlDatabase::removeDatabase(db.connectionName());
    });

    m_scanWatcher->setFuture(future);
}

// ── Thumbnails ────────────────────────────────

void GalleryModel::generateThumbs()
{
    if (m_generatingThumbs) return;

    // Collect entries that need a thumb (images only, videos get a placeholder)
    QList<PhotoEntry> todo;
    for (const PhotoEntry &e : std::as_const(m_all)) {
        if (!e.isVideo && (e.localThumb.isEmpty() || !QFile::exists(e.localThumb)))
            todo << e;
    }
    if (todo.isEmpty()) return;

    m_generatingThumbs = true;
    emit generatingThumbsChanged();

    const QString cacheDir = thumbCacheDir();
    const QString dbPathCopy = dbPath();

    auto future = QtConcurrent::run([todo, cacheDir, dbPathCopy, this]() {
        static QAtomicInteger<quint64> s_thumbConnectionCounter{0};
        const QString connectionName =
            QStringLiteral("kamera_thumb_%1").arg(s_thumbConnectionCounter.fetchAndAddOrdered(1));

        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(dbPathCopy);
        if (!db.open()) {
            qWarning() << "Thumbnail thread cannot open DB";
            return;
        }

        for (const PhotoEntry &entry : todo) {
            if (m_abort.load()) break;
            const QString thumbPath = cacheDir + QStringLiteral("/") + entry.id + QStringLiteral(".jpg");
            bool thumbSaved = false;

            if (QFile::exists(thumbPath)) {
                thumbSaved = true;
            } else if (entry.url.isLocalFile()) {
                QImageReader reader(entry.url.toLocalFile());
                reader.setAutoTransform(true);
                QImage img = reader.read();
                if (!img.isNull()) {
                    if (img.width() > 1024 || img.height() > 1024)
                        img = img.scaled(QSize(1024, 1024), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    thumbSaved = img.save(thumbPath, "JPEG", 85);
                }
            } else {
                // Remote file: fetch data via KIO on the main thread event loop
                QByteArray data;
                QEventLoop loop;

                QMetaObject::invokeMethod(qApp, [&]() {
                    auto *job = KIO::storedGet(entry.url, KIO::NoReload, KIO::HideProgressInfo);
                    QObject::connect(job, &KIO::StoredTransferJob::result, [&](KJob *j) {
                        if (!j->error())
                            data = static_cast<KIO::StoredTransferJob *>(j)->data();
                        loop.quit();
                    });
                    job->start();
                }, Qt::BlockingQueuedConnection);

                loop.exec();
                if (m_abort.load()) break;

                if (!data.isEmpty()) {
                    QImage img;
                    img.loadFromData(data);
                    if (!img.isNull()) {
                        if (img.width() > 1024 || img.height() > 1024)
                            img = img.scaled(QSize(1024, 1024), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        thumbSaved = img.save(thumbPath, "JPEG", 85);
                    }
                }
            }

            if (thumbSaved) {
                QSqlQuery q(db);
                q.prepare(QStringLiteral("UPDATE photos SET local_thumb = ? WHERE id = ?"));
                q.addBindValue(thumbPath);
                q.addBindValue(entry.id);
                q.exec();

                QMetaObject::invokeMethod(this, [this, id = entry.id]() {
                    emit thumbReady(id);
                });
            }
        }

        db.close();
        QSqlDatabase::removeDatabase(connectionName);
    });

    m_thumbWatcher->setFuture(future);
}

// ── File actions ──────────────────────────────

void GalleryModel::openFullRes(int idx)
{
    if (idx < 0 || idx >= m_filtered.size()) return;
    const QUrl url = m_filtered.at(idx).url;

    auto *job = new KIO::OpenUrlJob(url);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(
        KJobUiDelegate::AutoHandlingEnabled, nullptr));
    job->setRunExecutables(false);
    job->start();
}

void GalleryModel::copyToPC(int idx, const QString &destPath)
{
    if (idx < 0 || idx >= m_filtered.size()) return;
    const PhotoEntry &e = m_filtered.at(idx);

    QUrl dest = QUrl::fromLocalFile(destPath + QStringLiteral("/") + e.filename);
    auto *job = KIO::copy(e.url, dest, KIO::DefaultFlags);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(
        KJobUiDelegate::AutoHandlingEnabled, nullptr));
    job->start();
}

void GalleryModel::deleteFromPhone(int idx)
{
    if (idx < 0 || idx >= m_filtered.size()) return;
    const PhotoEntry e = m_filtered.at(idx);

    auto *job = KIO::del(e.url, KIO::DefaultFlags);
    connect(job, &KJob::finished, this, [this, e](KJob *j) {
        if (j->error()) {
            emit error(j->errorString());
            return;
        }
        // Remove from DB
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("DELETE FROM photos WHERE id = ?"));
        q.addBindValue(e.id);
        q.exec();
        loadFromDb();
    });
    job->start();
}

void GalleryModel::renameFile(int idx, const QString &newName)
{
    if (idx < 0 || idx >= m_filtered.size()) return;
    const PhotoEntry e = m_filtered.at(idx);

    QUrl dest = e.url.adjusted(QUrl::RemoveFilename);
    dest.setPath(dest.path() + newName);

    auto *job = KIO::rename(e.url, dest, KIO::DefaultFlags);
    connect(job, &KJob::finished, this, [this, e, newName, dest](KJob *j) {
        if (j->error()) {
            emit error(j->errorString());
            return;
        }
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "UPDATE photos SET filename = ?, url = ? WHERE id = ?"));
        q.addBindValue(newName);
        q.addBindValue(dest.toString());
        q.addBindValue(e.id);
        q.exec();
        loadFromDb();
    });
    job->start();
}

// ── Helpers ───────────────────────────────────

QString GalleryModel::thumbPathForIndex(int idx) const
{
    if (idx < 0 || idx >= m_filtered.size()) return {};
    return m_filtered.at(idx).localThumb;
}

QVariantMap GalleryModel::exifForIndex(int idx) const
{
    if (idx < 0 || idx >= m_filtered.size()) return {};
    const PhotoEntry &e = m_filtered.at(idx);
    QVariantMap map;
    map[QStringLiteral("filename")]  = e.filename;
    map[QStringLiteral("dateTaken")] = e.dateTaken.toString(QStringLiteral("dd MMM yyyy, hh:mm"));
    map[QStringLiteral("folder")]    = e.folder;
    map[QStringLiteral("fileSize")]  = QString::number(e.fileSize / 1024) + QStringLiteral(" KB");
    map[QStringLiteral("isVideo")]   = e.isVideo;
    return map;
}

int GalleryModel::nextIndex(int current) const
{
    if (m_filtered.isEmpty()) return -1;
    return (current + 1) % m_filtered.size();
}

int GalleryModel::prevIndex(int current) const
{
    if (m_filtered.isEmpty()) return -1;
    return (current - 1 + m_filtered.size()) % m_filtered.size();
}

void GalleryModel::preloadNeighbors(int current, int distance)
{
    if (current < 0 || m_filtered.isEmpty() || distance <= 0) return;

    QList<QUrl> urlsToPreload;
    const int total = m_filtered.size();

    // Preload next and prev `distance` neighbors (e.g. 4 ahead, 4 behind)
    for (int step = 1; step <= distance; ++step) {
        int nextIdx = (current + step) % total;
        int prevIdx = (current - step + total) % total;

        if (nextIdx >= 0 && nextIdx < total && !m_filtered.at(nextIdx).isVideo) {
            urlsToPreload.append(m_filtered.at(nextIdx).url);
        }
        if (prevIdx >= 0 && prevIdx < total && prevIdx != nextIdx && !m_filtered.at(prevIdx).isVideo) {
            urlsToPreload.append(m_filtered.at(prevIdx).url);
        }
    }

    if (urlsToPreload.isEmpty()) return;

    QtConcurrent::run([urlsToPreload, this]() {
        for (const QUrl &url : urlsToPreload) {
            if (m_abort.load()) break;
            const QString urlStr = url.toString();
            if (ImageDiskCache::contains(urlStr)) continue;

            QImage img;
            if (url.isLocalFile() || !urlStr.contains(QStringLiteral("://"))) {
                QString localPath = url.isLocalFile() ? url.toLocalFile() : urlStr;
                QImageReader reader(localPath);
                reader.setAutoTransform(true);
                img = reader.read();
            } else {
                // Remote file
                QByteArray data;
                QEventLoop loop;
                QMetaObject::invokeMethod(qApp, [&]() {
                    auto *job = KIO::storedGet(url, KIO::NoReload, KIO::HideProgressInfo);
                    QObject::connect(job, &KIO::StoredTransferJob::result, [&](KJob *j) {
                        if (!j->error())
                            data = static_cast<KIO::StoredTransferJob *>(j)->data();
                        loop.quit();
                    });
                    job->start();
                }, Qt::QueuedConnection);

                loop.exec();
                if (m_abort.load()) break;

                if (!data.isEmpty()) {
                    img.loadFromData(data);
                }
            }

            if (!img.isNull()) {
                ImageDiskCache::put(urlStr, img, 20);
            }
        }
    });
}

// ── Grouping ──────────────────────────────────

QString GalleryModel::groupLabelForIndex(int idx) const
{
    if (idx < 0 || idx >= m_filtered.size()) return {};
    const QDateTime dt = m_filtered.at(idx).dateTaken;
    if (!dt.isValid()) return QStringLiteral("Unknown date");

    const QDate today = QDate::currentDate();
    const QDate d     = dt.date();

    if (d == today)
        return QStringLiteral("Today");
    if (d == today.addDays(-1))
        return QStringLiteral("Yesterday");
    if (d >= today.addDays(-7))
        return QStringLiteral("Last 7 days");
    if (d.year() == today.year() && d.month() == today.month())
        return QStringLiteral("This month");
    return dt.toString(QStringLiteral("MMMM yyyy"));
}

bool GalleryModel::isFirstInGroup(int idx) const
{
    if (idx <= 0) return true;
    return groupLabelForIndex(idx) != groupLabelForIndex(idx - 1);
}

// ── View / Filter setters ─────────────────────

void GalleryModel::setViewMode(const QString &mode)
{
    if (m_viewMode == mode) return;
    m_viewMode = mode;
    emit viewModeChanged();

    beginResetModel();
    applyFilter();
    endResetModel();
    emit countChanged();
}

void GalleryModel::setActiveFolder(const QString &f)
{
    if (m_activeFolder == f) return;
    m_activeFolder = f;
    emit activeFolderChanged();

    if (m_viewMode == QLatin1String("folder")) {
        beginResetModel();
        applyFilter();
        endResetModel();
        emit countChanged();
    }
}
