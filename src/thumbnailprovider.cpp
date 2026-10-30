#include "thumbnailprovider.h"
#include "imagediskcache.h"
#include "videoutils.h"
#include "kiohelper.h"

#include <QImage>
#include <QImageReader>
#include <QFile>
#include <QUrl>
#include <QPointer>
#include <QThreadPool>
#include <QCoreApplication>

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
    : m_cancelled(std::make_shared<std::atomic<bool>>(false))
{
    auto cancelled = m_cancelled;
    QPointer<FullImageResponse> self(this);

    QThreadPool::globalInstance()->start([self, cancelled, id, requestedSize]() {
        if (cancelled->load()) return;

        QString urlStr = QUrl::fromPercentEncoding(id.toUtf8());
        QUrl url(urlStr);
        QImage img;

        // 1. Check if already present in disk cache
        img = ImageDiskCache::get(urlStr);

        // 2. If not in cache, load from source
        if (img.isNull() && !cancelled->load()) {
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
                bool ok = false;
                QByteArray data = KioHelper::fetchViaKio(url, 15000, &ok);
                if (cancelled->load()) return;

                if (!data.isEmpty()) {
                    if (isVideo) {
                        img = VideoUtils::extractFirstFrameFromData(data);
                        if (img.isNull()) {
                            img = VideoUtils::generateVideoPlaceholder(640, 640);
                        }
                    } else {
                        img = KioHelper::decodeImageWithAutoTransform(data);
                    }
                }
            }

            // Save into disk cache (LRU up to 20 images)
            if (!img.isNull() && !cancelled->load()) {
                ImageDiskCache::put(urlStr, img, 20);
            }
        }

        if (cancelled->load()) return;

        if (requestedSize.isValid() && !img.isNull()) {
            img = img.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        if (cancelled->load()) return;

        QMetaObject::invokeMethod(qApp, [self, cancelled, img]() {
            if (self && !cancelled->load()) {
                self->m_image = img;
                emit self->finished();
            }
        });
    });
}

void FullImageResponse::cancel()
{
    if (m_cancelled) {
        m_cancelled->store(true);
    }
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
