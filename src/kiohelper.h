#pragma once

#include <QUrl>
#include <QByteArray>
#include <QImage>
#include <QImageReader>
#include <QBuffer>
#include <QPointer>
#include <QCoreApplication>
#include <KIO/StoredTransferJob>

#include <memory>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>

namespace KioHelper
{
    struct KioTask {
        std::mutex mtx;
        std::condition_variable cv;
        QByteArray data;
        bool finished = false;
        bool success = false;
        QPointer<KIO::StoredTransferJob> job;
    };

    // Safely fetches remote data via KIO from any thread using heap-allocated context,
    // condition variables, and responsive cancellation.
    inline QByteArray fetchViaKio(const QUrl &url, int timeoutMs = 15000, bool *ok = nullptr, std::atomic<bool> *cancelFlag = nullptr)
    {
        if (ok) *ok = false;
        if (cancelFlag && cancelFlag->load()) return {};

        auto task = std::make_shared<KioTask>();

        QMetaObject::invokeMethod(qApp, [task, url]() {
            auto *job = KIO::storedGet(url, KIO::NoReload, KIO::HideProgressInfo);
            task->job = job;
            QObject::connect(job, &KIO::StoredTransferJob::result, [task](KJob *j) {
                std::unique_lock<std::mutex> lock(task->mtx);
                if (!j->error()) {
                    task->data = static_cast<KIO::StoredTransferJob *>(j)->data();
                    task->success = true;
                }
                task->finished = true;
                task->cv.notify_all();
            });
            job->start();
        });

        std::unique_lock<std::mutex> lock(task->mtx);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

        while (!task->finished) {
            if (cancelFlag && cancelFlag->load()) {
                break;
            }
            if (task->cv.wait_until(lock, std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(100))) == std::cv_status::timeout) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    break;
                }
            }
        }

        if (!task->finished) {
            // Cancelled or timed out: kill the job quietly on the main thread so socket resources release cleanly
            QMetaObject::invokeMethod(qApp, [task]() {
                if (task->job) {
                    task->job->kill(KJob::Quietly);
                }
            });
            return {};
        }

        if (ok) *ok = task->success;
        return task->data;
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
