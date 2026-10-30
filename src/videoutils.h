#pragma once

#include <QImage>
#include <QString>
#include <QByteArray>

namespace VideoUtils
{
    // Checks by filename extension if the file is a video
    bool isVideoFile(const QString &filename);

    // Extracts the first frame of a local video file using ffmpeg
    QImage extractFirstFrame(const QString &localPath);

    // Extracts the first frame from in-memory video data (e.g. downloaded via KIO)
    QImage extractFirstFrameFromData(const QByteArray &videoData);

    // Generates a fallback video thumbnail icon
    QImage generateVideoPlaceholder(int width = 320, int height = 320);
}
