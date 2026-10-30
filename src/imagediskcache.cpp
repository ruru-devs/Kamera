#include "imagediskcache.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>

namespace ImageDiskCache
{
    static QMutex s_mutex;

    QString cacheDir()
    {
        return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
               + QStringLiteral("/full_disk_cache");
    }

    static QString cacheFilePath(const QString &urlStr, bool png = false)
    {
        const QString hash = QString::fromLatin1(
            QCryptographicHash::hash(urlStr.toUtf8(), QCryptographicHash::Sha256).toHex());
        return cacheDir() + QStringLiteral("/") + hash + (png ? QStringLiteral(".png") : QStringLiteral(".jpg"));
    }

    void put(const QString &urlStr, const QImage &img, int maxEntries)
    {
        if (img.isNull()) return;

        QMutexLocker locker(&s_mutex);
        const QString dirPath = cacheDir();
        QDir dir(dirPath);
        if (!dir.exists()) {
            dir.mkpath(dirPath);
        }

        const bool hasAlpha = img.hasAlphaChannel();
        const QString filePath = cacheFilePath(urlStr, hasAlpha);
        
        if (hasAlpha) {
            img.save(filePath, "PNG");
        } else {
            img.save(filePath, "JPEG", 95);
        }

        // LRU cleanup: prune oldest files exceeding maxEntries
        QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Time | QDir::Reversed);
        while (files.size() > maxEntries) {
            QFile::remove(files.first().filePath());
            files.removeFirst();
        }
    }

    QImage get(const QString &urlStr)
    {
        QMutexLocker locker(&s_mutex);
        const QString pathPng = cacheFilePath(urlStr, true);
        const QString pathJpg = cacheFilePath(urlStr, false);

        QString filePath;
        if (QFile::exists(pathPng)) {
            filePath = pathPng;
        } else if (QFile::exists(pathJpg)) {
            filePath = pathJpg;
        }

        if (!filePath.isEmpty()) {
            // Touch file to update access/modification time for LRU
            QFile file(filePath);
            if (file.open(QIODevice::ReadWrite)) {
                file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
                file.close();
            }
            QImage img(filePath);
            if (!img.isNull()) {
                return img;
            }
        }
        return {};
    }

    bool contains(const QString &urlStr)
    {
        QMutexLocker locker(&s_mutex);
        return QFile::exists(cacheFilePath(urlStr, true)) || QFile::exists(cacheFilePath(urlStr, false));
    }

    void clear()
    {
        QMutexLocker locker(&s_mutex);
        const QString dirPath = cacheDir();
        QDir dir(dirPath);
        if (dir.exists()) {
            dir.removeRecursively();
        }
    }
}
