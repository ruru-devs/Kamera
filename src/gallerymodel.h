#pragma once

#include <QAbstractListModel>
#include <QUrl>
#include <QDateTime>
#include <QStringList>
#include <QSqlDatabase>
#include <QFutureWatcher>
#include <QSet>
#include <atomic>

struct PhotoEntry {
    QString   id;          // unique: url string md5
    QUrl      url;
    QString   filename;
    QDateTime dateTaken;   // from EXIF or file mtime
    QString   folder;      // full watched-folder path
    qint64    fileSize;
    bool      isVideo;
    QString   localThumb;  // path in ~/.cache/kamera/thumbs/
    bool      hasThumb     = false;
    bool      thumbFailed  = false;
};

class GalleryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(bool generatingThumbs READ generatingThumbs NOTIFY generatingThumbsChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString viewMode READ viewMode WRITE setViewMode NOTIFY viewModeChanged)
    Q_PROPERTY(QString activeFolder READ activeFolder WRITE setActiveFolder NOTIFY activeFolderChanged)
    Q_PROPERTY(QStringList watchedFolders READ watchedFolders NOTIFY watchedFoldersChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole,
        UrlRole,
        FilenameRole,
        DateTakenRole,
        DateStringRole,
        FolderRole,
        IsVideoRole,
        LocalThumbRole,
        HasThumbRole,
    };

    explicit GalleryModel(QObject *parent = nullptr);
    ~GalleryModel() override;

    // QAbstractListModel
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool scanning() const { return m_scanning; }
    bool generatingThumbs() const { return m_generatingThumbs; }
    QString viewMode() const { return m_viewMode; }
    QString activeFolder() const { return m_activeFolder; }
    QStringList watchedFolders() const { return m_watchedFolders; }

    Q_INVOKABLE void addWatchedFolder(const QString &kioPath);
    Q_INVOKABLE void removeWatchedFolder(const QString &kioPath);
    Q_INVOKABLE void scan();
    Q_INVOKABLE void openFullRes(int index);
    Q_INVOKABLE void copyToPC(int index, const QString &destPath);
    Q_INVOKABLE void deleteFromPhone(int index);
    Q_INVOKABLE void renameFile(int index, const QString &newName);
    Q_INVOKABLE QString thumbPathForIndex(int index) const;
    Q_INVOKABLE QVariantMap exifForIndex(int index) const;
    Q_INVOKABLE void cancelBackgroundTasks();
    Q_INVOKABLE void preloadNeighbors(int index, int distance = 4);

    // ID / Index helpers for viewer tracking
    Q_INVOKABLE int indexOfId(const QString &id) const;
    Q_INVOKABLE QString idForIndex(int index) const;
    Q_INVOKABLE QVariantMap get(int index) const;

    // Navigation
    Q_INVOKABLE int nextIndex(int current) const;
    Q_INVOKABLE int prevIndex(int current) const;

    void setViewMode(const QString &mode);    // "date" | "folder"
    void setActiveFolder(const QString &f);

    // date grouping helpers exposed to QML
    Q_INVOKABLE QString groupLabelForIndex(int index) const;
    Q_INVOKABLE bool isFirstInGroup(int index) const;

signals:
    void scanningChanged();
    void generatingThumbsChanged();
    void countChanged();
    void viewModeChanged();
    void activeFolderChanged();
    void watchedFoldersChanged();
    void thumbReady(const QString &photoId);
    void scanFinished();
    void error(const QString &msg);
    void notification(const QString &msg);

private:
    void initDb();
    void loadFromDb();
    void applyFilter();           // rebuilds m_filtered from m_all
    void generateThumbs();        // background thumb generation
    QString thumbCacheDir() const;
    QString dbPath() const;

    QList<PhotoEntry> m_all;       // all indexed photos
    QList<PhotoEntry> m_filtered;  // currently displayed (view of m_all)
    QSet<QString>     m_failedThumbIds;

    QString      m_viewMode    = QStringLiteral("date");
    QString      m_activeFolder;
    QStringList  m_watchedFolders;
    bool         m_scanning    = false;
    bool         m_generatingThumbs = false;

    QSqlDatabase m_db;
    QFutureWatcher<void> *m_scanWatcher    = nullptr;
    QFutureWatcher<void> *m_thumbWatcher   = nullptr;
    QFutureWatcher<void> *m_preloadWatcher = nullptr;
    std::atomic<bool>     m_abort{false};
    std::atomic<bool>     m_preloadAbort{false};
};
