#pragma once

#include <QString>
#include <QtLogging>

namespace Logger
{
    // Initializes the logging system:
    // - Configures log location (~/.local/share/kamera/kamera.log)
    // - Enforces a 5MB size limit and 30-day retention policy (pruning expired entries)
    // - Installs the Qt message handler to capture all qDebug/qInfo/qWarning/qCritical/qFatal output
    void init();

    // Returns the absolute file path to the current log file
    QString logFilePath();

    // Qt message handler callback
    void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);
}
