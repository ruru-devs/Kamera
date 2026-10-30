#include "videoutils.h"

#include <QProcess>
#include <QFileInfo>
#include <QStringList>
#include <QPainter>
#include <QPolygon>
#include <QPoint>

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
            QStringLiteral("-nostdin"),
            QStringLiteral("-protocol_whitelist"), QStringLiteral("file,pipe"),
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
            QStringLiteral("-nostdin"),
            QStringLiteral("-protocol_whitelist"), QStringLiteral("file,pipe"),
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

    QImage generateVideoPlaceholder(int width, int height)
    {
        QImage placeholder(width, height, QImage::Format_ARGB32_Premultiplied);
        placeholder.fill(QColor(24, 24, 37)); // Catppuccin mantle-like dark tone

        QPainter painter(&placeholder);
        painter.setRenderHint(QPainter::Antialiasing, true);

        // Draw central filmstrip / play badge graphic
        const int circleRadius = qMin(width, height) / 4;
        const QPoint center(width / 2, height / 2);

        painter.setBrush(QColor(49, 50, 68, 200));
        painter.setPen(QPen(QColor(180, 190, 254), 2));
        painter.drawEllipse(center, circleRadius, circleRadius);

        // Draw play triangle
        const int triSize = circleRadius * 0.7;
        QPolygon triangle;
        triangle << QPoint(center.x() - triSize * 0.4, center.y() - triSize * 0.6)
                 << QPoint(center.x() + triSize * 0.7, center.y())
                 << QPoint(center.x() - triSize * 0.4, center.y() + triSize * 0.6);

        painter.setBrush(QColor(245, 245, 245));
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(triangle);

        return placeholder;
    }
}
