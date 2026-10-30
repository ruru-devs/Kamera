#pragma once

#include <QUrl>
#include <QByteArray>
#include <QImage>
#include <QImageReader>
#include <QBuffer>
#include <QEventLoop>
#include <QTimer>
#include <QCoreApplication>
#include <KIO/StoredTransferJob>

namespace KioHelper
{
    // Fetches remote data via KIO safely with a timeout and thread synchronization
    inline QByteArray fetchViaKio(const QUrl &url, int timeoutMs = 15000, bool *ok = nullptr)
    {
        if (ok) *ok = false;
        QByteArray data;
        bool success = false;
        QEventLoop loop;

        QMetaObject::invokeMethod(qApp, [&]() {
            auto *job = KIO::storedGet(url, KIO::NoReload, KIO::HideProgressInfo);
            QObject::connect(job, &KIO::StoredTransferJob::result, &loop, [&data, &success, &loop](KJob *j) {
                if (!j->error()) {
                    data = static_cast<KIO::StoredTransferJob *>(j)->data();
                    success = true;
                }
                loop.quit();
            }, Qt::QueuedConnection);

            QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
            job->start();
        }, Qt::QueuedConnection);

        loop.exec();

        if (ok) *ok = success;
        return data;
    }

    // Decodes image data from memory while honoring EXIF orientation
    inline QImage decodeImageWithAutoTransform(const QByteArray &data)
    {
        if (data.isEmpty()) return {};
        QBuffer buffer;
        buffer.setData(data);
        if (!buffer.open(QIODevice::ReadOnly)) return {};

        QImageReader reader(&buffer);
        reader.setAutoTransform(true);
        return reader.read();
    }
}
