#include "gallerymodel.h"
#include "imagediskcache.h"
#include "videoutils.h"
#include "kiohelper.h"

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
#include <KIO/StoredTransferJob>
#include <KIO/JobUiDelegateFactory>
#include <KFileItem>
#include <KNotification>
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
    if (filename.startsWith(QLatin1Char('.'))) {
        return false; // Skip dotfiles and Android temporary .trashed/.pending files
    }

    static const QStringList imgExts = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("png"), QStringLiteral("webp"),
        QStringLiteral("heic"), QStringLiteral("heif"),
        QStringLiteral("bmp"), QStringLiteral("gif"),
        QStringLiteral("tiff"), QStringLiteral("tif"),
    };
    const QString ext = QFileInfo(filename).suffix().toLower();
    return imgExts.contains(ext) || VideoUtils::isVideoFile(filename);
}

// ──────────────────────────────────────────────
// GalleryModel
// ──────────────────────────────────────────────

GalleryModel::GalleryModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_scanWatcher    = new QFutureWatcher<void>(this);
    m_thumbWatcher   = new QFutureWatcher<void>(this);
    m_preloadWatcher = new QFutureWatcher<void>(this);

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
            if (!e.hasThumb && !e.thumbFailed && !m_failedThumbIds.contains(e.id)) {
                needsMore = true;
                break;
            }
        }
        if (needsMore && !m_abort.load()) {
            generateThumbs();
        }
    });

    connect(this, &GalleryModel::thumbReady, this, [this](const QString &id) {
        const QString thumbPath = thumbCacheDir() + QStringLiteral("/") + id + QStringLiteral(".jpg");
        for (auto &e : m_all) {
            if (e.id == id) {
                e.localThumb = thumbPath;
                e.hasThumb = true;
                e.thumbFailed = false;
                break;
            }
        }
        for (int i = 0; i < m_filtered.size(); ++i) {
            if (m_filtered[i].id == id) {
                m_filtered[i].localThumb = thumbPath;
                m_filtered[i].hasThumb = true;
                m_filtered[i].thumbFailed = false;
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
    if (m_scanWatcher) {
        m_scanWatcher->waitForFinished();
    }
    if (m_thumbWatcher) {
        m_thumbWatcher->waitForFinished();
    }
    if (m_preloadWatcher) {
        m_preloadWatcher->waitForFinished();
    }
}

void GalleryModel::cancelBackgroundTasks()
{
    m_abort.store(true);
    m_preloadAbort.store(true);
    if (m_scanWatcher) {
        m_scanWatcher->cancel();
    }
    if (m_thumbWatcher) {
        m_thumbWatcher->cancel();
    }
    if (m_preloadWatcher) {
        m_preloadWatcher->cancel();
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
    q.exec(QStringLiteral("PRAGMA journal_mode = WAL;"));
    q.exec(QStringLiteral("PRAGMA synchronous = NORMAL;"));

    q.exec(QStringLiteral(R"sql(
        CREATE TABLE IF NOT EXISTS photos (
            id           TEXT PRIMARY KEY,
            url          TEXT NOT NULL UNIQUE,
            filename     TEXT,
            date_taken   TEXT,
            folder       TEXT,
            file_size    INTEGER,
            is_video     INTEGER,
            local_thumb  TEXT,
            thumb_failed INTEGER DEFAULT 0
        )
    )sql"));

    // Migration in case column doesn't exist yet
    q.exec(QStringLiteral("ALTER TABLE photos ADD COLUMN thumb_failed INTEGER DEFAULT 0;"));

    q.exec(QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_photos_url ON photos(url);"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_photos_folder ON photos(folder);"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_photos_date ON photos(date_taken);"));

    q.exec(QStringLiteral(R"sql(
        CREATE TABLE IF NOT EXISTS watched_folders (
            path TEXT PRIMARY KEY
        )
    )sql"));

    // Clean up any corrupt rows where folder URL was accidentally saved as photo URL
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

    // Migration: update photos whose folder was previously saved as just base folderName
    for (const QString &wf : std::as_const(m_watchedFolders)) {
        QString trimmedWf = wf;
        while (trimmedWf.endsWith(QLatin1Char('/')))
            trimmedWf.chop(1);
        const QString baseName = QUrl(trimmedWf).fileName();
        if (!baseName.isEmpty()) {
            QSqlQuery upd(m_db);
            upd.prepare(QStringLiteral("UPDATE photos SET folder = ? WHERE folder = ?"));
            upd.addBindValue(wf);
            upd.addBindValue(baseName);
            upd.exec();
        }
    }
}

void GalleryModel::loadFromDb()
{
    beginResetModel();
    m_all.clear();

    QSqlQuery q(m_db);
    q.exec(QStringLiteral(
        "SELECT id, url, filename, date_taken, folder, file_size, is_video, local_thumb, thumb_failed "
        "FROM photos ORDER BY date_taken DESC"
    ));

    const QString cacheDir = thumbCacheDir();

    while (q.next()) {
        PhotoEntry e;
        e.id          = q.value(0).toString();
        e.url         = QUrl(q.value(1).toString());
        e.filename    = q.value(2).toString();
        e.dateTaken   = QDateTime::fromString(q.value(3).toString(), Qt::ISODate);
        e.folder      = q.value(4).toString();
        e.fileSize    = q.value(5).toLongLong();
        e.isVideo     = q.value(6).toInt() == 1;
        e.localThumb  = q.value(7).toString();
        e.thumbFailed = q.value(8).toInt() == 1;

        if (e.localThumb.isEmpty()) {
            const QString candidate = cacheDir + QStringLiteral("/") + e.id + QStringLiteral(".jpg");
            if (QFile::exists(candidate)) {
                e.localThumb = candidate;
            }
        }

        e.hasThumb = !e.localThumb.isEmpty() && QFile::exists(e.localThumb);
        m_all.append(e);
    }

    applyFilter();
    endResetModel();
    emit countChanged();
}

static bool folderMatches(const PhotoEntry &e, const QString &activeFolder)
{
    if (activeFolder.isEmpty()) return true;

    QString normActive = activeFolder;
    while (normActive.endsWith(QLatin1Char('/')))
        normActive.chop(1);

    QString normFolder = e.folder;
    while (normFolder.endsWith(QLatin1Char('/')))
        normFolder.chop(1);

    // 1. Direct match (normalized, case-insensitive)
    if (normFolder.compare(normActive, Qt::CaseInsensitive) == 0)
        return true;

    // 2. Photo URL is located inside activeFolder directory tree
    const QString urlStr = e.url.toString();
    if (urlStr.startsWith(normActive + QLatin1Char('/'), Qt::CaseInsensitive) ||
        urlStr.startsWith(normActive, Qt::CaseInsensitive))
    {
        return true;
    }

    // 3. Fallback for legacy DB rows where folder was stored as base folder name (e.g. "Camera")
    const QString baseActive = QUrl(normActive).fileName();
    if (!baseActive.isEmpty() && normFolder.compare(baseActive, Qt::CaseInsensitive) == 0)
        return true;

    return false;
}

void GalleryModel::applyFilter()
{
    m_filtered.clear();
    if (m_viewMode == QLatin1String("folder") && !m_activeFolder.isEmpty()) {
        for (const PhotoEntry &e : std::as_const(m_all)) {
            if (folderMatches(e, m_activeFolder))
                m_filtered.append(e);
        }
    } else {
        m_filtered = m_all;
    }
}

// ── QAbstractListModel ────────────────────────

int GalleryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_filtered.size();
}

QVariant GalleryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_filtered.size())
        return {};

    const PhotoEntry &e = m_filtered.at(index.row());
    switch (role) {
    case IdRole:         return e.id;
    case UrlRole:        return e.url.toString();
    case FilenameRole:   return e.filename;
    case DateTakenRole:  return e.dateTaken;
    case DateStringRole: return e.dateTaken.toString(QStringLiteral("dd MMM yyyy"));
    case FolderRole:     return e.folder;
    case IsVideoRole:    return e.isVideo;
    case LocalThumbRole: return e.localThumb;
    case HasThumbRole:   return e.hasThumb;
    default:             return {};
    }
}

QHash<int, QByteArray> GalleryModel::roleNames() const
{
    return {
        {IdRole,         "id"},
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

// ── Watched Folders ───────────────────────────

void GalleryModel::addWatchedFolder(const QString &kioPath)
{
    if (kioPath.isEmpty() || m_watchedFolders.contains(kioPath)) return;

    m_watchedFolders << kioPath;
    emit watchedFoldersChanged();

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO watched_folders (path) VALUES (?)"));
    q.addBindValue(kioPath);
    q.exec();

    scan();
}

void GalleryModel::removeWatchedFolder(const QString &kioPath)
{
    if (!m_watchedFolders.contains(kioPath)) return;

    m_watchedFolders.removeAll(kioPath);
    emit watchedFoldersChanged();

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM watched_folders WHERE path = ?"));
    q.addBindValue(kioPath);
    q.exec();

    q.prepare(QStringLiteral("DELETE FROM photos WHERE folder = ?"));
    q.addBindValue(kioPath);
    q.exec();

    loadFromDb();
}

// ── Scanning ──────────────────────────────────

void GalleryModel::scan()
{
    if (m_scanning) return;
    m_scanning = true;
    m_abort.store(false);
    emit scanningChanged();

    // Reset failed thumbnails on rescan
    m_failedThumbIds.clear();
    QSqlQuery resetFailed(m_db);
    resetFailed.exec(QStringLiteral("UPDATE photos SET thumb_failed = 0"));

    const QStringList folders = m_watchedFolders;
    const QString dbPathCopy = dbPath();
    const QString thumbDir = thumbCacheDir();

    auto future = QtConcurrent::run([folders, dbPathCopy, thumbDir, this]() {
        static QAtomicInteger<quint64> s_scanConnectionCounter{0};
        const QString connectionName =
            QStringLiteral("kamera_scan_%1").arg(s_scanConnectionCounter.fetchAndAddOrdered(1));

        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            db.setDatabaseName(dbPathCopy);
            if (!db.open()) {
                qWarning() << "Scan thread cannot open DB";
                return;
            }

            for (const QString &folderPath : folders) {
                if (m_abort.load()) break;
                QUrl folderUrl(folderPath);

                QList<KFileItem> items;
                bool listError = false;
                QString errorText;
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
                    QObject::connect(job, &KJob::finished, &loop, [&listError, &errorText, &loop](KJob *j) {
                        if (j->error()) {
                            listError = true;
                            errorText = j->errorString();
                        }
                        loop.quit();
                    }, Qt::QueuedConnection);

                    job->start();
                }, Qt::QueuedConnection);

                loop.exec();
                if (m_abort.load()) break;

                if (listError) {
                    QMetaObject::invokeMethod(this, [this, errorText]() {
                        emit error(errorText);
                    });
                    continue;
                }

                // Collect found URLs for pruning removed items
                QSet<QString> foundUrls;
                for (const KFileItem &item : std::as_const(items)) {
                    if (isMediaFile(item.name())) {
                        foundUrls.insert(item.url().toString());
                    }
                }

                db.transaction();

                // 1. Prune DB entries that were deleted on the source
                QSqlQuery delCheck(db);
                delCheck.prepare(QStringLiteral("SELECT id, url, local_thumb FROM photos WHERE folder = ?"));
                delCheck.addBindValue(folderPath);
                if (delCheck.exec()) {
                    while (delCheck.next()) {
                        const QString url = delCheck.value(1).toString();
                        if (!foundUrls.contains(url)) {
                            const QString id = delCheck.value(0).toString();
                            const QString thumb = delCheck.value(2).toString();
                            if (!thumb.isEmpty() && QFile::exists(thumb)) {
                                QFile::remove(thumb);
                            }
                            QSqlQuery prune(db);
                            prune.prepare(QStringLiteral("DELETE FROM photos WHERE id = ?"));
                            prune.addBindValue(id);
                            prune.exec();
                        }
                    }
                }

                // 2. Batch insert/update newly found or modified items
                QSqlQuery ins(db);
                ins.prepare(QStringLiteral(R"sql(
                    INSERT INTO photos
                        (id, url, filename, date_taken, folder, file_size, is_video, local_thumb, thumb_failed)
                    VALUES (?, ?, ?, ?, ?, ?, ?, '', 0)
                    ON CONFLICT(url) DO UPDATE SET
                        filename = excluded.filename,
                        date_taken = excluded.date_taken,
                        folder = excluded.folder,
                        file_size = excluded.file_size,
                        is_video = excluded.is_video
                )sql"));

                for (const KFileItem &item : std::as_const(items)) {
                    if (m_abort.load()) break;
                    if (!isMediaFile(item.name())) continue;

                    const QUrl url = item.url();
                    const QString id = photoId(url);
                    QDateTime dt = item.time(KFileItem::ModificationTime);

                    ins.addBindValue(id);
                    ins.addBindValue(url.toString());
                    ins.addBindValue(item.name());
                    ins.addBindValue(dt.toString(Qt::ISODate));
                    ins.addBindValue(folderPath);
                    ins.addBindValue(item.size());
                    ins.addBindValue(VideoUtils::isVideoFile(item.name()) ? 1 : 0);
                    ins.exec();
                }

                db.commit();
            }

            db.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
    });

    m_scanWatcher->setFuture(future);
}

// ── Thumbnails ────────────────────────────────

void GalleryModel::generateThumbs()
{
    if (m_generatingThumbs) return;

    // Collect entries that need a thumb and haven't failed
    QList<PhotoEntry> todo;
    for (const PhotoEntry &e : std::as_const(m_all)) {
        if (!e.hasThumb && !e.thumbFailed && !m_failedThumbIds.contains(e.id)) {
            todo << e;
        }
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

        {
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
                    QString localPath = entry.url.toLocalFile();
                    QImage img;
                    if (entry.isVideo) {
                        img = VideoUtils::extractFirstFrame(localPath);
                        if (img.isNull()) {
                            img = VideoUtils::generateVideoPlaceholder(320, 320);
                        }
                    } else {
                        QImageReader reader(localPath);
                        reader.setAutoTransform(true);
                        img = reader.read();
                    }

                    if (!img.isNull()) {
                        if (img.width() > 320 || img.height() > 320)
                            img = img.scaled(QSize(320, 320), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        thumbSaved = img.save(thumbPath, "JPEG", 85);
                    }
                } else {
                    // Remote file
                    bool ok = false;
                    QByteArray data = KioHelper::fetchViaKio(entry.url, 12000, &ok);
                    if (m_abort.load()) break;

                    QImage img;
                    if (!data.isEmpty()) {
                        if (entry.isVideo) {
                            img = VideoUtils::extractFirstFrameFromData(data);
                        } else {
                            img = KioHelper::decodeImageWithAutoTransform(data);
                        }
                    }

                    // If remote video frame extraction couldn't parse the stream (e.g. moov atom at end),
                    // use clean video placeholder so thumbnail generation completes without hanging
                    if (img.isNull() && entry.isVideo) {
                        img = VideoUtils::generateVideoPlaceholder(320, 320);
                    }

                    if (!img.isNull()) {
                        if (img.width() > 320 || img.height() > 320)
                            img = img.scaled(QSize(320, 320), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        thumbSaved = img.save(thumbPath, "JPEG", 85);
                    }
                }

                if (thumbSaved) {
                    QSqlQuery q(db);
                    q.prepare(QStringLiteral("UPDATE photos SET local_thumb = ?, thumb_failed = 0 WHERE id = ?"));
                    q.addBindValue(thumbPath);
                    q.addBindValue(entry.id);
                    q.exec();

                    QMetaObject::invokeMethod(this, [this, id = entry.id]() {
                        emit thumbReady(id);
                    });
                } else {
                    // Record failure to prevent endless looping
                    QSqlQuery q(db);
                    q.prepare(QStringLiteral("UPDATE photos SET thumb_failed = 1 WHERE id = ?"));
                    q.addBindValue(entry.id);
                    q.exec();

                    QMetaObject::invokeMethod(this, [this, id = entry.id]() {
                        m_failedThumbIds.insert(id);
                    });
                }
            }

            db.close();
        }
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
    const PhotoEntry e = m_filtered.at(idx);

    QUrl dest = QUrl::fromLocalFile(destPath + QStringLiteral("/") + e.filename);
    auto *job = KIO::copy(e.url, dest, KIO::DefaultFlags);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(
        KJobUiDelegate::AutoHandlingEnabled, nullptr));

    connect(job, &KJob::result, this, [this, e, destPath](KJob *j) {
        if (j->error()) {
            emit error(j->errorString());
        } else {
            const QString msg = tr("Copied %1 to %2").arg(e.filename, destPath);
            emit notification(msg);
            KNotification::event(QStringLiteral("notification"), msg);
        }
    });

    job->start();
}

void GalleryModel::deleteFromPhone(int idx)
{
    if (idx < 0 || idx >= m_filtered.size()) return;
    const PhotoEntry e = m_filtered.at(idx);

    // For local files, use trash if available; for remote devices, del
    KJob *job = e.url.isLocalFile() ? static_cast<KJob *>(KIO::trash(e.url)) : static_cast<KJob *>(KIO::del(e.url));

    connect(job, &KJob::finished, this, [this, e](KJob *j) {
        if (j->error()) {
            emit error(j->errorString());
            return;
        }

        // Remove cached thumb
        if (!e.localThumb.isEmpty() && QFile::exists(e.localThumb)) {
            QFile::remove(e.localThumb);
        }

        // Remove from DB
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("DELETE FROM photos WHERE id = ?"));
        q.addBindValue(e.id);
        q.exec();

        emit notification(tr("Deleted %1").arg(e.filename));
        loadFromDb();
    });
    job->start();
}

void GalleryModel::renameFile(int idx, const QString &newName)
{
    if (idx < 0 || idx >= m_filtered.size()) return;
    const PhotoEntry e = m_filtered.at(idx);

    if (newName.trimmed().isEmpty() || newName == e.filename || newName.contains(QLatin1Char('/'))) {
        return;
    }

    QUrl dest = e.url.adjusted(QUrl::RemoveFilename);
    dest.setPath(dest.path() + newName);

    const QString newId = photoId(dest);

    auto *job = KIO::rename(e.url, dest, KIO::DefaultFlags);
    connect(job, &KJob::finished, this, [this, e, newName, dest, newId](KJob *j) {
        if (j->error()) {
            emit error(j->errorString());
            return;
        }

        QString newThumbPath = e.localThumb;
        if (!e.localThumb.isEmpty() && QFile::exists(e.localThumb)) {
            newThumbPath = thumbCacheDir() + QStringLiteral("/") + newId + QStringLiteral(".jpg");
            QFile::rename(e.localThumb, newThumbPath);
        }

        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "UPDATE photos SET id = ?, filename = ?, url = ?, local_thumb = ? WHERE id = ?"));
        q.addBindValue(newId);
        q.addBindValue(newName);
        q.addBindValue(dest.toString());
        q.addBindValue(newThumbPath);
        q.addBindValue(e.id);
        q.exec();

        emit notification(tr("Renamed %1 to %2").arg(e.filename, newName));
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

QVariantMap GalleryModel::get(int idx) const
{
    if (idx < 0 || idx >= m_filtered.size()) return {};
    const PhotoEntry &e = m_filtered.at(idx);
    QVariantMap map;
    map[QStringLiteral("id")]         = e.id;
    map[QStringLiteral("url")]        = e.url.toString();
    map[QStringLiteral("filename")]   = e.filename;
    map[QStringLiteral("dateTaken")]  = e.dateTaken;
    map[QStringLiteral("folder")]     = e.folder;
    map[QStringLiteral("fileSize")]   = e.fileSize;
    map[QStringLiteral("isVideo")]    = e.isVideo;
    map[QStringLiteral("localThumb")] = e.localThumb;
    map[QStringLiteral("hasThumb")]   = e.hasThumb;
    return map;
}

int GalleryModel::indexOfId(const QString &id) const
{
    if (id.isEmpty()) return -1;
    for (int i = 0; i < m_filtered.size(); ++i) {
        if (m_filtered.at(i).id == id)
            return i;
    }
    return -1;
}

QString GalleryModel::idForIndex(int index) const
{
    if (index < 0 || index >= m_filtered.size()) return {};
    return m_filtered.at(index).id;
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

    // Signal abort to any in-flight preload so it terminates promptly
    m_preloadAbort.store(true);

    struct PreloadTarget {
        QUrl url;
        bool isVideo;
    };
    QList<PreloadTarget> targets;
    const int total = m_filtered.size();

    for (int step = 1; step <= distance; ++step) {
        int nextIdx = (current + step) % total;
        int prevIdx = (current - step + total) % total;

        if (nextIdx >= 0 && nextIdx < total) {
            const PhotoEntry &e = m_filtered.at(nextIdx);
            if (!e.url.isLocalFile()) {
                targets.append({e.url, e.isVideo});
            }
        }
        if (prevIdx >= 0 && prevIdx < total && prevIdx != nextIdx) {
            const PhotoEntry &e = m_filtered.at(prevIdx);
            if (!e.url.isLocalFile()) {
                targets.append({e.url, e.isVideo});
            }
        }
    }

    if (targets.isEmpty()) return;

    m_preloadAbort.store(false);

    QThreadPool::globalInstance()->start([targets, this]() {
        for (const auto &target : targets) {
            if (m_preloadAbort.load() || m_abort.load()) break;
            const QString urlStr = target.url.toString();
            if (ImageDiskCache::contains(urlStr)) continue;

            bool ok = false;
            QByteArray data = KioHelper::fetchViaKio(target.url, 10000, &ok, &m_preloadAbort);
            if (m_preloadAbort.load() || m_abort.load()) break;

            QImage img;
            if (!data.isEmpty()) {
                if (target.isVideo) {
                    img = VideoUtils::extractFirstFrameFromData(data);
                    if (img.isNull()) {
                        img = VideoUtils::generateVideoPlaceholder(640, 640);
                    }
                } else {
                    img = KioHelper::decodeImageWithAutoTransform(data);
                }
            }

            if (!img.isNull() && !m_preloadAbort.load() && !m_abort.load()) {
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

    if (m_viewMode == QLatin1String("folder") && m_activeFolder.isEmpty() && !m_watchedFolders.isEmpty()) {
        m_activeFolder = m_watchedFolders.first();
        emit activeFolderChanged();
    }

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
