#include "videoutils.h"

#include <QProcess>
#include <QFileInfo>
#include <QStringList>

namespace VideoUtils
{
    bool isVideoFile(const QString &filename)
    {
        static const QStringList vidExts = {
            QStringLiteral("mp4"), QStringLiteral("mkv"),
            QStringLiteral("mov"), QStringLiteral("avi"),
            QStringLiteral("webm"), QStringLiteral("3gp"),
            QStringLiteral("flv"), QStringLiteral("wmv"),
            QStringLiteral("m4v"), QStringLiteral("ts"),
        };
        return vidExts.contains(QFileInfo(filename).suffix().toLower());
    }

    QImage extractFirstFrame(const QString &localPath)
    {
        QProcess proc;
        QStringList args = {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-ss"), QStringLiteral("00:00:00"),
            QStringLiteral("-i"), localPath,
            QStringLiteral("-vframes"), QStringLiteral("1"),
            QStringLiteral("-f"), QStringLiteral("image2"),
            QStringLiteral("-c:v"), QStringLiteral("mjpeg"),
            QStringLiteral("pipe:1")
        };
        proc.start(QStringLiteral("ffmpeg"), args);
        if (!proc.waitForFinished(7000)) {
            proc.kill();
            return {};
        }
        QByteArray data = proc.readAllStandardOutput();
        if (data.isEmpty()) return {};
        QImage img;
        img.loadFromData(data, "JPEG");
        return img;
    }

    QImage extractFirstFrameFromData(const QByteArray &videoData)
    {
        if (videoData.isEmpty()) return {};

        QProcess proc;
        QStringList args = {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-ss"), QStringLiteral("00:00:00"),
            QStringLiteral("-i"), QStringLiteral("pipe:0"),
            QStringLiteral("-vframes"), QStringLiteral("1"),
            QStringLiteral("-f"), QStringLiteral("image2"),
            QStringLiteral("-c:v"), QStringLiteral("mjpeg"),
            QStringLiteral("pipe:1")
        };
        proc.start(QStringLiteral("ffmpeg"), args);
        if (!proc.waitForStarted(3000)) return {};

        proc.write(videoData);
        proc.closeWriteChannel();

        if (!proc.waitForFinished(7000)) {
            proc.kill();
            return {};
        }
        QByteArray data = proc.readAllStandardOutput();
        if (data.isEmpty()) return {};
        QImage img;
        img.loadFromData(data, "JPEG");
        return img;
    }
}
