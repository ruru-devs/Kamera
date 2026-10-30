#pragma once

#include <QQuickImageProvider>
#include <QQuickAsyncImageProvider>
#include <QQuickTextureFactory>
#include <QImage>
#include <memory>
#include <atomic>

// Serves local cached thumbnail files to QML via
// image://thumb/<percent-encoded-absolute-path>
class ThumbnailProvider : public QQuickImageProvider
{
public:
    explicit ThumbnailProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

// Asynchronously loads and serves original full-resolution images
// (supporting local files as well as remote KIO URLs like kdeconnect://) via
// image://full/<percent-encoded-url>
class FullImageResponse : public QQuickImageResponse
{
    Q_OBJECT
public:
    explicit FullImageResponse(const QString &id, const QSize &requestedSize);
    QQuickTextureFactory *textureFactory() const override;
    void cancel() override;

private:
    QImage m_image;
    std::shared_ptr<std::atomic<bool>> m_cancelled;
};

class FullImageProvider : public QQuickAsyncImageProvider
{
public:
    explicit FullImageProvider();
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};
