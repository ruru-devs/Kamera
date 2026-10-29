#include "thumbnailprovider.h"
#include "imagediskcache.h"
#include "videoutils.h"

#include <QImage>
#include <QImageReader>
#include <QFile>
#include <QUrl>
#include <QEventLoop>
#include <QCoreApplication>
#include <QtConcurrent/QtConcurrent>
#include <KIO/StoredTransferJob>

ThumbnailProvider::ThumbnailProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{}

// id is the absolute local path of the cached thumbnail file,
// percent-encoded by QML callers (Image { source: "image://thumb/" + localThumb })
QImage ThumbnailProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // QML passes the path directly; decode any percent-encoding
    QString path = QUrl::fromPercentEncoding(id.toUtf8());

    QImage img(path);
    if (img.isNull()) {
        // Return a tiny placeholder so QML doesn't spin forever
        QImage placeholder(1, 1, QImage::Format_ARGB32);
        placeholder.fill(Qt::transparent);
        if (size) *size = placeholder.size();
        return placeholder;
    }

    if (requestedSize.isValid())
        img = img.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    if (size) *size = img.size();
    return img;
}

// ──────────────────────────────────────────────
// FullImageResponse & FullImageProvider
// ──────────────────────────────────────────────

FullImageResponse::FullImageResponse(const QString &id, const QSize &requestedSize)
{
    QtConcurrent::run([this, id, requestedSize]() {
        QString urlStr = QUrl::fromPercentEncoding(id.toUtf8());
        QUrl url(urlStr);
        QImage img;

        // 1. Check if already present in disk cache
        img = ImageDiskCache::get(urlStr);

        // 2. If not in cache, load from source
        if (img.isNull()) {
            const bool isVideo = VideoUtils::isVideoFile(url.fileName().isEmpty() ? urlStr : url.fileName());

            if (url.isLocalFile() || !urlStr.contains(QStringLiteral("://"))) {
                QString localPath = url.isLocalFile() ? url.toLocalFile() : urlStr;
                if (isVideo) {
                    img = VideoUtils::extractFirstFrame(localPath);
                } else {
                    QImageReader reader(localPath);
                    reader.setAutoTransform(true);
                    img = reader.read();
                }
            } else {
                // Remote file (e.g. kdeconnect://): fetch data via KIO
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

                if (!data.isEmpty()) {
                    if (isVideo) {
                        img = VideoUtils::extractFirstFrameFromData(data);
                    } else {
                        img.loadFromData(data);
                    }
                }
            }

            // Save into disk cache (LRU up to 20 images)
            if (!img.isNull()) {
                ImageDiskCache::put(urlStr, img, 20);
            }
        }

        if (requestedSize.isValid() && !img.isNull()) {
            img = img.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        m_image = img;
        emit finished();
    });
}

QQuickTextureFactory *FullImageResponse::textureFactory() const
{
    return QQuickTextureFactory::textureFactoryForImage(m_image);
}

FullImageProvider::FullImageProvider()
    : QQuickAsyncImageProvider()
{}

QQuickImageResponse *FullImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    return new FullImageResponse(id, requestedSize);
}
