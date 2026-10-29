#pragma once

#include <QString>
#include <QImage>

namespace ImageDiskCache
{
    // Returns directory path in ~/.cache/kamera/full_disk_cache/
    QString cacheDir();

    // Cache an image to disk using URL as key, keeping at most maxEntries files
    void put(const QString &urlStr, const QImage &img, int maxEntries = 20);

    // Retrieve cached full image from disk if available
    QImage get(const QString &urlStr);

    // Check if an image is in disk cache
    bool contains(const QString &urlStr);

    // Clean up all cached full images on disk (called on app exit)
    void clear();
}
